#include <stdio.h>

// deps
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_time.h"

#include "nanovg.h"
#include "sokol_nanovg.h"

static struct {
    NVGcontext* vg;
} state;

void draw(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRect(vg, 100, 100, 120, 30);
    nvgCircle(vg, 120, 120, 5);
    nvgPathWinding(vg, NVG_HOLE);   // Mark circle as a hole.
    nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
    nvgFill(vg);
}

void frame()
{
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = {0.2f, 0.2f, 0.2f, 1.0f},
            },
        },
        .swapchain = sglue_swapchain(),
    });

    nvgBeginFrame(state.vg, sapp_width(), sapp_height(), 1.0f);
    // draw with NanoVG API...
    draw(state.vg);
    nvgEndFrame(state.vg);

    sg_end_pass();
    sg_commit();
}

void event(const sapp_event* ev)
{
    switch (ev->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                sapp_quit();
            }
            break;
        default:
            break;
    }
}

void init()
{
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
    });

    state.vg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
}

void cleanup()
{
    nvgDeleteSokol(state.vg);
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    return (sapp_desc) {
        .width = 640,
        .height = 480,
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
    };
}

