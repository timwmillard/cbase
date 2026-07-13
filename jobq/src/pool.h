#ifndef JOBQ_POOL_H
#define JOBQ_POOL_H

#include <pthread.h>
#include <stdint.h>

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
