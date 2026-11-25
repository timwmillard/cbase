/* net_url.h — http://, https://, ws:// and wss:// URLs. */

#ifndef NET_URL_H
#define NET_URL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { NET_HTTP, NET_WS } net_scheme;

typedef struct {
   net_scheme scheme;
   bool secure; // https or wss
   bool ipv6;   // host was written as [literal]
   char host[256];
   int port;
   char path[2048]; // path and query; at least "/"
} net_url;

// 0 on success, -1 for anything that isn't a usable URL of the four schemes.
int net_url_parse(const char *url, net_url *u);
// The Host header value: brackets for IPv6, the port unless it's the default.
// Returns the length, or -1 if it doesn't fit.
int net_url_host_header(const net_url *u, char *out, size_t cap);

#endif
