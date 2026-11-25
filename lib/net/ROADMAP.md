# webc roadmap

Proposed changes to turn `lib/net` into a general-purpose client library,
distributed as the generated single header `lib/webc.h`. Roughly in priority
order within each section.

Scope stays the same throughout: client only, the OS's network stack and TLS,
no third-party dependencies. Anything that needs zlib, a vendored TLS stack or
a server socket is out of scope.

## Principles

- **Standalone, like `base.h`.** webc is a library for this ecosystem, not a
  drop-in for arbitrary codebases. Public names stay short and unprefixed
  (`http_*`, `ws_*`, `sse_*`, `net_*`); no `webc_` prefix.
- **No dependency on `base.h`.** webc needs only what `net_sys.c` already
  provides. Integration with `base.h` happens through the allocator hook and
  plain `(ptr, len)` data; a thin adapter (e.g. `base_webc/`) can depend on
  both if it's ever wanted.

## 0. Setup

- [ ] Import the rest from `~/dev/x/chat`: `webc.manifest`,
      `webc_preamble.h`, `HTTP_H.md`, `WS_H.md`, and the tests
      (`net_proto_test.c`, `http_test.c`, `ws_echo_test.c`, `webc_test.c`,
      `http_check.c`, `ws_check.c`).
- [ ] Update the manifest paths for `lib/net/` → output `lib/webc.h`.
- [ ] `make webc.h` target in `lib/Makefile` using `tool/singleh`; commit the
      generated header.
- [ ] A test that builds against the generated `webc.h` (catches
      amalgamation breakage), plus the protocol tests against the sources.
- [ ] `WEBC_VERSION` macro; default User-Agent `webc/<version>` instead of
      `net/1`.

## 1. Interface (before anything depends on it)

### Naming rules

One verb per kind of object, not per protocol:

| Kind                                              | Create / destroy       |
| ------------------------------------------------- | ---------------------- |
| Background handle (heap, runs on a network thread) | `X_start` / `X_free`   |
| Embedded value (no I/O, caller-owned storage)      | `X_init` / `X_free`    |
| Blocking call (done when it returns)              | plain verb: `http_fetch` |

- Protocol actions stay distinct from lifetime: `ws_close` sends a close
  frame, `ws_free` releases the handle.
- Every callback takes the handle first and `user_data` last.
- Every background object has exactly one terminal callback, and it carries
  the error.
- Structs: a background handle's *target* is a `_request` (`http_request`,
  `ws_request`) and its *configuration* a `_desc` (`http_desc`, `ws_desc`),
  so every start is `X_start(how, what)` with desc NULL for the defaults.
  Responses are `_response`; a value's configuration is a `_desc`
  (`sse_desc`). The desc is where shared config (TLS, proxy, default
  headers) grows into a future `http_client` used by both http and ws.
- WS reuses neither `http_request` nor HTTP's code path: the handshake must
  be a bodiless GET to a `ws://`/`wss://` URL and needs `protocols`.
- Shared desc fields use the same names in http and ws: `connect_timeout_ms`,
  `tls`, `user_data`, ...

Changes this implies:

- [x] `ws_connect` → `ws_start`.
- [x] Split `http_desc` into `http_desc` (how) and `http_request` (what):
      `http_start(const http_desc *desc, const http_request *req)`, desc may
      be NULL — see below.
- [x] Same split for WS: `ws_request` (url, headers, protocols) and
      `ws_desc` (timeouts, ping, size limit, callbacks):
      `ws_start(const ws_desc *desc, const ws_request *req)`.
- [ ] WS: fold `on_error` into `on_close(w, code, reason, const net_error *)`
      so there is one terminal callback. Done together with error codes.
- [x] SSE: `on_event` gains the parser as its first argument:
      `on_event(sse_parser *p, const char *event, const char *data, size_t
      len, void *user_data)`.

### Symbol visibility

- [ ] Internal symbols leak from the amalgamation: `net_conn_*`,
      `net_url_*`, `net_mutex_*`, `net_strv_*`, `ws_sha1`,
      `ws_base64_encode`, `ws_frame_encode`, `http_build_request`,
      `http_parse_head`, ... Mark them with a `NET_INTERNAL` macro that is
      `static` in the single header and plain `extern` in the multi-file
      build (so the tests can still reach the `*_proto` code). Keeps the
      public surface to what `net.h` declares.

### Error codes

