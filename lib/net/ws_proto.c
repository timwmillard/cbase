#include "ws_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SHA-1 and base64, for the handshake keys */

static uint32_t rol32(uint32_t x, int n) {
   return (x << n) | (x >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t *b) {
   uint32_t w[80];
   for (int i = 0; i < 16; i++) {
      w[i] = (uint32_t)b[i * 4] << 24 | (uint32_t)b[i * 4 + 1] << 16 |
             (uint32_t)b[i * 4 + 2] << 8 | (uint32_t)b[i * 4 + 3];
   }
   for (int i = 16; i < 80; i++)
      w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

   uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
   for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
         f = (bb & c) | (~bb & d);
         k = 0x5A827999;
      } else if (i < 40) {
         f = bb ^ c ^ d;
         k = 0x6ED9EBA1;
      } else if (i < 60) {
         f = (bb & c) | (bb & d) | (c & d);
         k = 0x8F1BBCDC;
      } else {
         f = bb ^ c ^ d;
         k = 0xCA62C1D6;
      }
      uint32_t t = rol32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol32(bb, 30);
      bb = a;
      a = t;
   }
   h[0] += a;
   h[1] += bb;
   h[2] += c;
   h[3] += d;
   h[4] += e;
}

void ws_sha1(const void *data, size_t len, uint8_t out[20]) {
   uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                    0xC3D2E1F0};
   const uint8_t *p = data;
   size_t left = len;
   for (; left >= 64; left -= 64, p += 64)
      sha1_block(h, p);

   uint8_t tail[128] = {0};
   memcpy(tail, p, left);
   tail[left] = 0x80;
   size_t tail_len = left < 56 ? 64 : 128;
   uint64_t bits = (uint64_t)len * 8;
   for (int i = 0; i < 8; i++)
      tail[tail_len - 1 - i] = (uint8_t)(bits >> (i * 8));
   sha1_block(h, tail);
   if (tail_len == 128)
      sha1_block(h, tail + 64);

   for (int i = 0; i < 5; i++) {
      out[i * 4] = (uint8_t)(h[i] >> 24);
      out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
      out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
      out[i * 4 + 3] = (uint8_t)h[i];
   }
}

size_t ws_base64_encode(const uint8_t *in, size_t len, char *out) {
   static const char alphabet[] =
       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t n = 0;
   for (size_t i = 0; i < len; i += 3) {
      uint32_t v = (uint32_t)in[i] << 16;
      if (i + 1 < len)
         v |= (uint32_t)in[i + 1] << 8;
      if (i + 2 < len)
         v |= in[i + 2];
      out[n++] = alphabet[(v >> 18) & 63];
      out[n++] = alphabet[(v >> 12) & 63];
      out[n++] = i + 1 < len ? alphabet[(v >> 6) & 63] : '=';
      out[n++] = i + 2 < len ? alphabet[v & 63] : '=';
   }
   out[n] = '\0';
   return n;
}

void ws_make_key(const uint8_t random[16], char key[25]) {
   ws_base64_encode(random, 16, key);
}

void ws_accept_key(const char *key, char accept[29]) {
   static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
   char buf[128];
   int n = snprintf(buf, sizeof(buf), "%s%s", key, guid);
   uint8_t digest[20];
   ws_sha1(buf, n > 0 ? (size_t)n : 0, digest);
   ws_base64_encode(digest, sizeof(digest), accept);
}

/* Handshake */

static bool has_line_break(const char *s) {
   return strpbrk(s, "\r\n") != NULL;
}

