/* net_sys.h — the little the net library needs from the OS besides
 * networking: mutexes, detached threads, a monotonic clock, random bytes.
 * Internal.
 */

#ifndef NET_SYS_H
#define NET_SYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef CRITICAL_SECTION net_mutex;
#else
#include <pthread.h>
typedef pthread_mutex_t net_mutex;
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define NET_THREAD_LOCAL __declspec(thread)
#else
#define NET_THREAD_LOCAL _Thread_local
#endif

void net_mutex_init(net_mutex *m);
void net_mutex_destroy(net_mutex *m);
void net_mutex_lock(net_mutex *m);
void net_mutex_unlock(net_mutex *m);

// Runs fn(arg) on a new detached thread, with SIGPIPE blocked on POSIX.
bool net_thread_start(void (*fn)(void *), void *arg);

int64_t net_now_ms(void);
// From the OS's random source; good enough for masks and nonces.
void net_random(void *out, size_t n);

// NULL-terminated string lists, copied for the network thread. *ok becomes
// false when out of memory.
char **net_strv_copy(const char *const *list, bool *ok);
void net_strv_free(char **list);

#endif
