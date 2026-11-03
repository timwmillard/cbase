#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Schema                                                              */
/* ------------------------------------------------------------------ */

static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS jobs ("
    "  id           INTEGER PRIMARY KEY,"
    "  kind         TEXT NOT NULL,"
    "  args         TEXT NOT NULL DEFAULT '{}'," /* JSON */
    "  unique_key   TEXT,"                       /* NULL = not unique */
    "  state        TEXT NOT NULL DEFAULT 'available'"
    "               CHECK (state IN "
    "('available','running','completed','discarded')),"
    "  priority     INTEGER NOT NULL DEFAULT 0,"
    "  attempt      INTEGER NOT NULL DEFAULT 0,"
    "  max_attempts INTEGER NOT NULL DEFAULT 5,"
    "  scheduled_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "  claimed_at   INTEGER,"
    "  worker       TEXT,"
    "  finished_at  INTEGER,"
    "  last_error   TEXT,"
    "  created_at   INTEGER NOT NULL DEFAULT (unixepoch())"
    ");"

    /* Fast 'next job' lookup. */
    "CREATE INDEX IF NOT EXISTS jobs_ready"
    "  ON jobs(state, scheduled_at, priority DESC, id)"
    "  WHERE state = 'available';"

    /* Uniqueness only applies while a job is pending or in flight.
     * Add 'completed' to the predicate for once-ever semantics
     * (and then never DELETE completed rows). */
    "CREATE UNIQUE INDEX IF NOT EXISTS jobs_unique"
    "  ON jobs(unique_key)"
    "  WHERE unique_key IS NOT NULL AND state IN ('available','running');"

    "CREATE INDEX IF NOT EXISTS jobs_running"
    "  ON jobs(claimed_at) WHERE state = 'running';";

int jobq_open(const char *path, sqlite3 **out_db) {
  sqlite3 *db = NULL;
  if (sqlite3_open(path, &db) != SQLITE_OK) {
    fprintf(stderr, "jobq: open %s: %s\n", path, sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
  }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
  sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);
  sqlite3_exec(db, "PRAGMA foreign_keys=ON;", 0, 0, 0);
  *out_db = db;
  return 0;
}

int jobq_migrate(sqlite3 *db) {
  char *err = NULL;
  if (sqlite3_exec(db, SCHEMA, 0, 0, &err) != SQLITE_OK) {
    fprintf(stderr, "jobq: migrate: %s\n", err ? err : "?");
    sqlite3_free(err);
    return -1;
  }
  /* Startup crash recovery: we are the only process, so anything still
   * 'running' is an orphan from a previous life. */
  sqlite3_exec(
      db,
      "UPDATE jobs SET state='available', worker=NULL, claimed_at=NULL,"
      " last_error=coalesce(last_error,'')||' [rescued at startup]'"
      " WHERE state='running';",
      0, 0, 0);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Enqueue                                                             */
/* ------------------------------------------------------------------ */

int64_t jobq_enqueue(sqlite3 *db, const char *kind, const char *args_json,
                     const char *unique_key, int64_t run_at, int priority,
                     int max_attempts) {
  /* INSERT OR IGNORE: the partial unique index on unique_key makes
   * duplicate pending/running jobs a silent no-op, atomically. */
  static const char *SQL =
      "INSERT OR IGNORE INTO jobs"
      " (kind, args, unique_key, priority, max_attempts, scheduled_at)"
      " VALUES (?1, ?2, ?3, ?4, ?5,"
      "         CASE WHEN ?6 > 0 THEN ?6 ELSE unixepoch() END);";

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    goto err;

  sqlite3_bind_text(st, 1, kind, -1, SQLITE_STATIC);
  sqlite3_bind_text(st, 2, args_json ? args_json : "{}", -1, SQLITE_STATIC);
  if (unique_key)
    sqlite3_bind_text(st, 3, unique_key, -1, SQLITE_STATIC);
  else
    sqlite3_bind_null(st, 3);
  sqlite3_bind_int(st, 4, priority);
  sqlite3_bind_int(st, 5, max_attempts > 0 ? max_attempts : 5);
  sqlite3_bind_int64(st, 6, run_at);

  if (sqlite3_step(st) != SQLITE_DONE)
    goto err;
  sqlite3_finalize(st);

  if (sqlite3_changes(db) == 0)
    return 0; /* duplicate unique_key: skipped */
  return sqlite3_last_insert_rowid(db);

err:
  fprintf(stderr, "jobq: enqueue: %s\n", sqlite3_errmsg(db));
  sqlite3_finalize(st);
  return -1;
}

/* ------------------------------------------------------------------ */
/* Claim                                                               */
/* ------------------------------------------------------------------ */

int jobq_claim(sqlite3 *db, const char *worker_name, jobq_job *out) {
  /* Single-statement claim. SQLite serializes writers, so this is
   * race-free without SELECT ... FOR UPDATE machinery. */
  static const char *SQL =
      "UPDATE jobs SET"
      "  state = 'running',"
      "  attempt = attempt + 1,"
      "  claimed_at = unixepoch(),"
      "  worker = ?1 "
      "WHERE id = (SELECT id FROM jobs"
      "            WHERE state = 'available' AND scheduled_at <= unixepoch()"
      "            ORDER BY priority DESC, id LIMIT 1) "
      "RETURNING id, kind, args, attempt, max_attempts;";

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    goto err;
  sqlite3_bind_text(st, 1, worker_name, -1, SQLITE_STATIC);

  int rc = sqlite3_step(st);
  if (rc == SQLITE_DONE) {
    sqlite3_finalize(st);
    return 0;
  } /* empty */
  if (rc != SQLITE_ROW)
    goto err;

  memset(out, 0, sizeof *out);
  out->id = sqlite3_column_int64(st, 0);
  out->kind = strdup((const char *)sqlite3_column_text(st, 1));
  out->args = strdup((const char *)sqlite3_column_text(st, 2));
  out->attempt = sqlite3_column_int(st, 3);
  out->max_attempts = sqlite3_column_int(st, 4);
  sqlite3_finalize(st);
  return 1;

err:
  fprintf(stderr, "jobq: claim: %s\n", sqlite3_errmsg(db));
  sqlite3_finalize(st);
  return -1;
}

/* ------------------------------------------------------------------ */
/* Complete / fail / rescue                                            */
/* ------------------------------------------------------------------ */

int jobq_complete(sqlite3 *db, int64_t id) {
  /* Swap for "DELETE FROM jobs WHERE id=?1" if you don't want history.
   * (Keep rows if your unique index predicate includes 'completed'.) */
  static const char *SQL =
      "UPDATE jobs SET state='completed', finished_at=unixepoch()"
      " WHERE id = ?1;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int64(st, 1, id);
  int ok = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
  sqlite3_finalize(st);
  return ok;
}

static int64_t backoff_secs(int attempt) {
  /* 15s, 30s, 60s, ... capped at 1h. Tune to taste. */
  int64_t s = 15;
  for (int i = 1; i < attempt && s < 3600; i++)
    s *= 2;
  return s > 3600 ? 3600 : s;
}

int jobq_fail(sqlite3 *db, const jobq_job *job, const char *err) {
  const int retry = job->attempt < job->max_attempts;
  static const char *SQL =
      "UPDATE jobs SET"
      "  state = ?2,"
      "  last_error = ?3,"
      "  scheduled_at = unixepoch() + ?4,"
      "  claimed_at = NULL, worker = NULL,"
      "  finished_at = CASE WHEN ?2 = 'discarded' THEN unixepoch() END"
      " WHERE id = ?1;";

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int64(st, 1, job->id);
  sqlite3_bind_text(st, 2, retry ? "available" : "discarded", -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(st, 3, err ? err : "unknown error", -1, SQLITE_STATIC);
  sqlite3_bind_int64(st, 4, retry ? backoff_secs(job->attempt) : 0);
  int ok = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
  sqlite3_finalize(st);
  return ok;
}

int jobq_rescue(sqlite3 *db, int64_t timeout_secs) {
  /* A rescued job may still be limping along in a hung worker; handlers
   * must be idempotent (true of any at-least-once queue). */
  static const char *SQL =
      "UPDATE jobs SET"
      "  state = CASE WHEN attempt < max_attempts THEN 'available'"
      "               ELSE 'discarded' END,"
      "  last_error = 'rescued: worker exceeded '||?1||'s',"
      "  claimed_at = NULL, worker = NULL"
      " WHERE state = 'running' AND claimed_at < unixepoch() - ?1;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int64(st, 1, timeout_secs);
  int ok = sqlite3_step(st) == SQLITE_DONE ? sqlite3_changes(db) : -1;
  sqlite3_finalize(st);
  return ok;
}

int jobq_heartbeat(sqlite3 *db, int64_t id) {
  static const char *SQL = "UPDATE jobs SET claimed_at = unixepoch()"
                           " WHERE id = ?1 AND state = 'running';";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int64(st, 1, id);
  int ok = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
  sqlite3_finalize(st);
  return ok;
}

void jobq_job_free(jobq_job *j) {
  if (!j)
    return;
  free(j->kind);
  j->kind = NULL;
  free(j->args);
  j->args = NULL;
}
