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
        -- source tree built with its own CMake (add_subdirectory), needed for the Metal renderer
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
