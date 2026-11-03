#ifndef JOBQ_QUEUE_H
#define JOBQ_QUEUE_H

#include <sqlite3.h>
#include <stdint.h>

typedef struct {
  int64_t id;
  char *kind;
  char *args;  /* JSON text, owned; free with jobq_job_free */
  int attempt; /* attempt number of THIS run (1-based) */
  int max_attempts;
} jobq_job;

/* Create tables/indexes if missing. Also flips any 'running' jobs back to
 * 'available' -- call once at startup, before workers exist (crash recovery).
 */
int jobq_migrate(sqlite3 *db);

/* Enqueue a job.
 *   unique_key: NULL for none. If set, a job with the same key already in
 *               state available/running makes this a no-op.
 *   run_at:     unix time; 0 means "now".
 * Returns new job id, 0 if skipped as duplicate, -1 on error. */
int64_t jobq_enqueue(sqlite3 *db, const char *kind, const char *args_json,
                     const char *unique_key, int64_t run_at, int priority,
                     int max_attempts);

/* Atomically claim the next available job.
 * Returns 1 and fills *out (caller frees), 0 if queue empty, -1 on error. */
int jobq_claim(sqlite3 *db, const char *worker_name, jobq_job *out);

int jobq_complete(sqlite3 *db, int64_t id);

/* Record failure: retries with backoff if attempts remain, else discards. */
int jobq_fail(sqlite3 *db, const jobq_job *job, const char *err);

/* Requeue jobs stuck in 'running' longer than timeout_secs (dead/hung worker).
 * Run periodically from a sweeper. Returns number rescued, or -1. */
int jobq_rescue(sqlite3 *db, int64_t timeout_secs);

/* Refresh claimed_at for a long-running job so the sweeper leaves it alone. */
int jobq_heartbeat(sqlite3 *db, int64_t id);

void jobq_job_free(jobq_job *j);

/* Open a connection with the pragmas every conn should have (WAL, busy_timeout,
 * foreign_keys). Each thread must use its own connection. */
int jobq_open(const char *path, sqlite3 **out_db);

#endif
