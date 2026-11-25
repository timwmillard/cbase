# webc roadmap

Proposed changes to turn `lib/net` into a general-purpose client library,
distributed as the generated single header `lib/webc.h`. Roughly in priority
order within each section.

Scope stays the same throughout: client only, the OS's network stack and TLS,
no third-party dependencies. Anything that needs zlib, a vendored TLS stack or
a server socket is out of scope.

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

### Naming and symbol visibility

- [ ] Public names `http`, `ws`, `sse_*` are generic and will collide with
      other code. Rename to a `webc_` prefix (`webc_http`, `webc_ws`,
      `webc_sse`), or keep short names behind a `WEBC_API` macro — decide
      once.
- [ ] Internal symbols leak from the amalgamation: `net_conn_*`,
      `net_url_*`, `net_mutex_*`, `net_strv_*`, `ws_sha1`,
      `ws_base64_encode`, `ws_frame_encode`, `http_build_request`,
      `http_parse_head`, ... Mark them with a `WEBC_INTERNAL` macro that is
      `static` in the single header and plain `extern` in the multi-file
      build (so the tests can still reach the `*_proto` code).

### Error codes

- [ ] Errors are strings only, so callers can't branch on them. Add an enum
      and keep the string as detail:
      `WEBC_ERR_DNS`, `_CONNECT`, `_TLS`, `_CERT`, `_TIMEOUT`, `_CANCELLED`,
      `_PROTOCOL`, `_TOO_LARGE`, `_NOMEM`, ...
- [ ] `on_done(h, const webc_error *err, ud)` and `ws on_error` likewise.

### Threading model: add a poll mode

- [ ] Callbacks only run on the network thread, which forces every game/UI
      client (e.g. wobble) to write its own mutex + queue. Add an opt-in
      queued mode, e.g. `.queued = true` then
      `bool webc_http_poll(webc_http *, webc_http_event *)` and
      `bool webc_ws_poll(webc_ws *, webc_ws_event *)`, drained on the
      caller's thread each frame. Event data owned by the library until the
      next poll.

### Blocking convenience

- [ ] `webc_fetch(const webc_http_desc *, webc_result *)`: blocking, returns
      status, owned response headers and a malloc'd body (with a max body
      size). `webc_result_free()`. Covers the most common use case in a few
      lines on top of `http_start`.

### Smaller API fixes

- [ ] Response headers are only valid during `on_response`; poll mode and
      `webc_fetch` need an owned copy.
- [ ] Expose HTTP version and reason phrase in `http_response` (cheap).
- [ ] WS `on_open` should report the negotiated `Sec-WebSocket-Protocol` and
      the handshake response headers; a refused upgrade should report its
      HTTP status rather than burying it in the error string.
- [ ] WS backpressure: the outgoing queue grows without bound. Add
      `webc_ws_buffered_amount()` and/or a `max_send_buffer` that makes
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
      idle eviction); later.
- [ ] URL helpers: percent-encoding for query building; userinfo
      (`user:pass@`) → Basic auth, or keep rejecting it and document.

Out of scope: gzip/deflate (needs zlib — so never send `Accept-Encoding`),
cookies jar, HTTP/2, HTTP/3.

## 3. TLS options

A `.tls` sub-struct shared by http and ws:

- [ ] `insecure_skip_verify` for localhost / self-signed development. Loud
      name, off by default.
- [ ] Custom CA: a PEM string or file in addition to (or instead of) the
      system store. Unix already loads bundles internally; Apple via
      `sec_protocol_options_set_verify_block`; Windows via a custom chain
      engine or manual verification.
- [ ] Later: certificate / public-key pinning, client certificates. Each is
      real work in all three backends.

## 4. WebSocket features

- [ ] `webc_ws_ping()` and an `on_pong` callback (RTT measurement for games).
- [ ] Distinguish the close code/reason we sent from the one received.
- [ ] Follow redirects during the upgrade handshake.
- [ ] Optionally reconnect with backoff (opt-in, off by default).

Out of scope: permessage-deflate (needs zlib).

## 5. SSE features

- [ ] Report `id:` in the event callback and `retry:` to the caller
      (currently dropped, `sse.c:74`).
- [ ] Optional EventSource-style `webc_sse_connect()` on top of `http`:
      reconnects with `Last-Event-ID` and honours `retry`.

## 6. Library-wide

- [ ] Allocator hooks, sokol-style `.allocator = {alloc, free, user_data}`
      on each desc, or `WEBC_MALLOC` / `WEBC_REALLOC` / `WEBC_FREE` macros
      for the single header.
- [ ] Optional logging hook (`.logger`) for connection and TLS diagnostics.
- [ ] Document the thread-per-request model; set a modest thread stack size
      instead of the platform default.
- [ ] Unify the HTTP/WS docs (`HTTP_H.md`, `WS_H.md`) into this directory's
      README once the interface settles.

## Suggested order

1. Setup (section 0)
2. Naming + static internals
3. Error codes
4. `webc_fetch`
5. Poll mode
6. Redirects, total timeout, cancellable DNS
7. TLS options
8. The rest as needed