int ws_build_request(char *out, size_t cap, const net_url *u,
                     const char *key, const char *const *headers,
                     const char *const *protocols) {
   char host[300];
   if (net_url_host_header(u, host, sizeof(host)) < 0)
      return -1;

   size_t n = 0;
#define APPEND(...)                                                            \
   do {                                                                        \
      int m_ = snprintf(out + n, cap - n, __VA_ARGS__);                        \
      if (m_ < 0 || (size_t)m_ >= cap - n)                                     \
         return -1;                                                            \
      n += (size_t)m_;                                                         \
   } while (0)

   APPEND("GET %s HTTP/1.1\r\n"
          "Host: %s\r\n"
          "Upgrade: websocket\r\n"
          "Connection: Upgrade\r\n"
          "Sec-WebSocket-Key: %s\r\n"
          "Sec-WebSocket-Version: 13\r\n",
          u->path, host, key);
   if (protocols && protocols[0]) {
      APPEND("Sec-WebSocket-Protocol: ");
      for (int i = 0; protocols[i]; i++) {
         if (has_line_break(protocols[i]))
            return -1;
         APPEND("%s%s", i ? ", " : "", protocols[i]);
      }
      APPEND("\r\n");
   }
   for (int i = 0; headers && headers[i]; i++) {
      if (has_line_break(headers[i]))
         return -1;
      APPEND("%s\r\n", headers[i]);
   }
   APPEND("\r\n");
#undef APPEND
   return (int)n;
}

// Case-insensitive search for a token in a header value.
static bool ws_value_has(const char *v, size_t len, const char *token) {
   size_t t = strlen(token);
   for (size_t i = 0; i + t <= len; i++) {
      size_t j = 0;
      while (j < t && tolower((unsigned char)v[i + j]) == token[j])
         j++;
      if (j == t)
         return true;
   }
   return false;
}

int ws_parse_response(const char *buf, size_t len, const char *key,
                      size_t *consumed, char *err, size_t err_cap) {
   const char *end = NULL;
   for (size_t i = 0; i + 4 <= len; i++) {
      if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
         end = buf + i;
         break;
      }
   }
   if (!end) {
      if (len > 16384) {
         snprintf(err, err_cap, "handshake response too large");
         return -1;
      }
      return 0;
   }
   *consumed = (size_t)(end - buf) + 4;

   const char *line_end = memchr(buf, '\r', (size_t)(end - buf) + 1);
   int status = 0;
   if (line_end - buf < 12 || memcmp(buf, "HTTP/1.", 7) != 0 ||
       sscanf(buf + 9, "%3d", &status) != 1) {
      snprintf(err, err_cap, "not an HTTP response");
      return -1;
   }
   if (status != 101) {
      snprintf(err, err_cap, "server refused the upgrade: %.*s",
               (int)(line_end - buf > 80 ? 80 : line_end - buf), buf);
      return -1;
   }

   char expected[29];
   ws_accept_key(key, expected);
   bool upgrade = false, connection = false, accept = false;
   for (const char *line = line_end + 2; line < end;) {
      const char *eol = memchr(line, '\r', (size_t)(end - line) + 1);
      const char *colon = memchr(line, ':', (size_t)(eol - line));
      if (colon) {
         size_t name_len = (size_t)(colon - line);
         const char *v = colon + 1;
         while (v < eol && (*v == ' ' || *v == '\t'))
            v++;
         size_t v_len = (size_t)(eol - v);
         while (v_len && (v[v_len - 1] == ' ' || v[v_len - 1] == '\t'))
            v_len--;
         if (name_len == 7 && ws_value_has(line, 7, "upgrade"))
            upgrade = ws_value_has(v, v_len, "websocket");
         else if (name_len == 10 && ws_value_has(line, 10, "connection"))
            connection = ws_value_has(v, v_len, "upgrade");
         else if (name_len == 20 &&
                  ws_value_has(line, 20, "sec-websocket-accept"))
            accept = v_len == 28 && memcmp(v, expected, 28) == 0;
      }
      line = eol + 2;
   }
   if (!upgrade || !connection) {
      snprintf(err, err_cap, "server did not upgrade to websocket");
      return -1;
   }
   if (!accept) {
      snprintf(err, err_cap, "bad Sec-WebSocket-Accept");
      return -1;
   }
   return 1;
}

/* Frames */

