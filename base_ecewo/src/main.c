#include <stdio.h>

#include "ecewo-static.h"
#include "ecewo.h"
#include "slog.h"

int main(void) {
   slog_handler *handler = slog_color_text_handler_new(stdout, SLOG_INFO);
   slog_logger *logger = slog_new(handler);
   slog_set_default(logger);
   ecewo_app_t *app = ecewo_create();
   ecewo_static_init();

   // Serve ./public at the root URL
   if (ecewo_serve_static(app, "/", "./public", NULL) != 0) {
      fprintf(stderr, "Failed to mount static directory\n");
      return 1;
   }

   /*
    * GET /               -> ./public/index.html
    * GET /about.html     -> ./public/about.html
    * GET /css/style.css  -> ./public/css/style.css
    * GET /js/app.js      -> ./public/js/app.js
    * GET /img/logo.png   -> ./public/img/logo.png
    */

   slog_info("Server starting");

   // ecewo_atexit(app, ecewo_static_cleanup, NULL);
   ecewo_listen(app, 3000);
   return 0;
}
