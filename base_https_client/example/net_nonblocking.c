/* net_nonblocking.c — non-blocking usage of net.h.
 *
 * Same request as net_blocking.c (GET example.com:80), but send/recv
 * are done on a non-blocking socket: every call that would otherwise
 * block instead returns -2, and net_wait_write/net_wait_read are used
 * to sleep until the socket is ready before retrying. This is the
 * shape a real event loop would drive net.h with, just single-
 * connection and synchronous here for clarity.
 *
 * Note: net_connect() itself is always a blocking call -- net.h has
 * no async connect. Non-blocking mode is switched on afterwards, for
 * the send/recv phase.
 *
 * Build & run:
 *   cc -I../src -o /tmp/net_nonblocking net_nonblocking.c && /tmp/net_nonblocking
 */

#define NET_IMPLEMENTATION
#include "net.h"

#include <stdio.h>
#include <string.h>

#define WAIT_TIMEOUT_MS 5000

static const char *request = "GET / HTTP/1.1\r\n"
                             "Host: example.com\r\n"
                             "Connection: close\r\n"
                             "\r\n";

int main(void) {
   net_init();

   net_socket s = net_connect("example.com", "80"); /* still blocking */
   if (s == NET_INVALID_SOCKET) {
      fprintf(stderr, "connect failed: errno %d\n", net_last_error());
      net_shutdown_lib();
      return 1;
   }

   if (net_set_nonblocking(s, 1) != 0) {
      fprintf(stderr, "set_nonblocking failed: errno %d\n", net_last_error());
      net_close(s);
      net_shutdown_lib();
      return 1;
   }

   /* Non-blocking send: retry on -2 (would-block) after waiting for
    * the socket to become writable. */
   const char *p = request;
   int left = (int)strlen(request);
   while (left > 0) {
      int n = net_send(s, p, left);
      if (n == -2) {
         int r = net_wait_write(s, WAIT_TIMEOUT_MS);
         if (r != 1) {
            fprintf(stderr, "timed out waiting to send\n");
            net_close(s);
            net_shutdown_lib();
            return 1;
         }
         continue;
      }
      if (n < 0) {
         fprintf(stderr, "send failed: errno %d\n", net_last_error());
         net_close(s);
         net_shutdown_lib();
         return 1;
      }
      p += n;
      left -= n;
   }

   /* Non-blocking recv: retry on -2 after waiting for the socket to
    * become readable. n == 0 still means the peer closed. */
   char buf[4096];
   for (;;) {
      int n = net_recv(s, buf, sizeof(buf));
      if (n == -2) {
         int r = net_wait_read(s, WAIT_TIMEOUT_MS);
         if (r != 1) {
            fprintf(stderr, "timed out waiting for data\n");
            net_close(s);
            net_shutdown_lib();
            return 1;
         }
         continue;
      }
      if (n == 0)
         break; /* peer closed -- response complete */
      if (n < 0) {
         fprintf(stderr, "recv failed: errno %d\n", net_last_error());
         net_close(s);
         net_shutdown_lib();
         return 1;
      }
      fwrite(buf, 1, (size_t)n, stdout);
   }

   net_close(s);
   net_shutdown_lib();
   return 0;
}
