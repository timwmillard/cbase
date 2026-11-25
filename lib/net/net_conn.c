/* Picks the core's backend for this platform. Define NET_BACKEND_UNIX to use
 * the POSIX backend on Apple systems too (the tests do, to cover it on
 * macOS). */

#if defined(__APPLE__) && !defined(NET_BACKEND_UNIX)
#include "net_conn_apple.c"
#elif defined(_WIN32)
#include "net_conn_win32.c"
#elif defined(__EMSCRIPTEN__)
#error "net: browsers have no sockets; see HTTP_H.md (browser backends)"
#else
#include "net_conn_unix.c"
#endif

/* Shared by every backend */

// Waits for events, ignoring wake-ups. False at the deadline or on error.
static bool wait_for(net_conn *c, int events, int64_t deadline) {
   for (;;) {
      int r = net_conn_wait(c, events, deadline);
      if (r == 0)
         snprintf(c->error, sizeof(c->error), "timed out");
      if (r <= 0)
         return false;
      if (r & events)
         return true;
   }
}

bool net_conn_write_all(net_conn *c, const void *buf, size_t n,
                        int64_t deadline) {
   const char *p = buf;
   while (n) {
      long r = net_conn_write(c, p, n);
      if (r == NET_ERR)
         return false;
      if (r == NET_AGAIN) {
         if (!wait_for(c, NET_WRITE, deadline))
            return false;
         continue;
      }
      p += r;
      n -= (size_t)r;
   }
   return true;
}

long net_conn_read_some(net_conn *c, void *buf, size_t n, int64_t deadline) {
   for (;;) {
      long r = net_conn_read(c, buf, n);
      if (r != NET_AGAIN)
         return r;
      if (!wait_for(c, NET_READ, deadline))
         return NET_ERR;
   }
}
