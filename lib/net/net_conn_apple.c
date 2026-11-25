/* Apple core: Network.framework does TCP and TLS (with the system trust
 * store). nw_connection is asynchronous: its handlers run on a private serial
 * queue and leave results here under lock, and the network thread waits on
 * cond for them.
 *
 * Every handler holds a "pending" count, and net_conn_free waits for the
 * connection to be cancelled and that count to reach zero before freeing, so
 * no handler can touch freed memory.
 */

#include <Network/Network.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_conn.h"
#include "net_sys.h"

// Bytes handed to nw_connection_send and not yet confirmed. Above this,
// write returns NET_AGAIN.
#define SEND_LIMIT (1u << 20)
#define RECEIVE_MAX 65536

struct net_conn {
   pthread_mutex_t lock;
   pthread_cond_t cond;
   dispatch_queue_t queue;
   nw_connection_t conn;

   bool ready, failed, finished; // finished: the cancelled state arrived
   bool cancelled, woken;
   int pending; // handlers still to run: receives, sends

   bool receiving, eof;
   uint8_t *in;
   size_t in_len, in_pos, in_cap;
   size_t send_bytes;

   char error[256];
};

net_conn *net_conn_new(void) {
   net_conn *c = calloc(1, sizeof(*c));
   if (!c)
      return NULL;
   pthread_mutex_init(&c->lock, NULL);
   pthread_cond_init(&c->cond, NULL);
   return c;
}

const char *net_conn_error(const net_conn *c) {
   return c->error;
}

/* Errors */

static const char *tls_error_string(int code) {
   switch (code) {
   case -9807:
      return "invalid certificate chain";
   case -9808:
      return "bad certificate";
   case -9812:
      return "unknown root certificate";
   case -9813:
      return "no root certificate";
   case -9814:
      return "certificate expired";
   case -9815:
      return "certificate not yet valid";
   case -9843:
      return "certificate host name mismatch";
   default:
      return "handshake failed";
   }
}

// Caller holds lock. Keeps the first error.
static void set_error(net_conn *c, nw_error_t error, const char *message) {
   if (c->error[0])
      return;
   if (message || !error) {
      snprintf(c->error, sizeof(c->error), "%s",
               message ? message : "connection failed");
      return;
   }
   int code = nw_error_get_error_code(error);
   switch (nw_error_get_error_domain(error)) {
   case nw_error_domain_posix:
      snprintf(c->error, sizeof(c->error), "%s", strerror(code));
      break;
   case nw_error_domain_dns:
      snprintf(c->error, sizeof(c->error), "DNS lookup failed (%d)", code);
      break;
   case nw_error_domain_tls:
      snprintf(c->error, sizeof(c->error), "TLS: %s (%d)",
               tls_error_string(code), code);
      break;
   default:
      snprintf(c->error, sizeof(c->error), "network error %d", code);
      break;
   }
}

// Caller holds lock. Waits on cond until the deadline; false once it passed.
static bool cond_wait(net_conn *c, int64_t deadline) {
   if (deadline < 0) {
      pthread_cond_wait(&c->cond, &c->lock);
      return true;
   }
   int64_t left = deadline - net_now_ms();
   if (left <= 0)
      return false;
   struct timespec rel = {(time_t)(left / 1000), (long)(left % 1000) * 1000000};
   pthread_cond_timedwait_relative_np(&c->cond, &c->lock, &rel);
   return true;
}

/* Connecting */

static void on_state(net_conn *c, nw_connection_state_t state,
                     nw_error_t error) {
   pthread_mutex_lock(&c->lock);
   switch (state) {
   case nw_connection_state_ready:
      c->ready = true;
      break;
   case nw_connection_state_waiting:
      // Network.framework would keep retrying (e.g. connection refused);
      // the caller decides when to try again.
   case nw_connection_state_failed:
      set_error(c, error, NULL);
      if (!c->failed) {
         c->failed = true;
         nw_connection_cancel(c->conn);
      }
      break;
   case nw_connection_state_cancelled:
      c->finished = true;
      break;
   default:
      break;
   }
   pthread_cond_broadcast(&c->cond);
   pthread_mutex_unlock(&c->lock);
}

