/* Windows core: Winsock with select, and for TLS SChannel through SSPI (the
 * Windows certificate store, automatic validation of the chain and the host
 * name). Windows 8 or later. A loopback UDP socket is the wake-up: other
 * threads send it a datagram, and select watches it with the connection.
 *
 * TLS buffers: in holds received bytes not yet decrypted; DecryptMessage
 * works in place, so plain points into in until it has been read, and then
 * the record is dropped. out holds an encrypted record not yet sent.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_conn.h"
#include "net_sys.h"

struct net_conn {
   SOCKET sock;
   SOCKET wake; // UDP, bound to 127.0.0.1
   struct sockaddr_in wake_addr;
   volatile LONG cancelled;
   bool wsa;

   bool tls;
   CredHandle cred;
   CtxtHandle ctx;
   bool have_cred, have_ctx;
   SecPkgContext_StreamSizes sizes;
   char *in;
   size_t in_len, in_cap;
   char *plain;
   size_t plain_len, record_len; // record_len: bytes of in under plain
   bool incomplete; // in holds only part of a record
   char *out;
   size_t out_len, out_pos, out_cap;
   bool eof;

   char error[256];
};

static void win_error(net_conn *c, const char *doing, DWORD e) {
   char buf[200] = "";
   DWORD n = FormatMessageA(
       FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0,
       buf, sizeof(buf), NULL);
   while (n && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.'))
      buf[--n] = '\0';
   snprintf(c->error, sizeof(c->error), "%s%s%s (%lu)", doing,
            doing[0] ? ": " : "", n ? buf : "error", (unsigned long)e);
}

static void tls_error(net_conn *c, SECURITY_STATUS st) {
   const char *what = NULL;
   switch (st) {
   case SEC_E_UNTRUSTED_ROOT:
   case CERT_E_UNTRUSTEDROOT:
   case CERT_E_CHAINING:
      what = "untrusted certificate authority";
      break;
   case SEC_E_CERT_EXPIRED:
   case CERT_E_EXPIRED:
      what = "certificate expired or not yet valid";
      break;
   case SEC_E_WRONG_PRINCIPAL:
   case CERT_E_CN_NO_MATCH:
      what = "certificate host name mismatch";
      break;
   case CRYPT_E_REVOKED:
   case CERT_E_REVOKED:
      what = "certificate revoked";
      break;
   case SEC_E_CERT_UNKNOWN:
      what = "bad certificate";
      break;
   case SEC_E_ALGORITHM_MISMATCH:
      what = "no common protocol version or cipher";
      break;
   default:
      break;
   }
   if (what)
      snprintf(c->error, sizeof(c->error), "TLS: %s", what);
   else
      win_error(c, "TLS", (DWORD)st);
}

/* Lifetime */

net_conn *net_conn_new(void) {
   net_conn *c = calloc(1, sizeof(*c));
   if (!c)
      return NULL;
   c->sock = c->wake = INVALID_SOCKET;
   WSADATA wsa;
   if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      free(c);
      return NULL;
   }
   c->wsa = true;

   c->wake = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   c->wake_addr.sin_family = AF_INET;
   c->wake_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   int len = sizeof(c->wake_addr);
   u_long one = 1;
   if (c->wake == INVALID_SOCKET ||
       bind(c->wake, (struct sockaddr *)&c->wake_addr, len) != 0 ||
       getsockname(c->wake, (struct sockaddr *)&c->wake_addr, &len) != 0 ||
       ioctlsocket(c->wake, FIONBIO, &one) != 0) {
      net_conn_free(c);
      return NULL;
   }
   return c;
}

void net_conn_free(net_conn *c) {
   if (!c)
      return;
   if (c->have_ctx)
      DeleteSecurityContext(&c->ctx);
   if (c->have_cred)
      FreeCredentialsHandle(&c->cred);
   if (c->sock != INVALID_SOCKET)
      closesocket(c->sock);
   if (c->wake != INVALID_SOCKET)
      closesocket(c->wake);
   if (c->wsa)
      WSACleanup();
   free(c->in);
   free(c->out);
   free(c);
}

const char *net_conn_error(const net_conn *c) {
   return c->error;
}

void net_conn_wake(net_conn *c) {
   sendto(c->wake, "x", 1, 0, (struct sockaddr *)&c->wake_addr,
          sizeof(c->wake_addr));
}

void net_conn_cancel(net_conn *c) {
   InterlockedExchange(&c->cancelled, 1);
   net_conn_wake(c);
}

