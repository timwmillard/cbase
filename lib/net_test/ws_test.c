/* Drives ws.c and the platform backend against an in-process ws:// server
 * built on ws_proto.c. The URL path picks how the server behaves:
 *
 *   /echo    echoes text and binary; some text messages are commands, see
 *            echo_command()
 *   /sink    reads nothing for SINK_DELAY_MS (so the client's writes back
 *            up), then counts what arrives; "done" answers with a summary
 *   /hang    accepts the TCP connection and never answers the handshake
 *   /silent  completes the handshake, then reads and ignores everything:
 *            no pongs, no answer to a close
 *
 * Grown from ~/dev/x/chat/tests/ws_echo_test.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "net/net.h"
#include "net/ws_proto.h"

static int failures;

#define CHECK(cond)                                                            \
   do {                                                                        \
      if (!(cond)) {                                                           \
         fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,      \
                 #cond);                                                       \
         failures++;                                                           \
      }                                                                        \
   } while (0)

#define SINK_DELAY_MS 300

static int64_t now_ms(void) {
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
   struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
   nanosleep(&t, NULL);
}

/* Server */

static bool send_all(int fd, const void *data, size_t len) {
   const char *p = data;
   while (len) {
      ssize_t n = send(fd, p, len, 0);
      if (n <= 0)
         return false;
      p += n;
      len -= (size_t)n;
   }
   return true;
}

static bool send_frame(int fd, int op, const void *data, size_t len) {
   uint8_t *frame = malloc(len + WS_FRAME_HEADER_MAX);
   size_t n = ws_frame_encode(frame, op, data, len, NULL);
   bool ok = send_all(fd, frame, n);
   free(frame);
   return ok;
}

// A frame ws_frame_encode can't make: fin may be false. Payload < 126 bytes.
static bool send_raw_frame(int fd, bool fin, int op, const char *s) {
   uint8_t frame[128];
   size_t len = strlen(s);
   frame[0] = (uint8_t)((fin ? 0x80 : 0) | op);
   frame[1] = (uint8_t)len;
   memcpy(frame + 2, s, len);
   return send_all(fd, frame, len + 2);
}

static bool send_text(int fd, const char *s) {
   return send_frame(fd, WS_OP_TEXT, s, strlen(s));
}

static bool send_close(int fd, int code, const char *reason) {
   uint8_t payload[125];
   size_t m = code == 1005 ? 0 : ws_close_payload(payload, code, reason);
   return send_frame(fd, WS_OP_CLOSE, payload, m);
}

// The value of a request header, "" when missing.
static void header(const char *req, const char *name, char *out, size_t cap) {
   out[0] = '\0';
   size_t n = strlen(name);
   for (const char *line = strstr(req, "\r\n"); line && line[2] != '\r';
        line = strstr(line + 2, "\r\n")) {
      if (strncasecmp(line + 2, name, n) == 0 && line[2 + n] == ':') {
         const char *v = line + 3 + n;
         while (*v == ' ')
            v++;
         size_t len = strcspn(v, "\r");
         snprintf(out, cap, "%.*s", (int)len, v);
         return;
      }
   }
}

typedef struct {
   int fd;
   char req[8192];
   ws_reader r;
   // /sink
   size_t bytes;
   int msgs, last_seq[8];
   bool in_order;
   // /echo ping-me
   char want_pong[16];
} conn;

