#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/http_proto.h"
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

static void hex(const uint8_t *b, size_t n, char *out) {
   for (size_t i = 0; i < n; i++)
      snprintf(out + i * 2, 3, "%02x", b[i]);
}

static void test_url(void) {
   net_url u;
   CHECK(net_url_parse("wss://example.com/socket?vsn=2.0.0", &u) == 0);
   CHECK(u.secure && u.port == 443);
   CHECK(strcmp(u.host, "example.com") == 0);
   CHECK(strcmp(u.path, "/socket?vsn=2.0.0") == 0);

   CHECK(net_url_parse("WS://localhost:4000", &u) == 0);
   CHECK(!u.secure && u.port == 4000 && strcmp(u.path, "/") == 0);

   CHECK(net_url_parse("ws://host?x=1#frag", &u) == 0);
   CHECK(strcmp(u.path, "/?x=1") == 0);

   CHECK(net_url_parse("ws://[::1]:8080/a", &u) == 0);
   CHECK(u.ipv6 && strcmp(u.host, "::1") == 0 && u.port == 8080);

   char host[300];
   CHECK(net_url_host_header(&u, host, sizeof(host)) > 0);
   CHECK(strcmp(host, "[::1]:8080") == 0);
   CHECK(net_url_parse("wss://example.com:443/", &u) == 0);
   CHECK(net_url_host_header(&u, host, sizeof(host)) > 0);
   CHECK(strcmp(host, "example.com") == 0);

   CHECK(net_url_parse("https://api.openai.com/v1/chat/completions", &u) == 0);
   CHECK(u.scheme == NET_HTTP && u.secure && u.port == 443);
   CHECK(strcmp(u.path, "/v1/chat/completions") == 0);
   CHECK(net_url_parse("http://localhost:11434", &u) == 0);
   CHECK(u.scheme == NET_HTTP && !u.secure && u.port == 11434);
   CHECK(net_url_parse("HTTP://h/", &u) == 0 && u.port == 80);
   CHECK(net_url_parse("wss://h/", &u) == 0 && u.scheme == NET_WS);

   CHECK(net_url_parse("ftp://example.com", &u) == -1);
   CHECK(net_url_parse("https:/example.com", &u) == -1);
   CHECK(net_url_parse("ws://", &u) == -1);
   CHECK(net_url_parse("ws://host:0", &u) == -1);
   CHECK(net_url_parse("ws://host:65536", &u) == -1);
   CHECK(net_url_parse("ws://host:", &u) == -1);
   CHECK(net_url_parse("ws://user@host", &u) == -1);
   CHECK(net_url_parse("ws://[::1", &u) == -1);
   CHECK(net_url_parse("ws://host/a b", &u) == -1);
}