static void drain_wake(net_conn *c) {
   char buf[64];
   while (recv(c->wake, buf, sizeof(buf), 0) > 0) {
   }
}

/* Sending encrypted records */

// Sends what is left of out. False on a socket error.
static bool flush_out(net_conn *c) {
   while (c->out_pos < c->out_len) {
      size_t n = c->out_len - c->out_pos;
      int r = send(c->sock, c->out + c->out_pos, n > INT_MAX ? INT_MAX : (int)n,
                   0);
      if (r == SOCKET_ERROR) {
         if (WSAGetLastError() == WSAEWOULDBLOCK)
            return true;
         win_error(c, "", (DWORD)WSAGetLastError());
         return false;
      }
      c->out_pos += (size_t)r;
   }
   c->out_pos = c->out_len = 0;
   return true;
}

static bool conn_reserve(char **buf, size_t *cap, size_t need) {
   if (need <= *cap)
      return true;
   char *p = realloc(*buf, need);
   if (!p)
      return false;
   *buf = p;
   *cap = need;
   return true;
}

// Queues a handshake token for sending.
static bool queue_out(net_conn *c, const void *data, size_t len) {
   if (!conn_reserve(&c->out, &c->out_cap, c->out_len + len)) {
      snprintf(c->error, sizeof(c->error), "out of memory");
      return false;
   }
   memcpy(c->out + c->out_len, data, len);
   c->out_len += len;
   return true;
}

/* Waiting */

// select on the socket (read and / or write, and errors for connect) and the
// wake-up socket.
static int select_socket(net_conn *c, bool rd, bool wr, int64_t deadline) {
   for (;;) {
      if (InterlockedCompareExchange(&c->cancelled, 0, 0)) {
         snprintf(c->error, sizeof(c->error), "cancelled");
         return NET_ERR;
      }
      struct timeval tv, *ptv = NULL;
      if (deadline >= 0) {
         int64_t left = deadline - net_now_ms();
         if (left <= 0)
            return 0;
         if (left > 86400000)
            left = 86400000;
         tv.tv_sec = (long)(left / 1000);
         tv.tv_usec = (long)(left % 1000) * 1000;
         ptv = &tv;
      }
      fd_set rs, ws, es;
      FD_ZERO(&rs);
      FD_ZERO(&ws);
      FD_ZERO(&es);
      FD_SET(c->wake, &rs);
      if (rd)
         FD_SET(c->sock, &rs);
      if (wr)
         FD_SET(c->sock, &ws);
      FD_SET(c->sock, &es);
      int r = select(0, &rs, &ws, &es, ptv);
      if (r == SOCKET_ERROR) {
         win_error(c, "select", (DWORD)WSAGetLastError());
         return NET_ERR;
      }
      int ready = 0;
      if (FD_ISSET(c->wake, &rs)) {
         drain_wake(c);
         if (InterlockedCompareExchange(&c->cancelled, 0, 0))
            continue;
         ready |= NET_WAKE;
      }
      if (FD_ISSET(c->sock, &rs) || FD_ISSET(c->sock, &ws) ||
          FD_ISSET(c->sock, &es))
         ready |= NET_READ | NET_WRITE;
      if (ready)
         return ready;
   }
}

int net_conn_wait(net_conn *c, int events, int64_t deadline) {
   // Decrypted or undecrypted bytes may already be here.
   if ((events & NET_READ) &&
       (c->plain_len || (c->tls && c->in_len && !c->incomplete)))
      return NET_READ;
   for (;;) {
      bool pending = c->out_pos < c->out_len;
      int r = select_socket(c, (events & NET_READ) != 0,
                            (events & NET_WRITE) || pending, deadline);
      if (r <= 0)
         return r;
      if (pending && (r & NET_WRITE)) {
         if (!flush_out(c))
            return events & (NET_READ | NET_WRITE); // the next call fails
         // A pending record alone isn't something the caller waits for.
         if (!(r & NET_WAKE) && !(events & NET_READ) &&
             c->out_pos < c->out_len)
            continue;
      }
      if (r & (NET_READ | NET_WRITE))
         r = (r & NET_WAKE) | (events & (NET_READ | NET_WRITE));
      return r;
   }
}

/* Plain I/O */

