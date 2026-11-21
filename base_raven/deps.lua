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
        'oscar-soirey/Raven',
        -- tag = 'v0.3.0',
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
