return {
    config = {
        dir = 'deps',
        subdir = false,
        flatten = true,
    },
    {
        'vinnyhorgan/nanovg',
        submodules = true,
        files = {
            'src/**',
            'sokol/sokol_app.h', 'sokol/sokol_gfx.h', 'sokol/sokol_glue.h', 'sokol/sokol_time.h',
        },
    },
}
