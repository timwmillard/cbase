/*
 * net.h — minimal cross-platform TCP client socket shim.
 *
 * Single header, zero dependencies beyond the OS. Wraps the ~5%
 * of the sockets API that differs between Winsock and BSD sockets
 * (Linux/macOS/BSD). Client-side TCP only, by design.
 *
 * Usage:
 *   #define NET_IMPLEMENTATION
 *   #include "net.h"          // in exactly ONE .c file
 *
 *   #include "net.h"          // everywhere else
 *
 * Example:
 *   net_init();
 *   net_socket_t s = net_connect("example.com", "443");
 *   if (s == NET_INVALID_SOCKET) { ... net_last_error() ... }
 *   net_set_nonblocking(s, 1);              // optional
 *   int n = net_send(s, buf, len);          // -1 err, -2 would-block
 *   int m = net_recv(s, buf, cap);          // 0 = peer closed
 *   net_close(s);
 *   net_shutdown_lib();
 *
 * License: public domain / MIT, your choice.
 */

#ifndef NET_H_INCLUDED
#define NET_H_INCLUDED

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Platform types                                                      */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#else
typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at program start / end. No-ops outside Windows. */
int net_init(void);
void net_shutdown_lib(void);

/* Resolve host (name or IP literal) and connect. Blocking.
 * Returns NET_INVALID_SOCKET on failure. */
net_socket_t net_connect(const char *host, const char *port);

/* 1 = non-blocking, 0 = blocking. Returns 0 on success. */
int net_set_nonblocking(net_socket_t s, int nonblocking);

/* Disable Nagle (useful for request/response protocols like TLS
 * handshakes). Returns 0 on success. */
int net_set_nodelay(net_socket_t s, int nodelay);

/* Returns bytes sent/received (>0), or:
 *   0  : (recv only) peer closed the connection
 *  -1  : error
 *  -2  : would block (non-blocking socket, try again later)      */
int net_send(net_socket_t s, const void *buf, int len);
int net_recv(net_socket_t s, void *buf, int cap);

/* Wait until socket is readable (net_wait_read) or writable
 * (net_wait_write). timeout_ms < 0 waits forever.
 * Returns 1 = ready, 0 = timeout, -1 = error.
 * net_wait_write is also how you complete a non-blocking connect. */
int net_wait_read(net_socket_t s, int timeout_ms);
int net_wait_write(net_socket_t s, int timeout_ms);

void net_close(net_socket_t s);

/* Last OS error code (errno / WSAGetLastError). */
int net_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_H_INCLUDED */

/* ================================================================== */
/* Implementation                                                      */
/* ================================================================== */

#ifdef NET_IMPLEMENTATION

#ifdef _WIN32
/* winsock2.h already included above */
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* ---- init / shutdown --------------------------------------------- */

int net_init(void) {
#ifdef _WIN32
   WSADATA wsa;
   return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
   return 0;
#endif
}

void net_shutdown_lib(void) {
#ifdef _WIN32
   WSACleanup();
#endif
}

/* ---- error handling ---------------------------------------------- */

int net_last_error(void) {
#ifdef _WIN32
   return WSAGetLastError();
#else
   return errno;
#endif
}

static int net__would_block(void) {
#ifdef _WIN32
   int e = WSAGetLastError();
   return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
   return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

/* ---- connect ------------------------------------------------------ */

net_socket_t net_connect(const char *host, const char *port) {
   struct addrinfo hints, *res = NULL, *ai;
   net_socket_t s = NET_INVALID_SOCKET;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
   hints.ai_socktype = SOCK_STREAM;

   if (getaddrinfo(host, port, &hints, &res) != 0)
      return NET_INVALID_SOCKET;

   /* Try each resolved address until one connects. */
   for (ai = res; ai; ai = ai->ai_next) {
      s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (s == NET_INVALID_SOCKET)
         continue;
      if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0)
         break; /* connected */
      net_close(s);
      s = NET_INVALID_SOCKET;
   }

   freeaddrinfo(res);
   return s;
}

/* ---- socket options ----------------------------------------------- */

int net_set_nonblocking(net_socket_t s, int nonblocking) {
#ifdef _WIN32
   u_long mode = nonblocking ? 1 : 0;
   return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
   int flags = fcntl(s, F_GETFL, 0);
   if (flags < 0)
      return -1;
   flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
   return fcntl(s, F_SETFL, flags) == 0 ? 0 : -1;
#endif
}

int net_set_nodelay(net_socket_t s, int nodelay) {
   int v = nodelay ? 1 : 0;
   return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&v,
                     sizeof(v)) == 0
              ? 0
              : -1;
}

/* ---- I/O ----------------------------------------------------------- */

int net_send(net_socket_t s, const void *buf, int len) {
   int n = (int)send(s, (const char *)buf, len, 0);
   if (n >= 0)
      return n;
   return net__would_block() ? -2 : -1;
}

int net_recv(net_socket_t s, void *buf, int cap) {
   int n = (int)recv(s, (char *)buf, cap, 0);
   if (n > 0)
      return n;
   if (n == 0)
      return 0; /* peer closed */
   return net__would_block() ? -2 : -1;
}

/* ---- readiness waiting (select: portable everywhere) --------------- */

static int net__wait(net_socket_t s, int for_write, int timeout_ms) {
   fd_set set;
   struct timeval tv, *ptv = NULL;

   FD_ZERO(&set);
   FD_SET(s, &set);

   if (timeout_ms >= 0) {
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      ptv = &tv;
   }

   /* First arg is ignored on Windows; must be max fd + 1 on POSIX. */
   int r = select((int)s + 1, for_write ? NULL : &set, for_write ? &set : NULL,
                  NULL, ptv);
   if (r > 0)
      return 1;
   if (r == 0)
      return 0;
#ifndef _WIN32
   if (errno == EINTR)
      return 0; /* treat signal as timeout */
#endif
   return -1;
}

int net_wait_read(net_socket_t s, int timeout_ms) {
   return net__wait(s, 0, timeout_ms);
}

int net_wait_write(net_socket_t s, int timeout_ms) {
   return net__wait(s, 1, timeout_ms);
}

/* ---- close --------------------------------------------------------- */

void net_close(net_socket_t s) {
   if (s == NET_INVALID_SOCKET)
      return;
#ifdef _WIN32
   closesocket(s);
#else
   close(s);
#endif
}

#endif /* NET_IMPLEMENTATION */