// /echo's commands. Returns false to end the connection.
static bool echo_command(conn *c, const char *s, size_t len) {
   char cmd[64];
   snprintf(cmd, sizeof(cmd), "%.*s", (int)(len < 63 ? len : 63), s);
   if (strcmp(cmd, "close-me") == 0) {
      send_close(c->fd, 4000, "bye"); // the client answers, then we stop
   } else if (strcmp(cmd, "close-empty") == 0) {
      send_close(c->fd, 1005, NULL);
   } else if (strcmp(cmd, "drop-me") == 0) {
      return false; // no close frame
   } else if (strcmp(cmd, "ping-me") == 0) {
      snprintf(c->want_pong, sizeof(c->want_pong), "p-42");
      send_frame(c->fd, WS_OP_PING, "p-42", 4);
   } else if (strcmp(cmd, "fragments") == 0) {
      // "hello" in three pieces with a ping in the middle.
      send_raw_frame(c->fd, false, WS_OP_TEXT, "he");
      send_raw_frame(c->fd, true, WS_OP_PING, "mid");
      send_raw_frame(c->fd, false, WS_OP_CONT, "ll");
      send_raw_frame(c->fd, true, WS_OP_CONT, "o");
   } else if (strncmp(cmd, "big:", 4) == 0) {
      size_t n = (size_t)strtoul(cmd + 4, NULL, 10);
      uint8_t *data = calloc(1, n);
      send_frame(c->fd, WS_OP_BINARY, data, n);
      free(data);
   } else if (strcmp(cmd, "headers") == 0) {
      char x[128], proto[128], out[300];
      header(c->req, "X-Test", x, sizeof(x));
      header(c->req, "Sec-WebSocket-Protocol", proto, sizeof(proto));
      snprintf(out, sizeof(out), "x=%s proto=%s", x, proto);
      send_text(c->fd, out);
   } else if (strcmp(cmd, "bad-utf8") == 0) {
      send_frame(c->fd, WS_OP_TEXT, "\xff\xfe", 2);
   } else {
      send_frame(c->fd, WS_OP_TEXT, s, len);
   }
   return true;
}

// /sink: text "s<sender>:<seq>" checks each sender's order; "done" answers.
static void sink_message(conn *c, const ws_event *ev) {
   c->msgs++;
   if (ev->type == WS_EV_BINARY) {
      c->bytes += ev->len;
      return;
   }
   char s[64];
   snprintf(s, sizeof(s), "%.*s", (int)(ev->len < 63 ? ev->len : 63),
            (const char *)ev->data);
   unsigned sender, seq;
   if (strcmp(s, "done") == 0) {
      char out[128];
      snprintf(out, sizeof(out), "bytes=%zu msgs=%d order=%s", c->bytes,
               c->msgs - 1, c->in_order ? "ok" : "bad");
      send_text(c->fd, out);
   } else if (sscanf(s, "s%u:%u", &sender, &seq) == 2 && sender < 8) {
      if ((int)seq != c->last_seq[sender] + 1)
         c->in_order = false;
      c->last_seq[sender] = (int)seq;
   }
}