static long sock_read(net_conn *c, void *buf, size_t n) {
   int r = recv(c->sock, buf, n > INT_MAX ? INT_MAX : (int)n, 0);
   if (r > 0)
      return r;
   if (r == 0)
      return NET_EOF;
   if (WSAGetLastError() == WSAEWOULDBLOCK)
      return NET_AGAIN;
   win_error(c, "", (DWORD)WSAGetLastError());
   return NET_ERR;
}

static long sock_write(net_conn *c, const void *buf, size_t n) {
   int r = send(c->sock, buf, n > INT_MAX ? INT_MAX : (int)n, 0);
   if (r >= 0)
      return r;
   if (WSAGetLastError() == WSAEWOULDBLOCK)
      return NET_AGAIN;
   win_error(c, "", (DWORD)WSAGetLastError());
   return NET_ERR;
}

/* TLS */

#define TLS_FLAGS                                                              \
   (ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_ALLOCATE_MEMORY |                     \
    ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |                          \
    ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM)

// Receives more ciphertext into in. NET_AGAIN, NET_EOF, NET_ERR or bytes.
static long receive_more(net_conn *c) {
   size_t need = c->sizes.cbMaximumMessage
                     ? c->sizes.cbHeader + c->sizes.cbMaximumMessage +
                           c->sizes.cbTrailer
                     : 16384 + 512;
   if (c->in_cap < need && !conn_reserve(&c->in, &c->in_cap, need)) {
      snprintf(c->error, sizeof(c->error), "out of memory");
      return NET_ERR;
   }
   if (c->in_len == c->in_cap) {
      snprintf(c->error, sizeof(c->error), "TLS: record too large");
      return NET_ERR;
   }
   long r = sock_read(c, c->in + c->in_len, c->in_cap - c->in_len);
   if (r > 0)
      c->in_len += (size_t)r;
   return r;
}

// Keeps the bytes the last SSPI call left unused (SECBUFFER_EXTRA), at the
// end of in.
static void keep_extra(net_conn *c, const SecBuffer *buffers, int n) {
   size_t extra = 0;
   for (int i = 0; i < n; i++) {
      if (buffers[i].BufferType == SECBUFFER_EXTRA)
         extra = buffers[i].cbBuffer;
   }
   if (extra)
      memmove(c->in, c->in + c->in_len - extra, extra);
   c->in_len = extra;
}

// One step of the handshake over what is in in. Returns SEC_E_OK when done,
// SEC_I_CONTINUE_NEEDED or SEC_E_INCOMPLETE_MESSAGE to go on, or an error.
static SECURITY_STATUS handshake_step(net_conn *c, const char *host,
                                      bool first) {
   SecBuffer in_bufs[2] = {{(ULONG)c->in_len, SECBUFFER_TOKEN, c->in},
                           {0, SECBUFFER_EMPTY, NULL}};
   SecBufferDesc in_desc = {SECBUFFER_VERSION, 2, in_bufs};
   SecBuffer out_bufs[1] = {{0, SECBUFFER_TOKEN, NULL}};
   SecBufferDesc out_desc = {SECBUFFER_VERSION, 1, out_bufs};
   DWORD flags = 0;
   SECURITY_STATUS st = InitializeSecurityContextA(
       &c->cred, first ? NULL : &c->ctx, (SEC_CHAR *)host, TLS_FLAGS, 0, 0,
       first ? NULL : &in_desc, 0, first ? &c->ctx : NULL, &out_desc, &flags,
       NULL);
   if (first && (st == SEC_E_OK || st == SEC_I_CONTINUE_NEEDED))
      c->have_ctx = true;
   if (out_bufs[0].pvBuffer) {
      bool ok = queue_out(c, out_bufs[0].pvBuffer, out_bufs[0].cbBuffer);
      FreeContextBuffer(out_bufs[0].pvBuffer);
      if (!ok)
         return SEC_E_INSUFFICIENT_MEMORY;
   }
   if (st == SEC_E_INCOMPLETE_MESSAGE || first)
      return st;
   if (st == SEC_E_OK || st == SEC_I_CONTINUE_NEEDED)
      keep_extra(c, in_bufs, 2);
   return st;
}

// Writes out and waits until it is all sent.
static bool send_out(net_conn *c, int64_t deadline) {
   while (c->out_pos < c->out_len) {
      if (!flush_out(c))
         return false;
      if (c->out_pos < c->out_len) {
         int r = select_socket(c, false, true, deadline);
         if (r == 0)
            snprintf(c->error, sizeof(c->error), "TLS handshake timed out");
         if (r <= 0)
            return false;
      }
   }
   return true;
}

