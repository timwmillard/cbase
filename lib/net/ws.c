/* WebSocket client over the core (net_conn.h): one thread per connection
 * runs the handshake and then the frame loop from ws_proto.c.
 *
 * Locks: cb_lock is held while a callback runs (so ws_free can wait for it);
 * lock guards the outgoing queue, the flags and conn. Order: cb_lock, lock.
 */

#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_conn.h"
#include "net_sys.h"
#include "net_url.h"
#include "ws_proto.h"

#define DEFAULT_CONNECT_TIMEOUT_MS 10000
#define DEFAULT_PING_INTERVAL_MS   30000
#define DEFAULT_MAX_MESSAGE        (16u << 20)
#define CLOSE_TIMEOUT_MS           5000

struct ws {
   void (*on_open)(ws *w, void *user_data);
   void (*on_message)(ws *w, const char *data, size_t len, bool binary,
                      void *user_data);
   void (*on_close)(ws *w, int code, const char *reason, void *user_data);
   void (*on_error)(ws *w, const char *message, void *user_data);
   void *user_data;
   net_url url;
   char **headers, **protocols;
   int connect_timeout_ms, ping_interval_ms;

   net_mutex cb_lock;
   bool freed; // under cb_lock: no more callbacks

   net_mutex lock;
   net_conn *conn; // NULL once the network thread has closed it
   uint8_t *out;   // encoded frames waiting for the network thread
   size_t out_len, out_cap;
   bool open, closing, aborted, done;
   int close_code;
   char close_reason[124];
   uint64_t rng;

   // Network thread only.
   ws_reader reader;
};

static NET_THREAD_LOCAL ws *ws_current;

// Not for cryptography: masks only need to be unpredictable to
// intermediaries. Seeded from net_random.
static uint64_t next_random(uint64_t *s) {
   *s ^= *s >> 12;
   *s ^= *s << 25;
   *s ^= *s >> 27;
   return *s * 2685821657736338717ull;
}

static void random_mask(ws *w, uint8_t mask[4]) {
   uint64_t r = next_random(&w->rng);
   for (int k = 0; k < 4; k++)
      mask[k] = (uint8_t)(r >> (k * 8 + 32));
}

// Caller holds lock.
static void wake(ws *w) {
   if (w->conn)
      net_conn_wake(w->conn);
}

/* Callbacks, held under cb_lock */

static void emit_open(ws *w) {
   net_mutex_lock(&w->cb_lock);
   if (!w->freed && w->on_open)
      w->on_open(w, w->user_data);
   net_mutex_unlock(&w->cb_lock);
}

static void emit_message(ws *w, const uint8_t *data, size_t len, bool binary) {
   net_mutex_lock(&w->cb_lock);
   if (!w->freed && w->on_message)
      w->on_message(w, len ? (const char *)data : "", len, binary,
                    w->user_data);
   net_mutex_unlock(&w->cb_lock);
}

static void emit_error(ws *w, const char *message) {
   net_mutex_lock(&w->cb_lock);
   if (!w->freed && w->on_error)
      w->on_error(w, message, w->user_data);
   net_mutex_unlock(&w->cb_lock);
}

static void emit_close(ws *w, int code, const char *reason) {
   net_mutex_lock(&w->cb_lock);
   if (!w->freed && w->on_close)
      w->on_close(w, code, reason, w->user_data);
   net_mutex_unlock(&w->cb_lock);
}

/* Outgoing frames */

// Caller holds lock.
static bool queue_frame(ws *w, int op, const void *data, size_t len) {
   size_t need = w->out_len + len + WS_FRAME_HEADER_MAX;
   if (need > w->out_cap) {
      size_t cap = w->out_cap ? w->out_cap : 4096;
      while (cap < need)
         cap *= 2;
      uint8_t *p = realloc(w->out, cap);
      if (!p)
         return false;
      w->out = p;
      w->out_cap = cap;
   }
   uint8_t mask[4];
   random_mask(w, mask);
   w->out_len += ws_frame_encode(w->out + w->out_len, op, data, len, mask);
   return true;
}

