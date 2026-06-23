#include <stdio.h>
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "sokol_imgui.h"

static struct {
    sg_pass_action pass_action;
} state = {0};

void init(void)
{

    sg_setup(&(sg_desc){
            .environment = sglue_environment(),
            .logger.func = slog_func,
            });

    simgui_setup(&(simgui_desc_t){0});

    state.pass_action = (sg_pass_action) {
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = {0.2, 0.2, 0.2, 1.0},
        },
    };
}

void save()
{
}

static char buf[512];
static float f;

void gui_draw()
{
    igText("Hello, world %d", 123);
    if (igButton("Save", (ImVec2){50, 40}))
        save();
    igInputText("string", buf, 512, 0, 0, 0);
    igSliderFloat("float", &f, 0.0f, 1.0f, 0, 0);
}

void frame(void)
{
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

    printf("%s: f = %f\n", buf, f);
}

void cleanup(void)
{
    simgui_shutdown();
    sg_shutdown();
}

void event(const sapp_event* ev)
{
    // Development only
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && ev->key_code == SAPP_KEYCODE_ESCAPE) {
        sapp_quit();
    }

    simgui_handle_event(ev);
}

sapp_desc sokol_main(int argc, char *argv[])
{
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
