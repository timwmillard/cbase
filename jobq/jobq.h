#include <sqlite3.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <errno.h>

/* jobq: embedded SQLite-backed job queue with Lua handlers.
 *
 * Library usage:
 *   #include <jobq.h>, link -ljobq -lpthread
 *   (sqlite3 and lua are compiled straight into libjobq; see CMakeLists.txt)
 *
 * Single-header distribution: `cmake --build build --target jobq_single_header`
 * generates ../jobq.h (this file amalgamated with queue.c/pool.c and the
 * embedded Lua runtime, stb-style). Copy that one file into another project;
 * #include it normally for declarations, and in exactly one .c file:
 *   #define JOBQ_IMPLEMENTATION
 *   #include "jobq.h"
 * You still need sqlite3.h/lua.h available and linked — those aren't
 * bundled, only jobq's own source and the Lua job-system runtime are.
 *
 *   sqlite3 *db; jobq_open("jobs.db", &db); jobq_migrate(db);
 *   jobq_pool pool;
 *   jobq_pool_start(&pool, "jobs.db", "jobs.lua", 4);
 *   ...
 *   jobq_enqueue(db, "email.send", "{\"to\":\"x@y\"}", NULL, 0, 0, 5);
 *   jobq_pool_kick(&pool);
 *   ...
 *   jobq_pool_stop(&pool);
 *
 * Threading rules: one sqlite3* and one lua_State per thread, never shared.
 * The pool manages its own; your threads enqueue on their own connections.
 * Call jobq_rescue() periodically from any one thread (e.g. your main loop).
 */
#ifndef JOBQ_H
#define JOBQ_H

#ifndef JOBQ_QUEUE_H
#define JOBQ_QUEUE_H


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
/* -- end src/queue.h -- */
#ifndef JOBQ_POOL_H
#define JOBQ_POOL_H


typedef struct {
    const char *db_path;        /* each worker opens its own connection */
    const char *jobs_lua_path;  /* handler dispatch script */
    int         nthreads;

    /* internal */
    pthread_t      *threads;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             shutdown;
} jobq_pool;

int  jobq_pool_start(jobq_pool *p, const char *db_path,
                     const char *jobs_lua_path, int nthreads);

/* Call after enqueueing (from any thread) for instant pickup.
 * Without it, workers still notice within one poll interval. */
void jobq_pool_kick(jobq_pool *p);

void jobq_pool_stop(jobq_pool *p);   /* signals shutdown, joins all workers */

#endif
/* -- end src/pool.h -- */

#define JOBQ_VERSION_MAJOR 0
#define JOBQ_VERSION_MINOR 1

#endif
/* -- end src/jobq.h -- */

