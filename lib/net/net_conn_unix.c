/* Unix core: POSIX sockets and poll, and for TLS the system's libssl
 * (OpenSSL 1.1+, OpenSSL 3 or LibreSSL) loaded at runtime, so the build needs
 * no TLS headers or library. A pipe is the wake-up.
 */

#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE // SO_NOSIGPIPE
#endif

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net_conn.h"
#include "net_sys.h"

/* libssl, resolved at runtime */

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;
typedef struct x509_verify_param_st X509_VERIFY_PARAM;

enum {
   SSL_ERROR_SSL = 1,
   SSL_ERROR_WANT_READ = 2,
   SSL_ERROR_WANT_WRITE = 3,
   SSL_ERROR_SYSCALL = 5,
   SSL_ERROR_ZERO_RETURN = 6,
   SSL_VERIFY_PEER = 1,
   SSL_CTRL_MODE = 33,
   SSL_CTRL_SET_TLSEXT_HOSTNAME = 55,
   SSL_CTRL_SET_MIN_PROTO_VERSION = 123,
   TLSEXT_NAMETYPE_host_name = 0,
   TLS1_2_VERSION = 0x0303,
   SSL_MODE_ENABLE_PARTIAL_WRITE = 0x1,
   SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER = 0x2,
};

static struct {
   const SSL_METHOD *(*TLS_client_method)(void);
   SSL_CTX *(*SSL_CTX_new)(const SSL_METHOD *);
   void (*SSL_CTX_free)(SSL_CTX *);
   long (*SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
   int (*SSL_CTX_set_default_verify_paths)(SSL_CTX *);
   int (*SSL_CTX_load_verify_locations)(SSL_CTX *, const char *, const char *);
   void (*SSL_CTX_set_verify)(SSL_CTX *, int, void *);
   SSL *(*SSL_new)(SSL_CTX *);
   void (*SSL_free)(SSL *);
   int (*SSL_set_fd)(SSL *, int);
   long (*SSL_ctrl)(SSL *, int, long, void *);
   int (*SSL_set1_host)(SSL *, const char *);
   int (*SSL_connect)(SSL *);
   int (*SSL_read)(SSL *, void *, int);
   int (*SSL_write)(SSL *, const void *, int);
   int (*SSL_pending)(const SSL *);
   int (*SSL_get_error)(const SSL *, int);
   int (*SSL_shutdown)(SSL *);
   long (*SSL_get_verify_result)(const SSL *);
   // Optional: better errors, TLS 1.2 minimum on LibreSSL, IP literals.
   int (*SSL_CTX_set_min_proto_version)(SSL_CTX *, int);
   X509_VERIFY_PARAM *(*SSL_get0_param)(SSL *);
   int (*X509_VERIFY_PARAM_set1_ip_asc)(X509_VERIFY_PARAM *, const char *);
   const char *(*X509_verify_cert_error_string)(long);
   unsigned long (*ERR_get_error)(void);
   void (*ERR_error_string_n)(unsigned long, char *, size_t);
   void (*ERR_clear_error)(void);
   bool loaded;
   char error[256];
} ssl;

static pthread_once_t ssl_once = PTHREAD_ONCE_INIT;

static void load_libssl(void) {
   static const char *const names[] = {
       "libssl.so.3",   // Linux, OpenSSL 3
       "libssl.so.30",  // FreeBSD 14
       "libssl.so.1.1", // Linux, OpenSSL 1.1
       "libssl.so.111", // FreeBSD 13
       "libssl.so",     // OpenBSD, NetBSD, DragonFly, illumos
       NULL,
   };
   void *lib = NULL;
   const char *env = getenv("NET_LIBSSL");
   if (env && env[0]) {
      lib = dlopen(env, RTLD_NOW | RTLD_LOCAL);
   } else {
      for (int i = 0; !lib && names[i]; i++)
         lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
   }
   if (!lib) {
      snprintf(ssl.error, sizeof(ssl.error),
               "no usable libssl found (set NET_LIBSSL to its path)");
      return;
   }

   // POSIX guarantees dlsym's result converts to a function pointer.
#define REQUIRED(name)                                                         \
   do {                                                                        \
      *(void **)&ssl.name = dlsym(lib, #name);                                 \
      if (!ssl.name) {                                                         \
         snprintf(ssl.error, sizeof(ssl.error),                                \
                  "libssl is too old (no %s); need OpenSSL 1.1 or newer",      \
                  #name);                                                      \
         return;                                                               \
      }                                                                        \
   } while (0)
#define OPTIONAL(name) *(void **)&ssl.name = dlsym(lib, #name)

   REQUIRED(TLS_client_method);
   REQUIRED(SSL_CTX_new);
   REQUIRED(SSL_CTX_free);
   REQUIRED(SSL_CTX_ctrl);
   REQUIRED(SSL_CTX_set_default_verify_paths);
   REQUIRED(SSL_CTX_load_verify_locations);
   REQUIRED(SSL_CTX_set_verify);
   REQUIRED(SSL_new);
   REQUIRED(SSL_free);
   REQUIRED(SSL_set_fd);
   REQUIRED(SSL_ctrl);
   REQUIRED(SSL_set1_host);
   REQUIRED(SSL_connect);
   REQUIRED(SSL_read);
   REQUIRED(SSL_write);
   REQUIRED(SSL_pending);
   REQUIRED(SSL_get_error);
   REQUIRED(SSL_shutdown);
   REQUIRED(SSL_get_verify_result);
   OPTIONAL(SSL_CTX_set_min_proto_version);
   OPTIONAL(SSL_get0_param);
   OPTIONAL(X509_VERIFY_PARAM_set1_ip_asc);
   OPTIONAL(X509_verify_cert_error_string);
   OPTIONAL(ERR_get_error);
   OPTIONAL(ERR_error_string_n);
   OPTIONAL(ERR_clear_error);
#undef REQUIRED
#undef OPTIONAL
   ssl.loaded = true;
}

/* Connection */

struct net_conn {
   int fd;
   int wake[2];
   atomic_bool cancelled;
   SSL_CTX *ctx;
   SSL *ssl;
   // What TLS needs before a read or write can go on (POLLIN or POLLOUT).
   short read_wants, write_wants;
   char error[256];
};

net_conn *net_conn_new(void) {
   net_conn *c = calloc(1, sizeof(*c));
   if (!c)
      return NULL;
   c->fd = -1;
   c->read_wants = POLLIN;
   c->write_wants = POLLOUT;
   if (pipe(c->wake) != 0) {
      free(c);
      return NULL;
   }
   for (int i = 0; i < 2; i++) {
      fcntl(c->wake[i], F_SETFD, FD_CLOEXEC);
      fcntl(c->wake[i], F_SETFL, fcntl(c->wake[i], F_GETFL) | O_NONBLOCK);
   }
   return c;
}

void net_conn_free(net_conn *c) {
   if (!c)
      return;
   if (c->ssl) {
      ssl.SSL_shutdown(c->ssl);
      ssl.SSL_free(c->ssl);
   }
   if (c->ctx)
      ssl.SSL_CTX_free(c->ctx);
   if (c->fd >= 0)
      close(c->fd);
   close(c->wake[0]);
   close(c->wake[1]);
   free(c);
}

const char *net_conn_error(const net_conn *c) {
   return c->error;
}

void net_conn_wake(net_conn *c) {
   ssize_t r = write(c->wake[1], "x", 1);
   (void)r; // a full pipe already means "wake up"
}

void net_conn_cancel(net_conn *c) {
   atomic_store(&c->cancelled, true);
   net_conn_wake(c);
}

static void drain_wake(net_conn *c) {
   char buf[64];
   while (read(c->wake[0], buf, sizeof(buf)) > 0) {
   }
}

// poll on the socket for events (POLLIN / POLLOUT) and on the wake-up pipe.
static int poll_socket(net_conn *c, short events, int64_t deadline) {
   for (;;) {
      if (atomic_load(&c->cancelled)) {
         snprintf(c->error, sizeof(c->error), "cancelled");
         return NET_ERR;
      }
      int timeout = -1;
      if (deadline >= 0) {
         int64_t left = deadline - net_now_ms();
         if (left <= 0)
            return 0;
         timeout = left > INT_MAX ? INT_MAX : (int)left;
      }
      struct pollfd p[2] = {{c->fd, events, 0}, {c->wake[0], POLLIN, 0}};
      int r = poll(p, 2, timeout);
      if (r < 0) {
         if (errno == EINTR)
            continue;
         snprintf(c->error, sizeof(c->error), "poll: %s", strerror(errno));
         return NET_ERR;
      }
      int ready = 0;
      if (r > 0 && p[1].revents) {
         drain_wake(c);
         if (atomic_load(&c->cancelled))
            continue;
         ready |= NET_WAKE;
      }
      if (r > 0 && p[0].revents)
         ready |= NET_READ | NET_WRITE; // the caller retries what it wanted
      if (ready)
         return ready;
   }
}

int net_conn_wait(net_conn *c, int events, int64_t deadline) {
   // Decrypted bytes waiting in libssl don't show up in poll.
   if ((events & NET_READ) && c->ssl && ssl.SSL_pending(c->ssl) > 0)
      return NET_READ;
   short ev = 0;
   if (events & NET_READ)
      ev |= c->read_wants;
   if (events & NET_WRITE)
      ev |= c->write_wants;
   int r = poll_socket(c, ev, deadline);
   if (r > 0 && (r & (NET_READ | NET_WRITE)))
      r = (r & ~(NET_READ | NET_WRITE)) | (events & (NET_READ | NET_WRITE));
   return r;
}

/* I/O */

static const char *ssl_error_string(char *buf, size_t cap) {
   unsigned long e = ssl.ERR_get_error ? ssl.ERR_get_error() : 0;
   if (e && ssl.ERR_error_string_n) {
      ssl.ERR_error_string_n(e, buf, cap);
      return buf;
   }
   return errno ? strerror(errno) : "TLS error";
}

static long tls_result(net_conn *c, int r, short *want) {
   if (r > 0)
      return r;
   switch (ssl.SSL_get_error(c->ssl, r)) {
   case SSL_ERROR_WANT_READ:
      *want = POLLIN;
      return NET_AGAIN;
   case SSL_ERROR_WANT_WRITE:
      *want = POLLOUT;
      return NET_AGAIN;
   case SSL_ERROR_ZERO_RETURN:
      return NET_EOF;
   case SSL_ERROR_SYSCALL:
      if (r == 0 && errno == 0)
         return NET_EOF; // closed without close_notify (OpenSSL 1.1)
      // fall through
   default: {
      char buf[200];
      snprintf(c->error, sizeof(c->error), "TLS: %s",
               ssl_error_string(buf, sizeof(buf)));
      return NET_ERR;
   }
   }
}

static void tls_prepare(void) {
   if (ssl.ERR_clear_error)
      ssl.ERR_clear_error();
   errno = 0;
}

long net_conn_read(net_conn *c, void *buf, size_t n) {
   if (c->ssl) {
      tls_prepare();
      int r = ssl.SSL_read(c->ssl, buf, n > INT_MAX ? INT_MAX : (int)n);
      c->read_wants = POLLIN;
      return tls_result(c, r, &c->read_wants);
   }
   for (;;) {
      ssize_t r = recv(c->fd, buf, n, 0);
      if (r > 0)
         return (long)r;
      if (r == 0)
         return NET_EOF;
      if (errno == EINTR)
         continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
         return NET_AGAIN;
      snprintf(c->error, sizeof(c->error), "%s", strerror(errno));
      return NET_ERR;
   }
}

long net_conn_write(net_conn *c, const void *buf, size_t n) {
   if (c->ssl) {
      tls_prepare();
      int r = ssl.SSL_write(c->ssl, buf, n > INT_MAX ? INT_MAX : (int)n);
      c->write_wants = POLLOUT;
      long res = tls_result(c, r, &c->write_wants);
      if (res == NET_EOF) {
         snprintf(c->error, sizeof(c->error), "connection closed by peer");
         return NET_ERR;
      }
      return res;
   }
   for (;;) {
      ssize_t r = send(c->fd, buf, n, 0);
      if (r >= 0)
         return (long)r;
      if (errno == EINTR)
         continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
         return NET_AGAIN;
      snprintf(c->error, sizeof(c->error), "%s", strerror(errno));
      return NET_ERR;
   }
}

/* Connecting */

static bool dial_tcp(net_conn *c, const char *host, int port,
                     int64_t deadline) {
   char port_str[8];
   snprintf(port_str, sizeof(port_str), "%d", port);
   struct addrinfo hints = {0}, *list = NULL;
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   int gai = getaddrinfo(host, port_str, &hints, &list);
   if (gai != 0) {
      snprintf(c->error, sizeof(c->error), "%s: %s", host, gai_strerror(gai));
      return false;
   }

   int last_errno = 0;
   bool timed_out = false;
   for (struct addrinfo *a = list; a; a = a->ai_next) {
      int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd < 0) {
         last_errno = errno;
         continue;
      }
      fcntl(fd, F_SETFD, FD_CLOEXEC);
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
      int one = 1;
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
      c->fd = fd;
      if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
         freeaddrinfo(list);
         return true;
      }
      if (errno == EINPROGRESS) {
         int r;
         do // a wake-up while dialing means nothing yet
            r = poll_socket(c, POLLOUT, deadline);
         while (r == NET_WAKE);
         int so_error = 0;
         socklen_t len = sizeof(so_error);
         if (r > 0 &&
             getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 &&
             so_error == 0) {
            freeaddrinfo(list);
            return true;
         }
         if (r < 0) {
            close(fd);
            c->fd = -1;
            freeaddrinfo(list);
            return false; // cancelled; poll_socket set the error
         }
         timed_out = r == 0;
         last_errno = so_error;
      } else {
         last_errno = errno;
      }
      close(fd);
      c->fd = -1;
      if (timed_out)
         break;
   }
   freeaddrinfo(list);
   if (timed_out)
      snprintf(c->error, sizeof(c->error), "connection timed out");
   else
      snprintf(c->error, sizeof(c->error), "connect to %s:%d: %s", host, port,
               strerror(last_errno ? last_errno : ECONNREFUSED));
   return false;
}

static bool is_ip_literal(const char *host) {
   unsigned char buf[16];
   return inet_pton(AF_INET, host, buf) == 1 ||
          inet_pton(AF_INET6, host, buf) == 1;
}

static bool tls_start(net_conn *c, const char *host, int64_t deadline) {
   pthread_once(&ssl_once, load_libssl);
   if (!ssl.loaded) {
      snprintf(c->error, sizeof(c->error), "%s", ssl.error);
      return false;
   }

   c->ctx = ssl.SSL_CTX_new(ssl.TLS_client_method());
   if (!c->ctx) {
      snprintf(c->error, sizeof(c->error), "TLS: cannot create context");
      return false;
   }
   if (ssl.SSL_CTX_set_min_proto_version)
      ssl.SSL_CTX_set_min_proto_version(c->ctx, TLS1_2_VERSION);
   else
      ssl.SSL_CTX_ctrl(c->ctx, SSL_CTRL_SET_MIN_PROTO_VERSION, TLS1_2_VERSION,
                       NULL);
   ssl.SSL_CTX_ctrl(c->ctx, SSL_CTRL_MODE,
                    SSL_MODE_ENABLE_PARTIAL_WRITE |
                        SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
                    NULL);
   ssl.SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
   ssl.SSL_CTX_set_default_verify_paths(c->ctx);
   // For systems whose libssl was built with another default directory.
   static const char *const bundles[] = {
       "/etc/ssl/cert.pem",                      // BSDs, Alpine, macOS
       "/etc/ssl/certs/ca-certificates.crt",     // Debian, Ubuntu, Arch
       "/etc/pki/tls/certs/ca-bundle.crt",       // Fedora, RHEL
       "/etc/ssl/ca-bundle.pem",                 // openSUSE
       "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD ca_root_nss
       NULL,
   };
   for (int i = 0; bundles[i]; i++) {
      if (access(bundles[i], R_OK) == 0)
         ssl.SSL_CTX_load_verify_locations(c->ctx, bundles[i], NULL);
   }
   if (ssl.ERR_clear_error)
      ssl.ERR_clear_error();

   c->ssl = ssl.SSL_new(c->ctx);
   if (!c->ssl || !ssl.SSL_set_fd(c->ssl, c->fd)) {
      snprintf(c->error, sizeof(c->error), "TLS: cannot create connection");
      return false;
   }
   if (is_ip_literal(host)) {
      // No SNI for addresses; the certificate must name the address.
      if (!ssl.SSL_get0_param || !ssl.X509_VERIFY_PARAM_set1_ip_asc ||
          !ssl.X509_VERIFY_PARAM_set1_ip_asc(ssl.SSL_get0_param(c->ssl),
                                             host)) {
         snprintf(c->error, sizeof(c->error),
                  "TLS: cannot verify an IP address with this libssl");
         return false;
      }
   } else {
      ssl.SSL_ctrl(c->ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME,
                   TLSEXT_NAMETYPE_host_name, (void *)host);
      if (!ssl.SSL_set1_host(c->ssl, host)) {
         snprintf(c->error, sizeof(c->error), "TLS: bad host name");
         return false;
      }
   }

   for (;;) {
      tls_prepare();
      int r = ssl.SSL_connect(c->ssl);
      if (r == 1)
         return true;
      short want = 0;
      long res = tls_result(c, r, &want);
      if (res != NET_AGAIN) {
         long verify = ssl.SSL_get_verify_result(c->ssl);
         if (verify != 0)
            snprintf(c->error, sizeof(c->error),
                     "TLS: certificate verify failed: %s",
                     ssl.X509_verify_cert_error_string
                         ? ssl.X509_verify_cert_error_string(verify)
                         : "see X509_V_ERR codes");
         else if (res == NET_EOF)
            snprintf(c->error, sizeof(c->error),
                     "TLS: connection closed during the handshake");
         return false;
      }
      int ready;
      do
         ready = poll_socket(c, want, deadline);
      while (ready == NET_WAKE);
      if (ready == 0)
         snprintf(c->error, sizeof(c->error), "TLS handshake timed out");
      if (ready <= 0)
         return false;
   }
}

bool net_conn_dial(net_conn *c, const char *host, int port, bool tls,
                   int64_t deadline) {
   return dial_tcp(c, host, port, deadline) &&
          (!tls || tls_start(c, host, deadline));
}