static void serve(conn *c) {
   size_t len = 0;
   char *end = NULL;
   while (!end && len < sizeof(c->req) - 1) {
      ssize_t n = recv(c->fd, c->req + len, sizeof(c->req) - 1 - len, 0);
      if (n <= 0)
         return;
      len += (size_t)n;
      c->req[len] = '\0';
      end = strstr(c->req, "\r\n\r\n");
   }
   if (!end)
      return;
   char path[64] = "";
   sscanf(c->req, "%*s %63s", path);

   if (strcmp(path, "/hang") == 0) {
      char buf[1024];
      while (recv(c->fd, buf, sizeof(buf), 0) > 0) {
      }
      return;
   }

   char key[64], accept[29], resp[512], proto[128];
   header(c->req, "Sec-WebSocket-Key", key, sizeof(key));
   header(c->req, "Sec-WebSocket-Protocol", proto, sizeof(proto));
   proto[strcspn(proto, ", ")] = '\0'; // pick the first one offered
   ws_accept_key(key, accept);
   char proto_line[160] = "";
   if (proto[0])
      snprintf(proto_line, sizeof(proto_line),
               "Sec-WebSocket-Protocol: %s\r\n", proto);
   int n = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n%s\r\n",
                    accept, proto_line);
   send_all(c->fd, resp, (size_t)n);

   bool echo = strcmp(path, "/echo") == 0;
   bool sink = strcmp(path, "/sink") == 0;
   bool silent = strcmp(path, "/silent") == 0;
   if (sink) {
      sleep_ms(SINK_DELAY_MS);
      c->in_order = true;
      for (int i = 0; i < 8; i++)
         c->last_seq[i] = -1;
   }

   ws_reader_init(&c->r, true, 64u << 20);
   size_t used = (size_t)(end + 4 - c->req);
   ws_reader_feed(&c->r, c->req + used, len - used);
   uint8_t *buf = malloc(1 << 16);
   for (bool done = false; !done;) {
      for (;;) {
         ws_event ev = ws_reader_next(&c->r);
         if (ev.type == WS_EV_NONE || silent)
            break;
         if (ev.type == WS_EV_TEXT || ev.type == WS_EV_BINARY) {
            if (sink)
               sink_message(c, &ev);
            else if (echo && ev.type == WS_EV_TEXT)
               done = !echo_command(c, (const char *)ev.data, ev.len);
            else
               send_frame(c->fd, WS_OP_BINARY, ev.data, ev.len);
         } else if (ev.type == WS_EV_PING) {
            send_frame(c->fd, WS_OP_PONG, ev.data, ev.len);
         } else if (ev.type == WS_EV_PONG) {
            char got[16];
            snprintf(got, sizeof(got), "%.*s", (int)ev.len,
                     (const char *)ev.data);
            if (c->want_pong[0]) {
               char out[64];
               snprintf(out, sizeof(out), "pong:%s", got);
               send_text(c->fd, out);
               c->want_pong[0] = '\0';
            }
         } else if (ev.type == WS_EV_CLOSE) {
            send_close(c->fd, ev.code, ev.reason);
            done = true;
         } else if (ev.type == WS_EV_ERROR) {
            fprintf(stderr, "server: %s\n", ev.reason);
            done = true;
         }
         if (done)
            break;
      }
      if (done)
         break;
      ssize_t got = recv(c->fd, buf, 1 << 16, 0);
      if (got <= 0)
         break;
      if (silent)
         continue;
      ws_reader_feed(&c->r, buf, (size_t)got);
   }
   free(buf);
   ws_reader_free(&c->r);
}

static void *conn_thread(void *arg) {
   conn *c = arg;
   serve(c);
   close(c->fd);
   free(c);
   return NULL;
}

static void *accept_loop(void *arg) {
   int listener = (int)(intptr_t)arg;
   for (;;) {
      int fd = accept(listener, NULL, NULL);
      if (fd < 0) {
         if (errno == EINTR)
            continue;
         return NULL;
      }
      conn *c = calloc(1, sizeof(*c));
      c->fd = fd;
      pthread_t t;
      pthread_create(&t, NULL, conn_thread, c);
      pthread_detach(t);
   }
}

static int start_server(void) {
   int listener = socket(AF_INET, SOCK_STREAM, 0);
   int one = 1;
   setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   struct sockaddr_in addr = {0};
   addr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
   socklen_t len = sizeof(addr);
   if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
       listen(listener, 32) != 0 ||
       getsockname(listener, (struct sockaddr *)&addr, &len) != 0) {
      perror("server");
      exit(1);
   }
   pthread_t t;
   pthread_create(&t, NULL, accept_loop, (void *)(intptr_t)listener);
   pthread_detach(t);
   return ntohs(addr.sin_port);
}

// A port nothing listens on.
static int closed_port(void) {
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   struct sockaddr_in addr = {0};
   addr.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
   socklen_t len = sizeof(addr);
   bind(fd, (struct sockaddr *)&addr, sizeof(addr));
   getsockname(fd, (struct sockaddr *)&addr, &len);
   close(fd);
   return ntohs(addr.sin_port);
}

/* Client */

static int port;

static void url_for(char *out, size_t cap, const char *path) {
   snprintf(out, cap, "ws://127.0.0.1:%d%s", port, path);
}

typedef struct {
   pthread_mutex_t m;
   pthread_cond_t c;
   bool opened, closed;
   bool free_on_close, free_on_message;
   int close_code, messages, errors, callbacks_after_close;
   char reason[128], error[256];
   uint8_t *msg;
   size_t msg_len;
   bool msg_binary;
   int64_t closed_at;
} client;

