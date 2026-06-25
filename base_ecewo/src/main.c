#include <stdio.h>

#include "ecewo.h"
#include "slog.h"

void hello_world(ecewo_request_t *req, ecewo_response_t *res) {
   slog_info("Hello, World!");
   ecewo_send_text(res, ECEWO_OK, "Hello, World!");
}

int main(void) {

   slog_handler *handler = slog_color_text_handler_new(stdout, SLOG_INFO);
   slog_logger *logger = slog_new(handler);
   slog_set_default(logger);

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