bool net_conn_dial(net_conn *c, const char *host, int port, bool tls,
                   int64_t deadline) {
   char port_str[8];
   snprintf(port_str, sizeof(port_str), "%d", port);
   nw_endpoint_t endpoint = nw_endpoint_create_host(host, port_str);
   if (!endpoint) {
      snprintf(c->error, sizeof(c->error), "bad host name");
      return false;
   }

   nw_parameters_configure_protocol_block_t configure_tls =
       NW_PARAMETERS_DISABLE_PROTOCOL;
   if (tls) {
      configure_tls = ^(nw_protocol_options_t options) {
        sec_protocol_options_t sec = nw_tls_copy_sec_protocol_options(options);
        sec_protocol_options_set_min_tls_protocol_version(
            sec, tls_protocol_version_TLSv12);
        sec_release(sec);
      };
   }
   nw_parameters_t params = nw_parameters_create_secure_tcp(
       configure_tls, ^(nw_protocol_options_t options) {
         nw_tcp_options_set_no_delay(options, true);
       });

   c->queue = dispatch_queue_create("net", DISPATCH_QUEUE_SERIAL);
   c->conn = nw_connection_create(endpoint, params);
   nw_release(endpoint);
   nw_release(params);
   nw_connection_set_queue(c->conn, c->queue);
   nw_connection_set_state_changed_handler(
       c->conn, ^(nw_connection_state_t state, nw_error_t error) {
         on_state(c, state, error);
       });
   nw_connection_start(c->conn);

   pthread_mutex_lock(&c->lock);
   bool ok = false;
   for (;;) {
      if (c->cancelled) {
         snprintf(c->error, sizeof(c->error), "cancelled");
         break;
      }
      if (c->failed)
         break;
      if (c->ready) {
         ok = true;
         break;
      }
      if (!cond_wait(c, deadline)) {
         snprintf(c->error, sizeof(c->error), "connection timed out");
         break;
      }
   }
   c->woken = false; // a wake-up while dialing means nothing yet
   pthread_mutex_unlock(&c->lock);
   return ok;
}

/* Reading */

// Caller holds lock. Claims the one outstanding receive; the caller then
// calls issue_receive after unlocking, since the handler takes the lock.
static bool claim_receive(net_conn *c) {
   if (c->receiving || c->eof || c->failed || !c->conn)
      return false;
   c->receiving = true;
   c->pending++;
   return true;
}

static void issue_receive(net_conn *c) {
   nw_connection_receive(
       c->conn, 1, RECEIVE_MAX,
       ^(dispatch_data_t content, nw_content_context_t context,
         bool is_complete, nw_error_t error) {
         pthread_mutex_lock(&c->lock);
         size_t size = content ? dispatch_data_get_size(content) : 0;
         if (size) {
            if (c->in_pos == c->in_len)
               c->in_pos = c->in_len = 0;
            if (c->in_len + size > c->in_cap) {
               if (c->in_pos) {
                  memmove(c->in, c->in + c->in_pos, c->in_len - c->in_pos);
                  c->in_len -= c->in_pos;
                  c->in_pos = 0;
               }
               size_t cap = c->in_cap ? c->in_cap : RECEIVE_MAX;
               while (cap < c->in_len + size)
                  cap *= 2;
               uint8_t *p = cap > c->in_cap ? realloc(c->in, cap) : c->in;
               if (!p) {
                  set_error(c, NULL, "out of memory");
                  c->failed = true;
                  size = 0;
               } else {
                  c->in = p;
                  c->in_cap = cap;
               }
            }
            if (size) {
               dispatch_data_apply(content, ^bool(dispatch_data_t region,
                                                  size_t offset,
                                                  const void *buffer,
                                                  size_t n) {
                 (void)region;
                 (void)offset;
                 memcpy(c->in + c->in_len, buffer, n);
                 c->in_len += n;
                 return true;
               });
            }
         }
         if (error) {
            set_error(c, error, NULL);
            c->failed = true;
         } else if (is_complete &&
                    (!size || !context ||
                     nw_content_context_get_is_final(context))) {
            c->eof = true; // the peer closed its side
         }
         c->receiving = false;
         c->pending--;
         pthread_cond_broadcast(&c->cond);
         pthread_mutex_unlock(&c->lock);
       });
}

