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
        'yappologistic/CUI',
        tag = 'v1.5.0',
        files = {
            'CMakeLists.txt',
            'LICENSE',
            'cmake/**',
            'include/**',
            'src/**',
            'third_party/**',
        },
    },
}
