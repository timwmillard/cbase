#include "pool.h"
#include "queue.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_INTERVAL_SECS 1   /* backstop for scheduled/retry jobs */

typedef struct {
    jobq_pool *pool;
    int        index;
} worker_arg;

/* Each worker owns one connection and one lua_State: no locks needed
 * around either. lua_State is NOT thread-safe; never share it. */
typedef struct {
    sqlite3   *db;
    lua_State *L;
    char       name[32];
} worker_ctx;

/* ---- Lua bindings available to handlers ------------------------------ */

/* jobq.enqueue(kind, args_json [, opts])
 * opts: { unique_key=, run_at=, priority=, max_attempts= }
 * Lets jobs enqueue follow-up jobs. args must already be a JSON string
 * (encode in Lua; see jobs.lua canonical_json for unique keys). */
static int l_enqueue(lua_State *L)
{
    worker_ctx *ctx = (worker_ctx *)lua_touserdata(L, lua_upvalueindex(1));
    const char *kind = luaL_checkstring(L, 1);
    const char *args = luaL_optstring(L, 2, "{}");

    const char *ukey = NULL;
    int64_t run_at = 0; int prio = 0, maxa = 5;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "unique_key");
        ukey = lua_tostring(L, -1);          /* stays valid: left on stack */
        lua_getfield(L, 3, "run_at");
        run_at = (int64_t)lua_tointeger(L, -1);
        lua_getfield(L, 3, "priority");
        prio = (int)lua_tointeger(L, -1);
        lua_getfield(L, 3, "max_attempts");
        if (!lua_isnil(L, -1)) maxa = (int)lua_tointeger(L, -1);
        lua_pop(L, 3);                       /* keep unique_key string alive */
    }

    int64_t id = jobq_enqueue(ctx->db, kind, args, ukey, run_at, prio, maxa);
    if (id < 0) return luaL_error(L, "jobq.enqueue failed");
    lua_pushinteger(L, (lua_Integer)id);   /* 0 = skipped duplicate */
    return 1;
}

/* jobq.heartbeat(id): call from long-running handlers so the rescue
 * sweeper doesn't reclaim the job out from under you. */
static int l_heartbeat(lua_State *L)
{
    worker_ctx *ctx = (worker_ctx *)lua_touserdata(L, lua_upvalueindex(1));
    jobq_heartbeat(ctx->db, (int64_t)luaL_checkinteger(L, 1));
    return 0;
}

static int setup_lua(worker_ctx *ctx, const char *jobs_lua_path)
{
    ctx->L = luaL_newstate();
    if (!ctx->L) return -1;
    luaL_openlibs(ctx->L);

    /* jobq table with C functions closing over this worker's ctx */
    lua_newtable(ctx->L);
    lua_pushlightuserdata(ctx->L, ctx);
    lua_pushcclosure(ctx->L, l_enqueue, 1);
    lua_setfield(ctx->L, -2, "enqueue");
    lua_pushlightuserdata(ctx->L, ctx);
    lua_pushcclosure(ctx->L, l_heartbeat, 1);
    lua_setfield(ctx->L, -2, "heartbeat");
    lua_setglobal(ctx->L, "jobq");

    if (luaL_dofile(ctx->L, jobs_lua_path) != LUA_OK) {
        fprintf(stderr, "%s: loading %s: %s\n", ctx->name, jobs_lua_path,
                lua_tostring(ctx->L, -1));
        return -1;
    }
    return 0;
}

/* ---- Job execution ---------------------------------------------------- */

static void run_job(worker_ctx *ctx, jobq_job *job)
{
    lua_State *L = ctx->L;
    int base = lua_gettop(L);

    lua_getglobal(L, "dispatch");            /* defined in jobs.lua */
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
        fprintf(stderr, "%s: job %lld (%s) attempt %d/%d failed: %s\n",
                ctx->name, (long long)job->id, job->kind,
                job->attempt, job->max_attempts, err ? err : "?");
        jobq_fail(ctx->db, job, err);
    }
    lua_settop(L, base);
    /* lua_gc(L, LUA_GCSTEP, 0);  -- optional nudge between jobs */
}

/* ---- Worker loop ------------------------------------------------------ */

static void *worker_main(void *argp)
{
    worker_arg *wa = (worker_arg *)argp;
    jobq_pool  *p  = wa->pool;
    worker_ctx  ctx = {0};
    snprintf(ctx.name, sizeof ctx.name, "worker-%d", wa->index);
    free(wa);

    if (jobq_open(p->db_path, &ctx.db) != 0) return NULL;
    if (setup_lua(&ctx, p->jobs_lua_path) != 0) goto out;

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
        if (p->shutdown) { pthread_mutex_unlock(&p->mu); break; }
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += POLL_INTERVAL_SECS;
        pthread_cond_timedwait(&p->cv, &p->mu, &deadline);
        int stop = p->shutdown;
        pthread_mutex_unlock(&p->mu);
        if (stop) break;
    }

out:
    if (ctx.L)  lua_close(ctx.L);
    if (ctx.db) sqlite3_close(ctx.db);
    return NULL;
}

/* ---- Pool lifecycle ---------------------------------------------------- */

int jobq_pool_start(jobq_pool *p, const char *db_path,
                    const char *jobs_lua_path, int nthreads)
{
    memset(p, 0, sizeof *p);
    p->db_path = db_path;
    p->jobs_lua_path = jobs_lua_path;
    p->nthreads = nthreads;
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);

    p->threads = calloc((size_t)nthreads, sizeof(pthread_t));
    if (!p->threads) return -1;

    for (int i = 0; i < nthreads; i++) {
        worker_arg *wa = malloc(sizeof *wa);
        wa->pool = p; wa->index = i;
        if (pthread_create(&p->threads[i], NULL, worker_main, wa) != 0) {
            free(wa);
            p->nthreads = i;
            jobq_pool_stop(p);
            return -1;
        }
    }
    return 0;
}

void jobq_pool_kick(jobq_pool *p)
{
    pthread_mutex_lock(&p->mu);
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);
}

void jobq_pool_stop(jobq_pool *p)
{
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
