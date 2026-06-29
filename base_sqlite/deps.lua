return {
    config = { dir = 'deps' },
    {
        'timwmillard/cbase',
        files = { 'lib/base.h' },
    },
    {
        'https://sqlite.org/2026/sqlite-amalgamation-3530300.zip',
        files = { 'sqlite3.c', 'sqlite3.h' },
    },
}
