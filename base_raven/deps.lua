return {
    config = {
        dir = 'deps',
    },
    {
        'timwmillard/cbase',
        subdir = '',
        flatten = true,
        files = { 'lib/base.h' },
    },
    {
        'RainerXE/Raven',
        branch = 'sokol-backend',
    },
    {
        'timwmillard/cbase',
        dev = '..',
        name = 'cbase-tool',
        dir = '.',
        subdir = '',
        flatten = false,
        files = {
            'tool/embedc/embedc.c',
            'tool/embedc/CMakeLists.txt',
        },
    },
}