- [ ] Errors are strings only, so callers can't branch on them. Add a shared
      error type:

      ```c
      typedef struct {
         int code;          // NET_OK (0), NET_ERR_DNS, ...
         char message[256]; // detail, "" on success
      } net_error;
      ```

      Codes: `NET_ERR_DNS`, `_CONNECT`, `_TLS`, `_CERT`, `_TIMEOUT`,
      `_CANCELLED`, `_PROTOCOL`, `_TOO_LARGE`, `_NOMEM`, ... The internal
      `net_conn` results (`NET_AGAIN`, `NET_ERR`, `NET_EOF`) get renamed
      (`NET_CONN_*`) so they don't read as public codes.
- [ ] `http on_done(h, const net_error *err, ud)`: `err` is NULL on success.

### HTTP: desc, request, response

`http_desc` is *how* to send (reusable across requests, NULL = defaults);
`http_request` is *what* to send; `http_response` is shared by `on_response`
and `http_fetch`.

```c
typedef struct {
   int connect_timeout_ms; // 0 = 10 s
   int idle_timeout_ms;    // 0 = 120 s, -1 = none
   int timeout_ms;         // total deadline, 0 = none
   size_t max_body_size;   // http_fetch only, 0 = 64 MiB
   // later: tls, max_redirects, proxy, user_agent, allocator

   // http_start only; http_fetch ignores them. Run on a network thread.
   void (*on_response)(http *h, const http_response *res, void *user_data);
   void (*on_data)(http *h, const char *data, size_t len, void *user_data);
   void (*on_done)(http *h, const net_error *err, void *user_data);
   void *user_data;
} http_desc;

typedef struct {
   const char *url;            // http:// or https://
   const char *method;         // NULL: GET, or POST when there is a body
   const char *const *headers; // "Name: value", NULL-terminated
   const void *body;           // sent with Content-Length when not NULL
   size_t body_len;
} http_request;

typedef struct {
   int status;
   const char *headers; size_t headers_len; // "Name: value\r\n" lines
   const char *body;    size_t body_len;    // NUL-terminated; NULL in on_response
   net_error error;                         // set when http_fetch fails
} http_response;

// Blocking: runs on the calling thread, done when it returns. True if a
// response arrived (any status: a 404 is not an error); false with
// res->error set. Always call http_response_free afterwards.
bool http_fetch(const http_desc *desc, const http_request *req,
                http_response *res);
void http_response_free(http_response *res);

// Async: callbacks from desc run on a network thread.
http *http_start(const http_desc *desc, const http_request *req);
void  http_free(http *h);

// Works on both an on_response head and a fetched response.
const char *http_header(const http_response *res, const char *name,
                        size_t *len);
```

```c
http_response res;
if (http_fetch(NULL, &(http_request){
                         .url = "https://api.example.com/v1/models",
                         .headers = (const char *[]){"Authorization: Bearer ...", NULL},
                     }, &res))
   printf("%d\n%.*s\n", res.status, (int)res.body_len, res.body);
else
   fprintf(stderr, "fetch failed: %s\n", res.error.message);
http_response_free(&res);
```

- [ ] Implement `http_fetch` by calling `http.c`'s existing synchronous
      `request()` directly on the caller's thread with a sink that collects
      head and body; the streaming and blocking paths share all protocol
      code.
- [ ] Exceeding `max_body_size` fails with `NET_ERR_TOO_LARGE` rather than
      truncating.
- [ ] A blocking fetch can't be cancelled from another thread; `timeout_ms`
      is the escape hatch (another reason for cancellable DNS, section 2).
- [ ] Later: the `desc` argument can grow into an `http_client` object that
      owns the keep-alive pool, proxy and default headers, with NULL still
      meaning a default client.

### Poll mode

- [ ] Callbacks only run on the network thread, which forces every game/UI
      client (e.g. wobble) to write its own mutex + queue. Add an opt-in
      queued mode, e.g. `.queued = true` on the desc, then
      `bool http_poll(http *, http_event *)` and
      `bool ws_poll(ws *, ws_event *)`, drained on the caller's thread each
      frame. Event data owned by the library until the next poll.

### Smaller API fixes

- [ ] Response headers are only valid during `on_response`; poll mode and
      `http_fetch` need an owned copy.
- [ ] Expose HTTP version and reason phrase in `http_response` (cheap).
- [ ] WS `on_open` should report the negotiated `Sec-WebSocket-Protocol` and
      the handshake response headers; a refused upgrade should report its
      HTTP status rather than burying it in the error string.
