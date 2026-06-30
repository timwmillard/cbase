#include <stdio.h>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimplot.h"
#include "sokol_imgui.h"

static struct {
   sg_pass_action pass_action;

   struct ImPlotContext *ctx_plot;

   bool show_demo_window;
   bool show_demo_plot_window;
} state = {0};

void init(void) {
   sg_setup(&(sg_desc){
       .environment = sglue_environment(),
       .logger.func = slog_func,
   });

   simgui_setup(&(simgui_desc_t){0});

   state.pass_action = (sg_pass_action){
       .colors[0] =
           {
               .load_action = SG_LOADACTION_CLEAR,
               .clear_value = {0.2, 0.2, 0.2, 1.0},
           },
   };
   state.ctx_plot = ImPlot_CreateContext();

   state.show_demo_window = true;
   state.show_demo_plot_window = true;
}

void save() {
}

static char buf[512];

void gui_draw() {
   if (state.show_demo_window) {
      igShowDemoWindow(&state.show_demo_window);
   }
   if (state.show_demo_plot_window) {
      ImPlot_ShowDemoWindow(&state.show_demo_plot_window);
   }
}

void frame(void) {
   simgui_new_frame(&(simgui_frame_desc_t){
       .width = sapp_width(),
       .height = sapp_height(),
       .delta_time = sapp_frame_duration(),
       .dpi_scale = sapp_dpi_scale(),
   });

   gui_draw();

   sg_begin_pass(&(sg_pass){
       .action = state.pass_action,
       .swapchain = sglue_swapchain(),
   });

   simgui_render();

   sg_end_pass();
   sg_commit();
}

void cleanup(void) {
   simgui_shutdown();
   sg_shutdown();
}

void event(const sapp_event *ev) {
   // Development only
   if (ev->type == SAPP_EVENTTYPE_KEY_DOWN &&
       ev->key_code == SAPP_KEYCODE_ESCAPE) {
      sapp_quit();
   }

   simgui_handle_event(ev);
}

sapp_desc sokol_main(int argc, char *argv[]) {
   return (sapp_desc){
       .init_cb = init,
       .frame_cb = frame,
       .cleanup_cb = cleanup,
       .event_cb = event,
       .width = 800,
       .height = 600,
       .window_title = "ImGui Demo with sokol",
   };
}
