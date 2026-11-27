/* Drives http.c (and sse.c) against an in-process HTTP server. The path
 * picks the answer; see respond(). Ported from ~/dev/x/chat/tests. */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "net/net.h"

static int failures;

#define CHECK(cond)                                                            \
   do {                                                                        \
      if (!(cond)) {                                                           \
         fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,      \
                 #cond);                                                       \
         failures++;                                                           \
      }                                                                        \
   } while (0)

#define BIG_SIZE (3u << 20)

static void sleep_ms(int ms) {
   struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
   nanosleep(&ts, NULL);
}

/* Server */

static bool send_str(int fd, const char *s, size_t len) {
   while (len) {
      ssize_t n = send(fd, s, len, 0);
      if (n <= 0)
         return false;
      s += n;
      len -= (size_t)n;
   }
   return true;
}

// Case-insensitive strstr; strcasestr isn't POSIX.
static const char *find(const char *s, const char *what) {
   size_t n = strlen(what);
   for (; *s; s++) {
      if (strncasecmp(s, what, n) == 0)
         return s;
   }
   return NULL;
}

#define SEND(fd, s) send_str(fd, s, strlen(s))

static bool send_chunk(int fd, const char *s) {
   char size[32];
   snprintf(size, sizeof(size), "%zx\r\n", strlen(s));
   return SEND(fd, size) && SEND(fd, s) && SEND(fd, "\r\n");
}