- [ ] WS backpressure: the outgoing queue grows without bound. Add
      `ws_buffered_amount()` and/or a `max_send_buffer` that makes
      `ws_send_*` return false.
- [ ] `net_url` path is fixed at 2048 bytes; long GET query strings fail as
      "bad URL". Allocate it (or raise the limit substantially).
- [ ] Caller-supplied `Host`, `Content-Length` or `Connection` headers are
      sent twice. Either reject them or let the caller's value replace the
      library's.

## 2. HTTP features

- [ ] **Redirects**: follow 301/302/303/307/308 with `max_redirects`
      (default ~5, 0 = off); 303 (and 301/302 from POST) become GET; drop
      `Authorization` / `Cookie` on cross-origin redirects; https → http
      refused by default.
- [ ] **Total request timeout** (`timeout_ms`) alongside connect and idle.
- [ ] **Cancellable DNS** on Unix and Windows: `getaddrinfo` blocks and
      ignores both the deadline and `net_conn_cancel`. Options: resolve on a
      helper thread and abandon it on timeout/cancel, or `getaddrinfo_a` on
      glibc / `GetAddrInfoExW` with cancel on Windows. (Network.framework
      already handles this.)
- [ ] **Happy Eyeballs / parallel connect** across IPv4 and IPv6 results
      (currently sequential through the `getaddrinfo` list). Lower priority.
- [ ] **Streaming request body**: `on_send` pull callback or chunked
      request encoding for uploads that don't fit in memory.
- [ ] **Proxy**: `HTTPS_PROXY` / `HTTP_PROXY` / `NO_PROXY` and an explicit
      `.proxy` field, via HTTP CONNECT. Check what Network.framework already
      does with system proxy settings.
- [ ] **Keep-alive / connection reuse**: a small pool keyed by
      (host, port, tls) instead of a new TCP + TLS handshake per request.
      Bigger change (drops `Connection: close`, needs pool lifetime and
      idle eviction); lives in the future `http_client`.
- [ ] URL helpers: percent-encoding for query building; userinfo
      (`user:pass@`) → Basic auth, or keep rejecting it and document.

Out of scope: gzip/deflate (needs zlib — so never send `Accept-Encoding`),
cookies jar, HTTP/2, HTTP/3.

## 3. TLS options

A `.tls` sub-struct shared by `http_desc` and `ws_desc`:

- [ ] `insecure_skip_verify` for localhost / self-signed development. Loud
      name, off by default.
- [ ] Custom CA: a PEM string or file in addition to (or instead of) the
      system store. Unix already loads bundles internally; Apple via
      `sec_protocol_options_set_verify_block`; Windows via a custom chain
      engine or manual verification.
- [ ] Later: certificate / public-key pinning, client certificates. Each is
      real work in all three backends.

## 4. WebSocket features

- [ ] `ws_ping()` and an `on_pong` callback (RTT measurement for games).
- [ ] Distinguish the close code/reason we sent from the one received.
- [ ] Follow redirects during the upgrade handshake.
- [ ] Optionally reconnect with backoff (opt-in, off by default).

Out of scope: permessage-deflate (needs zlib).

## 5. SSE features

- [ ] Report `id:` in the event callback and `retry:` to the caller
      (currently dropped, `sse.c:74`).
- [ ] Optional EventSource-style `sse_start()` on top of `http`: reconnects
      with `Last-Event-ID` and honours `retry`. (Name follows the naming
      rules: a background handle; the parser stays `sse_init`.)

## 6. Library-wide

- [ ] Allocator hooks: `.allocator = {alloc, free, user_data}` on each desc,
      or `NET_MALLOC` / `NET_REALLOC` / `NET_FREE` macros for the single
      header.
- [ ] Optional logging hook (`.logger`) for connection and TLS diagnostics.
- [ ] Document the thread-per-request model; set a modest thread stack size
      instead of the platform default.
- [ ] Unify the HTTP/WS docs (`HTTP_H.md`, `WS_H.md`) into this directory's
      README once the interface settles.

## Suggested order

1. Setup (section 0)
2. Naming rules + static internals
3. Error codes
4. `http_desc` / `http_request` split and `http_fetch`
5. Poll mode
6. Redirects, total timeout, cancellable DNS
7. TLS options
8. The rest as needed
