return {
    config = { dir = 'deps' },
    {
        'raysan5/raylib',
        tag = '6.0',
        flatten = false,
        dest = 'deps/raylib',
        files = {
            'src/**',
            'cmake/**',
            'CMakeLists.txt',
            'CMakeOptions.txt',
            'README.md',
            'LICENSE',
            'raylib.pc.in',
        },
    },
}