static bool tls_start(net_conn *c, const char *host, int64_t deadline) {
   c->tls = true;
   SCHANNEL_CRED cred = {0};
   cred.dwVersion = SCHANNEL_CRED_VERSION;
   // Protocol versions follow the system's policy; validation is automatic,
   // against the Windows certificate store and the host name.
   cred.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_AUTO_CRED_VALIDATION |
                  SCH_CRED_NO_DEFAULT_CREDS;
   SECURITY_STATUS st = AcquireCredentialsHandleA(
       NULL, (SEC_CHAR *)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &cred, NULL,
       NULL, &c->cred, NULL);
   if (st != SEC_E_OK) {
      tls_error(c, st);
      return false;
   }
   c->have_cred = true;

   for (bool first = true;; first = false) {
      st = handshake_step(c, host, first);
      if (!send_out(c, deadline))
         return false;
      if (st == SEC_E_OK)
         break;
      if (st == SEC_I_INCOMPLETE_CREDENTIALS) {
         snprintf(c->error, sizeof(c->error),
                  "TLS: the server asks for a client certificate");
         return false;
      }
      if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_INCOMPLETE_MESSAGE) {
         tls_error(c, st);
         return false;
      }
      if (st == SEC_I_CONTINUE_NEEDED && c->in_len)
         continue; // the server's next message is already here
      for (;;) {
         long r = receive_more(c);
         if (r > 0)
            break;
         if (r == NET_EOF) {
            snprintf(c->error, sizeof(c->error),
                     "TLS: connection closed during the handshake");
            return false;
         }
         if (r == NET_ERR)
            return false;
         int w = select_socket(c, true, false, deadline);
         if (w == 0)
            snprintf(c->error, sizeof(c->error), "TLS handshake timed out");
         if (w <= 0)
            return false;
      }
   }

   st = QueryContextAttributesA(&c->ctx, SECPKG_ATTR_STREAM_SIZES, &c->sizes);
   if (st != SEC_E_OK) {
      tls_error(c, st);
      return false;
   }
   size_t need =
       c->sizes.cbHeader + c->sizes.cbMaximumMessage + c->sizes.cbTrailer;
   if (!conn_reserve(&c->in, &c->in_cap, need) ||
       !conn_reserve(&c->out, &c->out_cap, need)) {
      snprintf(c->error, sizeof(c->error), "out of memory");
      return false;
   }
   return true;
}

// SEC_I_RENEGOTIATE: the server sent a handshake message after the handshake
// (TLS 1.3 session tickets, key updates). It is in in; feed it back to
// InitializeSecurityContext.
static bool renegotiate(net_conn *c, const SecBuffer *bufs, int n) {
   keep_extra(c, bufs, n);
   SECURITY_STATUS st = handshake_step(c, NULL, false);
   if (st != SEC_E_OK && st != SEC_I_CONTINUE_NEEDED &&
       st != SEC_E_INCOMPLETE_MESSAGE) {
      tls_error(c, st);
      return false;
   }
   return flush_out(c);
}

static long tls_read(net_conn *c, void *buf, size_t n) {
   for (;;) {
      if (c->plain_len) {
         size_t k = c->plain_len < n ? c->plain_len : n;
         memcpy(buf, c->plain, k);
         c->plain += k;
         c->plain_len -= k;
         if (!c->plain_len) {
            // The record is used up: keep what came after it.
            memmove(c->in, c->in + c->record_len, c->in_len - c->record_len);
            c->in_len -= c->record_len;
            c->record_len = 0;
         }
         return (long)k;
      }
      if (c->eof)
         return NET_EOF;
      if (c->in_len) {
         SecBuffer b[4] = {{(ULONG)c->in_len, SECBUFFER_DATA, c->in},
                           {0, SECBUFFER_EMPTY, NULL},
                           {0, SECBUFFER_EMPTY, NULL},
                           {0, SECBUFFER_EMPTY, NULL}};
         SecBufferDesc desc = {SECBUFFER_VERSION, 4, b};
         SECURITY_STATUS st = DecryptMessage(&c->ctx, &desc, 0, NULL);
         if (st == SEC_E_OK) {
            const SecBuffer *data = NULL;
            size_t extra = 0;
            for (int i = 1; i < 4; i++) {
               if (b[i].BufferType == SECBUFFER_DATA)
                  data = &b[i];
               else if (b[i].BufferType == SECBUFFER_EXTRA)
                  extra = b[i].cbBuffer;
            }
            c->record_len = c->in_len - extra;
            if (data && data->cbBuffer) {
               c->plain = data->pvBuffer;
               c->plain_len = data->cbBuffer;
            } else {
               memmove(c->in, c->in + c->record_len, extra);
               c->in_len = extra;
               c->record_len = 0;
            }
            continue;
         }
         if (st == SEC_I_CONTEXT_EXPIRED) {
            c->eof = true; // close_notify
            return NET_EOF;
         }
         if (st == SEC_I_RENEGOTIATE) {
            if (!renegotiate(c, b, 4))
               return NET_ERR;
            continue;
         }
         if (st != SEC_E_INCOMPLETE_MESSAGE) {
            tls_error(c, st);
            return NET_ERR;
         }
         c->incomplete = true;
      }
      long r = receive_more(c);
      if (r <= 0)
         return r;
      c->incomplete = false;
   }
}

