return {
    config = { dir = 'deps' },
    {
        'floooh/sokol',
        files = {
            'sokol_app.h', 'sokol_gfx.h', 'sokol_glue.h',
            'sokol_log.h',
            'util/sokol_imgui.h'
        },
    },
    {
        "cimgui/cimgui",
        branch = "docking_inter",
        submodules = true,
        dest = "deps/cimgui",
        flatten = false,
        files = {
            "cimconfig.h", "cimgui.h", "cimgui.cpp",
            "imgui/imconfig.h", "imgui/imgui.h", "imgui/imgui.cpp", "imgui/imgui_demo.cpp",
            "imgui/imgui_draw.cpp", "imgui/imgui_internal.h", "imgui/imgui_tables.cpp",
            "imgui/imgui_widgets.cpp", "imgui/imstb_rectpack.h", "imgui/imstb_textedit.h",
            "imgui/imstb_truetype.h",
        },
    },
}