long net_conn_read(net_conn *c, void *buf, size_t n) {
   pthread_mutex_lock(&c->lock);
   long r;
   if (c->in_pos < c->in_len) {
      size_t k = c->in_len - c->in_pos;
      if (k > n)
         k = n;
      memcpy(buf, c->in + c->in_pos, k);
      c->in_pos += k;
      r = (long)k;
   } else if (c->failed) {
      r = NET_ERR;
   } else if (c->eof) {
      r = NET_EOF;
   } else {
      r = NET_AGAIN;
   }
   bool receive = claim_receive(c);
   pthread_mutex_unlock(&c->lock);
   if (receive)
      issue_receive(c);
   return r;
}

/* Writing */

long net_conn_write(net_conn *c, const void *buf, size_t n) {
   pthread_mutex_lock(&c->lock);
   if (c->failed) {
      pthread_mutex_unlock(&c->lock);
      return NET_ERR;
   }
   if (c->send_bytes >= SEND_LIMIT) {
      pthread_mutex_unlock(&c->lock);
      return NET_AGAIN;
   }
   size_t k = n < SEND_LIMIT ? n : SEND_LIMIT;
   c->send_bytes += k;
   c->pending++;
   pthread_mutex_unlock(&c->lock);

   // DESTRUCTOR_DEFAULT copies the bytes now.
   dispatch_data_t data =
       dispatch_data_create(buf, k, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
   nw_connection_send(c->conn, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,
                      false, ^(nw_error_t error) {
                        pthread_mutex_lock(&c->lock);
                        c->send_bytes -= k;
                        if (error) {
                           set_error(c, error, NULL);
                           c->failed = true;
                        }
                        c->pending--;
                        pthread_cond_broadcast(&c->cond);
                        pthread_mutex_unlock(&c->lock);
                      });
   dispatch_release(data);
   return (long)k;
}

/* Waiting */

int net_conn_wait(net_conn *c, int events, int64_t deadline) {
   pthread_mutex_lock(&c->lock);
   int ready = 0;
   for (;;) {
      if (c->cancelled) {
         snprintf(c->error, sizeof(c->error), "cancelled");
         ready = NET_ERR;
         break;
      }
      if (c->woken) {
         c->woken = false;
         ready |= NET_WAKE;
      }
      if ((events & NET_READ) &&
          (c->in_pos < c->in_len || c->eof || c->failed))
         ready |= NET_READ;
      if ((events & NET_WRITE) && (c->send_bytes < SEND_LIMIT || c->failed))
         ready |= NET_WRITE;
      if (ready)
         break;
      if ((events & NET_READ) && claim_receive(c)) {
         pthread_mutex_unlock(&c->lock);
         issue_receive(c);
         pthread_mutex_lock(&c->lock);
         continue; // it may have completed meanwhile
      }
      if (!cond_wait(c, deadline))
         break;
   }
   pthread_mutex_unlock(&c->lock);
   return ready;
}

void net_conn_wake(net_conn *c) {
   pthread_mutex_lock(&c->lock);
   c->woken = true;
   pthread_cond_broadcast(&c->cond);
   pthread_mutex_unlock(&c->lock);
}

void net_conn_cancel(net_conn *c) {
   pthread_mutex_lock(&c->lock);
   c->cancelled = true;
   pthread_cond_broadcast(&c->cond);
   pthread_mutex_unlock(&c->lock);
}

/* Freeing */

void net_conn_free(net_conn *c) {
   if (!c)
      return;
   if (c->conn) {
      nw_connection_cancel(c->conn);
      pthread_mutex_lock(&c->lock);
      while (!c->finished || c->pending > 0)
         pthread_cond_wait(&c->cond, &c->lock);
      pthread_mutex_unlock(&c->lock);
      nw_release(c->conn);
      dispatch_release(c->queue);
   }
   pthread_mutex_destroy(&c->lock);
   pthread_cond_destroy(&c->cond);
   free(c->in);
   free(c);
}
