/* jobqd: standalone worker daemon + CLI for the jobq library.
 *
 *   jobqd [options]                          run worker daemon
 *   jobqd [options] enqueue <kind> [json]    enqueue one job and exit
 *   jobqd [options] status                   print queue counts and exit
 *
 * Options:
 *   -d PATH   database file          (default: jobs.db)
 *   -l PATH   handlers script        (default: jobs.lua)
 *   -n N      worker threads         (default: 4)
 *   -t SECS   rescue timeout         (default: 300)
 *   -u KEY    unique key             (enqueue only)
 *   -a SECS   run after delay        (enqueue only)
 */
#include "jobq.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RESCUE_EVERY 30

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static int cmd_enqueue(sqlite3 *db, const char *kind, const char *args,
                       const char *ukey, long delay) {
  int64_t id = jobq_enqueue(db, kind, args, ukey,
                            delay > 0 ? time(NULL) + delay : 0, 0, 5);
  if (id < 0) {
    fprintf(stderr, "enqueue failed\n");
    return 1;
  }
  if (id == 0) {
    printf("skipped (duplicate unique_key)\n");
    return 0;
  }
  printf("%lld\n", (long long)id);
  return 0;
}

static int cmd_status(sqlite3 *db) {
  sqlite3_stmt *st;
  const char *sql =
      "SELECT state, kind, count(*) FROM jobs GROUP BY 1,2 ORDER BY 1,2;";
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
    return 1;
  while (sqlite3_step(st) == SQLITE_ROW)
    printf("%-10s %-24s %lld\n", sqlite3_column_text(st, 0),
           sqlite3_column_text(st, 1), (long long)sqlite3_column_int64(st, 2));
  sqlite3_finalize(st);
  return 0;
}

int main(int argc, char **argv) {
  const char *db_path = "jobs.db", *lua_path = "jobs.lua", *ukey = NULL;
  int nworkers = 4;
  long rescue_timeout = 300, delay = 0;

  int opt;
  while ((opt = getopt(argc, argv, "d:l:n:t:u:a:h")) != -1) {
    switch (opt) {
    case 'd':
      db_path = optarg;
      break;
    case 'l':
      lua_path = optarg;
      break;
    case 'n':
      nworkers = atoi(optarg);
      break;
    case 't':
      rescue_timeout = atol(optarg);
      break;
    case 'u':
      ukey = optarg;
      break;
    case 'a':
      delay = atol(optarg);
      break;
    default:
      fprintf(stderr,
              "usage: %s [-d db] [-l jobs.lua] [-n workers]"
              " [-t rescue_secs] [enqueue <kind> [json] [-u key]"
              " [-a delay] | status]\n",
              argv[0]);
      return opt == 'h' ? 0 : 2;
    }
  }

  sqlite3 *db = NULL;
  if (jobq_open(db_path, &db) != 0)
    return 1;

  /* Subcommands (no workers started) */
  if (optind < argc) {
    const char *cmd = argv[optind];
    int rc = 2;
    if (strcmp(cmd, "enqueue") == 0 && optind + 1 < argc) {
      if (jobq_migrate(db) != 0)
        return 1;
      rc =
          cmd_enqueue(db, argv[optind + 1],
                      optind + 2 < argc ? argv[optind + 2] : "{}", ukey, delay);
    } else if (strcmp(cmd, "status") == 0) {
      rc = cmd_status(db);
    } else {
      fprintf(stderr, "unknown command: %s\n", cmd);
    }
    sqlite3_close(db);
    return rc;
  }

  /* Daemon mode */
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGPIPE, SIG_IGN);

  if (jobq_migrate(db) != 0)
    return 1; /* also rescues startup orphans */

  jobq_pool pool;
  if (jobq_pool_start(&pool, db_path, lua_path, nworkers) != 0) {
    fprintf(stderr, "failed to start worker pool\n");
    return 1;
  }
  fprintf(stderr, "jobqd: %d workers, db=%s handlers=%s\n", nworkers, db_path,
          lua_path);

  int tick = 0;
  while (!g_stop) {
    sleep(1);
    if (++tick % RESCUE_EVERY == 0) {
      int n = jobq_rescue(db, rescue_timeout);
      if (n > 0) {
        fprintf(stderr, "jobqd: rescued %d stuck job(s)\n", n);
        jobq_pool_kick(&pool);
      }
    }
  }

  fprintf(stderr, "jobqd: shutting down...\n");
  jobq_pool_stop(&pool);
  sqlite3_close(db);
  return 0;
}