static long tls_write(net_conn *c, const void *buf, size_t n) {
   if (!flush_out(c))
      return NET_ERR;
   if (c->out_pos < c->out_len)
      return NET_AGAIN;
   size_t k = n < c->sizes.cbMaximumMessage ? n : c->sizes.cbMaximumMessage;
   char *p = c->out;
   memcpy(p + c->sizes.cbHeader, buf, k);
   SecBuffer b[4] = {
       {c->sizes.cbHeader, SECBUFFER_STREAM_HEADER, p},
       {(ULONG)k, SECBUFFER_DATA, p + c->sizes.cbHeader},
       {c->sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,
        p + c->sizes.cbHeader + k},
       {0, SECBUFFER_EMPTY, NULL},
   };
   SecBufferDesc desc = {SECBUFFER_VERSION, 4, b};
   SECURITY_STATUS st = EncryptMessage(&c->ctx, 0, &desc, 0);
   if (st != SEC_E_OK) {
      tls_error(c, st);
      return NET_ERR;
   }
   c->out_pos = 0;
   c->out_len = b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer;
   if (!flush_out(c))
      return NET_ERR;
   return (long)k; // taken; the rest of the record goes out from wait
}

long net_conn_read(net_conn *c, void *buf, size_t n) {
   return c->tls ? tls_read(c, buf, n) : sock_read(c, buf, n);
}

long net_conn_write(net_conn *c, const void *buf, size_t n) {
   return c->tls ? tls_write(c, buf, n) : sock_write(c, buf, n);
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
      win_error(c, host, (DWORD)gai);
      return false;
   }

   DWORD last = 0;
   bool timed_out = false;
   for (struct addrinfo *a = list; a; a = a->ai_next) {
      SOCKET s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (s == INVALID_SOCKET) {
         last = (DWORD)WSAGetLastError();
         continue;
      }
      u_long one = 1;
      ioctlsocket(s, FIONBIO, &one);
      BOOL nodelay = TRUE;
      setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay,
                 sizeof(nodelay));
      c->sock = s;
      if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) {
         freeaddrinfo(list);
         return true;
      }
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
         int r;
         do // a wake-up while dialing means nothing yet
            r = select_socket(c, false, true, deadline);
         while (r == NET_WAKE);
         int so_error = 0, len = sizeof(so_error);
         if (r > 0 &&
             getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len) ==
                 0 &&
             so_error == 0) {
            freeaddrinfo(list);
            return true;
         }
         if (r < 0) {
            closesocket(s);
            c->sock = INVALID_SOCKET;
            freeaddrinfo(list);
            return false; // cancelled; select_socket set the error
         }
         timed_out = r == 0;
         last = (DWORD)so_error;
      } else {
         last = (DWORD)WSAGetLastError();
      }
      closesocket(s);
      c->sock = INVALID_SOCKET;
      if (timed_out)
         break;
   }
   freeaddrinfo(list);
   if (timed_out) {
      snprintf(c->error, sizeof(c->error), "connection timed out");
   } else {
      char doing[300];
      snprintf(doing, sizeof(doing), "connect to %s:%d", host, port);
      win_error(c, doing, last ? last : WSAECONNREFUSED);
   }
   return false;
}

bool net_conn_dial(net_conn *c, const char *host, int port, bool tls,
                   int64_t deadline) {
   return dial_tcp(c, host, port, deadline) &&
          (!tls || tls_start(c, host, deadline));
}