static void queue_control(ws *w, int op, const void *data, size_t len) {
   net_mutex_lock(&w->lock);
   queue_frame(w, op, data, len);
   net_mutex_unlock(&w->lock);
}

/* The handshake */

static bool handshake(ws *w, int64_t deadline, char *err, size_t cap) {
   uint8_t nonce[16];
   char key[25];
   net_random(nonce, sizeof(nonce));
   ws_make_key(nonce, key);

   char req[8192];
   int n = ws_build_request(req, sizeof(req), &w->url, key,
                            (const char *const *)w->headers,
                            (const char *const *)w->protocols);
   if (n < 0) {
      snprintf(err, cap, "request headers too large");
      return false;
   }
   if (!net_conn_write_all(w->conn, req, (size_t)n, deadline)) {
      snprintf(err, cap, "handshake: %s", net_conn_error(w->conn));
      return false;
   }

   char resp[16400];
   size_t len = 0;
   for (;;) {
      long r =
          net_conn_read_some(w->conn, resp + len, sizeof(resp) - len, deadline);
      if (r == NET_EOF) {
         snprintf(err, cap, "connection closed during the handshake");
         return false;
      }
      if (r < 0) {
         snprintf(err, cap, "handshake: %s", net_conn_error(w->conn));
         return false;
      }
      len += (size_t)r;
      size_t used = 0;
      int ok = ws_parse_response(resp, len, key, &used, err, cap);
      if (ok < 0)
         return false;
      if (ok > 0) {
         // Frames the server sent right after its answer.
         if (len > used &&
             !ws_reader_feed(&w->reader, resp + used, len - used)) {
            snprintf(err, cap, "out of memory");
            return false;
         }
         return true;
      }
   }
}

/* The open connection */

typedef struct {
   uint8_t *buf;
   size_t len, pos, cap;
} out_buf;

// Takes the frames queued by other threads. Returns whether the user has
// asked to close (the close frame is then in the batch).
static bool take_queue(ws *w, out_buf *o, bool *aborted) {
   net_mutex_lock(&w->lock);
   if (o->pos == o->len) {
      uint8_t *buf = o->buf;
      size_t cap = o->cap;
      o->buf = w->out;
      o->cap = w->out_cap;
      o->len = w->out_len;
      o->pos = 0;
      w->out = buf;
      w->out_cap = cap;
      w->out_len = 0;
   }
   bool closing = w->closing && w->out_len == 0;
   *aborted = w->aborted;
   net_mutex_unlock(&w->lock);
   return closing;
}