#ifdef JOBQ_IMPLEMENTATION



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
/* -- end src/queue.c -- */
const char dkjson_lua_data[] =
	"-- Module options:\n"
	"local always_try_using_lpeg = true\n"
	"local register_global_module_table = false\n"
	"local global_module_name = 'json'\n"
	"\n"
	"--[==[\n"
	"\n"
	"David Kolf's JSON module for Lua 5.1/5.2\n"
	"\n"
	"Version 2.5\n"
	"\n"
	"\n"
	"For the documentation see the corresponding readme.txt or visit\n"
	"<http://dkolf.de/src/dkjson-lua.fsl/>.\n"
	"\n"
	"You can contact the author by sending an e-mail to 'david' at the\n"
	"domain 'dkolf.de'.\n"
	"\n"
	"\n"
	"Copyright (C) 2010-2014 David Heiko Kolf\n"
	"\n"
	"Permission is hereby granted, free of charge, to any person obtaining\n"
	"a copy of this software and associated documentation files (the\n"
	"\"Software\"), to deal in the Software without restriction, including\n"
	"without limitation the rights to use, copy, modify, merge, publish,\n"
	"distribute, sublicense, and/or sell copies of the Software, and to\n"
	"permit persons to whom the Software is furnished to do so, subject to\n"
	"the following conditions:\n"
	"\n"
	"The above copyright notice and this permission notice shall be\n"
	"included in all copies or substantial portions of the Software.\n"
	"\n"
	"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n"
	"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n"
	"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n"
	"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS\n"
	"BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN\n"
	"ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN\n"
	"CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
	"SOFTWARE.\n"
	"\n"
	"--]==]\n"
	"\n"
	"-- global dependencies:\n"
	"local pairs, type, tostring, tonumber, getmetatable, setmetatable, rawset =\n"
	"      pairs, type, tostring, tonumber, getmetatable, setmetatable, rawset\n"
	"local error, require, pcall, select = error, require, pcall, select\n"
	"local floor, huge = math.floor, math.huge\n"
	"local strrep, gsub, strsub, strbyte, strchar, strfind, strlen, strformat =\n"
	"      string.rep, string.gsub, string.sub, string.byte, string.char,\n"
	"      string.find, string.len, string.format\n"
	"local strmatch = string.match\n"
	"local concat = table.concat\n"
	"\n"
	"local json = { version = \"dkjson 2.5\" }\n"
	"\n"
	"if register_global_module_table then\n"
	"  _G[global_module_name] = json\n"
	"end\n"
	"\n"
	"local _ENV = nil -- blocking globals in Lua 5.2\n"
	"\n"
	"pcall (function()\n"
	"  -- Enable access to blocked metatables.\n"
	"  -- Don't worry, this module doesn't change anything in them.\n"
	"  local debmeta = require \"debug\".getmetatable\n"
	"  if debmeta then getmetatable = debmeta end\n"
	"end)\n"
	"\n"
	"json.null = setmetatable ({}, {\n"
	"  __tojson = function () return \"null\" end\n"
	"})\n"
	"\n"
	"local function isarray (tbl)\n"
	"  local max, n, arraylen = 0, 0, 0\n"
	"  for k,v in pairs (tbl) do\n"
	"    if k == 'n' and type(v) == 'number' then\n"
	"      arraylen = v\n"
	"      if v > max then\n"
	"        max = v\n"
	"      end\n"
	"    else\n"
	"      if type(k) ~= 'number' or k < 1 or floor(k) ~= k then\n"
	"        return false\n"
	"      end\n"
	"      if k > max then\n"
	"        max = k\n"
	"      end\n"
	"      n = n + 1\n"
	"    end\n"
	"  end\n"
	"  if max > 10 and max > arraylen and max > n * 2 then\n"
	"    return false -- don't create an array with too many holes\n"
	"  end\n"
	"  return true, max\n"
	"end\n"
	"\n"
	"local escapecodes = {\n"
	"  [\"\\\"\"] = \"\\\\\\\"\", [\"\\\\\"] = \"\\\\\\\\\", [\"\\b\"] = \"\\\\b\", [\"\\f\"] = \"\\\\f\",\n"
	"  [\"\\n\"] = \"\\\\n\",  [\"\\r\"] = \"\\\\r\",  [\"\\t\"] = \"\\\\t\"\n"
	"}\n"
	"\n"
	"local function escapeutf8 (uchar)\n"
	"  local value = escapecodes[uchar]\n"
	"  if value then\n"
	"    return value\n"
	"  end\n"
	"  local a, b, c, d = strbyte (uchar, 1, 4)\n"
	"  a, b, c, d = a or 0, b or 0, c or 0, d or 0\n"
	"  if a <= 0x7f then\n"
	"    value = a\n"
	"  elseif 0xc0 <= a and a <= 0xdf and b >= 0x80 then\n"
	"    value = (a - 0xc0) * 0x40 + b - 0x80\n"
	"  elseif 0xe0 <= a and a <= 0xef and b >= 0x80 and c >= 0x80 then\n"
	"    value = ((a - 0xe0) * 0x40 + b - 0x80) * 0x40 + c - 0x80\n"
	"  elseif 0xf0 <= a and a <= 0xf7 and b >= 0x80 and c >= 0x80 and d >= 0x80 then\n"
	"    value = (((a - 0xf0) * 0x40 + b - 0x80) * 0x40 + c - 0x80) * 0x40 + d - 0x80\n"
	"  else\n"
	"    return \"\"\n"
	"  end\n"
	"  if value <= 0xffff then\n"
	"    return strformat (\"\\\\u%.4x\", value)\n"
	"  elseif value <= 0x10ffff then\n"
	"    -- encode as UTF-16 surrogate pair\n"
	"    value = value - 0x10000\n"
	"    local highsur, lowsur = 0xD800 + floor (value/0x400), 0xDC00 + (value % 0x400)\n"
	"    return strformat (\"\\\\u%.4x\\\\u%.4x\", highsur, lowsur)\n"
	"  else\n"
	"    return \"\"\n"
	"  end\n"
	"end\n"
	"\n"
	"local function fsub (str, pattern, repl)\n"
	"  -- gsub always builds a new string in a buffer, even when no match\n"
	"  -- exists. First using find should be more efficient when most strings\n"
	"  -- don't contain the pattern.\n"
	"  if strfind (str, pattern) then\n"
	"    return gsub (str, pattern, repl)\n"
	"  else\n"
	"    return str\n"
	"  end\n"
	"end\n"
	"\n"
	"local function quotestring (value)\n"
	"  -- based on the regexp \"escapable\" in https://github.com/douglascrockford/JSON-js\n"
	"  value = fsub (value, \"[%z\\1-\\31\\\"\\\\\\127]\", escapeutf8)\n"
	"  if strfind (value, \"[\\194\\216\\220\\225\\226\\239]\") then\n"
	"    value = fsub (value, \"\\194[\\128-\\159\\173]\", escapeutf8)\n"
	"    value = fsub (value, \"\\216[\\128-\\132]\", escapeutf8)\n"
	"    value = fsub (value, \"\\220\\143\", escapeutf8)\n"
	"    value = fsub (value, \"\\225\\158[\\180\\181]\", escapeutf8)\n"
	"    value = fsub (value, \"\\226\\128[\\140-\\143\\168-\\175]\", escapeutf8)\n"
	"    value = fsub (value, \"\\226\\129[\\160-\\175]\", escapeutf8)\n"
	"    value = fsub (value, \"\\239\\187\\191\", escapeutf8)\n"
	"    value = fsub (value, \"\\239\\191[\\176-\\191]\", escapeutf8)\n"
	"  end\n"
	"  return \"\\\"\" .. value .. \"\\\"\"\n"
	"end\n"
	"json.quotestring = quotestring\n"
	"\n"
	"local function replace(str, o, n)\n"
	"  local i, j = strfind (str, o, 1, true)\n"
	"  if i then\n"
	"    return strsub(str, 1, i-1) .. n .. strsub(str, j+1, -1)\n"
	"  else\n"
	"    return str\n"
	"  end\n"
	"end\n"
	"\n"
	"-- locale independent num2str and str2num functions\n"
	"local decpoint, numfilter\n"
	"\n"
	"local function updatedecpoint ()\n"
	"  decpoint = strmatch(tostring(0.5), \"([^05+])\")\n"
	"  -- build a filter that can be used to remove group separators\n"
	"  numfilter = \"[^0-9%-%+eE\" .. gsub(decpoint, \"[%^%$%(%)%%%.%[%]%*%+%-%?]\", \"%%%0\") .. \"]+\"\n"
	"end\n"
	"\n"
	"updatedecpoint()\n"
	"\n"
	"local function num2str (num)\n"
	"  return replace(fsub(tostring(num), numfilter, \"\"), decpoint, \".\")\n"
	"end\n"
	"\n"
	"local function str2num (str)\n"
	"  local num = tonumber(replace(str, \".\", decpoint))\n"
	"  if not num then\n"
	"    updatedecpoint()\n"
	"    num = tonumber(replace(str, \".\", decpoint))\n"
	"  end\n"
	"  return num\n"
	"end\n"
	"\n"
	"local function addnewline2 (level, buffer, buflen)\n"
	"  buffer[buflen+1] = \"\\n\"\n"
	"  buffer[buflen+2] = strrep (\"  \", level)\n"
	"  buflen = buflen + 2\n"
	"  return buflen\n"
	"end\n"
	"\n"
	"function json.addnewline (state)\n"
	"  if state.indent then\n"
	"    state.bufferlen = addnewline2 (state.level or 0,\n"
	"                           state.buffer, state.bufferlen or #(state.buffer))\n"
	"  end\n"
	"end\n"
	"\n"
	"local encode2 -- forward declaration\n"
	"\n"
	"local function addpair (key, value, prev, indent, level, buffer, buflen, tables, globalorder, state)\n"
	"  local kt = type (key)\n"
	"  if kt ~= 'string' and kt ~= 'number' then\n"
	"    return nil, \"type '\" .. kt .. \"' is not supported as a key by JSON.\"\n"
	"  end\n"
	"  if prev then\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = \",\"\n"
	"  end\n"
	"  if indent then\n"
	"    buflen = addnewline2 (level, buffer, buflen)\n"
	"  end\n"
	"  buffer[buflen+1] = quotestring (key)\n"
	"  buffer[buflen+2] = \":\"\n"
	"  return encode2 (value, indent, level, buffer, buflen + 2, tables, globalorder, state)\n"
	"end\n"
	"\n"
	"local function appendcustom(res, buffer, state)\n"
	"  local buflen = state.bufferlen\n"
	"  if type (res) == 'string' then\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = res\n"
	"  end\n"
	"  return buflen\n"
	"end\n"
	"\n"
	"local function exception(reason, value, state, buffer, buflen, defaultmessage)\n"
	"  defaultmessage = defaultmessage or reason\n"
	"  local handler = state.exception\n"
	"  if not handler then\n"
	"    return nil, defaultmessage\n"
	"  else\n"
	"    state.bufferlen = buflen\n"
	"    local ret, msg = handler (reason, value, state, defaultmessage)\n"
	"    if not ret then return nil, msg or defaultmessage end\n"
	"    return appendcustom(ret, buffer, state)\n"
	"  end\n"
	"end\n"
	"\n"
	"function json.encodeexception(reason, value, state, defaultmessage)\n"
	"  return quotestring(\"<\" .. defaultmessage .. \">\")\n"
	"end\n"
	"\n"
	"encode2 = function (value, indent, level, buffer, buflen, tables, globalorder, state)\n"
	"  local valtype = type (value)\n"
	"  local valmeta = getmetatable (value)\n"
	"  valmeta = type (valmeta) == 'table' and valmeta -- only tables\n"
	"  local valtojson = valmeta and valmeta.__tojson\n"
	"  if valtojson then\n"
	"    if tables[value] then\n"
	"      return exception('reference cycle', value, state, buffer, buflen)\n"
	"    end\n"
	"    tables[value] = true\n"
	"    state.bufferlen = buflen\n"
	"    local ret, msg = valtojson (value, state)\n"
	"    if not ret then return exception('custom encoder failed', value, state, buffer, buflen, msg) end\n"
	"    tables[value] = nil\n"
	"    buflen = appendcustom(ret, buffer, state)\n"
	"  elseif value == nil then\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = \"null\"\n"
	"  elseif valtype == 'number' then\n"
	"    local s\n"
	"    if value ~= value or value >= huge or -value >= huge then\n"
	"      -- This is the behaviour of the original JSON implementation.\n"
	"      s = \"null\"\n"
	"    else\n"
	"      s = num2str (value)\n"
	"    end\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = s\n"
	"  elseif valtype == 'boolean' then\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = value and \"true\" or \"false\"\n"
	"  elseif valtype == 'string' then\n"
	"    buflen = buflen + 1\n"
	"    buffer[buflen] = quotestring (value)\n"
	"  elseif valtype == 'table' then\n"
	"    if tables[value] then\n"
	"      return exception('reference cycle', value, state, buffer, buflen)\n"
	"    end\n"
	"    tables[value] = true\n"
	"    level = level + 1\n"
	"    local isa, n = isarray (value)\n"
	"    if n == 0 and valmeta and valmeta.__jsontype == 'object' then\n"
	"      isa = false\n"
	"    end\n"
	"    local msg\n"
	"    if isa then -- JSON array\n"
	"      buflen = buflen + 1\n"
	"      buffer[buflen] = \"[\"\n"
	"      for i = 1, n do\n"
	"        buflen, msg = encode2 (value[i], indent, level, buffer, buflen, tables, globalorder, state)\n"
	"        if not buflen then return nil, msg end\n"
	"        if i < n then\n"
	"          buflen = buflen + 1\n"
	"          buffer[buflen] = \",\"\n"
	"        end\n"
	"      end\n"
	"      buflen = buflen + 1\n"
	"      buffer[buflen] = \"]\"\n"
	"    else -- JSON object\n"
	"      local prev = false\n"
	"      buflen = buflen + 1\n"
	"      buffer[buflen] = \"{\"\n"
	"      local order = valmeta and valmeta.__jsonorder or globalorder\n"
	"      if order then\n"
	"        local used = {}\n"
	"        n = #order\n"
	"        for i = 1, n do\n"
	"          local k = order[i]\n"
	"          local v = value[k]\n"
	"          if v then\n"
	"            used[k] = true\n"
	"            buflen, msg = addpair (k, v, prev, indent, level, buffer, buflen, tables, globalorder, state)\n"
	"            prev = true -- add a seperator before the next element\n"
	"          end\n"
	"        end\n"
	"        for k,v in pairs (value) do\n"
	"          if not used[k] then\n"
	"            buflen, msg = addpair (k, v, prev, indent, level, buffer, buflen, tables, globalorder, state)\n"
	"            if not buflen then return nil, msg end\n"
	"            prev = true -- add a seperator before the next element\n"
	"          end\n"
	"        end\n"
	"      else -- unordered\n"
	"        for k,v in pairs (value) do\n"
	"          buflen, msg = addpair (k, v, prev, indent, level, buffer, buflen, tables, globalorder, state)\n"
	"          if not buflen then return nil, msg end\n"
	"          prev = true -- add a seperator before the next element\n"
	"        end\n"
	"      end\n"
	"      if indent then\n"
	"        buflen = addnewline2 (level - 1, buffer, buflen)\n"
	"      end\n"
	"      buflen = buflen + 1\n"
	"      buffer[buflen] = \"}\"\n"
	"    end\n"
	"    tables[value] = nil\n"
	"  else\n"
	"    return exception ('unsupported type', value, state, buffer, buflen,\n"
	"      \"type '\" .. valtype .. \"' is not supported by JSON.\")\n"
	"  end\n"
	"  return buflen\n"
	"end\n"
	"\n"
	"function json.encode (value, state)\n"
	"  state = state or {}\n"
	"  local oldbuffer = state.buffer\n"
	"  local buffer = oldbuffer or {}\n"
	"  state.buffer = buffer\n"
	"  updatedecpoint()\n"
	"  local ret, msg = encode2 (value, state.indent, state.level or 0,\n"
	"                   buffer, state.bufferlen or 0, state.tables or {}, state.keyorder, state)\n"
	"  if not ret then\n"
	"    error (msg, 2)\n"
	"  elseif oldbuffer == buffer then\n"
	"    state.bufferlen = ret\n"
	"    return true\n"
	"  else\n"
	"    state.bufferlen = nil\n"
	"    state.buffer = nil\n"
	"    return concat (buffer)\n"
	"  end\n"
	"end\n"
	"\n"
	"local function loc (str, where)\n"
	"  local line, pos, linepos = 1, 1, 0\n"
	"  while true do\n"
	"    pos = strfind (str, \"\\n\", pos, true)\n"
	"    if pos and pos < where then\n"
	"      line = line + 1\n"
	"      linepos = pos\n"
	"      pos = pos + 1\n"
	"    else\n"
	"      break\n"
	"    end\n"
	"  end\n"
	"  return \"line \" .. line .. \", column \" .. (where - linepos)\n"
	"end\n"
	"\n"
	"local function unterminated (str, what, where)\n"
	"  return nil, strlen (str) + 1, \"unterminated \" .. what .. \" at \" .. loc (str, where)\n"
	"end\n"
	"\n"
	"local function scanwhite (str, pos)\n"
	"  while true do\n"
	"    pos = strfind (str, \"%S\", pos)\n"
	"    if not pos then return nil end\n"
	"    local sub2 = strsub (str, pos, pos + 1)\n"
	"    if sub2 == \"\\239\\187\" and strsub (str, pos + 2, pos + 2) == \"\\191\" then\n"
	"      -- UTF-8 Byte Order Mark\n"
	"      pos = pos + 3\n"
	"    elseif sub2 == \"//\" then\n"
	"      pos = strfind (str, \"[\\n\\r]\", pos + 2)\n"
	"      if not pos then return nil end\n"
	"    elseif sub2 == \"/*\" then\n"
	"      pos = strfind (str, \"*/\", pos + 2)\n"
	"      if not pos then return nil end\n"
	"      pos = pos + 2\n"
	"    else\n"
	"      return pos\n"
	"    end\n"
	"  end\n"
	"end\n"
	"\n"
	"local escapechars = {\n"
	"  [\"\\\"\"] = \"\\\"\", [\"\\\\\"] = \"\\\\\", [\"/\"] = \"/\", [\"b\"] = \"\\b\", [\"f\"] = \"\\f\",\n"
	"  [\"n\"] = \"\\n\", [\"r\"] = \"\\r\", [\"t\"] = \"\\t\"\n"
	"}\n"
	"\n"
	"local function unichar (value)\n"
	"  if value < 0 then\n"
	"    return nil\n"
	"  elseif value <= 0x007f then\n"
	"    return strchar (value)\n"
	"  elseif value <= 0x07ff then\n"
	"    return strchar (0xc0 + floor(value/0x40),\n"
	"                    0x80 + (floor(value) % 0x40))\n"
	"  elseif value <= 0xffff then\n"
	"    return strchar (0xe0 + floor(value/0x1000),\n"
	"                    0x80 + (floor(value/0x40) % 0x40),\n"
	"                    0x80 + (floor(value) % 0x40))\n"
	"  elseif value <= 0x10ffff then\n"
	"    return strchar (0xf0 + floor(value/0x40000),\n"
	"                    0x80 + (floor(value/0x1000) % 0x40),\n"
	"                    0x80 + (floor(value/0x40) % 0x40),\n"
	"                    0x80 + (floor(value) % 0x40))\n"
	"  else\n"
	"    return nil\n"
	"  end\n"
	"end\n"
	"\n"
	"local function scanstring (str, pos)\n"
	"  local lastpos = pos + 1\n"
	"  local buffer, n = {}, 0\n"
	"  while true do\n"
	"    local nextpos = strfind (str, \"[\\\"\\\\]\", lastpos)\n"
	"    if not nextpos then\n"
	"      return unterminated (str, \"string\", pos)\n"
	"    end\n"
	"    if nextpos > lastpos then\n"
	"      n = n + 1\n"
	"      buffer[n] = strsub (str, lastpos, nextpos - 1)\n"
	"    end\n"
	"    if strsub (str, nextpos, nextpos) == \"\\\"\" then\n"
	"      lastpos = nextpos + 1\n"
	"      break\n"
	"    else\n"
	"      local escchar = strsub (str, nextpos + 1, nextpos + 1)\n"
	"      local value\n"
	"      if escchar == \"u\" then\n"
	"        value = tonumber (strsub (str, nextpos + 2, nextpos + 5), 16)\n"
	"        if value then\n"
	"          local value2\n"
	"          if 0xD800 <= value and value <= 0xDBff then\n"
	"            -- we have the high surrogate of UTF-16. Check if there is a\n"
	"            -- low surrogate escaped nearby to combine them.\n"
	"            if strsub (str, nextpos + 6, nextpos + 7) == \"\\\\u\" then\n"
	"              value2 = tonumber (strsub (str, nextpos + 8, nextpos + 11), 16)\n"
	"              if value2 and 0xDC00 <= value2 and value2 <= 0xDFFF then\n"
	"                value = (value - 0xD800)  * 0x400 + (value2 - 0xDC00) + 0x10000\n"
	"              else\n"
	"                value2 = nil -- in case it was out of range for a low surrogate\n"
	"              end\n"
	"            end\n"
	"          end\n"
	"          value = value and unichar (value)\n"
	"          if value then\n"
	"            if value2 then\n"
	"              lastpos = nextpos + 12\n"
	"            else\n"
	"              lastpos = nextpos + 6\n"
	"            end\n"
	"          end\n"
	"        end\n"
	"      end\n"
	"      if not value then\n"
	"        value = escapechars[escchar] or escchar\n"
	"        lastpos = nextpos + 2\n"
	"      end\n"
	"      n = n + 1\n"
	"      buffer[n] = value\n"
	"    end\n"
	"  end\n"
	"  if n == 1 then\n"
	"    return buffer[1], lastpos\n"
	"  elseif n > 1 then\n"
	"    return concat (buffer), lastpos\n"
	"  else\n"
	"    return \"\", lastpos\n"
	"  end\n"
	"end\n"
	"\n"
	"local scanvalue -- forward declaration\n"
	"\n"
	"local function scantable (what, closechar, str, startpos, nullval, objectmeta, arraymeta)\n"
	"  local len = strlen (str)\n"
	"  local tbl, n = {}, 0\n"
	"  local pos = startpos + 1\n"
	"  if what == 'object' then\n"
	"    setmetatable (tbl, objectmeta)\n"
	"  else\n"
	"    setmetatable (tbl, arraymeta)\n"
	"  end\n"
	"  while true do\n"
	"    pos = scanwhite (str, pos)\n"
	"    if not pos then return unterminated (str, what, startpos) end\n"
	"    local char = strsub (str, pos, pos)\n"
	"    if char == closechar then\n"
	"      return tbl, pos + 1\n"
	"    end\n"
	"    local val1, err\n"
	"    val1, pos, err = scanvalue (str, pos, nullval, objectmeta, arraymeta)\n"
	"    if err then return nil, pos, err end\n"
	"    pos = scanwhite (str, pos)\n"
	"    if not pos then return unterminated (str, what, startpos) end\n"
	"    char = strsub (str, pos, pos)\n"
	"    if char == \":\" then\n"
	"      if val1 == nil then\n"
	"        return nil, pos, \"cannot use nil as table index (at \" .. loc (str, pos) .. \")\"\n"
	"      end\n"
	"      pos = scanwhite (str, pos + 1)\n"
	"      if not pos then return unterminated (str, what, startpos) end\n"
	"      local val2\n"
	"      val2, pos, err = scanvalue (str, pos, nullval, objectmeta, arraymeta)\n"
	"      if err then return nil, pos, err end\n"
	"      tbl[val1] = val2\n"
	"      pos = scanwhite (str, pos)\n"
	"      if not pos then return unterminated (str, what, startpos) end\n"
	"      char = strsub (str, pos, pos)\n"
	"    else\n"
	"      n = n + 1\n"
	"      tbl[n] = val1\n"
	"    end\n"
	"    if char == \",\" then\n"
	"      pos = pos + 1\n"
	"    end\n"
	"  end\n"
	"end\n"
	"\n"
	"scanvalue = function (str, pos, nullval, objectmeta, arraymeta)\n"
	"  pos = pos or 1\n"
	"  pos = scanwhite (str, pos)\n"
	"  if not pos then\n"
	"    return nil, strlen (str) + 1, \"no valid JSON value (reached the end)\"\n"
	"  end\n"
	"  local char = strsub (str, pos, pos)\n"
	"  if char == \"{\" then\n"
	"    return scantable ('object', \"}\", str, pos, nullval, objectmeta, arraymeta)\n"
	"  elseif char == \"[\" then\n"
	"    return scantable ('array', \"]\", str, pos, nullval, objectmeta, arraymeta)\n"
	"  elseif char == \"\\\"\" then\n"
	"    return scanstring (str, pos)\n"
	"  else\n"
	"    local pstart, pend = strfind (str, \"^%-?[%d%.]+[eE]?[%+%-]?%d*\", pos)\n"
	"    if pstart then\n"
	"      local number = str2num (strsub (str, pstart, pend))\n"
	"      if number then\n"
	"        return number, pend + 1\n"
	"      end\n"
	"    end\n"
	"    pstart, pend = strfind (str, \"^%a%w*\", pos)\n"
	"    if pstart then\n"
	"      local name = strsub (str, pstart, pend)\n"
	"      if name == \"true\" then\n"
	"        return true, pend + 1\n"
	"      elseif name == \"false\" then\n"
	"        return false, pend + 1\n"
	"      elseif name == \"null\" then\n"
	"        return nullval, pend + 1\n"
	"      end\n"
	"    end\n"
	"    return nil, pos, \"no valid JSON value at \" .. loc (str, pos)\n"
	"  end\n"
	"end\n"
	"\n"
	"local function optionalmetatables(...)\n"
	"  if select(\"#\", ...) > 0 then\n"
	"    return ...\n"
	"  else\n"
	"    return {__jsontype = 'object'}, {__jsontype = 'array'}\n"
	"  end\n"
	"end\n"
	"\n"
	"function json.decode (str, pos, nullval, ...)\n"
	"  local objectmeta, arraymeta = optionalmetatables(...)\n"
	"  return scanvalue (str, pos, nullval, objectmeta, arraymeta)\n"
	"end\n"
	"\n"
	"function json.use_lpeg ()\n"
	"  local g = require (\"lpeg\")\n"
	"\n"
	"  if g.version() == \"0.11\" then\n"
	"    error \"due to a bug in LPeg 0.11, it cannot be used for JSON matching\"\n"
	"  end\n"
	"\n"
	"  local pegmatch = g.match\n"
	"  local P, S, R = g.P, g.S, g.R\n"
	"\n"
	"  local function ErrorCall (str, pos, msg, state)\n"
	"    if not state.msg then\n"
	"      state.msg = msg .. \" at \" .. loc (str, pos)\n"
	"      state.pos = pos\n"
	"    end\n"
	"    return false\n"
	"  end\n"
	"\n"
	"  local function Err (msg)\n"
	"    return g.Cmt (g.Cc (msg) * g.Carg (2), ErrorCall)\n"
	"  end\n"
	"\n"
	"  local SingleLineComment = P\"//\" * (1 - S\"\\n\\r\")^0\n"
	"  local MultiLineComment = P\"/*\" * (1 - P\"*/\")^0 * P\"*/\"\n"
	"  local Space = (S\" \\n\\r\\t\" + P\"\\239\\187\\191\" + SingleLineComment + MultiLineComment)^0\n"
	"\n"
	"  local PlainChar = 1 - S\"\\\"\\\\\\n\\r\"\n"
	"  local EscapeSequence = (P\"\\\\\" * g.C (S\"\\\"\\\\/bfnrt\" + Err \"unsupported escape sequence\")) / escapechars\n"
	"  local HexDigit = R(\"09\", \"af\", \"AF\")\n"
	"  local function UTF16Surrogate (match, pos, high, low)\n"
	"    high, low = tonumber (high, 16), tonumber (low, 16)\n"
	"    if 0xD800 <= high and high <= 0xDBff and 0xDC00 <= low and low <= 0xDFFF then\n"
	"      return true, unichar ((high - 0xD800)  * 0x400 + (low - 0xDC00) + 0x10000)\n"
	"    else\n"
	"      return false\n"
	"    end\n"
	"  end\n"
	"  local function UTF16BMP (hex)\n"
	"    return unichar (tonumber (hex, 16))\n"
	"  end\n"
	"  local U16Sequence = (P\"\\\\u\" * g.C (HexDigit * HexDigit * HexDigit * HexDigit))\n"
	"  local UnicodeEscape = g.Cmt (U16Sequence * U16Sequence, UTF16Surrogate) + U16Sequence/UTF16BMP\n"
	"  local Char = UnicodeEscape + EscapeSequence + PlainChar\n"
	"  local String = P\"\\\"\" * g.Cs (Char ^ 0) * (P\"\\\"\" + Err \"unterminated string\")\n"
	"  local Integer = P\"-\"^(-1) * (P\"0\" + (R\"19\" * R\"09\"^0))\n"
	"  local Fractal = P\".\" * R\"09\"^0\n"
	"  local Exponent = (S\"eE\") * (S\"+-\")^(-1) * R\"09\"^1\n"
	"  local Number = (Integer * Fractal^(-1) * Exponent^(-1))/str2num\n"
	"  local Constant = P\"true\" * g.Cc (true) + P\"false\" * g.Cc (false) + P\"null\" * g.Carg (1)\n"
	"  local SimpleValue = Number + String + Constant\n"
	"  local ArrayContent, ObjectContent\n"
	"\n"
	"  -- The functions parsearray and parseobject parse only a single value/pair\n"
	"  -- at a time and store them directly to avoid hitting the LPeg limits.\n"
	"  local function parsearray (str, pos, nullval, state)\n"
	"    local obj, cont\n"
	"    local npos\n"
	"    local t, nt = {}, 0\n"
	"    repeat\n"
	"      obj, cont, npos = pegmatch (ArrayContent, str, pos, nullval, state)\n"
	"      if not npos then break end\n"
	"      pos = npos\n"
	"      nt = nt + 1\n"
	"      t[nt] = obj\n"
	"    until cont == 'last'\n"
	"    return pos, setmetatable (t, state.arraymeta)\n"
	"  end\n"
	"\n"
	"  local function parseobject (str, pos, nullval, state)\n"
	"    local obj, key, cont\n"
	"    local npos\n"
	"    local t = {}\n"
	"    repeat\n"
	"      key, obj, cont, npos = pegmatch (ObjectContent, str, pos, nullval, state)\n"
	"      if not npos then break end\n"
	"      pos = npos\n"
	"      t[key] = obj\n"
	"    until cont == 'last'\n"
	"    return pos, setmetatable (t, state.objectmeta)\n"
	"  end\n"
	"\n"
	"  local Array = P\"[\" * g.Cmt (g.Carg(1) * g.Carg(2), parsearray) * Space * (P\"]\" + Err \"']' expected\")\n"
	"  local Object = P\"{\" * g.Cmt (g.Carg(1) * g.Carg(2), parseobject) * Space * (P\"}\" + Err \"'}' expected\")\n"
	"  local Value = Space * (Array + Object + SimpleValue)\n"
	"  local ExpectedValue = Value + Space * Err \"value expected\"\n"
	"  ArrayContent = Value * Space * (P\",\" * g.Cc'cont' + g.Cc'last') * g.Cp()\n"
	"  local Pair = g.Cg (Space * String * Space * (P\":\" + Err \"colon expected\") * ExpectedValue)\n"
	"  ObjectContent = Pair * Space * (P\",\" * g.Cc'cont' + g.Cc'last') * g.Cp()\n"
	"  local DecodeValue = ExpectedValue * g.Cp ()\n"
	"\n"
	"  function json.decode (str, pos, nullval, ...)\n"
	"    local state = {}\n"
	"    state.objectmeta, state.arraymeta = optionalmetatables(...)\n"
	"    local obj, retpos = pegmatch (DecodeValue, str, pos, nullval, state)\n"
	"    if state.msg then\n"
	"      return nil, state.pos, state.msg\n"
	"    else\n"
	"      return obj, retpos\n"
	"    end\n"
	"  end\n"
	"\n"
	"  -- use this function only once:\n"
	"  json.use_lpeg = function () return json end\n"
	"\n"
	"  json.using_lpeg = true\n"
	"\n"
	"  return json -- so you can get the module using json = require \"dkjson\".use_lpeg()\n"
	"end\n"
	"\n"
	"if always_try_using_lpeg then\n"
	"  pcall (json.use_lpeg)\n"
	"end\n"
	"\n"
	"return json\n"
	"\n"
	"";
