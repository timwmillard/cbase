/*******************************************************************************

net_blocking.c — plain blocking usage of net.h.

Connects to example.com:80, sends a simple HTTP GET, and prints
whatever comes back until the peer closes the connection.

Build & run:
cc -I../src -o /tmp/net_blocking net_blocking.c && /tmp/net_blocking

*******************************************************************************/

#define NET_IMPLEMENTATION
#include "net.h"

#include <stdio.h>
#include <string.h>

static const char *request = "GET / HTTP/1.1\r\n"
                             "Host: example.com\r\n"
                             "Connection: close\r\n"
                             "\r\n";

int main(void) {
   net_init();

   net_socket s = net_connect("example.com", "80");
   if (s == NET_INVALID_SOCKET) {
      fprintf(stderr, "connect failed: errno %d\n", net_last_error());
      net_shutdown_lib();
      return 1;
   }

   /* Blocking send: for a request this small it will normally go out
    * in one call, but loop to be correct in general -- send() is
    * allowed to write fewer bytes than asked. */
   const char *p = request;
   int left = (int)strlen(request);
   while (left > 0) {
      int n = net_send(s, p, left);
      if (n < 0) { /* blocking socket: -1 is the only error, no -2 */
         fprintf(stderr, "send failed: errno %d\n", net_last_error());
         net_close(s);
         net_shutdown_lib();
         return 1;
      }
      p += n;
      left -= n;
   }

   /* Blocking recv: each call waits for data. Loop until the server
    * closes the connection (n == 0). */
   char buf[4096];
   for (;;) {
      int n = net_recv(s, buf, sizeof(buf));
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
