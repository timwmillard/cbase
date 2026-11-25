#include "http_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool name_is(const char *s, size_t len, const char *name) {
   size_t n = strlen(name);
   if (len != n)
      return false;
   for (size_t i = 0; i < n; i++) {
      if (tolower((unsigned char)s[i]) != tolower((unsigned char)name[i]))
         return false;
   }
   return true;
}

static bool header_given(const char *const *headers, const char *name) {
   for (int i = 0; headers && headers[i]; i++) {
      const char *colon = strchr(headers[i], ':');
      if (colon && name_is(headers[i], (size_t)(colon - headers[i]), name))
         return true;
   }
   return false;
}

int http_build_request(char *out, size_t cap, const char *method,
                       const net_url *u, const char *const *headers,
                       bool has_body, size_t body_len) {
   char host[300];
   if (net_url_host_header(u, host, sizeof(host)) < 0 ||
       strpbrk(method, " \r\n"))
      return -1;

   size_t n = 0;
#define APPEND(...)                                                            \
   do {                                                                        \
      int m_ = snprintf(out + n, cap - n, __VA_ARGS__);                        \
      if (m_ < 0 || (size_t)m_ >= cap - n)                                     \
         return -1;                                                            \
      n += (size_t)m_;                                                         \
   } while (0)

   APPEND("%s %s HTTP/1.1\r\nHost: %s\r\n", method, u->path, host);
   for (int i = 0; headers && headers[i]; i++) {
      if (strpbrk(headers[i], "\r\n"))
         return -1;
      APPEND("%s\r\n", headers[i]);
   }
   if (!header_given(headers, "user-agent"))
      APPEND("User-Agent: net/1\r\n");
   if (has_body)
      APPEND("Content-Length: %zu\r\n", body_len);
   APPEND("Connection: close\r\n\r\n");
#undef APPEND
   return (int)n;
}

const char *http_header_find(const char *headers, size_t headers_len,
                             const char *name, size_t *len) {
   const char *end = headers + headers_len;
   for (const char *line = headers; line < end;) {
      const char *eol = memchr(line, '\n', (size_t)(end - line));
      if (!eol)
         eol = end;
      const char *colon = memchr(line, ':', (size_t)(eol - line));
      if (colon && name_is(line, (size_t)(colon - line), name)) {
         const char *v = colon + 1, *v_end = eol;
         while (v < v_end && (*v == ' ' || *v == '\t'))
            v++;
         while (v_end > v && (v_end[-1] == '\r' || v_end[-1] == ' ' ||
                              v_end[-1] == '\t'))
            v_end--;
         *len = (size_t)(v_end - v);
         return v;
      }
      line = eol + 1;
   }
   return NULL;
}

// Case-insensitive search for a token in a header value.
static bool http_value_has(const char *v, size_t len, const char *token) {
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

int http_parse_head(const char *buf, size_t len, bool head_request,
                    http_head *h, size_t *consumed, char *err,
                    size_t err_cap) {
   memset(h, 0, sizeof(*h));
   const char *end = NULL;
   for (size_t i = 0; i + 4 <= len; i++) {
      if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
         end = buf + i + 2; // keep the last header's CRLF
         break;
      }
   }
   if (!end) {
      if (len >= HTTP_HEAD_MAX) {
         snprintf(err, err_cap, "response headers too large");
         return -1;
      }
      return 0;
   }
   *consumed = (size_t)(end - buf) + 2;

   const char *line_end = memchr(buf, '\r', (size_t)(end - buf));
   if (line_end - buf < 12 || memcmp(buf, "HTTP/1.", 7) != 0 ||
       buf[8] != ' ' || !isdigit((unsigned char)buf[9]) ||
       !isdigit((unsigned char)buf[10]) || !isdigit((unsigned char)buf[11])) {
      snprintf(err, err_cap, "not an HTTP response");
      return -1;
   }
   h->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
   h->headers = line_end + 2;
   h->headers_len = (size_t)(end - h->headers);

   size_t te_len, vlen;
   const char *te = http_header_find(h->headers, h->headers_len,
                                     "transfer-encoding", &te_len);
   const char *cl =
       http_header_find(h->headers, h->headers_len, "content-length", &vlen);
   if (head_request || h->status < 200 || h->status == 204 ||
       h->status == 304) {
      h->body = HTTP_BODY_NONE;
   } else if (te) {
      if (!http_value_has(te, te_len, "chunked")) {
         snprintf(err, err_cap, "unsupported transfer encoding");
         return -1;
      }
      h->body = HTTP_BODY_CHUNKED;
   } else if (cl) {
      uint64_t n = 0;
      if (vlen == 0)
         goto bad_length;
      for (size_t i = 0; i < vlen; i++) {
         if (!isdigit((unsigned char)cl[i]) || n > (UINT64_MAX - 9) / 10)
            goto bad_length;
         n = n * 10 + (uint64_t)(cl[i] - '0');
      }
      h->body = n ? HTTP_BODY_LENGTH : HTTP_BODY_NONE;
      h->length = n;
   } else {
      h->body = HTTP_BODY_CLOSE;
   }
   return 1;

bad_length:
   snprintf(err, err_cap, "bad Content-Length");
   return -1;
}

