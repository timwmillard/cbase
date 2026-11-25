/* HTTP client over the core (net_conn.h): one thread per request sends it,
 * reads the head and streams the body to the callbacks.
 *
 * Locks: cb_lock is held while a callback runs (so http_free can wait for
 * it); lock guards conn and the flags. Order: cb_lock, lock.
 */

#include "net.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_proto.h"
#include "net_conn.h"
#include "net_sys.h"
#include "net_url.h"

#define DEFAULT_CONNECT_TIMEOUT_MS 10000
#define DEFAULT_IDLE_TIMEOUT_MS    120000
#define READ_SIZE                  16384

struct http {
   void (*on_response)(http *h, const http_response *res, void *user_data);
   void (*on_data)(http *h, const char *data, size_t len, void *user_data);
   void (*on_done)(http *h, const char *error, void *user_data);
   void *user_data;
   net_url url;
   char method[16];
   char **headers;
   uint8_t *body;
   size_t body_len;
   bool has_body;
   int connect_timeout_ms, idle_timeout_ms;

   net_mutex cb_lock;
   bool freed; // under cb_lock: no more callbacks

   net_mutex lock;
   net_conn *conn; // NULL once the network thread has closed it
   bool aborted, done;
};

static NET_THREAD_LOCAL http *http_current;

const char *http_header(const http_response *res, const char *name,
                        size_t *len) {
   return http_header_find(res->headers, res->headers_len, name, len);
}

/* Callbacks, held under cb_lock */

static void emit_response(http *h, const http_head *head) {
   net_mutex_lock(&h->cb_lock);
   if (!h->freed && h->on_response) {
      http_response res = {head->status, head->headers, head->headers_len};
      h->on_response(h, &res, h->user_data);
   }
   net_mutex_unlock(&h->cb_lock);
}

static void emit_data(http *h, const char *data, size_t len) {
   if (!len)
      return;
   net_mutex_lock(&h->cb_lock);
   if (!h->freed && h->on_data)
      h->on_data(h, data, len, h->user_data);
   net_mutex_unlock(&h->cb_lock);
}

static void emit_done(http *h, const char *error) {
   net_mutex_lock(&h->cb_lock);
   if (!h->freed && h->on_done)
      h->on_done(h, error, h->user_data);
   net_mutex_unlock(&h->cb_lock);
}

/* The request */

static int64_t idle_deadline(http *h) {
   return h->idle_timeout_ms > 0 ? net_now_ms() + h->idle_timeout_ms : -1;
}

static void conn_error(http *h, const char *doing, char *err, size_t cap) {
   const char *e = net_conn_error(h->conn);
   if (strcmp(e, "timed out") == 0)
      snprintf(err, cap, "%s: no answer from the server for %d s", doing,
               h->idle_timeout_ms / 1000);
   else
      snprintf(err, cap, "%s: %s", doing, e);
}

// Delivers the body bytes in buf. False with err set if the body is
// malformed.
static bool decode(http *h, http_body *b, const char *buf, size_t len,
                   char *err, size_t cap) {
   while (len && !b->done) {
      const char *data;
      size_t data_len;
      long used = http_body_decode(b, buf, len, &data, &data_len);
      if (used < 0) {
         snprintf(err, cap, "malformed chunked body");
         return false;
      }
      emit_data(h, data, data_len);
      buf += used;
      len -= (size_t)used;
   }
   return true;
}

