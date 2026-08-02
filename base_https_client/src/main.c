#include <stdio.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "base.h"
#include "picohttpparser.h"

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

#define REQUEST                                                                \
   "GET / HTTP/1.1\r\n"                                                        \
   "Connection: close\r\n"                                                     \
   "\r\n"

typedef struct {
   int status_code;
   string body;

   arena arena;
} http_response;

int main(int argc, char *argv[]) {

   if (argc < 3) {
      fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
      return 1;
   }
   const char *host = argv[1];
   const char *port = argv[2];

   struct addrinfo hints = {
       .ai_socktype = SOCK_STREAM,
       .ai_family = AF_UNSPEC,
   };
   struct addrinfo *res;
   int rc;
   rc = getaddrinfo(host, port, &hints, &res);
   if (rc != 0) {
      fprintf(stderr, "Unable to connect %d\n", rc);
      return 1;
   }

   int sock;
   for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
      string ip = ip_string(&g_arena, ai->ai_addr);
      string port = port_string(&g_arena, ai->ai_addr);
      printf("IP: " STR_FMT ", Port: " STR_FMT "\n", STR_ARG(ip),
             STR_ARG(port));

      sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      rc = connect(sock, ai->ai_addr, ai->ai_addrlen);
      if (rc == 0) {
         break;
      }
      close(sock);
   }
   freeaddrinfo(res);

   isize n;
   n = send(sock, REQUEST, sizeof(REQUEST) - 1, 0);

   string_builder sb = sb_init(&g_arena);

#define BUF_SIZE 4096 * 4
   u8 buf[BUF_SIZE] = {0};

   int i = 0;
   while ((n = recv(sock, buf, BUF_SIZE - 1, 0)) > 0) {
      sb_append_from_n(&sb, (const char *)buf, n);
      i++;
   }
   close(sock);
   printf(STR_FMT, STR_ARG(sb_string(&sb)));
   printf("\nConnection success: loop=%d, sb.len=%lu\n", i, sb.len);

   // int phr_parse_response(const char *_buf, size_t len, int *minor_version,
   //                        int *status, const char **msg, size_t *msg_len,
   //                        struct phr_header *headers, size_t *num_headers,
   //                        size_t last_len);
   int minor_version;
   int status;
   const char *msg;
   size_t msg_len;
   struct phr_header headers[100];
   size_t num_headers;
   rc = phr_parse_response(sb.data, sb.len, &minor_version, &status, &msg,
                           &msg_len, headers, &num_headers, 0);
   if (rc <= 0) {
      printf("ERROR: %s\n", "parse error");
      return 1;
   }
   printf("HTTP/1.%d %d %.*s\n", minor_version, status, (int)msg_len, msg);
   for (size_t i = 0; i < num_headers; i++) {
      printf("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
             (int)headers[i].value_len, headers[i].value);
   }
   return 0;
}
