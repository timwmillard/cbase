# jobq

Embedded job queue: SQLite for state, a small pthread pool for workers, Lua
for job handlers. Replaces River (Go + Postgres) for a single-process server.

Builds as a **library** (`libjobq.a` / `libjobq.so`) for embedding in your
server, and a **standalone daemon/CLI** (`jobqd`) built on the same library.

## Layout

    src/jobq.h      umbrella header for library consumers
    src/queue.h,c   schema + queue ops: enqueue, claim, complete, fail,
                    rescue, heartbeat. Pure SQLite, no threading knowledge.
    src/pool.h,c    worker pool. Each thread owns one sqlite3 connection and
                    one lua_State. Condition-variable wakeup + 1s poll backstop.
    src/main.c      jobqd only: CLI parsing, daemon loop, rescue sweeper.
    lua/jobs.lua    handler table + dispatch(), canonical_json helper.

## Build

    make            # libjobq.a, libjobq.so, jobqd
    make install    # PREFIX=/usr/local: lib/, include/jobq/, bin/jobqd

Needs sqlite3 and lua5.x dev packages; lua-cjson at runtime.

## Standalone usage

    jobqd -l lua/jobs.lua -n 4              # run worker daemon
    jobqd enqueue email.send '{"to":"x@y"}' # enqueue from shell/cron
    jobqd -u nightly -a 3600 enqueue report.generate '{}'   # unique, delayed
    jobqd status                            # counts by state/kind

Daemon options: -d db path, -l handlers script, -n workers, -t rescue timeout.

## Library usage

    #include <jobq/jobq.h>      // link: -ljobq -lsqlite3 -llua5.4 -lpthread

    sqlite3 *db;
    jobq_open("jobs.db", &db);          // WAL, busy_timeout preconfigured
    jobq_migrate(db);                   // schema + startup crash recovery

    jobq_pool pool;
    jobq_pool_start(&pool, "jobs.db", "jobs.lua", 4);

    // wherever work originates:
    jobq_enqueue(db, "email.send", "{\"to\":\"x@y\"}", NULL, 0, 0, 5);
    jobq_pool_kick(&pool);              // instant pickup (else <=1s poll)

    // in your periodic tick, any one thread:
    jobq_rescue(db, 300);

    // shutdown (drains in-flight jobs):
    jobq_pool_stop(&pool);

Threading rules: one `sqlite3*` and one `lua_State` per thread, never shared.
The pool manages its own; your threads enqueue on their own connections.

## How it hangs together

- **Claim** is one `UPDATE ... RETURNING` statement. SQLite's single writer
  makes it atomic -- no SKIP LOCKED, no advisory locks.
- **Wakeup**: `jobq_pool_kick()` after enqueue for instant pickup; idle
  workers also re-poll every second, which picks up retries and
  `run_at`-scheduled jobs.
- **Retries**: handler `error()` -> `jobq_fail` -> back to `available` with
  exponential backoff (15s doubling, 1h cap), or `discarded` after
  `max_attempts`.
- **Crash recovery**: `jobq_migrate()` flips orphaned `running` jobs back to
  `available` at startup (safe: single process writes this DB).
- **Long-lived recovery**: run `jobq_rescue()` periodically; it requeues jobs
  stuck `running` past the timeout. Long handlers call
  `jobq.heartbeat(job.id)` to stay claimed. Rescue implies at-least-once
  delivery: write handlers to be idempotent.
- **Unique jobs**: partial unique index on `unique_key` over
  `('available','running')` + `INSERT OR IGNORE`. `jobq_enqueue` returns 0
  when deduped. Build keys from canonical (sorted-key) JSON -- see
  `canonical_json` in jobs.lua -- since comparison is byte-wise. Add
  `'completed'` to the index predicate for once-ever semantics (and stop
  deleting completed rows if you do).

## Handlers (lua/jobs.lua)

Add functions to the `handlers` table keyed by `kind`. `error()` fails the
job into retry; returning completes it. Handlers get
`jobq.enqueue(kind, args_json, opts)` for follow-up jobs and
`jobq.heartbeat(id)` for long runs.

## Notes on running jobqd alongside your server

Multiple processes on one SQLite file work (WAL + busy_timeout are set), but
the startup rescue in `jobq_migrate` assumes a single process -- it would
steal jobs running in the other process. If you run jobqd workers *and* an
embedding server together, let only one of them call `jobq_migrate`/rescue,
or gate startup-rescue behind a flag. Simplest: use jobqd's enqueue/status
CLI freely (harmless), but run workers in only one process.

## Tuning / later

- **Worker count**: threads block on HTTP I/O, so size for peak concurrent
  jobs, not cores. Set HTTP client timeouts -- rescue can requeue a stuck
  job but only a socket timeout frees the thread.
- **Completed-row growth**: `DELETE` on complete (comment in
  `jobq_complete`), or prune in the sweeper:
  `DELETE FROM jobs WHERE state IN ('completed','discarded') AND finished_at < unixepoch() - 7*86400;`
- **Runaway Lua**: a watchdog can `lua_sethook` a hung state and raise from
  the hook; doesn't interrupt blocking C calls.
- **Introspection**: `jobqd status`, or the table is right there in sqlite3.
