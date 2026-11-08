#include <stdio.h>

#include <Raven.h>

#include "gui_xml.h"

static int count = 0;
static Rid count_label;

static void on_increment(Rid sender, const char *action, void *user_data) {
   char text[32];
   snprintf(text, sizeof(text), "Count: %d", ++count);
   Rlabel_set_text(count_label, text);
}

int main(int argc, char *argv[]) {
   Rinit(R_BACKEND_SOKOL);

   Raction_bind("counter.increment", on_increment, NULL);

   Rid window = Rwindow_create_from_xml(gui_xml_data, gui_xml_len);
   if (window == R_INVALID_ID) {
      Rshutdown();
      return 1;
   }

   count_label = Rwidget_find(window, "count");

   while (Rwait_frame(-1.0)) {
   }

   Rshutdown();

   return 0;
}
