/* net_test.c — simple sanity tests for net.h.
 *
 * net.h is client-only by design, so exercising a real round trip
 * still needs something listening on the other end. Rather than fork
 * a separate acceptor process (which would make this file platform-
 * specific for no good reason), this relies on a property of TCP: a
 * blocking connect() to a listening socket completes as soon as the
 * SYN is queued in the accept backlog -- *before* anyone calls
 * accept(). So the whole test runs single-threaded and portably:
 * bind+listen, net_connect(), then accept() just dequeues the
 * already-established connection.
 *
 * Build & run:
 *   cc -Isrc -o /tmp/net_test src/net_test.c && /tmp/net_test
 */

#include <stdio.h>
#include <string.h> /* system string.h (strlen/strncmp) */

#define NET_IMPLEMENTATION
#include "net.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_BOLD  "\033[1m"
#define COLOR_RESET "\033[0m"

#define PASS(name) printf(COLOR_GREEN "  PASS" COLOR_RESET "  %s\n", name)

static int _failed = 0;

/* ASSERT(expr, fmt, ...): on failure prints the owning test_name (an in-scope
 * string each test declares) plus a printf-style message and the expression. */
#define ASSERT(expr, ...)                                                      \
   do {                                                                        \
      if (!(expr)) {                                                           \
         printf(COLOR_RED "  FAIL" COLOR_RESET "  %s:%d  [%s] ", __FILE__,     \
                __LINE__, test_name);                                          \
         printf(__VA_ARGS__);                                                  \
         printf("  (%s)\n", #expr);                                            \
         _failed++;                                                            \
      }                                                                        \
   } while (0)

#define ASSERT_STR(s, cstr)                                                    \
   ASSERT((s).len == strlen(cstr) && strncmp((s).data, cstr, (s).len) == 0,    \
          "%s == \"%s\"", #s, cstr)

/* ================================================================== */
/* init / connect                                                      */
/* ================================================================== */

void test_init(void) {
   const char *test_name = "init";
   ASSERT(net_init() == 0, "net_init succeeds");
   PASS(test_name);
}

void test_connect_failure(void) {
   const char *test_name = "connect failure";

   /* Nothing listens on loopback port 1. */
   net_socket s = net_connect("127.0.0.1", "1");
   ASSERT(s == NET_INVALID_SOCKET, "connect to a closed port fails");

   /* Unresolvable hostname. */
   net_socket s2 = net_connect("this-host-should-not-resolve.invalid", "80");
   ASSERT(s2 == NET_INVALID_SOCKET, "connect to an unresolvable host fails");

   PASS(test_name);
}

void test_nonblocking_invalid_socket(void) {
   const char *test_name = "nonblocking invalid socket";
   ASSERT(net_set_nonblocking(NET_INVALID_SOCKET, 1) == -1,
          "rejects an invalid socket");
   PASS(test_name);
}

/* ================================================================== */
/* loopback helpers                                                    */
/* ================================================================== */

/* Binds an OS-assigned loopback port and starts listening.
 * Writes the chosen port (host byte order) to *port_out. */
static net_socket start_server(int *port_out) {
   net_socket srv = socket(AF_INET, SOCK_STREAM, 0);
   if (srv == NET_INVALID_SOCKET)
      return NET_INVALID_SOCKET;

   int yes = 1;
   setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

   struct sockaddr_in addr = {0};
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   addr.sin_port = 0; /* let the OS pick a free port */

   if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
       listen(srv, 1) != 0) {
      net_close(srv);
      return NET_INVALID_SOCKET;
   }

   socklen_t alen = sizeof(addr);
   getsockname(srv, (struct sockaddr *)&addr, &alen);
   *port_out = ntohs(addr.sin_port);
   return srv;
}

/* Connects to 127.0.0.1:port via net_connect (*client_out) and returns
 * the matching peer connection dequeued from the server's backlog. */
static net_socket accept_client(net_socket srv, int port,
                                net_socket *client_out) {
   char port_str[16];
   snprintf(port_str, sizeof(port_str), "%d", port);

   *client_out = net_connect("127.0.0.1", port_str);
   if (*client_out == NET_INVALID_SOCKET)
      return NET_INVALID_SOCKET;

   return accept(srv, NULL, NULL);
}

/* ================================================================== */
/* send / recv / wait                                                  */
/* ================================================================== */

/* Round trip: client sends "ping" via net.h, server (a raw socket)
 * echoes it back, client reads it via net.h. */
void test_send_recv_roundtrip(void) {
   const char *test_name = "send recv roundtrip";

   int port;
   net_socket srv = start_server(&port);
   ASSERT(srv != NET_INVALID_SOCKET, "server binds and listens");
   if (srv == NET_INVALID_SOCKET)
      return;

   net_socket client;
   net_socket peer = accept_client(srv, port, &client);
   ASSERT(client != NET_INVALID_SOCKET, "net_connect succeeds");
   ASSERT(peer != NET_INVALID_SOCKET, "server accepts the connection");

   ASSERT(net_set_nodelay(client, 1) == 0, "nodelay can be set");

   const char *msg = "ping";
   int sent = net_send(client, msg, (int)strlen(msg));
   ASSERT(sent == (int)strlen(msg), "sends all %zu bytes", strlen(msg));

   char echo_buf[256];
   int echoed = (int)recv(peer, echo_buf, sizeof(echo_buf), 0);
   ASSERT(echoed == (int)strlen(msg), "server receives the message");
   send(peer, echo_buf, echoed, 0);

   ASSERT(net_wait_read(client, 1000) == 1, "client sees data ready");

   char buf[256] = {0};
   int got = net_recv(client, buf, sizeof(buf) - 1);
   ASSERT(got == (int)strlen(msg), "receives the echoed reply");
   ASSERT(got > 0 && memcmp(buf, msg, got) == 0, "reply content matches");

   net_close(client);
   net_close(peer);
   net_close(srv);
   PASS(test_name);
}

/* Server hangs up immediately; net_recv should report peer-closed (0). */
void test_peer_close(void) {
   const char *test_name = "peer close";

   int port;
   net_socket srv = start_server(&port);
   ASSERT(srv != NET_INVALID_SOCKET, "server binds and listens");
   if (srv == NET_INVALID_SOCKET)
      return;

   net_socket client;
   net_socket peer = accept_client(srv, port, &client);
   ASSERT(client != NET_INVALID_SOCKET, "net_connect succeeds");
   ASSERT(peer != NET_INVALID_SOCKET, "server accepts the connection");

   net_close(peer); /* hang up before the client reads anything */

   ASSERT(net_wait_read(client, 1000) == 1, "client sees the close as ready");

   char buf[16];
   int n = net_recv(client, buf, sizeof(buf));
   ASSERT(n == 0, "recv reports peer closed");

   net_close(client);
   net_close(srv);
   PASS(test_name);
}

/* Server accepts but never sends anything: net_wait_read should time out. */
void test_wait_read_timeout(void) {
   const char *test_name = "wait read timeout";

   int port;
   net_socket srv = start_server(&port);
   ASSERT(srv != NET_INVALID_SOCKET, "server binds and listens");
   if (srv == NET_INVALID_SOCKET)
      return;

   net_socket client;
   net_socket peer = accept_client(srv, port, &client);
   ASSERT(client != NET_INVALID_SOCKET, "net_connect succeeds");
   ASSERT(peer != NET_INVALID_SOCKET, "server accepts the connection");

   ASSERT(net_wait_read(client, 100) == 0, "times out when nothing is sent");

   net_close(client);
   net_close(peer);
   net_close(srv);
   PASS(test_name);
}

int main(void) {
   printf(COLOR_BOLD "\nnet tests\n" COLOR_RESET "\n");

   test_init();
   test_connect_failure();
   test_nonblocking_invalid_socket();
   test_send_recv_roundtrip();
   test_peer_close();
   test_wait_read_timeout();

   net_shutdown_lib();

   if (_failed == 0)
      printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET
             "\n\n");
   else
      printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n",
             _failed);
   return _failed != 0;
}