const unsigned int dkjson_lua_len = 22416;
/* -- end src/dkjson_lua.h -- */
const char runtime_lua_data[] =
	"-- Job-system runtime: JSON handling, canonical encoding, and handler\n"
	"-- dispatch. Embedded into the binary (see CMakeLists.txt / runtime_lua.h)\n"
	"-- and executed in each worker's lua_State, after the `jobq` module is\n"
	"-- published and before the user's handlers script (-l PATH) loads, so that\n"
	"-- script can contain nothing but jobq.register(kind, fn) calls.\n"
	"\n"
	"local jobq = require(\"jobq\")\n"
	"local dkjson = require(\"dkjson\")\n"
	"local cjson = {\n"
	"  encode = dkjson.encode,\n"
	"  decode = function(str)\n"
	"    local obj, _, err = dkjson.decode(str)\n"
	"    if err then return nil, err end\n"
	"    return obj\n"
	"  end,\n"
	"}\n"
	"\n"
	"-- Canonical JSON: sorted keys, so equal args => equal bytes. Uniqueness is\n"
	"-- byte-comparison; cjson.encode does NOT sort keys. Use this when building\n"
	"-- unique_key values at the producer side too.\n"
	"local function canonical_json(v)\n"
	"  local t = type(v)\n"
	"  if t == \"table\" then\n"
	"    local n = #v\n"
	"    if n > 0 then\n"
	"      local parts = {}\n"
	"      for i = 1, n do parts[i] = canonical_json(v[i]) end\n"
	"      return \"[\" .. table.concat(parts, \",\") .. \"]\"\n"
	"    end\n"
	"    local keys = {}\n"
	"    for k in pairs(v) do keys[#keys + 1] = k end\n"
	"    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)\n"
	"    local parts = {}\n"
	"    for i, k in ipairs(keys) do\n"
	"      parts[i] = cjson.encode(tostring(k)) .. \":\" .. canonical_json(v[k])\n"
	"    end\n"
	"    return \"{\" .. table.concat(parts, \",\") .. \"}\"\n"
	"  end\n"
	"  return cjson.encode(v)\n"
	"end\n"
	"_G.canonical_json = canonical_json\n"
	"\n"
	"local handlers = {}\n"
	"jobq.register = function(kind, fn) handlers[kind] = fn end\n"
	"\n"
	"-- Entry point called from C for every claimed job.\n"
	"function dispatch(id, kind, args_json)\n"
	"  local h = handlers[kind]\n"
	"  if not h then\n"
	"    error((\"no handler for kind %q\"):format(kind))\n"
	"  end\n"
	"  local args, err = cjson.decode(args_json)\n"
	"  if args == nil then\n"
	"    error((\"bad args JSON for job %d: %s\"):format(id, tostring(err)))\n"
	"  end\n"
	"  return h(args, { id = id, kind = kind })\n"
	"end\n"
	"";