/* Body */

enum { CHUNK_SIZE, CHUNK_DATA, CHUNK_DATA_END, CHUNK_TRAILER };

void http_body_init(http_body *b, const http_head *h) {
   memset(b, 0, sizeof(*b));
   b->kind = h->body;
   b->left = h->length;
   b->done = h->body == HTTP_BODY_NONE;
}

static int hex_value(char c) {
   if (c >= '0' && c <= '9')
      return c - '0';
   c = (char)tolower((unsigned char)c);
   return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// The size at the start of a chunk-size line; extensions after it are
// ignored. -1 if there is none or it is absurd.
static int64_t chunk_size(const char *line, size_t len) {
   int64_t n = 0;
   size_t i = 0;
   for (; i < len && hex_value(line[i]) >= 0; i++) {
      if (n > (INT64_MAX >> 4))
         return -1;
      n = n * 16 + hex_value(line[i]);
   }
   return i ? n : -1;
}

long http_body_decode(http_body *b, const char *in, size_t len,
                      const char **data, size_t *data_len) {
   *data = in;
   *data_len = 0;
   switch (b->kind) {
   case HTTP_BODY_NONE:
      b->done = true;
      return 0;
   case HTTP_BODY_CLOSE:
      *data_len = len;
      return (long)len;
   case HTTP_BODY_LENGTH: {
      size_t k = b->left < len ? (size_t)b->left : len;
      *data_len = k;
      b->left -= k;
      b->done = b->left == 0;
      return (long)k;
   }
   case HTTP_BODY_CHUNKED:
      break;
   }

   size_t i = 0;
   while (i < len && !b->done) {
      char c = in[i];
      switch (b->state) {
      case CHUNK_SIZE:
         i++;
         if (c == '\n') {
            int64_t n = chunk_size(b->line, b->line_len);
            if (n < 0)
               return -1;
            b->line_len = 0;
            b->left = (uint64_t)n;
            b->state = n ? CHUNK_DATA : CHUNK_TRAILER;
         } else if (b->line_len < sizeof(b->line)) {
            b->line[b->line_len++] = c;
         }
         break;
      case CHUNK_DATA: {
         size_t k = b->left < len - i ? (size_t)b->left : len - i;
         *data = in + i;
         *data_len = k;
         b->left -= k;
         if (b->left == 0)
            b->state = CHUNK_DATA_END;
         return (long)(i + k);
      }
      case CHUNK_DATA_END:
         i++;
         if (c == '\n')
            b->state = CHUNK_SIZE;
         else if (c != '\r')
            return -1;
         break;
      case CHUNK_TRAILER:
         // Trailer fields are skipped; an empty line ends the body.
         i++;
         if (c == '\n') {
            b->done = b->line_len == 0;
            b->line_len = 0;
         } else if (c != '\r') {
            b->line_len = 1;
         }
         break;
      }
   }
   return (long)i;
}
