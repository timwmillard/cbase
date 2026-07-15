return {
    config = {
        dir = 'deps',
        flatten = true,
    },
    {
        'vinnyhorgan/nanovg',
        submodules = true,
        subdir = '',
        files = {
            'src/**',
            'sokol/sokol_app.h', 'sokol/sokol_gfx.h', 'sokol/sokol_glue.h', 'sokol/sokol_time.h',
        },
    },
}