static void on_open(ws *w, void *user) {
   (void)w;
   client *c = user;
   pthread_mutex_lock(&c->m);
   if (c->closed)
      c->callbacks_after_close++;
   c->opened = true;
   pthread_cond_broadcast(&c->c);
   pthread_mutex_unlock(&c->m);
}

static void on_message(ws *w, const char *data, size_t len, bool binary,
                       void *user) {
   client *c = user;
   pthread_mutex_lock(&c->m);
   free(c->msg);
   c->msg = malloc(len + 1);
   memcpy(c->msg, data, len);
   c->msg[len] = '\0';
   c->msg_len = len;
   c->msg_binary = binary;
   c->messages++;
   if (c->closed)
      c->callbacks_after_close++;
   bool free_now = c->free_on_message;
   pthread_cond_broadcast(&c->c);
   pthread_mutex_unlock(&c->m);
   if (free_now)
      ws_free(w); // from inside a callback: on_close must not follow
}

static void on_error(ws *w, const char *message, void *user) {
   (void)w;
   client *c = user;
   pthread_mutex_lock(&c->m);
   if (c->closed)
      c->callbacks_after_close++;
   c->errors++;
   snprintf(c->error, sizeof(c->error), "%s", message);
   pthread_mutex_unlock(&c->m);
}

static void on_close(ws *w, int code, const char *reason, void *user) {
   client *c = user;
   pthread_mutex_lock(&c->m);
   if (c->closed)
      c->callbacks_after_close++;
   c->closed = true;
   c->closed_at = now_ms();
   c->close_code = code;
   snprintf(c->reason, sizeof(c->reason), "%s", reason);
   bool free_now = c->free_on_close;
   pthread_cond_broadcast(&c->c);
   pthread_mutex_unlock(&c->m);
   if (free_now)
      ws_free(w);
}

static ws_desc client_desc(client *c) {
   memset(c, 0, sizeof(*c));
   pthread_mutex_init(&c->m, NULL);
   pthread_cond_init(&c->c, NULL);
   return (ws_desc){
       .on_open = on_open,
       .on_message = on_message,
       .on_close = on_close,
       .on_error = on_error,
       .user_data = c,
   };
}

static void client_free(client *c) {
   free(c->msg);
   pthread_mutex_destroy(&c->m);
   pthread_cond_destroy(&c->c);
}

static ws *open_to(ws_desc d, const char *path) {
   char url[64];
   url_for(url, sizeof(url), path);
   ws *w = ws_start(&d, &(ws_request){.url = url});
   CHECK(w != NULL);
   return w;
}

