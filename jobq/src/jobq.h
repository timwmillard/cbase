/* jobq: embedded SQLite-backed job queue with Lua handlers.
 *
 * Library usage:
 *   #include <jobq.h>, link -ljobq -lsqlite3 -llua5.4 -lpthread
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

#include "queue.h"
#include "pool.h"

#define JOBQ_VERSION_MAJOR 0
#define JOBQ_VERSION_MINOR 1

#endif
