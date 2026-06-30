return {
    config = {
        dir = 'deps',
        subdir = false,
        flatten = true,
    },
    {
        'timwmillard/cbase',
        files = { 'lib/base.h' },
    },
    {
        'https://sqlite.org/2026/sqlite-amalgamation-3530300.zip',
        files = { 'sqlite3.c', 'sqlite3.h' },
    },
    {
        'timwmillard/cbase',
        name = 'cbase-tool',
        dir = '.',
        flatten = false,
        files = {
            'tool/bin2c/bin2c.c',
            'tool/sql2c/sql2c.c',
            'tool/sql2c/CMakeLists.txt',
        },
    },
}
