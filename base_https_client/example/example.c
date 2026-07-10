/*
 * http_response.h — incremental HTTP/1.1 response parser for the
 * net.h + TLSe stack. Built on picohttpparser (vendor these too:
 * picohttpparser.c + picohttpparser.h, MIT, from github.com/h2o).
 *
 * Feed it plaintext bytes as they come out of tls_read(); it tells
 * you when the response is complete and hands you status, headers,
 * and the de-chunked body.
 *
 * Handles all three HTTP/1.1 body framings:
 *   - Content-Length
 *   - Transfer-Encoding: chunked   (via phr_decode_chunked)
 *   - close-delimited              (body ends at connection close)
 *
 * Usage:
 *   #define HTTP_RESPONSE_IMPLEMENTATION
 *   #include "http_response.h"      // in one .c file
 *
 *   http_response_t r;
 *   http_response_init(&r);
 *   ...
 *   while ((n = tls_read(tls, pt, sizeof(pt))) > 0) {
 *       int rc = http_response_feed(&r, pt, n);
 *       if (rc < 0)  { bad response }
 *       if (rc == 1) { done — use r.status, r.body, r.body_len }
 *   }
 *   // on connection close (close-delimited responses):
 *   if (http_response_finish(&r) == 1) { done }
 *   ...
 *   http_response_free(&r);
 */

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "picohttpparser.h"
#include <stddef.h>

#define HTTP_MAX_HEADERS 64

typedef struct {
   /* Results (valid once complete) */
   int status; /* e.g. 200 */
   struct phr_header headers[HTTP_MAX_HEADERS];
   size_t num_headers;
   char *body; /* decoded body (points into buf) */
   size_t body_len;

   /* Internal state */
   char *buf; /* accumulated bytes */
   size_t len, cap;
   size_t header_end; /* offset where body starts */
   int headers_done;
   enum {
      FRAME_UNKNOWN,
      FRAME_CONTENT_LENGTH,
      FRAME_CHUNKED,
      FRAME_CLOSE
   } framing;
   size_t content_length;
   struct phr_chunked_decoder chunked;
   size_t chunk_undecoded; /* raw bytes not yet de-chunked */
   int complete;
} http_response_t;

void http_response_init(http_response_t *r);
void http_response_free(http_response_t *r);

/* Feed bytes. Returns: 1 = response complete, 0 = need more data,
 * negative = parse error. Safe to keep calling with more data until
 * it returns nonzero. */
int http_response_feed(http_response_t *r, const void *data, size_t n);

/* Call when the connection closes. Returns 1 if the response is now
 * complete (close-delimited framing), negative if it was truncated
 * mid-body (Content-Length/chunked not satisfied). */
int http_response_finish(http_response_t *r);

/* Case-insensitive header lookup. Returns NULL if absent.
 * Value is NOT null-terminated: length goes to *out_len. */
const char *http_response_header(const http_response_t *r, const char *name,
                                 size_t *out_len);

#endif /* HTTP_RESPONSE_H */

/* ================================================================== */
#ifdef HTTP_RESPONSE_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

void http_response_init(http_response_t *r) {
   memset(r, 0, sizeof(*r));
   memset(&r->chunked, 0, sizeof(r->chunked));
   r->chunked.consume_trailer = 1; /* eat trailing headers after chunks */
}

void http_response_free(http_response_t *r) {
   free(r->buf);
   memset(r, 0, sizeof(*r));
}

static int http__append(http_response_t *r, const void *data, size_t n) {
   if (r->len + n > r->cap) {
      size_t ncap = r->cap ? r->cap * 2 : 8192;
      while (ncap < r->len + n)
         ncap *= 2;
      char *nb = realloc(r->buf, ncap);
      if (!nb)
         return -1;
      r->buf = nb;
      r->cap = ncap;
   }
   memcpy(r->buf + r->len, data, n);
   r->len += n;
   return 0;
}

static int http__ieq(const char *a, size_t alen, const char *b) {
   size_t blen = strlen(b);
   if (alen != blen)
      return 0;
   for (size_t i = 0; i < alen; i++) {
      char ca = a[i], cb = b[i];
      if (ca >= 'A' && ca <= 'Z')
         ca += 32;
      if (cb >= 'A' && cb <= 'Z')
         cb += 32;
      if (ca != cb)
         return 0;
   }
   return 1;
}

