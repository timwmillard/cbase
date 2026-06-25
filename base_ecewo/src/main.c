#include <stdio.h>

#include "ecewo.h"

void hello_world(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_send_text(res, ECEWO_OK, "Hello, World!");
}

int main(void) {
  ecewo_app_t *app = ecewo_create();
  if (!app) {
    fprintf(stderr, "Failed to initialize server\n");
    return -1;
  }

  ECEWO_GET(app, "/", hello_world);

  if (ecewo_listen(app, 3000) != 0) {
    fprintf(stderr, "Failed to start server\n");
    return -1;
  }

  return 0;
}
