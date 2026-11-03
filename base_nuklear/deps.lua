return {
    config = {
        dir = 'deps',
    },
    {
        'floooh/sokol',
        subdir = '',
        flatten = true,
        files = {
            'sokol_app.h', 'sokol_gfx.h', 'sokol_glue.h',
            'util/sokol_nuklear.h',
        },
    },
    {
        'Immediate-Mode-UI/Nuklear',
        subdir = '',
        flatten = true,
        files = {
            'nuklear.h',
        },
    },
}