// Waits up to secs for cond, which reads the client's fields under its lock.
#define WAIT_FOR(cl, secs, cond)                                               \
   do {                                                                        \
      struct timespec until_;                                                  \
      clock_gettime(CLOCK_REALTIME, &until_);                                  \
      until_.tv_sec += (secs);                                                 \
      pthread_mutex_lock(&(cl)->m);                                            \
      while (!(cond) &&                                                        \
             pthread_cond_timedwait(&(cl)->c, &(cl)->m, &until_) == 0) {       \
      }                                                                        \
      bool ok_ = (cond);                                                       \
      pthread_mutex_unlock(&(cl)->m);                                          \
      if (!ok_) {                                                              \
         fprintf(stderr, "%s:%d: timed out waiting for %s\n", __FILE__,        \
                 __LINE__, #cond);                                             \
         failures++;                                                           \
      }                                                                        \
   } while (0)
#define WAIT(cl, cond) WAIT_FOR(cl, 10, cond)

// The last message as a string, read under the lock.
static bool last_is(client *c, const char *s) {
   pthread_mutex_lock(&c->m);
   bool same = c->msg && strcmp((char *)c->msg, s) == 0;
   if (!same)
      fprintf(stderr, "  last message: \"%s\", want \"%s\"\n",
              c->msg ? (char *)c->msg : "(none)", s);
   pthread_mutex_unlock(&c->m);
   return same;
}

// Takes 200 ms: checks that no callback ran after ws_free returned.
static void check_quiet_after_free(client *c) {
   pthread_mutex_lock(&c->m);
   int before = c->messages + c->errors + (c->closed ? 1 : 0) +
                (c->opened ? 1 : 0);
   pthread_mutex_unlock(&c->m);
   sleep_ms(200);
   pthread_mutex_lock(&c->m);
   int after = c->messages + c->errors + (c->closed ? 1 : 0) +
               (c->opened ? 1 : 0);
   pthread_mutex_unlock(&c->m);
   CHECK(before == after);
}

/* Tests */

static void test_bad_args(void) {
   CHECK(ws_start(NULL, NULL) == NULL);
   CHECK(ws_start(NULL, &(ws_request){.url = "http://example.com"}) == NULL);
   CHECK(ws_start(NULL, &(ws_request){.url = "ws://"}) == NULL);
   CHECK(ws_start(NULL, &(ws_request){0}) == NULL);
   CHECK(ws_start(&(ws_desc){0}, &(ws_request){.url = "not a url"}) == NULL);

   // A NULL desc is the defaults with no callbacks: still connects and frees.
   char url[64];
   url_for(url, sizeof(url), "/echo");
   ws *w = ws_start(NULL, &(ws_request){.url = url});
   CHECK(w != NULL);
   sleep_ms(50);
   ws_free(w);
   ws_free(NULL);
}

static void test_echo(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/echo");
   WAIT(&c, c.opened || c.closed);
   CHECK(c.opened);

   CHECK(ws_send_text(w, "hello", 5));
   WAIT(&c, c.messages == 1);
   CHECK(c.msg_len == 5 && !c.msg_binary && last_is(&c, "hello"));

   CHECK(ws_send_text(w, "", 0));
   WAIT(&c, c.messages == 2);
   CHECK(c.msg_len == 0 && !c.msg_binary);

   size_t big = 300000;
   uint8_t *data = malloc(big);
   for (size_t i = 0; i < big; i++)
      data[i] = (uint8_t)(i * 7);
   CHECK(ws_send_binary(w, data, big));
   WAIT(&c, c.messages == 3);
   CHECK(c.msg_len == big && c.msg_binary && memcmp(c.msg, data, big) == 0);
   free(data);

   for (int i = 0; i < 100; i++) {
      char m[16];
      int n = snprintf(m, sizeof(m), "m%d", i);
      CHECK(ws_send_text(w, m, (size_t)n));
   }
   WAIT(&c, c.messages == 103);
   CHECK(last_is(&c, "m99"));

   ws_close(w, WS_CLOSE_NORMAL, "done");
   CHECK(!ws_send_text(w, "late", 4));
   ws_close(w, 4999, "twice"); // ignored
   WAIT(&c, c.closed);
   CHECK(c.close_code == WS_CLOSE_NORMAL);
   CHECK(strcmp(c.reason, "done") == 0);
   CHECK(c.errors == 0);
   if (c.error[0])
      fprintf(stderr, "  error: %s\n", c.error);
   CHECK(!ws_send_text(w, "after close", 11));
   ws_free(w);
   CHECK(c.callbacks_after_close == 0);
   client_free(&c);
}

static void test_headers_and_protocols(void) {
   client c;
   ws_desc d = client_desc(&c);
   char url[64];
   url_for(url, sizeof(url), "/echo");
   ws *w = ws_start(&d, &(ws_request){
                            .url = url,
                            .headers = (const char *[]){"X-Test: abc", NULL},
                            .protocols = (const char *[]){"v2", "v1", NULL},
                        });
   WAIT(&c, c.opened || c.closed);
   CHECK(ws_send_text(w, "headers", 7));
   WAIT(&c, c.messages == 1);
   CHECK(last_is(&c, "x=abc proto=v2, v1"));
   ws_free(w);
   client_free(&c);

   // A header with a line break would inject headers: refused.
   d = client_desc(&c);
   w = ws_start(&d, &(ws_request){
                        .url = url,
                        .headers = (const char *[]){"X-Test: a\r\nEvil: 1",
                                                    NULL},
                    });
   if (w) {
      WAIT(&c, c.closed);
      CHECK(!c.opened && c.errors == 1);
      ws_free(w);
   }
   client_free(&c);
}

static void test_server_close(void) {
   client c;
   ws_desc d = client_desc(&c);
   c.free_on_close = true; // ws_free from inside on_close
   ws *w = open_to(d, "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "close-me", 8));
   WAIT(&c, c.closed);
   CHECK(c.close_code == 4000);
   CHECK(strcmp(c.reason, "bye") == 0);
   CHECK(c.errors == 0);
   sleep_ms(50);
   client_free(&c);

   // A close frame without a status code reads as 1005.
   d = client_desc(&c);
   w = open_to(d, "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "close-empty", 11));
   WAIT(&c, c.closed);
   CHECK(c.close_code == 1005 && c.errors == 0);
   ws_free(w);
   client_free(&c);
}

static void test_server_drops(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "drop-me", 7));
   WAIT(&c, c.closed);
   CHECK(c.close_code == WS_CLOSE_ABNORMAL);
   CHECK(c.errors == 1 && strstr(c.error, "closed by server") != NULL);
   ws_free(w);
   client_free(&c);
}

static void test_refused(void) {
   char url[64];
   snprintf(url, sizeof(url), "ws://127.0.0.1:%d/", closed_port());
   client c;
   ws_desc d = client_desc(&c);
   d.connect_timeout_ms = 3000;
   ws *w = ws_start(&d, &(ws_request){.url = url});
   WAIT(&c, c.closed);
   CHECK(!c.opened);
   CHECK(c.close_code == WS_CLOSE_ABNORMAL);
   CHECK(c.errors == 1);
   ws_free(w);
   client_free(&c);
}

static void test_handshake_timeout(void) {
   client c;
   ws_desc d = client_desc(&c);
   d.connect_timeout_ms = 300;
   int64_t start = now_ms();
   ws *w = open_to(d, "/hang");
   WAIT(&c, c.closed);
   int64_t took = now_ms() - start;
   CHECK(!c.opened && c.errors == 1);
   CHECK(c.close_code == WS_CLOSE_ABNORMAL);
   CHECK(took >= 250 && took < 3000);
   ws_free(w);
   client_free(&c);
}

static void test_free_while_open(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/echo");
   WAIT(&c, c.opened);
   ws_free(w);
   check_quiet_after_free(&c);
   client_free(&c);
}

static void test_free_in_on_message(void) {
   client c;
   ws_desc d = client_desc(&c);
   c.free_on_message = true;
   ws *w = open_to(d, "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "one", 3));
   CHECK(ws_send_text(w, "two", 3)); // may or may not arrive; never after free
   WAIT(&c, c.messages >= 1);
   sleep_ms(200);
   pthread_mutex_lock(&c.m);
   CHECK(c.messages == 1 && !c.closed && c.errors == 0);
   pthread_mutex_unlock(&c.m);
   client_free(&c);
}

static void test_free_while_connecting(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/hang");
   sleep_ms(50); // connected, waiting for the handshake answer
   int64_t start = now_ms();
   ws_free(w);
   CHECK(now_ms() - start < 500);
   check_quiet_after_free(&c);
   CHECK(!c.opened && !c.closed && c.errors == 0);
   client_free(&c);
}

static void test_close_while_connecting(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/hang");
   sleep_ms(50);
   CHECK(!ws_send_text(w, "too early", 9));
   int64_t start = now_ms();
   ws_close(w, 4001, "never mind");
   WAIT(&c, c.closed);
   CHECK(now_ms() - start < 1000);
   CHECK(!c.opened && c.errors == 0); // cancelled, not failed
   CHECK(c.close_code == 4001 && strcmp(c.reason, "never mind") == 0);
   ws_free(w);
   client_free(&c);
}

static void test_server_ping(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "ping-me", 7));
   WAIT(&c, c.messages == 1);
   CHECK(last_is(&c, "pong:p-42"));
   ws_free(w);
   client_free(&c);
}

