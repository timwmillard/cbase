#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net_sys.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#endif

typedef struct {
   void (*fn)(void *);
   void *arg;
} thread_start;

#ifdef _WIN32

void net_mutex_init(net_mutex *m) {
   InitializeCriticalSection(m);
}

void net_mutex_destroy(net_mutex *m) {
   DeleteCriticalSection(m);
}

void net_mutex_lock(net_mutex *m) {
   EnterCriticalSection(m);
}

void net_mutex_unlock(net_mutex *m) {
   LeaveCriticalSection(m);
}

static DWORD WINAPI thread_main(LPVOID arg) {
   thread_start s = *(thread_start *)arg;
   free(arg);
   s.fn(s.arg);
   return 0;
}

bool net_thread_start(void (*fn)(void *), void *arg) {
   thread_start *s = malloc(sizeof(*s));
   if (!s)
      return false;
   s->fn = fn;
   s->arg = arg;
   HANDLE t = CreateThread(NULL, 0, thread_main, s, 0, NULL);
   if (!t) {
      free(s);
      return false;
   }
   CloseHandle(t);
   return true;
}

int64_t net_now_ms(void) {
   return (int64_t)GetTickCount64();
}

void net_random(void *out, size_t n) {
   BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

#else

void net_mutex_init(net_mutex *m) {
   pthread_mutex_init(m, NULL);
}

void net_mutex_destroy(net_mutex *m) {
   pthread_mutex_destroy(m);
}

void net_mutex_lock(net_mutex *m) {
   pthread_mutex_lock(m);
}

void net_mutex_unlock(net_mutex *m) {
   pthread_mutex_unlock(m);
}

static void *thread_main(void *arg) {
   thread_start s = *(thread_start *)arg;
   free(arg);
   // Writes to a closed socket return EPIPE instead of killing the process.
   sigset_t sigpipe;
   sigemptyset(&sigpipe);
   sigaddset(&sigpipe, SIGPIPE);
   pthread_sigmask(SIG_BLOCK, &sigpipe, NULL);
   s.fn(s.arg);
   return NULL;
}

bool net_thread_start(void (*fn)(void *), void *arg) {
   thread_start *s = malloc(sizeof(*s));
   if (!s)
      return false;
   s->fn = fn;
   s->arg = arg;
   pthread_attr_t attr;
   pthread_attr_init(&attr);
   pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
   pthread_t t;
   int r = pthread_create(&t, &attr, thread_main, s);
   pthread_attr_destroy(&attr);
   if (r != 0) {
      free(s);
      return false;
   }
   return true;
}

int64_t net_now_ms(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void net_random(void *out, size_t n) {
   uint8_t *p = out;
   size_t got = 0;
   int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
   if (fd >= 0) {
      while (got < n) {
         ssize_t r = read(fd, p + got, n - got);
         if (r <= 0)
            break;
         got += (size_t)r;
      }
      close(fd);
   }
   // No /dev/urandom (a chroot): the clock and an address beat zeros.
   uint64_t s = (uint64_t)net_now_ms() ^ (uint64_t)(uintptr_t)out ^
                0x9E3779B97F4A7C15ull;
   for (; got < n; got++) {
      s ^= s >> 12;
      s ^= s << 25;
      s ^= s >> 27;
      p[got] = (uint8_t)((s * 2685821657736338717ull) >> 56);
   }
}

#endif

char **net_strv_copy(const char *const *list, bool *ok) {
   if (!list)
      return NULL;
   size_t n = 0;
   while (list[n])
      n++;
   char **copy = calloc(n + 1, sizeof(*copy));
   if (!copy) {
      *ok = false;
      return NULL;
   }
   for (size_t i = 0; i < n; i++) {
      size_t len = strlen(list[i]) + 1;
      copy[i] = malloc(len);
      if (!copy[i]) {
         *ok = false; // the list ends here, so net_strv_free frees it all
         break;
      }
      memcpy(copy[i], list[i], len);
   }
   return copy;
}

void net_strv_free(char **list) {
   for (size_t i = 0; list && list[i]; i++)
      free(list[i]);
   free(list);
}