const unsigned int runtime_lua_len = 1901;
/* -- end src/runtime_lua.h -- */



#define POLL_INTERVAL_SECS 1 /* backstop for scheduled/retry jobs */

typedef struct {
  jobq_pool *pool;
  int index;
} worker_arg;

/* Each worker owns one connection and one lua_State: no locks needed
 * around either. lua_State is NOT thread-safe; never share it. */
typedef struct {
  sqlite3 *db;
  lua_State *L;
  char name[32];
} worker_ctx;

/* ---- Lua bindings available to handlers ------------------------------ */

/* jobq.enqueue(kind, args_json [, opts])
 * opts: { unique_key=, run_at=, priority=, max_attempts= }
 * Lets jobs enqueue follow-up jobs. args must already be a JSON string
 * (encode in Lua; see jobs.lua canonical_json for unique keys). */
static int l_enqueue(lua_State *L) {
  worker_ctx *ctx = (worker_ctx *)lua_touserdata(L, lua_upvalueindex(1));
  const char *kind = luaL_checkstring(L, 1);
  const char *args = luaL_optstring(L, 2, "{}");

  const char *ukey = NULL;
  int64_t run_at = 0;
  int prio = 0, maxa = 5;
  if (lua_istable(L, 3)) {
    lua_getfield(L, 3, "unique_key");
    ukey = lua_tostring(L, -1); /* stays valid: left on stack */
    lua_getfield(L, 3, "run_at");
    run_at = (int64_t)lua_tointeger(L, -1);
    lua_getfield(L, 3, "priority");
    prio = (int)lua_tointeger(L, -1);
    lua_getfield(L, 3, "max_attempts");
    if (!lua_isnil(L, -1))
      maxa = (int)lua_tointeger(L, -1);
    lua_pop(L, 3); /* keep unique_key string alive */
  }

  int64_t id = jobq_enqueue(ctx->db, kind, args, ukey, run_at, prio, maxa);
  if (id < 0)
    return luaL_error(L, "jobq.enqueue failed");
  lua_pushinteger(L, (lua_Integer)id); /* 0 = skipped duplicate */
  return 1;
}

