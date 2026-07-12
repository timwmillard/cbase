#include <stdio.h>

#include "base.h"
#include "net.h"

int main(int argc, char *argv[]) {

   if (argc < 3) {
      fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
   }
   const char *host = argv[1];
   const char *port = argv[2];

   net_socket sock = net_connect(host, port);

   string hello = S("Hello world!!");
   printf("%.*s\n", STR_ARG(hello));

   return 0;
}
