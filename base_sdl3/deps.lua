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
        -- Only what CMake needs to configure + build SDL3-static: the root
        -- CMakeLists.txt, its cmake/ modules & .in templates, all headers,
        -- and all sources (platform backends are selected at configure time,
        -- so every src/** file must stay). Skips test/, examples/, docs/,
        -- Xcode/, VisualC*/, android-project/, wayland-protocols/, etc.
        'https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz',
        files = {
            'CMakeLists.txt',
            'LICENSE.txt',
            'cmake/**',
            'include/**',
            'src/**',
        },
    },
}