/* jobq.heartbeat(id): call from long-running handlers so the rescue
 * sweeper doesn't reclaim the job out from under you. */
static int l_heartbeat(lua_State *L) {
  worker_ctx *ctx = (worker_ctx *)lua_touserdata(L, lua_upvalueindex(1));
  jobq_heartbeat(ctx->db, (int64_t)luaL_checkinteger(L, 1));
  return 0;
}

/* Publish the table at the top of the stack as package.loaded[modname] (and
 * consume it), so a later require(modname) hits the cache instead of
 * touching disk — or, for jobq, instead of relying on a bare global. */
static void publish_module(lua_State *L, const char *modname) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, modname);
  lua_pop(L, 3);
}

/* Run an embedded chunk and publish its return value as a module. */
static int preload_module(lua_State *L, const char *modname,
                          const char *chunkname, const char *data, size_t len) {
  if (luaL_loadbuffer(L, data, len, chunkname) != LUA_OK ||
      lua_pcall(L, 0, 1, 0) != LUA_OK)
    return -1;
  publish_module(L, modname);
  return 0;
}

static int setup_lua(worker_ctx *ctx, const char *jobs_lua_path) {
  ctx->L = luaL_newstate();
  if (!ctx->L)
    return -1;
  luaL_openlibs(ctx->L);

  /* jobq: C functions closing over this worker's ctx, published as a real
   * module (require("jobq")) rather than a bare global. */
  lua_newtable(ctx->L);
  lua_pushlightuserdata(ctx->L, ctx);
  lua_pushcclosure(ctx->L, l_enqueue, 1);
  lua_setfield(ctx->L, -2, "enqueue");
  lua_pushlightuserdata(ctx->L, ctx);
  lua_pushcclosure(ctx->L, l_heartbeat, 1);
  lua_setfield(ctx->L, -2, "heartbeat");
  publish_module(ctx->L, "jobq");

  /* dkjson, embedded, preloaded so require("dkjson") below never touches
   * disk. Then the system runtime (JSON shim, canonical encoding,
   * dispatch, jobq.register) loads, so the handlers script below is free
   * to contain nothing but jobq.register(kind, fn) calls. */
  if (preload_module(ctx->L, "dkjson", "dkjson.lua", dkjson_lua_data,
                     dkjson_lua_len) != 0) {
    fprintf(stderr, "%s: loading dkjson: %s\n", ctx->name,
            lua_tostring(ctx->L, -1));
    return -1;
  }
  if (luaL_loadbuffer(ctx->L, runtime_lua_data, runtime_lua_len,
                      "runtime.lua") != LUA_OK ||
      lua_pcall(ctx->L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "%s: loading runtime: %s\n", ctx->name,
            lua_tostring(ctx->L, -1));
    return -1;
  }

  if (luaL_dofile(ctx->L, jobs_lua_path) != LUA_OK) {
    fprintf(stderr, "%s: loading %s: %s\n", ctx->name, jobs_lua_path,
            lua_tostring(ctx->L, -1));
    return -1;
  }
  return 0;
}

