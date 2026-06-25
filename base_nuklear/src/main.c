#include <stdio.h>

// deps
#include "nuklear.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_nuklear.h"

void draw(struct nk_context *ctx) {

   enum { EASY, HARD };
   static int op = EASY;
   static float value = 0.6f;
   static int i = 20;

   if (nk_begin(ctx, "Show", nk_rect(50, 50, 220, 220),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
      /* fixed widget pixel width */
      nk_layout_row_static(ctx, 30, 80, 1);
      if (nk_button_label(ctx, "button")) {
         /* event handling */
      }

      /* fixed widget window ratio width */
      nk_layout_row_dynamic(ctx, 30, 2);
      if (nk_option_label(ctx, "easy", op == EASY))
         op = EASY;
      if (nk_option_label(ctx, "hard", op == HARD))
         op = HARD;

      /* custom widget pixel width */
      nk_layout_row_begin(ctx, NK_STATIC, 30, 2);
      {
         nk_layout_row_push(ctx, 50);
         nk_label(ctx, "Volume:", NK_TEXT_LEFT);
         nk_layout_row_push(ctx, 110);
         nk_slider_float(ctx, 0, &value, 1.0f, 0.1f);
      }
      nk_layout_row_end(ctx);
   }
   nk_end(ctx);
}

void frame() {
   const int width = sapp_width();
   const int height = sapp_height();

   sg_begin_pass(&(sg_pass){
       .action =
           {
               .colors[0] =
                   {
                       .load_action = SG_LOADACTION_CLEAR,
                       .clear_value = {0.2f, 0.2f, 0.2f, 1.0f},
                   },
           },
       .swapchain = sglue_swapchain(),
   });

   struct nk_context *ctx = snk_new_frame();
   draw(ctx);

   snk_render(width, height);
   sg_end_pass();
   sg_commit();
}

void event(const sapp_event *ev) {
   snk_handle_event(ev);
   if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
      if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
         sapp_quit();
      }
   }
}

void init() {
   sg_setup(&(sg_desc){
       .environment = sglue_environment(),
   });

   snk_setup(&(snk_desc_t){
       .enable_set_mouse_cursor = true,
       .dpi_scale = sapp_dpi_scale(),
   });
}

void cleanup() {
   sg_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[]) {
   return (sapp_desc){
       .width = 640,
       .height = 480,
       .init_cb = init,
       .frame_cb = frame,
       .cleanup_cb = cleanup,
       .event_cb = event,
   };
}