static void test_sha1_base64(void) {
   uint8_t d[20];
   char h[41], b[64];

   ws_sha1("abc", 3, d);
   hex(d, 20, h);
   CHECK(strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);

   ws_sha1("", 0, d);
   hex(d, 20, h);
   CHECK(strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);

   // 56 bytes: the padding needs a second block.
   const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
   ws_sha1(s, strlen(s), d);
   hex(d, 20, h);
   CHECK(strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);

   static uint8_t million[1000000];
   memset(million, 'a', sizeof(million));
   ws_sha1(million, sizeof(million), d);
   hex(d, 20, h);
   CHECK(strcmp(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0);

   CHECK(ws_base64_encode((const uint8_t *)"", 0, b) == 0 && !b[0]);
   ws_base64_encode((const uint8_t *)"f", 1, b);
   CHECK(strcmp(b, "Zg==") == 0);
   ws_base64_encode((const uint8_t *)"fo", 2, b);
   CHECK(strcmp(b, "Zm8=") == 0);
   ws_base64_encode((const uint8_t *)"foobar", 6, b);
   CHECK(strcmp(b, "Zm9vYmFy") == 0);
}

static void test_handshake(void) {
   // RFC 6455, section 1.3.
   char accept[29];
   ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", accept);
   CHECK(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

   uint8_t random[16];
   memcpy(random, "the sample nonce", 16);
   char key[25];
   ws_make_key(random, key);
   CHECK(strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);

   net_url u;
   net_url_parse("ws://example.com:8080/chat", &u);
   const char *headers[] = {"Authorization: Bearer x", NULL};
   const char *protocols[] = {"chat", "superchat", NULL};
   char req[1024];
   int n = ws_build_request(req, sizeof(req), &u, key, headers, protocols);
   CHECK(n > 0);
   CHECK(strstr(req, "GET /chat HTTP/1.1\r\n") == req);
   CHECK(strstr(req, "Host: example.com:8080\r\n") != NULL);
   CHECK(strstr(req, "Sec-WebSocket-Protocol: chat, superchat\r\n") != NULL);
   CHECK(strstr(req, "Authorization: Bearer x\r\n\r\n") != NULL);
   CHECK(ws_build_request(req, 20, &u, key, NULL, NULL) == -1);
   const char *evil[] = {"X: a\r\nY: b", NULL};
   CHECK(ws_build_request(req, sizeof(req), &u, key, evil, NULL) == -1);

   const char *ok = "HTTP/1.1 101 Switching Protocols\r\n"
                    "upgrade: WebSocket\r\n"
                    "Connection: keep-alive, Upgrade\r\n"
                    "Sec-WebSocket-Accept:  s3pPLMBiTxaQ9kYGzzhZRbK+xOo= \r\n"
                    "\r\n\x81\x00";
   size_t used = 0;
   char err[128];
   CHECK(ws_parse_response(ok, strlen(ok) + 2, key, &used, err, sizeof(err)) ==
         1);
   CHECK(used == strlen(ok) - 1); // the frame byte after the headers
   CHECK(ws_parse_response(ok, 40, key, &used, err, sizeof(err)) == 0);

   const char *refused = "HTTP/1.1 403 Forbidden\r\n\r\n";
   CHECK(ws_parse_response(refused, strlen(refused), key, &used, err,
                           sizeof(err)) == -1);
   CHECK(strstr(err, "403") != NULL);

   const char *bad_accept =
       "HTTP/1.1 101 OK\r\nUpgrade: websocket\r\n"
       "Connection: Upgrade\r\n"
       "Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n"
       "\r\n";
   CHECK(ws_parse_response(bad_accept, strlen(bad_accept), key, &used, err,
                           sizeof(err)) == -1);
}

static void test_utf8(void) {
   CHECK(ws_utf8_valid((const uint8_t *)"hello", 5));
   CHECK(ws_utf8_valid((const uint8_t *)"\xc3\xa9t\xc3\xa9", 6));
   CHECK(ws_utf8_valid((const uint8_t *)"\xf0\x9f\x98\x80", 4));
   CHECK(!ws_utf8_valid((const uint8_t *)"\xc3", 1));         // truncated
   CHECK(!ws_utf8_valid((const uint8_t *)"\xc0\xaf", 2));     // overlong
   CHECK(!ws_utf8_valid((const uint8_t *)"\xed\xa0\x80", 3)); // surrogate
   CHECK(!ws_utf8_valid((const uint8_t *)"\xf4\x90\x80\x80", 4));
   CHECK(!ws_utf8_valid((const uint8_t *)"\xff", 1));
}

// Frames a server would send (unmasked) and a client would send (masked)
// round-trip through the reader, a byte at a time.
static void test_frames(void) {
   static const uint8_t mask[4] = {1, 2, 3, 4};
   size_t sizes[] = {0, 5, 125, 126, 65535, 65536, 200000};
   for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
      for (int server = 0; server < 2; server++) {
         size_t len = sizes[s];
         uint8_t *payload = malloc(len + 1);
         uint8_t *frame = malloc(len + WS_FRAME_HEADER_MAX);
         for (size_t i = 0; i < len; i++)
            payload[i] = (uint8_t)('a' + i % 26);
         size_t n = ws_frame_encode(frame, WS_OP_TEXT, payload, len,
                                    server ? mask : NULL);
         size_t expect_hdr = len < 126 ? 2 : len <= 65535 ? 4 : 10;
         CHECK(n == len + expect_hdr + (server ? 4 : 0));

         ws_reader r;
         ws_reader_init(&r, server, 1 << 20);
         ws_event ev = {0};
         size_t step = len > 1000 ? 4093 : 1;
         for (size_t i = 0; i < n; i += step) {
            size_t chunk = n - i < step ? n - i : step;
            ws_reader_feed(&r, frame + i, chunk);
            ev = ws_reader_next(&r);
            if (i + chunk < n)
               CHECK(ev.type == WS_EV_NONE);
         }
         CHECK(ev.type == WS_EV_TEXT);
         CHECK(ev.len == len && memcmp(ev.data, payload, len) == 0);
         CHECK(ws_reader_next(&r).type == WS_EV_NONE);
         ws_reader_free(&r);
         free(payload);
         free(frame);
      }
   }
}

static void test_fragments_and_control(void) {
   // "Hel" + ping + "lo", then a close, all in one read.
   uint8_t in[] = {0x01, 3,   'H', 'e',  'l', 0x89, 2,    'h', 'i', 0x80,
                   2,    'l', 'o', 0x88, 5,   0x03, 0xE8, 'b', 'y', 'e'};
   ws_reader r;
   ws_reader_init(&r, false, 1024);
   ws_reader_feed(&r, in, sizeof(in));
   ws_event ev = ws_reader_next(&r);
   CHECK(ev.type == WS_EV_PING && ev.len == 2 && memcmp(ev.data, "hi", 2) == 0);
   ev = ws_reader_next(&r);
   CHECK(ev.type == WS_EV_TEXT && ev.len == 5 &&
         memcmp(ev.data, "Hello", 5) == 0);
   ev = ws_reader_next(&r);
   CHECK(ev.type == WS_EV_CLOSE && ev.code == 1000 &&
         strcmp(ev.reason, "bye") == 0);
   ws_reader_free(&r);

   struct {
      uint8_t bytes[8];
      size_t len;
      int code;
   } bad[] = {
       {{0xC1, 0}, 2, 1002},                // reserved bit
       {{0x83, 0}, 2, 1002},                // unknown opcode
       {{0x80, 0}, 2, 1002},                // continuation with no message
       {{0x09, 0}, 2, 1002},                // fragmented ping
       {{0x89, 126, 0, 126}, 4, 1002},      // ping over 125 bytes
       {{0x81, 0x80, 0, 0, 0, 0}, 6, 1002}, // masked from server
       {{0x81, 2, 0xc3, 0x28}, 4, 1007},    // bad UTF-8
       {{0x88, 1, 3}, 3, 1002},             // one-byte close
       {{0x88, 2, 0x03, 0xED}, 4, 1002},    // close code 1005 on the wire
       {{0x82, 126, 0x10, 0x00}, 4, 1009},  // 4096 > limit
   };
   for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      ws_reader_init(&r, false, 1024);
      ws_reader_feed(&r, bad[i].bytes, bad[i].len);
      ev = ws_reader_next(&r);
      if (ev.type != WS_EV_ERROR || ev.code != bad[i].code) {
         fprintf(stderr, "bad frame %zu: type %d code %d\n", i, ev.type,
                 ev.code);
         failures++;
      }
      ws_reader_free(&r);
   }

   uint8_t close[125];
   char long_reason[200];
   memset(long_reason, 'x', sizeof(long_reason));
   long_reason[121] = (char)0xc3; // é straddling the 123-byte limit
   long_reason[122] = (char)0xa9;
   long_reason[123] = (char)0xc3;
   long_reason[124] = (char)0xa9;
   long_reason[199] = '\0';
   size_t n = ws_close_payload(close, 1001, long_reason);
   CHECK(n <= 125 && close[0] == 0x03 && close[1] == 0xE9);
   CHECK(ws_utf8_valid(close + 2, n - 2));
}

