return {
    config = {
        dir = 'deps',
    },
    {
        'timwmillard/cbase',
        files = { 'lib/base.h' },
        subdir = false,
        flatten = true,
    },
    {
        'libuv/libuv',
        files = {
            'include/**',
            'src/**',
            'CMakeLists.txt',
            'configure.ac',
            'libuv-static.pc.in',
        },
    },
    {
        'eduardsui/tlse',
        tag = 'v1.0.7',
    },
}
