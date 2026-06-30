return {
    config = {
        dir = 'deps',
        subdir = false,
        flatten = true,
    },
    {
        'floooh/sokol',
        files = {
            'sokol_app.h', 'sokol_gfx.h', 'sokol_glue.h',
            'util/sokol_nuklear.h',
        },
    },
    {
        'Immediate-Mode-UI/Nuklear',
        files = {
            'nuklear.h',
        },
    },
}