/* HTTP */

static void test_http_request(void) {
   net_url u;
   net_url_parse("https://api.example.com/v1/chat?x=1", &u);
   const char *headers[] = {"Authorization: Bearer k",
                            "Content-Type: application/json", NULL};
   char req[1024];
   int n = http_build_request(req, sizeof(req), "POST", &u, headers, true, 42);
   CHECK(n > 0);
   CHECK(strstr(req, "POST /v1/chat?x=1 HTTP/1.1\r\n") == req);
   CHECK(strstr(req, "\r\nHost: api.example.com\r\n") != NULL);
   CHECK(strstr(req, "\r\nAuthorization: Bearer k\r\n") != NULL);
   CHECK(strstr(req, "\r\nUser-Agent: net/1\r\n") != NULL);
   CHECK(strstr(req, "\r\nContent-Length: 42\r\n") != NULL);
   CHECK(n > 4 && strcmp(req + n - 4, "\r\n\r\n") == 0);

   const char *ua[] = {"user-agent: mine", NULL};
   n = http_build_request(req, sizeof(req), "GET", &u, ua, false, 0);
   CHECK(n > 0 && strstr(req, "net/1") == NULL);
   CHECK(strstr(req, "Content-Length") == NULL);

   const char *bad[] = {"X: a\r\nY: b", NULL};
   CHECK(http_build_request(req, sizeof(req), "GET", &u, bad, false, 0) < 0);
   CHECK(http_build_request(req, 20, "GET", &u, NULL, false, 0) < 0);
}

