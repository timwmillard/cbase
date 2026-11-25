/* http_proto.h — HTTP/1.1 without any I/O: the request head, the response
 * head and an incremental body decoder (Content-Length, chunked, until
 * close). Used by http.c and by the tests' server.
 */

#ifndef HTTP_PROTO_H
#define HTTP_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_url.h"

#define HTTP_HEAD_MAX 65536

// The request head: request line, Host, the caller's headers ("Name: value",
// NULL-terminated, may be NULL), a User-Agent unless one was given,
// Content-Length when has_body, and Connection: close. Returns the length, or
// -1 if it doesn't fit or a header contains a line break.
int http_build_request(char *out, size_t cap, const char *method,
                       const net_url *u, const char *const *headers,
                       bool has_body, size_t body_len);

typedef enum {
   HTTP_BODY_NONE,
   HTTP_BODY_LENGTH,
   HTTP_BODY_CHUNKED,
   HTTP_BODY_CLOSE, // until the server closes the connection
} http_body_kind;

typedef struct {
   int status;
   const char *headers; // the header lines, each ending in CRLF
   size_t headers_len;
   http_body_kind body;
   uint64_t length; // for HTTP_BODY_LENGTH
} http_head;

// Parses a response head at the start of buf. 1: complete, *consumed is its
// length (the body follows). 0: not complete yet. -1: malformed, with the
// reason in err. head_request: the request was HEAD, so there is no body.
int http_parse_head(const char *buf, size_t len, bool head_request,
                    http_head *h, size_t *consumed, char *err,
                    size_t err_cap);

// The value of header name (case-insensitive) in a block of header lines,
// trimmed, or NULL. Not NUL-terminated: *len is its length.
const char *http_header_find(const char *headers, size_t headers_len,
                             const char *name, size_t *len);

typedef struct {
   http_body_kind kind;
   uint64_t left; // of the body, or of the current chunk
   int state;     // chunked only
   char line[32];
   size_t line_len;
   bool done;
} http_body;

void http_body_init(http_body *b, const http_head *h);
// Decodes the start of in. Returns how much of in it used, or -1 for a
// malformed body; *data / *data_len are body bytes inside in (possibly
// none). Call again with the rest until all of in is used or b->done.
long http_body_decode(http_body *b, const char *in, size_t len,
                      const char **data, size_t *data_len);

#endif
