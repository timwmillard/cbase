#include <stdio.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "base.h"

string ip_string(arena *a, struct sockaddr *ip) {
   char buf[INET6_ADDRSTRLEN];

   if (ip->sa_family == AF_INET)
      inet_ntop(ip->sa_family, &((struct sockaddr_in *)ip)->sin_addr, buf,
                INET_ADDRSTRLEN);
   else if (ip->sa_family == AF_INET6)
      inet_ntop(ip->sa_family, &((struct sockaddr_in6 *)ip)->sin6_addr, buf,
                INET6_ADDRSTRLEN);
   else
      return S("");

   return string_from(a, buf);
}

string port_string(arena *a, struct sockaddr *ip) {
   char buf[6];
   int port = 0;
   if (ip->sa_family == AF_INET) {
      port = ((struct sockaddr_in *)ip)->sin_port;
   } else if (ip->sa_family == AF_INET6) {
      port = ((struct sockaddr_in6 *)ip)->sin6_port;
   }
   snprintf(buf, 6, "%d", ntohs(port));
   return string_from(a, buf);
}

arena g_arena;

int main(int argc, char *argv[]) {
   struct addrinfo hints = {
       .ai_socktype = SOCK_STREAM,
       .ai_family = AF_UNSPEC,
   };
   struct addrinfo *res;
   int rc;
   rc = getaddrinfo("www.google.com", "80", &hints, &res);
   if (rc != 0) {
      fprintf(stderr, "Unable to connect %d\n", rc);
      return 1;
   }
   printf("Connection success %d\n", rc);

   for (struct addrinfo *next = res; next != NULL; next = next->ai_next) {
      string ip = ip_string(&g_arena, next->ai_addr);
      printf("IP: " STR_FMT "\n", STR_ARG(ip));
      // print port
      string port = port_string(&g_arena, next->ai_addr);
      printf("Port: " STR_FMT "\n", STR_ARG(port));
   }
   freeaddrinfo(res);

   return 0;
}
