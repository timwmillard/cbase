/* net.h — the public API of webc (the net library): an HTTP/1.1 client with
 * streamed bodies, a WebSocket client (RFC 6455) and a Server-Sent Events
 * parser, on the operating system's TLS. Everything else in src/net is
 * internal.
 *
 * The clients are built on the net core (net_conn.h): Network.framework on
 * Apple, POSIX sockets + the system libssl loaded at runtime on other Unix,
 * Winsock + SChannel on Windows. See HTTP_H.md and WS_H.md.
 *
 * Each object is configured sokol-style: fill a *_desc struct with
 * designated initialisers and pass it to the constructor. Zero fields take
 * their defaults, and the desc is copied, so a compound literal is fine.
 *
 * Naming: background handles (heap, run on a network thread) are X_start /
 * X_free; values the caller embeds are X_init / X_free. A struct describing
 * a message is named for it (http_request, http_response); one configuring
 * an object is a _desc.
 */

#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>

/* HTTP ------------------------------------------------------------------ */

typedef struct http http;

typedef struct {
   int status;
   const char *headers; // the raw header lines, "Name: value\r\n" each
   size_t headers_len;
} http_response;

// What to send.
typedef struct {
   const char *url;            // http:// or https://
   const char *method;         // NULL: GET, or POST when there is a body
   const char *const *headers; // "Name: value", NULL-terminated
   const void *body;           // sent with Content-Length when not NULL
   size_t body_len;
} http_request;

// How to send it; one desc can serve many requests.
typedef struct {
   int connect_timeout_ms; // 0 = default (10 s)
   int idle_timeout_ms;    // 0 = default (120 s), -1 = none

   // Run on a network thread: on_response once the head arrives, on_data for
   // each piece of the body, and on_done exactly once, last. A non-2xx status
   // is not an error; error is NULL on success. All optional.
   void (*on_response)(http *h, const http_response *res, void *user_data);
   void (*on_data)(http *h, const char *data, size_t len, void *user_data);
   void (*on_done)(http *h, const char *error, void *user_data);
   void *user_data;
} http_desc;

//    http *h = http_start(
//       &(http_desc){.on_data = on_data, .on_done = on_done, .user_data = ctx},
//       &(http_request){.url = "https://example.com/"});
//
// Sends the request on a new network thread and returns at once. desc may be
// NULL for the defaults (and no callbacks); both are copied. NULL only for a
// missing or bad URL or out of memory.
http *http_start(const http_desc *desc, const http_request *req);

// Any thread, any time, including from a callback. Cancels the request if it
// is still running. Once it returns no callback is running and none will
// start.
void http_free(http *h);

// The value of a response header (case-insensitive name), or NULL. Not
// NUL-terminated: *len is its length. Valid during on_response.
const char *http_header(const http_response *res, const char *name,
                        size_t *len);

/* WebSocket ------------------------------------------------------------- */

typedef struct ws ws;

// What to connect to: the opening handshake.
typedef struct {
   const char *url;              // ws:// or wss://
   const char *const *headers;   // "Name: value", NULL-terminated
   const char *const *protocols; // Sec-WebSocket-Protocol, NULL-terminated
} ws_request;

// How to run the connection; one desc can serve many connections.
typedef struct {
   int connect_timeout_ms;  // 0 = default (10 s)
   int ping_interval_ms;    // 0 = default (30 s), -1 = off
   size_t max_message_size; // 0 = default (16 MiB)

   // Run on a network thread; on_close is always the last. All optional.
   void (*on_open)(ws *w, void *user_data);
   void (*on_message)(ws *w, const char *data, size_t len, bool binary,
                      void *user_data);
   void (*on_close)(ws *w, int code, const char *reason, void *user_data);
   void (*on_error)(ws *w, const char *message, void *user_data);
   void *user_data;
} ws_desc;

enum {
   WS_CLOSE_NORMAL = 1000,
   WS_CLOSE_GOING_AWAY = 1001,
   WS_CLOSE_PROTOCOL_ERROR = 1002,
   WS_CLOSE_UNSUPPORTED = 1003,
   WS_CLOSE_ABNORMAL = 1006, // never sent: the connection was lost
   WS_CLOSE_INVALID_DATA = 1007,
   WS_CLOSE_TOO_BIG = 1009,
};

//    ws *w = ws_start(
//       &(ws_desc){.on_message = on_message, .on_close = on_close,
//                  .user_data = ctx},
//       &(ws_request){.url = "wss://example.com/socket"});
//
// Starts connecting on a new network thread and returns at once. desc may be
// NULL for the defaults (and no callbacks); both are copied. NULL only for a
// missing or bad URL or out of memory; everything else is reported through
// the callbacks.
ws *ws_start(const ws_desc *desc, const ws_request *req);

// Thread-safe. False before on_open or after ws_close.
bool ws_send_text(ws *w, const char *data, size_t len);
bool ws_send_binary(ws *w, const void *data, size_t len);

// Thread-safe. Starts the close handshake; on_close follows.
void ws_close(ws *w, int code, const char *reason);

// Any thread, any time, including from a callback. Closes the connection if
// it is still open. Once it returns no callback is running and none will
// start.
void ws_free(ws *w);

/* Server-Sent Events ---------------------------------------------------- */

// A text/event-stream parser without any I/O. Feed it the body bytes as they
// arrive (e.g. from http's on_data); it calls back once per event.

typedef struct sse_parser sse_parser;

// event is the event: field, "message" when there was none. data is the
// data: lines joined with "\n", NUL-terminated. Both are valid during the
// call only.
typedef void (*sse_event_fn)(sse_parser *p, const char *event,
                             const char *data, size_t len, void *user_data);

//    sse_init(&p, &(sse_desc){.on_event = on_event, .user_data = ctx});
typedef struct {
   sse_event_fn on_event; // once per event
   void *user_data;
   size_t max; // the largest line or event accepted, 0 = 1 MiB
} sse_desc;

// Public so it can live on the stack or inside another struct; treat the
// fields as private.
struct sse_parser {
   sse_event_fn on_event;
   void *user_data;
   size_t max; // of one line and of one event's data
   char *line;
   size_t line_len, line_cap;
   char *data;
   size_t data_len, data_cap;
   char event[64];
   bool after_cr; // a following LF ends the same line
};

// desc may be NULL for the defaults (and no callback).
void sse_init(sse_parser *p, const sse_desc *desc);
// Calls on_event for each complete event. False if a line or an event is
// larger than max, or out of memory; the parser must not be fed again then.
bool sse_feed(sse_parser *p, const char *in, size_t len);
void sse_free(sse_parser *p);

#endif