/* ---- Job execution ---------------------------------------------------- */

static void run_job(worker_ctx *ctx, jobq_job *job) {
  lua_State *L = ctx->L;
  int base = lua_gettop(L);

  lua_getglobal(L, "dispatch"); /* defined in jobs.lua */
  if (!lua_isfunction(L, -1)) {
    jobq_fail(ctx->db, job, "jobs.lua does not define dispatch()");
    lua_settop(L, base);
    return;
  }
  lua_pushinteger(L, (lua_Integer)job->id);
  lua_pushstring(L, job->kind);
  lua_pushstring(L, job->args);

  if (lua_pcall(L, 3, 0, 0) == LUA_OK) {
    jobq_complete(ctx->db, job->id);
  } else {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "%s: job %lld (%s) attempt %d/%d failed: %s\n", ctx->name,
            (long long)job->id, job->kind, job->attempt, job->max_attempts,
            err ? err : "?");
    jobq_fail(ctx->db, job, err);
  }
  lua_settop(L, base);
  /* lua_gc(L, LUA_GCSTEP, 0);  -- optional nudge between jobs */
}

/* ---- Worker loop ------------------------------------------------------ */

static void *worker_main(void *argp) {
  worker_arg *wa = (worker_arg *)argp;
  jobq_pool *p = wa->pool;
  worker_ctx ctx = {0};
  snprintf(ctx.name, sizeof ctx.name, "worker-%d", wa->index);
  free(wa);

  if (jobq_open(p->db_path, &ctx.db) != 0)
    return NULL;
  if (setup_lua(&ctx, p->jobs_lua_path) != 0)
    goto out;

  for (;;) {
    /* Drain: keep claiming while work exists. */
    jobq_job job;
    int rc = jobq_claim(ctx.db, ctx.name, &job);
    if (rc == 1) {
      run_job(&ctx, &job);
      jobq_job_free(&job);
      continue;
    }
    if (rc < 0) {
      /* transient DB error: brief pause, then retry */
      struct timespec ts = {0, 100 * 1000 * 1000};
      nanosleep(&ts, NULL);
    }

    /* Idle: wait for a kick, or POLL_INTERVAL for scheduled jobs. */
    pthread_mutex_lock(&p->mu);
    if (p->shutdown) {
      pthread_mutex_unlock(&p->mu);
      break;
    }
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += POLL_INTERVAL_SECS;
    pthread_cond_timedwait(&p->cv, &p->mu, &deadline);
    int stop = p->shutdown;
    pthread_mutex_unlock(&p->mu);
    if (stop)
      break;
  }

out:
  if (ctx.L)
    lua_close(ctx.L);
  if (ctx.db)
    sqlite3_close(ctx.db);
  return NULL;
}