static void request(http *h, char *err, size_t cap) {
   net_conn *c = h->conn;
   int64_t deadline = net_now_ms() + h->connect_timeout_ms;
   if (!net_conn_dial(c, h->url.host, h->url.port, h->url.secure, deadline)) {
      snprintf(err, cap, "%s", net_conn_error(c));
      return;
   }

   char head[8192];
   int n = http_build_request(head, sizeof(head), h->method, &h->url,
                              (const char *const *)h->headers, h->has_body,
                              h->body_len);
   if (n < 0) {
      snprintf(err, cap, "request headers too large or invalid");
      return;
   }
   if (!net_conn_write_all(c, head, (size_t)n, idle_deadline(h)) ||
       (h->body_len &&
        !net_conn_write_all(c, h->body, h->body_len, idle_deadline(h)))) {
      conn_error(h, "sending the request", err, cap);
      return;
   }

   char *buf = malloc(HTTP_HEAD_MAX);
   if (!buf) {
      snprintf(err, cap, "out of memory");
      return;
   }
   bool head_request = strcmp(h->method, "HEAD") == 0;
   size_t len = 0, used = 0;
   http_head hd;
   for (;;) {
      int ok =
          len ? http_parse_head(buf, len, head_request, &hd, &used, err, cap)
              : 0;
      if (ok < 0)
         goto out;
      if (ok > 0 && hd.status >= 100 && hd.status < 200) {
         // An interim answer (100 Continue, 103 Early Hints): skip it.
         memmove(buf, buf + used, len - used);
         len -= used;
         continue;
      }
      if (ok > 0)
         break;
      long r = net_conn_read_some(c, buf + len, HTTP_HEAD_MAX - len,
                                  idle_deadline(h));
      if (r == NET_EOF) {
         snprintf(err, cap, "connection closed before the response");
         goto out;
      }
      if (r < 0) {
         conn_error(h, "waiting for the response", err, cap);
         goto out;
      }
      len += (size_t)r;
   }

   emit_response(h, &hd);
   http_body b;
   http_body_init(&b, &hd);
   if (!decode(h, &b, buf + used, len - used, err, cap))
      goto out;
   while (!b.done) {
      long r = net_conn_read_some(c, buf, READ_SIZE, idle_deadline(h));
      if (r == NET_EOF) {
         if (b.kind != HTTP_BODY_CLOSE)
            snprintf(err, cap, "connection closed before the end of the body");
         break;
      }
      if (r < 0) {
         conn_error(h, "reading the response", err, cap);
         break;
      }
      if (!decode(h, &b, buf, (size_t)r, err, cap))
         break;
   }
out:
   free(buf);
}

/* Lifetime */

static void http_destroy(http *h) {
   net_strv_free(h->headers);
   free(h->body);
   net_conn_free(h->conn);
   net_mutex_destroy(&h->lock);
   net_mutex_destroy(&h->cb_lock);
   free(h);
}

static void http_run(void *arg) {
   http *h = arg;
   http_current = h;
   char err[512] = "";
   request(h, err, sizeof(err));

   net_mutex_lock(&h->lock);
   net_conn *conn = h->conn;
   h->conn = NULL;
   net_mutex_unlock(&h->lock);
   net_conn_free(conn);

   emit_done(h, err[0] ? err : NULL);

   net_mutex_lock(&h->lock);
   h->done = true;
   bool free_now = h->aborted;
   net_mutex_unlock(&h->lock);
   if (free_now)
      http_destroy(h);
}

http *http_start(const http_desc *desc) {
   http_desc o = *desc;
   http *h = calloc(1, sizeof(*h));
   if (!h)
      return NULL;
   const char *method = o.method ? o.method : o.body ? "POST" : "GET";
   size_t method_len = strlen(method);
   if (!o.url || net_url_parse(o.url, &h->url) != 0 ||
       h->url.scheme != NET_HTTP || method_len == 0 ||
       method_len >= sizeof(h->method)) {
      free(h);
      return NULL;
   }
   for (size_t i = 0; i <= method_len; i++)
      h->method[i] = (char)toupper((unsigned char)method[i]);
   h->on_response = o.on_response;
   h->on_data = o.on_data;
   h->on_done = o.on_done;
   h->user_data = o.user_data;
   h->connect_timeout_ms = o.connect_timeout_ms > 0
                               ? o.connect_timeout_ms
                               : DEFAULT_CONNECT_TIMEOUT_MS;
   h->idle_timeout_ms = o.idle_timeout_ms == 0  ? DEFAULT_IDLE_TIMEOUT_MS
                        : o.idle_timeout_ms < 0 ? 0
                                                : o.idle_timeout_ms;
   net_mutex_init(&h->lock);
   net_mutex_init(&h->cb_lock);

   bool ok = true;
   h->headers = net_strv_copy(o.headers, &ok);
   if (ok && o.body) {
      h->has_body = true;
      h->body_len = o.body_len;
      h->body = malloc(o.body_len ? o.body_len : 1);
      if (h->body)
         memcpy(h->body, o.body, o.body_len);
      else
         ok = false;
   }
   h->conn = ok ? net_conn_new() : NULL;
   if (!h->conn || !net_thread_start(http_run, h)) {
      http_destroy(h);
      return NULL;
   }
   return h;
}

void http_free(http *h) {
   if (!h)
      return;
   bool self = http_current == h;
   if (!self)
      net_mutex_lock(&h->cb_lock);
   h->freed = true;
   if (!self)
      net_mutex_unlock(&h->cb_lock);

   net_mutex_lock(&h->lock);
   h->aborted = true;
   bool free_now = h->done;
   if (!free_now && h->conn)
      net_conn_cancel(h->conn);
   net_mutex_unlock(&h->lock);
   if (free_now)
      http_destroy(h);
}
