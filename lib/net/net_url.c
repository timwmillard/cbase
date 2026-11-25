#include "net_url.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool ascii_prefix(const char *s, const char *prefix) {
   for (; *prefix; s++, prefix++) {
      if (tolower((unsigned char)*s) != *prefix)
         return false;
   }
   return true;
}

static bool url_char_ok(char c) {
   return (unsigned char)c > 0x20 && (unsigned char)c != 0x7f;
}

int net_url_parse(const char *url, net_url *u) {
   static const struct {
      const char *prefix;
      net_scheme scheme;
      bool secure;
   } schemes[] = {
       {"https://", NET_HTTP, true},
       {"http://", NET_HTTP, false},
       {"wss://", NET_WS, true},
       {"ws://", NET_WS, false},
   };
   memset(u, 0, sizeof(*u));
   const char *p = NULL;
   for (size_t i = 0; i < sizeof(schemes) / sizeof(schemes[0]); i++) {
      if (ascii_prefix(url, schemes[i].prefix)) {
         u->scheme = schemes[i].scheme;
         u->secure = schemes[i].secure;
         p = url + strlen(schemes[i].prefix);
         break;
      }
   }
   if (!p)
      return -1;

   const char *host = p, *host_end;
   if (*p == '[') {
      host = ++p;
      while (*p && *p != ']')
         p++;
      if (*p != ']')
         return -1;
      host_end = p++;
      u->ipv6 = true;
   } else {
      while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#')
         p++;
      host_end = p;
   }
   size_t host_len = (size_t)(host_end - host);
   if (host_len == 0 || host_len >= sizeof(u->host))
      return -1;
   for (const char *c = host; c < host_end; c++) {
      if (!url_char_ok(*c) || *c == '@' || *c == '/')
         return -1;
   }
   memcpy(u->host, host, host_len);

   u->port = u->secure ? 443 : 80;
   if (*p == ':') {
      long port = 0;
      int digits = 0;
      for (p++; *p >= '0' && *p <= '9'; p++, digits++) {
         port = port * 10 + (*p - '0');
         if (port > 65535)
            return -1;
      }
      if (digits == 0 || port == 0)
         return -1;
      u->port = (int)port;
   }
   if (*p && *p != '/' && *p != '?' && *p != '#')
      return -1;

   size_t n = 0;
   if (*p != '/')
      u->path[n++] = '/';
   for (; *p && *p != '#'; p++) {
      if (!url_char_ok(*p) || n + 1 >= sizeof(u->path))
         return -1;
      u->path[n++] = *p;
   }
   u->path[n] = '\0';
   return 0;
}

int net_url_host_header(const net_url *u, char *out, size_t cap) {
   int n;
   const char *open = u->ipv6 ? "[" : "", *close = u->ipv6 ? "]" : "";
   if (u->port == (u->secure ? 443 : 80))
      n = snprintf(out, cap, "%s%s%s", open, u->host, close);
   else
      n = snprintf(out, cap, "%s%s%s:%d", open, u->host, close, u->port);
   return n < 0 || (size_t)n >= cap ? -1 : n;
}