/* ---- Pool lifecycle ---------------------------------------------------- */

int jobq_pool_start(jobq_pool *p, const char *db_path,
                    const char *jobs_lua_path, int nthreads) {
  memset(p, 0, sizeof *p);
  p->db_path = db_path;
  p->jobs_lua_path = jobs_lua_path;
  p->nthreads = nthreads;
  pthread_mutex_init(&p->mu, NULL);
  pthread_cond_init(&p->cv, NULL);

  p->threads = calloc((size_t)nthreads, sizeof(pthread_t));
  if (!p->threads)
    return -1;

  for (int i = 0; i < nthreads; i++) {
    worker_arg *wa = malloc(sizeof *wa);
    wa->pool = p;
    wa->index = i;
    if (pthread_create(&p->threads[i], NULL, worker_main, wa) != 0) {
      free(wa);
      p->nthreads = i;
      jobq_pool_stop(p);
      return -1;
    }
  }
  return 0;
}

void jobq_pool_kick(jobq_pool *p) {
  pthread_mutex_lock(&p->mu);
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mu);
}

void jobq_pool_stop(jobq_pool *p) {
  pthread_mutex_lock(&p->mu);
  p->shutdown = 1;
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mu);
  for (int i = 0; i < p->nthreads; i++)
    pthread_join(p->threads[i], NULL);
  free(p->threads);
  pthread_mutex_destroy(&p->mu);
  pthread_cond_destroy(&p->cv);
}
/* -- end src/pool.c -- */

#endif /* JOBQ_IMPLEMENTATION */
