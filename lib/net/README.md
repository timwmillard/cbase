# webc — HTTP, WebSocket and SSE client for C

A small, general-purpose web client library for C programs: an HTTP/1.1
client with streamed bodies, a WebSocket client (RFC 6455) and a Server-Sent
Events parser. Client only — it makes requests, it never listens.

The sources live here in `lib/net/`; `lib/webc.h` is generated from them as a
single stb-style header for distribution (see [Single header](#single-header)).

## Goals

- **Minimal dependencies.** A C11 compiler and the system libraries. No
  vendored TLS, no zlib, no GPL.
- **The OS's network stack and TLS.** Certificates come from the system
  store, so they stay up to date without the library shipping a CA bundle.
- **One protocol implementation.** HTTP/1.1, WebSocket framing and SSE are
  written once in portable C with no I/O; only the byte stream (TCP + TLS)
  differs per platform.
- **Streaming.** HTTP bodies are delivered as they arrive (e.g. LLM tokens
  over SSE).
- **Lightweight.** About 4k lines in total.

## Platforms

| Platform        | Transport                    | TLS                                         |
| --------------- | ---------------------------- | ------------------------------------------- |
| macOS / iOS     | Network.framework            | Network.framework (system trust)            |
| Linux, BSDs     | POSIX sockets + `poll`       | system `libssl` loaded at runtime (`dlopen`) |
| Windows 8+      | Winsock + `select`           | SChannel (Windows certificate store)        |

On Unix the build needs no TLS headers or library: OpenSSL 1.1, OpenSSL 3 or
LibreSSL is found at runtime (`libssl.so.3`, `libssl.so.1.1`, ...). Set
`NET_LIBSSL=/path/to/libssl.so` to pick one explicitly. Define
`NET_BACKEND_UNIX` to use the POSIX backend on Apple systems too.

Link with:

- Apple: `-framework Network -framework Security`
- Windows: `ws2_32 secur32 bcrypt`
- Elsewhere: `-ldl -lpthread`

## Usage

Every object is configured sokol-style: fill a `*_desc` struct with
designated initialisers and pass it to the constructor. Zero fields take
their defaults, and the desc is copied, so a compound literal is fine.

Callbacks run on a network thread (one per request/connection). `*_free` may
be called from any thread at any time, including from inside a callback;
once it returns no callback is running and none will start.

### Naming

Public names are short and unprefixed (`http_*`, `ws_*`, `sse_*`): like
`base.h`, webc is standalone within this ecosystem.

| Kind                                               | Functions            |
| -------------------------------------------------- | -------------------- |
| Background handle (heap, runs on a network thread) | `X_start` / `X_free` |
| Embedded value (no I/O, caller-owned storage)      | `X_init` / `X_free`  |

- Background handles take two structs, `X_start(how, what)`: the `_desc` is
  how to run it (timeouts, callbacks; reusable, NULL for the defaults) and
  the `_request` is the target (`http_request`, `ws_request`). Responses are
  `_response`; a value's configuration is also a `_desc` (`sse_desc`).
- Callbacks take the handle first and `user_data` last.
- Protocol actions are separate from lifetime: `ws_close` sends a close
  frame, `ws_free` releases the handle.

### HTTP

`http_desc` is *how* to send (timeouts, callbacks; reusable, NULL for the
defaults), `http_request` is *what* to send.

```c
static void on_data(http *h, const char *data, size_t len, void *ud) {
   fwrite(data, 1, len, stdout);
}

static void on_done(http *h, const char *error, void *ud) {
   if (error)
      fprintf(stderr, "error: %s\n", error);
   // signal the main thread; it calls http_free(h)
}

http *h = http_start(
   &(http_desc){.on_data = on_data, .on_done = on_done},
   &(http_request){
      .url = "https://example.com/",
      .headers = (const char *[]){"Accept: text/html", NULL},
   });
```

- `method` defaults to GET, or POST when `body` is set.
- `on_response` runs once the head arrives; `http_header()` looks up a
  response header (valid during that callback only).
- A non-2xx status is not an error; `on_done`'s `error` is NULL on success.
- Defaults: 10 s connect timeout, 120 s idle timeout.

### WebSocket

```c
ws *w = ws_start(
   &(ws_desc){
      .on_open = on_open,
      .on_message = on_message, // (ws *, data, len, binary, user_data)
      .on_close = on_close,     // always the last callback
   },
   &(ws_request){
      .url = "wss://example.com/socket",
      .protocols = (const char *[]){"chat", NULL},
   });

ws_send_text(w, "hello", 5);   // thread-safe
ws_close(w, WS_CLOSE_NORMAL, "bye");
```

- Keepalive pings every 30 s by default (`ping_interval_ms = -1` turns it
  off); the connection fails if nothing is received for two intervals.
- Messages larger than `max_message_size` (default 16 MiB) close the
  connection with 1009.

### Server-Sent Events

A parser without I/O; feed it body bytes, e.g. from `http`'s `on_data`:

```c
sse_parser p;
sse_init(&p, &(sse_desc){.on_event = on_event, .user_data = ctx});
sse_feed(&p, data, len); // calls on_event(p, event, data, len, ud) per event
sse_free(&p);
```

## Layout

```
net.h               the public API: http, ws and sse
net_sys.h/.c        mutexes, threads, monotonic clock, random bytes
net_url.h/.c        http:// https:// ws:// wss:// URL parsing
net_conn.h          the core: one TCP or TLS byte stream (internal)
net_conn.c          picks the backend below
net_conn_apple.c    Network.framework
net_conn_unix.c     POSIX sockets + poll; libssl loaded at runtime
net_conn_win32.c    Winsock + SChannel
http.c              HTTP/1.1 client
http_proto.h/.c     request building, response head, body decoding (no I/O)
ws.c                WebSocket client
ws_proto.h/.c       RFC 6455 handshake, framing, SHA-1, base64 (no I/O)
sse.c               Server-Sent Events parser (no I/O)
```

Only `net.h` is public. The core (`net_conn.h`) is a blocking-style byte
stream driven by one network thread, with `net_conn_wake` / `net_conn_cancel`
as the only calls other threads may make.

## Single header

`lib/webc.h` is generated from these sources with `tool/singleh`; edit the
files here, never the generated header. Use it stb-style — in exactly one C
file:

```c
#define WEBC_IMPLEMENTATION
#include "webc.h"
```

The amalgamation is described by a singleh manifest (`webc.manifest`) and a
preamble (`webc_preamble.h`) that sets the feature macros ahead of the
hoisted system headers. See [ROADMAP.md](ROADMAP.md) for the remaining setup
(manifest import, `make webc.h` target, tests).

## Status

Imported from `~/dev/x/chat/src/net` (also used by the wobble client), with
the naming changes above applied; the API is no longer source-compatible
with those copies (`ws_connect(desc)` → `ws_start(desc, req)`,
`http_start(desc, req)`, the SSE callback takes the parser). See [ROADMAP.md](ROADMAP.md) for the remaining
interface changes and missing features.
