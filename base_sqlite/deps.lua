return {
    config = {
        dir = 'deps',
    },
    {
        'timwmillard/cbase',
        subdir = '',
        flatten = true,
        dev = '..',
        files = { 'lib/base.h' },
    },
    {
        'https://sqlite.org/2026/sqlite-amalgamation-3530300.zip',
        subdir = '',
        flatten = true,
        files = { 'sqlite3.c', 'sqlite3.h' },
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
            'tool/sql2c/sql2c.c',
            'tool/sql2c/CMakeLists.txt',
        },
    },
}