static void run_open(ws *w, int *code, char *reason, char *err, size_t cap) {
   out_buf o = {0};
   int64_t now = net_now_ms();
   int64_t ping_every = w->ping_interval_ms;
   int64_t next_ping = now + ping_every, last_recv = now;
   int64_t close_deadline = 0;
   bool sent_close = false, got_close = false;

   for (;;) {
      bool aborted;
      bool user_closing = take_queue(w, &o, &aborted);
      if (aborted)
         break;
      if (user_closing && !sent_close) {
         sent_close = true;
         close_deadline = net_now_ms() + CLOSE_TIMEOUT_MS;
         net_mutex_lock(&w->lock);
         *code = w->close_code;
         snprintf(reason, 124, "%s", w->close_reason);
         net_mutex_unlock(&w->lock);
      }

      while (o.pos < o.len) {
         long n = net_conn_write(w->conn, o.buf + o.pos, o.len - o.pos);
         if (n == NET_ERR) {
            snprintf(err, cap, "%s", net_conn_error(w->conn));
            goto out;
         }
         if (n == NET_AGAIN)
            break;
         o.pos += (size_t)n;
      }
      if (got_close && o.pos == o.len) {
         err[0] = '\0';
         break; // close handshake complete
      }

      int64_t deadline = ping_every > 0 ? next_ping : -1;
      if (sent_close && (deadline < 0 || close_deadline < deadline))
         deadline = close_deadline;
      int events = NET_READ | (o.pos < o.len ? NET_WRITE : 0);
      if (net_conn_wait(w->conn, events, deadline) == NET_ERR) {
         snprintf(err, cap, "%s", net_conn_error(w->conn));
         break;
      }

      // Read whatever is there.
      for (;;) {
         uint8_t buf[16384];
         long n = net_conn_read(w->conn, buf, sizeof(buf));
         if (n == NET_AGAIN)
            break;
         if (n < 0) {
            if (sent_close)
               err[0] = '\0'; // the server closed after our close frame
            else if (n == NET_EOF)
               snprintf(err, cap, "connection closed by server");
            else
               snprintf(err, cap, "%s", net_conn_error(w->conn));
            goto out;
         }
         last_recv = net_now_ms();
         if (!ws_reader_feed(&w->reader, buf, (size_t)n)) {
            snprintf(err, cap, "out of memory");
            goto out;
         }
         for (;;) {
            ws_event ev = ws_reader_next(&w->reader);
            if (ev.type == WS_EV_NONE)
               break;
            switch (ev.type) {
            case WS_EV_TEXT:
            case WS_EV_BINARY:
               if (!sent_close)
                  emit_message(w, ev.data, ev.len, ev.type == WS_EV_BINARY);
               break;
            case WS_EV_PING:
               queue_control(w, WS_OP_PONG, ev.data, ev.len);
               break;
            case WS_EV_PONG:
               break;
            case WS_EV_CLOSE:
               got_close = true;
               *code = ev.code;
               snprintf(reason, 124, "%s", ev.reason);
               if (!sent_close) {
                  uint8_t payload[125];
                  size_t len = ev.code == 1005
                                   ? 0
                                   : ws_close_payload(payload, ev.code, NULL);
                  queue_control(w, WS_OP_CLOSE, payload, len);
                  sent_close = true;
                  net_mutex_lock(&w->lock);
                  w->closing = true;
                  net_mutex_unlock(&w->lock);
               }
               break;
            case WS_EV_ERROR: {
               uint8_t payload[125];
               size_t len = ws_close_payload(payload, ev.code, ev.reason);
               queue_control(w, WS_OP_CLOSE, payload, len);
               *code = ev.code;
               snprintf(err, cap, "protocol error: %s", ev.reason);
               // Best effort: flush the close frame, then drop the line.
               take_queue(w, &o, &aborted);
               while (o.pos < o.len) {
                  long m =
                      net_conn_write(w->conn, o.buf + o.pos, o.len - o.pos);
                  if (m <= 0)
                     break;
                  o.pos += (size_t)m;
               }
               goto out;
            }
            case WS_EV_NONE:
               break;
            }
            if (got_close)
               break;
         }
         if (got_close)
            break;
      }

      now = net_now_ms();
      if (sent_close && now >= close_deadline && !got_close)
         break; // the server never answered our close
      if (ping_every > 0 && now >= next_ping && !sent_close) {
         if (now - last_recv > 2 * ping_every) {
            snprintf(err, cap, "connection timed out (no pong)");
            *code = WS_CLOSE_ABNORMAL;
            break;
         }
         queue_control(w, WS_OP_PING, NULL, 0);
         next_ping = now + ping_every;
      }
   }
out:
   free(o.buf);
}

/* Lifetime */

static void ws_destroy(ws *w) {
   net_strv_free(w->headers);
   net_strv_free(w->protocols);
   free(w->out);
   ws_reader_free(&w->reader);
   net_conn_free(w->conn);
   net_mutex_destroy(&w->lock);
   net_mutex_destroy(&w->cb_lock);
   free(w);
}