static void test_http_head(void) {
   char err[128];
   http_head h;
   size_t used = 0;
   const char *r = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/event-stream\r\n"
                   "Transfer-Encoding: Chunked\r\n"
                   "\r\n"
                   "5\r\nhello";
   size_t len = strlen(r);
   for (size_t i = 0; i < len - 10; i++)
      CHECK(http_parse_head(r, i, false, &h, &used, err, sizeof(err)) == 0);
   CHECK(http_parse_head(r, len, false, &h, &used, err, sizeof(err)) == 1);
   CHECK(h.status == 200 && h.body == HTTP_BODY_CHUNKED);
   CHECK(strcmp(r + used, "5\r\nhello") == 0);
   size_t vlen;
   const char *v =
       http_header_find(h.headers, h.headers_len, "content-type", &vlen);
   CHECK(v && vlen == 17 && memcmp(v, "text/event-stream", 17) == 0);
   CHECK(!http_header_find(h.headers, h.headers_len, "content", &vlen));

   r = "HTTP/1.1 404 Not Found\r\nContent-Length:  12 \r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         1);
   CHECK(h.status == 404 && h.body == HTTP_BODY_LENGTH && h.length == 12);
   CHECK(used == strlen(r));
   CHECK(http_parse_head(r, strlen(r), true, &h, &used, err, sizeof(err)) == 1);
   CHECK(h.body == HTTP_BODY_NONE);

   r = "HTTP/1.0 200 OK\r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         1);
   CHECK(h.body == HTTP_BODY_CLOSE && h.headers_len == 0);
   r = "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         1);
   CHECK(h.body == HTTP_BODY_NONE);

   r = "SSH-2.0-OpenSSH\r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         -1);
   r = "HTTP/1.1 200 OK\r\nContent-Length: 1x\r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         -1);
   r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n";
   CHECK(http_parse_head(r, strlen(r), false, &h, &used, err, sizeof(err)) ==
         -1);
}