const char *http_response_header(const http_response_t *r, const char *name,
                                 size_t *out_len) {
   for (size_t i = 0; i < r->num_headers; i++) {
      if (http__ieq(r->headers[i].name, r->headers[i].name_len, name)) {
         if (out_len)
            *out_len = r->headers[i].value_len;
         return r->headers[i].value;
      }
   }
   return NULL;
}

static void http__pick_framing(http_response_t *r) {
   size_t vlen;
   const char *v;

   if ((v = http_response_header(r, "transfer-encoding", &vlen)) != NULL &&
       http__ieq(v, vlen, "chunked")) {
      r->framing = FRAME_CHUNKED;
      return;
   }
   if ((v = http_response_header(r, "content-length", &vlen)) != NULL) {
      r->framing = FRAME_CONTENT_LENGTH;
      r->content_length = 0;
      for (size_t i = 0; i < vlen; i++)
         if (v[i] >= '0' && v[i] <= '9')
            r->content_length = r->content_length * 10 + (v[i] - '0');
      return;
   }
   /* No length info: body runs until the server closes (HTTP/1.0
    * style / "Connection: close" without length). */
   r->framing = FRAME_CLOSE;
}

/* Try to complete the body with whatever is currently buffered. */
static int http__process_body(http_response_t *r) {
   char *body = r->buf + r->header_end;
   size_t avail = r->len - r->header_end;

   switch (r->framing) {

   case FRAME_CONTENT_LENGTH:
      if (avail >= r->content_length) {
         r->body = body;
         r->body_len = r->content_length;
         r->complete = 1;
         return 1;
      }
      return 0;

   case FRAME_CHUNKED: {
      /* Decode in place, but only the bytes we haven't decoded yet.
       * phr_decode_chunked rewrites raw chunked data into plain
       * body bytes and returns:
       *   >= 0 : done (value = trailing bytes after the body)
       *   -2   : incomplete, feed more
       *   -1   : broken chunked encoding                       */
      size_t sz = avail - (r->body_len); /* new raw bytes */
      char *decode_at = body + r->body_len;
      ssize_t ret = phr_decode_chunked(&r->chunked, decode_at, &sz);
      if (ret == -1)
         return -1;
      r->body_len += sz; /* sz -> decoded count */
      r->len = r->header_end + r->body_len +
               (ret >= 0 ? 0 : 0); /* drop consumed raw */
      if (ret >= 0) {
         r->body = body;
         r->complete = 1;
         return 1;
      }
      return 0;
   }

   case FRAME_CLOSE:
      /* Body grows until http_response_finish(). */
      r->body = body;
      r->body_len = avail;
      return 0;

   default:
      return -1;
   }
}

int http_response_feed(http_response_t *r, const void *data, size_t n) {
   if (r->complete)
      return 1;
   if (http__append(r, data, n) < 0)
      return -1;

   if (!r->headers_done) {
      int minor;
      const char *msg;
      size_t msg_len;
      r->num_headers = HTTP_MAX_HEADERS;

      /* Re-parse the whole accumulated buffer each time; returns -2
       * while the header block is still incomplete. */
      int ret = phr_parse_response(r->buf, r->len, &minor, &r->status, &msg,
                                   &msg_len, r->headers, &r->num_headers, 0);
      if (ret == -2)
         return 0; /* need more bytes */
      if (ret == -1)
         return -1; /* malformed */

      r->headers_done = 1;
      r->header_end = (size_t)ret;
      http__pick_framing(r);
   }

   return http__process_body(r);
}

int http_response_finish(http_response_t *r) {
   if (r->complete)
      return 1;
   if (!r->headers_done)
      return -1; /* died before headers */
   if (r->framing == FRAME_CLOSE) {
      r->complete = 1; /* body is what we got */
      return 1;
   }
   return -1; /* truncated body */
}

#endif /* HTTP_RESPONSE_IMPLEMENTATION */