size_t ws_frame_encode(uint8_t *out, int opcode, const void *payload,
                       size_t len, const uint8_t mask[4]) {
   size_t n = 0;
   out[n++] = (uint8_t)(0x80 | opcode);
   uint8_t mask_bit = mask ? 0x80 : 0;
   if (len < 126) {
      out[n++] = (uint8_t)(mask_bit | len);
   } else if (len <= 0xFFFF) {
      out[n++] = mask_bit | 126;
      out[n++] = (uint8_t)(len >> 8);
      out[n++] = (uint8_t)len;
   } else {
      out[n++] = mask_bit | 127;
      for (int i = 7; i >= 0; i--)
         out[n++] = (uint8_t)((uint64_t)len >> (i * 8));
   }
   if (mask) {
      memcpy(out + n, mask, 4);
      n += 4;
   }
   const uint8_t *src = payload;
   for (size_t i = 0; i < len; i++)
      out[n + i] = mask ? (uint8_t)(src[i] ^ mask[i & 3]) : src[i];
   return n + len;
}

size_t ws_close_payload(uint8_t out[125], int code, const char *reason) {
   out[0] = (uint8_t)(code >> 8);
   out[1] = (uint8_t)code;
   size_t n = reason ? strlen(reason) : 0;
   if (n > 123) {
      // Cut on a character boundary so the reason stays valid UTF-8.
      n = 123;
      while (n > 0 && ((uint8_t)reason[n] & 0xC0) == 0x80)
         n--;
   }
   if (n)
      memcpy(out + 2, reason, n);
   return 2 + n;
}

