/* ws_proto.h — the WebSocket protocol (RFC 6455) without any I/O: the HTTP
 * upgrade, frame encoding and an incremental frame reader. Used by ws.c and
 * by the tests' echo server.
 */

#ifndef WS_PROTO_H
#define WS_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_url.h"

void ws_sha1(const void *data, size_t len, uint8_t out[20]);
// out needs 4 * ((len + 2) / 3) + 1 bytes; it is NUL-terminated. Returns the
// length without the NUL.
size_t ws_base64_encode(const uint8_t *in, size_t len, char *out);

// Sec-WebSocket-Key from 16 random bytes.
void ws_make_key(const uint8_t random[16], char key[25]);
// The Sec-WebSocket-Accept a server must answer for key.
void ws_accept_key(const char *key, char accept[29]);

// The client's upgrade request. headers ("Name: value") and protocols are
// NULL-terminated and may be NULL. Returns the length, or -1 if it doesn't
// fit or a header contains a line break.
int ws_build_request(char *out, size_t cap, const net_url *u,
                     const char *key, const char *const *headers,
                     const char *const *protocols);

// Checks the server's answer to the upgrade request. 1: accepted, *consumed
// is the length of the HTTP response (frames may follow it in buf). 0: not
// complete yet. -1: refused, with the reason in err.
int ws_parse_response(const char *buf, size_t len, const char *key,
                      size_t *consumed, char *err, size_t err_cap);

enum {
   WS_OP_CONT = 0x0,
   WS_OP_TEXT = 0x1,
   WS_OP_BINARY = 0x2,
   WS_OP_CLOSE = 0x8,
   WS_OP_PING = 0x9,
   WS_OP_PONG = 0xA,
};

#define WS_FRAME_HEADER_MAX 14

// Writes one final frame to out, which needs len + WS_FRAME_HEADER_MAX bytes.
// Clients pass a mask; servers pass NULL. Returns the frame's length.
size_t ws_frame_encode(uint8_t *out, int opcode, const void *payload,
                       size_t len, const uint8_t mask[4]);
// The payload of a close frame: code then reason, truncated to fit 125 bytes.
size_t ws_close_payload(uint8_t out[125], int code, const char *reason);

bool ws_utf8_valid(const uint8_t *s, size_t len);

typedef enum {
   WS_EV_NONE, // need more bytes
   WS_EV_TEXT,
   WS_EV_BINARY,
   WS_EV_PING,
   WS_EV_PONG,
   WS_EV_CLOSE, // code (1005 if none) and reason
   WS_EV_ERROR, // the peer broke the protocol: close with code, reason says why
} ws_event_type;

typedef struct {
   ws_event_type type;
   const uint8_t *data;
   size_t len;
   int code;
   const char *reason;
} ws_event;

typedef struct {
   bool server; // expects masked frames (a server reading a client)
   size_t max_message;
   uint8_t *in;
   size_t in_len, in_pos, in_cap;
   uint8_t *msg; // a fragmented message being put together
   size_t msg_len, msg_cap;
   int msg_op; // its opcode, 0 when none is in progress
   bool msg_done;
   char reason[124];
} ws_reader;

void ws_reader_init(ws_reader *r, bool server, size_t max_message);
void ws_reader_free(ws_reader *r);
// False when out of memory.
bool ws_reader_feed(ws_reader *r, const void *data, size_t len);
// The next event. Its data stays valid until the next call to next or feed.
// After WS_EV_ERROR the reader must not be used again.
ws_event ws_reader_next(ws_reader *r);

#endif