static void respond(int fd, const char *path, const char *headers,
                    const char *body, size_t body_len) {
   char buf[512];
   if (strcmp(path, "/length") == 0) {
      SEND(fd, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nX-Test: yes\r\n\r\n"
               "hello world");
   } else if (strcmp(path, "/chunked") == 0) {
      SEND(fd, "HTTP/1.1 100 Continue\r\n\r\n"
               "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      const char *parts[] = {"one ", "two ", "three", NULL};
      for (int i = 0; parts[i]; i++) {
         send_chunk(fd, parts[i]);
         sleep_ms(20);
      }
      SEND(fd, "0\r\n\r\n");
   } else if (strcmp(path, "/close") == 0) {
      SEND(fd, "HTTP/1.0 200 OK\r\n\r\nuntil the end");
   } else if (strcmp(path, "/sse") == 0) {
      SEND(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
      send_chunk(fd, ": keep-alive\n\n");
      for (int i = 1; i <= 3; i++) {
         snprintf(buf, sizeof(buf), "data: {\"n\":%d}\n\n", i);
         // Split each event across two chunks.
         size_t half = strlen(buf) / 2;
         char first[256];
         snprintf(first, sizeof(first), "%.*s", (int)half, buf);
         send_chunk(fd, first);
         send_chunk(fd, buf + half);
         sleep_ms(10);
      }
      send_chunk(fd, "data: [DONE]\n\n");
      SEND(fd, "0\r\n\r\n");
   } else if (strcmp(path, "/404") == 0) {
      SEND(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
   } else if (strcmp(path, "/big") == 0) {
      snprintf(buf, sizeof(buf),
               "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", BIG_SIZE);
      SEND(fd, buf);
      char *big = malloc(BIG_SIZE);
      for (size_t i = 0; i < BIG_SIZE; i++)
         big[i] = (char)('a' + i % 26);
      send_str(fd, big, BIG_SIZE);
      free(big);
   } else if (strcmp(path, "/echo") == 0) {
      const char *x = find(headers, "\r\nX-Test: ");
      snprintf(buf, sizeof(buf),
               "HTTP/1.1 201 Created\r\nContent-Length: %zu\r\n"
               "X-Seen: %s\r\n\r\n",
               body_len, x ? "yes" : "no");
      SEND(fd, buf);
      send_str(fd, body, body_len);
   } else if (strcmp(path, "/slow") == 0) {
      SEND(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      send_chunk(fd, "first");
      sleep_ms(1500);
   } else if (strcmp(path, "/forever") == 0) {
      SEND(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      for (int i = 0; i < 200 && send_chunk(fd, "tick "); i++)
         sleep_ms(10);
   } else if (strcmp(path, "/truncated") == 0) {
      SEND(fd, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort");
   } else {
      SEND(fd, "garbage\r\n\r\n");
   }
}

static void *serve(void *arg) {
   int fd = (int)(intptr_t)arg;
   enum { REQ_MAX = 1 << 16 };
   char *buf = malloc(REQ_MAX);
   size_t len = 0;
   char *end = NULL;
   while (!end && len < REQ_MAX - 1) {
      ssize_t n = recv(fd, buf + len, REQ_MAX - 1 - len, 0);
      if (n <= 0)
         goto out;
      len += (size_t)n;
      buf[len] = '\0';
      end = strstr(buf, "\r\n\r\n");
   }
   if (!end)
      goto out;
   size_t head_len = (size_t)(end + 4 - buf);
   size_t body_len = 0;
   const char *cl = find(buf, "\r\nContent-Length: ");
   if (cl && cl < end)
      body_len = (size_t)strtoul(cl + 18, NULL, 10);
   while (len < head_len + body_len && len < REQ_MAX - 1) {
      ssize_t n = recv(fd, buf + len, REQ_MAX - 1 - len, 0);
      if (n <= 0)
         goto out;
      len += (size_t)n;
   }
   char path[256] = "";
   sscanf(buf, "%*s %255s", path);
   *end = '\0';
   respond(fd, path, buf, buf + head_len, body_len);
out:
   free(buf);
   close(fd);
   return NULL;
}

static void *accept_loop(void *arg) {
   int listener = (int)(intptr_t)arg;
   for (;;) {
      int fd = accept(listener, NULL, NULL);
      if (fd < 0)
         return NULL;
      pthread_t t;
      pthread_create(&t, NULL, serve, (void *)(intptr_t)fd);
      pthread_detach(t);
   }
}

static int start_server(void) {
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   int one = 1;
   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   struct sockaddr_in a = {0};
   a.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
   bind(fd, (struct sockaddr *)&a, sizeof(a));
   listen(fd, 16);
   socklen_t alen = sizeof(a);
   getsockname(fd, (struct sockaddr *)&a, &alen);
   pthread_t t;
   pthread_create(&t, NULL, accept_loop, (void *)(intptr_t)fd);
   pthread_detach(t);
   return ntohs(a.sin_port);
}

/* Client */

typedef struct {
   pthread_mutex_t lock;
   pthread_cond_t cond;
   int status, responses, dones;
   char x_test[16];
   char *body;
   size_t body_len;
   char error[512];
   bool free_on_data;
   sse_parser sse;
   bool use_sse;
   char events[256];
} result;

static void on_event(sse_parser *p, const char *event, const char *data,
                     size_t len, void *user);

static void result_init(result *r) {
   memset(r, 0, sizeof(*r));
   pthread_mutex_init(&r->lock, NULL);
   pthread_cond_init(&r->cond, NULL);
   sse_init(&r->sse, &(sse_desc){.on_event = on_event, .user_data = r});
}

static void result_free(result *r) {
   free(r->body);
   sse_free(&r->sse);
   pthread_mutex_destroy(&r->lock);
   pthread_cond_destroy(&r->cond);
}

static void on_event(sse_parser *p, const char *event, const char *data,
                     size_t len, void *user) {
   (void)p;
   result *r = user;
   (void)len;
   size_t n = strlen(r->events);
   snprintf(r->events + n, sizeof(r->events) - n, "%s:%s;", event, data);
}

static void on_response(http *h, const http_response *res, void *user) {
   (void)h;
   result *r = user;
   pthread_mutex_lock(&r->lock);
   r->status = res->status;
   r->responses++;
   size_t len;
   const char *v = http_header(res, "x-test", &len);
   if (!v)
      v = http_header(res, "X-SEEN", &len);
   if (v)
      snprintf(r->x_test, sizeof(r->x_test), "%.*s", (int)len, v);
   pthread_mutex_unlock(&r->lock);
}

static void on_data(http *h, const char *data, size_t len, void *user) {
   result *r = user;
   pthread_mutex_lock(&r->lock);
   r->body = realloc(r->body, r->body_len + len + 1);
   memcpy(r->body + r->body_len, data, len);
   r->body_len += len;
   r->body[r->body_len] = '\0';
   if (r->use_sse)
      CHECK(sse_feed(&r->sse, data, len));
   bool free_now = r->free_on_data;
   pthread_cond_broadcast(&r->cond);
   pthread_mutex_unlock(&r->lock);
   if (free_now)
      http_free(h); // from inside a callback
}

static void on_done(http *h, const char *error, void *user) {
   (void)h;
   result *r = user;
   pthread_mutex_lock(&r->lock);
   r->dones++;
   snprintf(r->error, sizeof(r->error), "%s", error ? error : "");
   pthread_cond_broadcast(&r->cond);
   pthread_mutex_unlock(&r->lock);
}

// The callbacks every test uses, with o's options (may be NULL).
static http_desc desc_for(result *r, const http_desc *o) {
   http_desc d = o ? *o : (http_desc){0};
   d.on_response = on_response;
   d.on_data = on_data;
   d.on_done = on_done;
   d.user_data = r;
   return d;
}

// Waits until done (or some body arrived, with want_data) or 10 s pass.
static bool wait_for(result *r, bool want_data) {
   struct timespec until;
   clock_gettime(CLOCK_REALTIME, &until);
   until.tv_sec += 10;
   pthread_mutex_lock(&r->lock);
   while (!r->dones && !(want_data && r->body_len) &&
          pthread_cond_timedwait(&r->cond, &r->lock, &until) == 0) {
   }
   bool ok = r->dones || (want_data && r->body_len);
   pthread_mutex_unlock(&r->lock);
   return ok;
}

static int port;

// o and req (both may be NULL) add options and the request's headers/body.
static http *send_req(const char *path, result *r, const http_desc *o,
                      const http_request *req) {
   char url[128];
   snprintf(url, sizeof(url), "http://127.0.0.1:%d%s", port, path);
   http_desc d = desc_for(r, o);
   http_request q = req ? *req : (http_request){0};
   q.url = url;
   http *h = http_start(&d, &q);
   CHECK(h != NULL);
   return h;
}

static http *get(const char *path, result *r, const http_desc *o) {
   return send_req(path, r, o, NULL);
}

static void test_simple(void) {
   result r;
   result_init(&r);
   http *h = get("/length", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && r.responses == 1 && r.dones == 1);
   CHECK(r.error[0] == '\0');
   CHECK(r.body && strcmp(r.body, "hello world") == 0);
   CHECK(strcmp(r.x_test, "yes") == 0);
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/chunked", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && r.responses == 1 && r.error[0] == '\0');
   CHECK(r.body && strcmp(r.body, "one two three") == 0);
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/close", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && r.error[0] == '\0');
   CHECK(r.body && strcmp(r.body, "until the end") == 0);
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/404", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 404 && r.error[0] == '\0');
   CHECK(r.body && strcmp(r.body, "nope") == 0);
   http_free(h);
   result_free(&r);
}

static void test_post_and_big(void) {
   result r;
   result_init(&r);
   const char *headers[] = {"X-Test: 1", "Content-Type: application/json",
                            NULL};
   const char *json = "{\"model\":\"m\",\"stream\":true}";
   http *h = send_req("/echo", &r, NULL,
                      &(http_request){
                          .headers = headers,
                          .body = json,
                          .body_len = strlen(json),
                      });
   CHECK(wait_for(&r, false));
   CHECK(r.status == 201 && r.error[0] == '\0');
   CHECK(r.body && strcmp(r.body, json) == 0);
   CHECK(strcmp(r.x_test, "yes") == 0);
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/big", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && r.error[0] == '\0');
   CHECK(r.body_len == BIG_SIZE);
   bool same = r.body_len == BIG_SIZE;
   for (size_t i = 0; same && i < BIG_SIZE; i++)
      same = r.body[i] == (char)('a' + i % 26);
   CHECK(same);
   http_free(h);
   result_free(&r);
}

static void test_sse_stream(void) {
   result r;
   result_init(&r);
   r.use_sse = true;
   http *h = get("/sse", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && r.error[0] == '\0');
   CHECK(strcmp(r.events, "message:{\"n\":1};message:{\"n\":2};"
                          "message:{\"n\":3};message:[DONE];") == 0);
   http_free(h);
   result_free(&r);
}

static void test_errors(void) {
   result r;
   result_init(&r);
   http *h = get("/truncated", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.status == 200 && strstr(r.error, "before the end") != NULL);
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/garbage", &r, NULL);
   CHECK(wait_for(&r, false));
   CHECK(r.responses == 0 && strstr(r.error, "not an HTTP response"));
   http_free(h);
   result_free(&r);

   result_init(&r);
   h = get("/slow", &r, &(http_desc){.idle_timeout_ms = 300});
   CHECK(wait_for(&r, false));
   CHECK(r.body && strcmp(r.body, "first") == 0);
   CHECK(strstr(r.error, "no answer") != NULL);
   http_free(h);
   result_free(&r);

   // A port nothing listens on.
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   struct sockaddr_in a = {0};
   a.sin_family = AF_INET;
   inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
   bind(fd, (struct sockaddr *)&a, sizeof(a));
   socklen_t alen = sizeof(a);
   getsockname(fd, (struct sockaddr *)&a, &alen);
   close(fd);
   char url[64];
   snprintf(url, sizeof(url), "http://127.0.0.1:%d/", ntohs(a.sin_port));
   result_init(&r);
   http_desc d = desc_for(&r, NULL);
   h = http_start(&d, &(http_request){.url = url});
   CHECK(wait_for(&r, false));
   CHECK(r.responses == 0 && r.error[0] != '\0');
   http_free(h);
   result_free(&r);

   CHECK(http_start(&d, &(http_request){.url = "ws://127.0.0.1/"}) == NULL);
   CHECK(http_start(&d, &(http_request){.url = "http://"}) == NULL);
   CHECK(http_start(&d, &(http_request){0}) == NULL);
   CHECK(http_start(&d, NULL) == NULL);
   CHECK(http_start(NULL, &(http_request){.url = "http://"}) == NULL);
}

static void test_free_mid_stream(void) {
   result r;
   result_init(&r);
   http *h = get("/forever", &r, NULL);
   CHECK(wait_for(&r, true));
   http_free(h);
   pthread_mutex_lock(&r.lock);
   size_t len = r.body_len;
   int dones = r.dones;
   pthread_mutex_unlock(&r.lock);
   sleep_ms(100);
   CHECK(r.body_len == len); // no callback after http_free returned
   CHECK(dones == 0 && r.dones == 0);
   result_free(&r);

   result_init(&r);
   r.free_on_data = true;
   get("/forever", &r, NULL);
   CHECK(wait_for(&r, true));
   sleep_ms(100);
   pthread_mutex_lock(&r.lock);
   CHECK(r.body_len == 5 && r.dones == 0);
   pthread_mutex_unlock(&r.lock);
   result_free(&r);
}

int main(void) {
   signal(SIGPIPE, SIG_IGN); // the server writing to freed requests
   port = start_server();
   test_simple();
   test_post_and_big();
   test_sse_stream();
   test_errors();
   test_free_mid_stream();
   sleep_ms(50); // let freed requests' threads finish, for leak checkers
   if (failures) {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("http_test: ok\n");
   return 0;
}