bool ws_utf8_valid(const uint8_t *s, size_t len) {
   size_t i = 0;
   while (i < len) {
      uint8_t c = s[i];
      size_t need;
      uint32_t cp;
      if (c < 0x80) {
         i++;
         continue;
      } else if ((c & 0xE0) == 0xC0) {
         need = 1;
         cp = c & 0x1F;
      } else if ((c & 0xF0) == 0xE0) {
         need = 2;
         cp = c & 0x0F;
      } else if ((c & 0xF8) == 0xF0) {
         need = 3;
         cp = c & 0x07;
      } else {
         return false;
      }
      if (len - i <= need)
         return false;
      for (size_t k = 1; k <= need; k++) {
         if ((s[i + k] & 0xC0) != 0x80)
            return false;
         cp = cp << 6 | (s[i + k] & 0x3F);
      }
      static const uint32_t min[4] = {0, 0x80, 0x800, 0x10000};
      if (cp < min[need] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
         return false;
      i += need + 1;
   }
   return true;
}

void ws_reader_init(ws_reader *r, bool server, size_t max_message) {
   memset(r, 0, sizeof(*r));
   r->server = server;
   r->max_message = max_message;
}

void ws_reader_free(ws_reader *r) {
   free(r->in);
   free(r->msg);
   memset(r, 0, sizeof(*r));
}

static bool grow(uint8_t **buf, size_t *cap, size_t need) {
   if (need <= *cap)
      return true;
   size_t n = *cap ? *cap : 4096;
   while (n < need)
      n *= 2;
   uint8_t *p = realloc(*buf, n);
   if (!p)
      return false;
   *buf = p;
   *cap = n;
   return true;
}

bool ws_reader_feed(ws_reader *r, const void *data, size_t len) {
   if (r->in_pos) {
      memmove(r->in, r->in + r->in_pos, r->in_len - r->in_pos);
      r->in_len -= r->in_pos;
      r->in_pos = 0;
   }
   if (!grow(&r->in, &r->in_cap, r->in_len + len))
      return false;
   memcpy(r->in + r->in_len, data, len);
   r->in_len += len;
   return true;
}

static ws_event reader_error(int code, const char *reason) {
   return (ws_event){.type = WS_EV_ERROR, .code = code, .reason = reason};
}

static bool close_code_valid(int code) {
   if (code >= 3000 && code <= 4999)
      return true;
   return code >= 1000 && code <= 1014 && code != 1004 && code != 1005 &&
          code != 1006;
}

ws_event ws_reader_next(ws_reader *r) {
   if (r->msg_done) {
      r->msg_len = 0;
      r->msg_done = false;
   }
   for (;;) {
      const uint8_t *p = r->in + r->in_pos;
      size_t avail = r->in_len - r->in_pos;
      if (avail < 2)
         return (ws_event){0};

      bool fin = p[0] & 0x80;
      int op = p[0] & 0x0F;
      bool masked = p[1] & 0x80;
      uint64_t len = p[1] & 0x7F;
      size_t hdr = 2;
      if (p[0] & 0x70)
         return reader_error(1002, "reserved bits set");
      if (masked != r->server)
         return reader_error(1002, masked ? "masked frame from server"
                                          : "unmasked frame from client");
      if (op != WS_OP_CONT && op != WS_OP_TEXT && op != WS_OP_BINARY &&
          op != WS_OP_CLOSE && op != WS_OP_PING && op != WS_OP_PONG)
         return reader_error(1002, "unknown opcode");
      bool control = op & 0x8;
      if (control && (!fin || len > 125))
         return reader_error(1002, "bad control frame");
      if (op == WS_OP_CONT && !r->msg_op)
         return reader_error(1002, "continuation without a message");
      if ((op == WS_OP_TEXT || op == WS_OP_BINARY) && r->msg_op)
         return reader_error(1002, "new message inside a fragmented one");

      if (len == 126) {
         if (avail < 4)
            return (ws_event){0};
         len = (uint64_t)p[2] << 8 | p[3];
         hdr = 4;
      } else if (len == 127) {
         if (avail < 10)
            return (ws_event){0};
         len = 0;
         for (int i = 0; i < 8; i++)
            len = len << 8 | p[2 + i];
         if (len >> 63)
            return reader_error(1002, "bad frame length");
         hdr = 10;
      }
      if (!control && len + r->msg_len > r->max_message)
         return reader_error(1009, "message too big");
      uint8_t mask[4] = {0};
      if (masked) {
         if (avail < hdr + 4)
            return (ws_event){0};
         memcpy(mask, p + hdr, 4);
         hdr += 4;
      }
      if (avail < hdr + len)
         return (ws_event){0};

      uint8_t *payload = r->in + r->in_pos + hdr;
      if (masked) {
         for (size_t i = 0; i < len; i++)
            payload[i] ^= mask[i & 3];
      }
      r->in_pos += hdr + (size_t)len;

      ws_event ev = {.data = payload, .len = (size_t)len};
      switch (op) {
      case WS_OP_PING:
         ev.type = WS_EV_PING;
         return ev;
      case WS_OP_PONG:
         ev.type = WS_EV_PONG;
         return ev;
      case WS_OP_CLOSE:
         ev.type = WS_EV_CLOSE;
         ev.code = 1005;
         r->reason[0] = '\0';
         if (len == 1)
            return reader_error(1002, "bad close frame");
         if (len >= 2) {
            ev.code = payload[0] << 8 | payload[1];
            if (!close_code_valid(ev.code))
               return reader_error(1002, "bad close code");
            if (!ws_utf8_valid(payload + 2, (size_t)len - 2))
               return reader_error(1007, "close reason is not UTF-8");
            memcpy(r->reason, payload + 2, (size_t)len - 2);
            r->reason[len - 2] = '\0';
         }
         ev.reason = r->reason;
         return ev;
      }

      if (fin && op != WS_OP_CONT) {
         // Not fragmented: hand out the payload where it lies.
         ev.type = op == WS_OP_TEXT ? WS_EV_TEXT : WS_EV_BINARY;
      } else {
         if (op != WS_OP_CONT)
            r->msg_op = op;
         if (!grow(&r->msg, &r->msg_cap, r->msg_len + (size_t)len + 1))
            return reader_error(1011, "out of memory");
         memcpy(r->msg + r->msg_len, payload, (size_t)len);
         r->msg_len += (size_t)len;
         if (!fin)
            continue;
         ev.type = r->msg_op == WS_OP_TEXT ? WS_EV_TEXT : WS_EV_BINARY;
         ev.data = r->msg;
         ev.len = r->msg_len;
         r->msg_op = 0;
         r->msg_done = true;
      }
      if (ev.type == WS_EV_TEXT && !ws_utf8_valid(ev.data, ev.len))
         return reader_error(1007, "text is not UTF-8");
      return ev;
   }
}