// Decodes body with b, feeding step bytes at a time; returns the body.
static size_t decode_all(http_body *b, const char *body, size_t len,
                         size_t step, char *out, bool *bad) {
   size_t n = 0;
   *bad = false;
   for (size_t i = 0; i < len && !b->done;) {
      size_t k = len - i < step ? len - i : step;
      const char *in = body + i;
      size_t left = k;
      while (left && !b->done) {
         const char *data;
         size_t data_len;
         long used = http_body_decode(b, in, left, &data, &data_len);
         if (used < 0) {
            *bad = true;
            return n;
         }
         memcpy(out + n, data, data_len);
         n += data_len;
         in += used;
         left -= (size_t)used;
      }
      i += k;
   }
   return n;
}

static void test_http_body(void) {
   const char *chunked = "5;ext=1\r\nhello\r\n"
                         "1A\r\n abcdefghijklmnopqrstuvwxy\r\n"
                         "0\r\nTrailer: x\r\n\r\n";
   http_head h = {.body = HTTP_BODY_CHUNKED};
   char out[256];
   bool bad;
   for (size_t step = 1; step <= strlen(chunked); step++) {
      http_body b;
      http_body_init(&b, &h);
      size_t n = decode_all(&b, chunked, strlen(chunked), step, out, &bad);
      CHECK(!bad && b.done);
      CHECK(n == 31 && memcmp(out, "hello abcdefghijklmnopqrstuvwxy", 31) == 0);
   }
   http_body b;
   http_body_init(&b, &h);
   decode_all(&b, "zz\r\n", 4, 4, out, &bad);
   CHECK(bad);
   http_body_init(&b, &h);
   decode_all(&b, "2\r\nabX", 6, 6, out, &bad);
   CHECK(bad);

   http_head len_head = {.body = HTTP_BODY_LENGTH, .length = 4};
   http_body_init(&b, &len_head);
   size_t n = decode_all(&b, "abcdEXTRA", 9, 3, out, &bad);
   CHECK(!bad && b.done && n == 4 && memcmp(out, "abcd", 4) == 0);
}

/* SSE */

typedef struct {
   char log[512];
   size_t len;
} sse_log;

static void on_sse(sse_parser *p, const char *event, const char *data, size_t len,
                   void *user) {
   (void)p;
   sse_log *l = user;
   CHECK(data[len] == '\0');
   l->len += (size_t)snprintf(l->log + l->len, sizeof(l->log) - l->len,
                              "[%s|%s]", event, data);
}

static void test_sse(void) {
   const char *stream = ": comment\r\n"
                        "data: {\"a\":1}\r\n\r\n"
                        "event: update\n"
                        "data:line1\n"
                        "data: line2\n"
                        "id: 7\n"
                        "\n"
                        "data\r\r"
                        "event: empty\n\n"
                        "data: [DONE]\n\n";
   size_t len = strlen(stream);
   const char *s = stream;
   for (size_t step = 1; step <= len; step++) {
      sse_parser p;
      sse_log l = {0};
      sse_init(&p, &(sse_desc){.on_event = on_sse, .user_data = &l});
      for (size_t i = 0; i < len; i += step) {
         size_t k = len - i < step ? len - i : step;
         CHECK(sse_feed(&p, s + i, k));
      }
      CHECK(strcmp(l.log, "[message|{\"a\":1}][update|line1\nline2]"
                          "[message|][message|[DONE]]") == 0);
      sse_free(&p);
   }

   sse_parser p;
   sse_log l = {0};
   sse_init(&p, &(sse_desc){.on_event = on_sse, .user_data = &l, .max = 16});
   CHECK(!sse_feed(&p, "data: 0123456789abcdef\n", 23));
   sse_free(&p);
}

int main(void) {
   test_url();
   test_sha1_base64();
   test_handshake();
   test_utf8();
   test_frames();
   test_fragments_and_control();
   test_http_request();
   test_http_head();
   test_http_body();
   test_sse();
   if (failures) {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("net_proto_test: ok\n");
   return 0;
}
