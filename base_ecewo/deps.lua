return {
    config = { dir = 'deps' },
    {
        'ecewo/ecewo',
        tag = 'v4.1.0',
    },
    {
        'ecewo/ecewo-static',
        tag = 'v0.2.0',
    },
    {
        'ecewo/ecewo-fs',
        tag = 'v0.2.0',
    },
    -- ecewo's CMake normally FetchContent's these at configure time; we vendor
    -- them and point FETCHCONTENT_SOURCE_DIR_* at deps/ in CMakeLists.txt.
    {
        'libuv/libuv',
        tag = 'v1.52.0',
        flatten = false,
        dest = 'deps/libuv',
        files = {
            'include/**',
            'src/**',
            'CMakeLists.txt',
            'configure.ac',
            'libuv-static.pc.in',
        },
    },
    {
        -- Must be the release tarball, not the git repo: the tarball ships the
        -- pre-generated llhttp.c that the repo only produces via npm.
        name = 'llhttp',
        url = 'https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.4.1.tar.gz',
    },
    {
        -- slog.h lives at base/slog.h in the cbase repo; flatten it to
        -- deps/slog.h so #include "slog.h" resolves via include_directories(deps).
        'timwmillard/cbase',
        dest = 'deps',
        flatten = true,
        files = {
            'base/slog.h',
        },
    }
}
