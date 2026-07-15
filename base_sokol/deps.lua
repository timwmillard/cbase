return {
    config = {
        dir = 'deps',
    },
    {
        'floooh/sokol',
        subdir = '',
        flatten = true,
        files = { 'sokol_app.h', 'sokol_gfx.h', 'sokol_glue.h' },
    },
}