static void ws_run(void *arg) {
   ws *w = arg;
   ws_current = w;
   char err[512] = "";
   char reason[124] = "";
   int code = WS_CLOSE_ABNORMAL;
   int64_t deadline = net_now_ms() + w->connect_timeout_ms;
   if (!net_conn_dial(w->conn, w->url.host, w->url.port, w->url.secure,
                      deadline)) {
      snprintf(err, sizeof(err), "%s", net_conn_error(w->conn));
   } else if (handshake(w, deadline, err, sizeof(err))) {
      net_mutex_lock(&w->lock);
      bool go = !w->closing && !w->aborted;
      w->open = go;
      net_mutex_unlock(&w->lock);
      if (go) {
         emit_open(w);
         run_open(w, &code, reason, err, sizeof(err));
      }
   }

   net_mutex_lock(&w->lock);
   w->open = false;
   w->closing = true;
   net_conn *conn = w->conn;
   w->conn = NULL;
   bool cancelled =
       strcmp(err, "cancelled") == 0 || strstr(err, ": cancelled") != NULL;
   if (code == WS_CLOSE_ABNORMAL && w->close_code && (!err[0] || cancelled)) {
      code = w->close_code; // closed before it opened, or close timed out
      snprintf(reason, sizeof(reason), "%s", w->close_reason);
   }
   net_mutex_unlock(&w->lock);
   net_conn_free(conn);

   if (err[0] && !cancelled)
      emit_error(w, err);
   emit_close(w, code, reason);

   net_mutex_lock(&w->lock);
   w->done = true;
   bool free_now = w->aborted;
   net_mutex_unlock(&w->lock);
   if (free_now)
      ws_destroy(w);
}

ws *ws_connect(const ws_desc *desc) {
   ws_desc o = *desc;
   ws *w = calloc(1, sizeof(*w));
   if (!w)
      return NULL;
   if (!o.url || net_url_parse(o.url, &w->url) != 0 ||
       w->url.scheme != NET_WS) {
      free(w);
      return NULL;
   }
   w->on_open = o.on_open;
   w->on_message = o.on_message;
   w->on_close = o.on_close;
   w->on_error = o.on_error;
   w->user_data = o.user_data;
   w->connect_timeout_ms = o.connect_timeout_ms > 0
                               ? o.connect_timeout_ms
                               : DEFAULT_CONNECT_TIMEOUT_MS;
   w->ping_interval_ms = o.ping_interval_ms == 0  ? DEFAULT_PING_INTERVAL_MS
                         : o.ping_interval_ms < 0 ? 0
                                                  : o.ping_interval_ms;
   ws_reader_init(&w->reader, false,
                  o.max_message_size ? o.max_message_size
                                     : DEFAULT_MAX_MESSAGE);
   net_random(&w->rng, sizeof(w->rng));
   w->rng |= 1; // xorshift must not start at zero
   net_mutex_init(&w->lock);
   net_mutex_init(&w->cb_lock);

   bool ok = true;
   w->headers = net_strv_copy(o.headers, &ok);
   w->protocols = net_strv_copy(o.protocols, &ok);
   w->conn = ok ? net_conn_new() : NULL;
   if (!w->conn || !net_thread_start(ws_run, w)) {
      ws_destroy(w);
      return NULL;
   }
   return w;
}

static bool send_message(ws *w, int op, const void *data, size_t len) {
   net_mutex_lock(&w->lock);
   bool ok = w->open && !w->closing && queue_frame(w, op, data, len);
   if (ok)
      wake(w);
   net_mutex_unlock(&w->lock);
   return ok;
}

bool ws_send_text(ws *w, const char *data, size_t len) {
   return send_message(w, WS_OP_TEXT, data, len);
}

bool ws_send_binary(ws *w, const void *data, size_t len) {
   return send_message(w, WS_OP_BINARY, data, len);
}

void ws_close(ws *w, int code, const char *reason) {
   net_mutex_lock(&w->lock);
   if (!w->closing) {
      w->closing = true;
      w->close_code = code;
      snprintf(w->close_reason, sizeof(w->close_reason), "%s",
               reason ? reason : "");
      if (w->open) {
         uint8_t payload[125];
         size_t n = ws_close_payload(payload, code, reason);
         queue_frame(w, WS_OP_CLOSE, payload, n);
         wake(w);
      } else if (w->conn) {
         net_conn_cancel(w->conn); // still connecting: give up
      }
   }
   net_mutex_unlock(&w->lock);
}

void ws_free(ws *w) {
   if (!w)
      return;
   bool self = ws_current == w;
   if (!self)
      net_mutex_lock(&w->cb_lock);
   w->freed = true;
   if (!self)
      net_mutex_unlock(&w->cb_lock);

   net_mutex_lock(&w->lock);
   w->aborted = true;
   bool free_now = w->done;
   if (!free_now && w->conn)
      net_conn_cancel(w->conn);
   net_mutex_unlock(&w->lock);
   if (free_now)
      ws_destroy(w);
}