static void test_fragments(void) {
   client c;
   ws *w = open_to(client_desc(&c), "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "fragments", 9));
   WAIT(&c, c.messages == 1);
   CHECK(last_is(&c, "hello") && !c.msg_binary);
   ws_close(w, WS_CLOSE_NORMAL, NULL);
   WAIT(&c, c.closed);
   CHECK(c.errors == 0);
   ws_free(w);
   client_free(&c);
}

static void test_protocol_errors(void) {
   client c;
   ws_desc d = client_desc(&c);
   d.max_message_size = 1024;
   ws *w = open_to(d, "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "big:1000", 8));
   WAIT(&c, c.messages == 1);
   CHECK(c.msg_len == 1000);
   CHECK(ws_send_text(w, "big:2000", 8));
   WAIT(&c, c.closed);
   CHECK(c.messages == 1 && c.errors == 1);
   CHECK(c.close_code == WS_CLOSE_TOO_BIG);
   ws_free(w);
   client_free(&c);

   d = client_desc(&c);
   w = open_to(d, "/echo");
   WAIT(&c, c.opened);
   CHECK(ws_send_text(w, "bad-utf8", 8));
   WAIT(&c, c.closed);
   CHECK(c.messages == 0 && c.errors == 1);
   CHECK(c.close_code == WS_CLOSE_INVALID_DATA);
   ws_free(w);
   client_free(&c);
}

// A message sent while an earlier one is still going out must not wait for
// the next ping. The sink server reads nothing for SINK_DELAY_MS, so the big
// message fills the socket buffers; "done" is queued while the network thread
// is blocked writing. Pings are off: a lost wake-up would stall for good.
static void test_queued_during_write(void) {
   const int delays[] = {0, 1, 10, 50, 150};
   size_t big = 8u << 20;
   uint8_t *data = calloc(1, big);
   for (size_t k = 0; k < sizeof(delays) / sizeof(delays[0]); k++) {
      client c;
      ws_desc d = client_desc(&c);
      d.ping_interval_ms = -1;
      ws *w = open_to(d, "/sink");
      WAIT(&c, c.opened);
      int64_t start = now_ms();
      CHECK(ws_send_binary(w, data, big));
      sleep_ms(delays[k]);
      CHECK(ws_send_text(w, "done", 4));
      WAIT_FOR(&c, 5, c.messages == 1 || c.closed);
      char want[64];
      snprintf(want, sizeof(want), "bytes=%zu msgs=1 order=ok", big);
      CHECK(c.messages == 1 && last_is(&c, want));
      int64_t took = now_ms() - start;
      if (took > 3000)
         fprintf(stderr, "  delay %d ms: answer took %lld ms\n", delays[k],
                 (long long)took);
      ws_free(w);
      client_free(&c);
   }
   free(data);
}

// ws_send_* from several threads at once, while the queue is backed up: every
// message arrives once, each thread's in the order it sent them.
enum { SENDERS = 4, PER_SENDER = 500 };

typedef struct {
   ws *w;
   int id;
   int failed;
} sender;

static void *send_many(void *arg) {
   sender *s = arg;
   uint8_t chunk[4096] = {0};
   for (int i = 0; i < PER_SENDER; i++) {
      char m[32];
      int n = snprintf(m, sizeof(m), "s%d:%d", s->id, i);
      if (!ws_send_text(s->w, m, (size_t)n))
         s->failed++;
      if (i % 50 == 0 && !ws_send_binary(s->w, chunk, sizeof(chunk)))
         s->failed++;
   }
   return NULL;
}

static void test_concurrent_senders(void) {
   client c;
   ws_desc d = client_desc(&c);
   d.ping_interval_ms = -1;
   ws *w = open_to(d, "/sink");
   WAIT(&c, c.opened);
   pthread_t t[SENDERS];
   sender s[SENDERS];
   for (int i = 0; i < SENDERS; i++) {
      s[i] = (sender){.w = w, .id = i};
      pthread_create(&t[i], NULL, send_many, &s[i]);
   }
   for (int i = 0; i < SENDERS; i++) {
      pthread_join(t[i], NULL);
      CHECK(s[i].failed == 0);
   }
   CHECK(ws_send_text(w, "done", 4));
   WAIT_FOR(&c, 5, c.messages == 1 || c.closed);
   char want[64];
   int binaries = SENDERS * (PER_SENDER / 50);
   snprintf(want, sizeof(want), "bytes=%d msgs=%d order=ok", binaries * 4096,
            SENDERS * PER_SENDER + binaries);
   CHECK(last_is(&c, want));
   ws_free(w);
   client_free(&c);
}

// A server that stops answering is noticed by the pings.
static void test_no_pong(void) {
   client c;
   ws_desc d = client_desc(&c);
   d.ping_interval_ms = 100;
   ws *w = open_to(d, "/silent");
   WAIT(&c, c.opened);
   int64_t start = now_ms();
   WAIT_FOR(&c, 3, c.closed);
   int64_t took = now_ms() - start;
   CHECK(c.errors == 1 && strstr(c.error, "no pong") != NULL);
   CHECK(c.close_code == WS_CLOSE_ABNORMAL);
   CHECK(took >= 200 && took < 1500);
   ws_free(w);
   client_free(&c);
}

// A server that never answers our close: on_close after the close timeout,
// with the code we sent, and not reported as an error.
static void test_close_unanswered(void) {
   client c;
   ws_desc d = client_desc(&c);
   ws *w = open_to(d, "/silent");
   WAIT(&c, c.opened);
   int64_t start = now_ms();
   ws_close(w, WS_CLOSE_GOING_AWAY, "leaving");
   WAIT_FOR(&c, 10, c.closed);
   int64_t took = now_ms() - start;
   CHECK(c.close_code == WS_CLOSE_GOING_AWAY);
   CHECK(strcmp(c.reason, "leaving") == 0);
   CHECK(c.errors == 0);
   CHECK(took >= 4000 && took < 8000); // CLOSE_TIMEOUT_MS is 5 s
   ws_free(w);
   client_free(&c);
}

typedef struct {
   const char *name;
   void (*fn)(void);
} test;

int main(int argc, char **argv) {
   signal(SIGPIPE, SIG_IGN);
   port = start_server();

   const test tests[] = {
       {"bad_args", test_bad_args},
       {"echo", test_echo},
       {"headers_and_protocols", test_headers_and_protocols},
       {"server_close", test_server_close},
       {"server_drops", test_server_drops},
       {"refused", test_refused},
       {"handshake_timeout", test_handshake_timeout},
       {"free_while_open", test_free_while_open},
       {"free_in_on_message", test_free_in_on_message},
       {"free_while_connecting", test_free_while_connecting},
       {"close_while_connecting", test_close_while_connecting},
       {"server_ping", test_server_ping},
       {"fragments", test_fragments},
       {"protocol_errors", test_protocol_errors},
       {"queued_during_write", test_queued_during_write},
       {"concurrent_senders", test_concurrent_senders},
       {"no_pong", test_no_pong},
       {"close_unanswered", test_close_unanswered},
   };
   // Optional arguments: names of the tests to run.
   for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
      bool run = argc < 2;
      for (int a = 1; a < argc; a++)
         run |= strcmp(argv[a], tests[i].name) == 0;
      if (!run)
         continue;
      int before = failures;
      int64_t start = now_ms();
      tests[i].fn();
      printf("  %-24s %s (%lld ms)\n", tests[i].name,
             failures == before ? "ok" : "FAIL",
             (long long)(now_ms() - start));
   }

   sleep_ms(50); // let freed connections' threads finish, for leak checkers
   if (failures) {
      fprintf(stderr, "%s: %d failure(s)\n", argv[0], failures);
      return 1;
   }
   printf("%s: ok\n", argv[0]);
   return 0;
}
