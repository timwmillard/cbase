/* net_conn.h — the core: one TCP connection, optionally TLS, with the OS's
 * TLS and certificate store. Internal to the net library.
 *
 * A connection is driven by one network thread: dial, then read / write,
 * which never block, and wait, which blocks until the socket is ready, the
 * deadline passes or another thread calls net_conn_wake. net_conn_wake and
 * net_conn_cancel are the only functions other threads may call, and only
 * until net_conn_free.
 *
 * Deadlines are net_now_ms() values; -1 means none.
 */

#ifndef NET_CONN_H
#define NET_CONN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct net_conn net_conn;

// Results of read and write besides a byte count.
enum { NET_AGAIN = 0, NET_ERR = -1, NET_EOF = -2 };

// Events for wait.
enum { NET_READ = 1, NET_WRITE = 2, NET_WAKE = 4 };

net_conn *net_conn_new(void);

// Resolves host and connects, then does the TLS handshake and verifies the
// certificate against host when tls is set. False with net_conn_error set.
bool net_conn_dial(net_conn *c, const char *host, int port, bool tls,
                   int64_t deadline);

// > 0: bytes read. NET_AGAIN: nothing yet, wait for NET_READ. NET_EOF: the
// peer closed the connection. NET_ERR: see net_conn_error.
long net_conn_read(net_conn *c, void *buf, size_t n);
// > 0: bytes taken. NET_AGAIN: wait for NET_WRITE. NET_ERR.
long net_conn_write(net_conn *c, const void *buf, size_t n);

// Waits for any of events (NET_READ, NET_WRITE). Returns the events that are
// ready, possibly with NET_WAKE, 0 at the deadline, or NET_ERR when the
// connection was cancelled or poll itself failed. A ready event means "try
// the call again", not that it will move bytes.
int net_conn_wait(net_conn *c, int events, int64_t deadline);

// Any thread: makes the current or next wait return NET_WAKE.
void net_conn_wake(net_conn *c);
// Any thread: makes dial and wait fail with "cancelled" from now on.
void net_conn_cancel(net_conn *c);

// Blocking helpers for request / response phases, on top of the above;
// wake-ups are ignored. write_all: false on error or at the deadline
// ("timed out"). read_some: > 0, NET_EOF or NET_ERR (also at the deadline).
bool net_conn_write_all(net_conn *c, const void *buf, size_t n,
                        int64_t deadline);
long net_conn_read_some(net_conn *c, void *buf, size_t n, int64_t deadline);

// Why the last call failed.
const char *net_conn_error(const net_conn *c);

void net_conn_free(net_conn *c);

#endif
