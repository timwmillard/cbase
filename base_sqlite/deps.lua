return {
    config = { dir = 'deps' },
    {
        'timwmillard/cbase',
        files = { 'lib/base.h' },
    },
    {
        'timwmillard/cbase',
        files = {
            'tool/bin2c/bin2c.c',
            'tool/sql2c/sql2c.c',
            'tool/sql2c/CMakeLists.txt',
        },
    },
    {
        'https://sqlite.org/2026/sqlite-amalgamation-3530300.zip',
        files = { 'sqlite3.c', 'sqlite3.h' },
    },
}
