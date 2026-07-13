-- jobq's dependencies. Goal: zero *system* dependencies beyond a C compiler —
-- lua and sqlite3 are vendored as source/amalgamation, and JSON handling in
-- jobs.lua comes from a pure-Lua library instead of lua-cjson (a C extension
-- that needs luarocks), so worker Lua states need nothing beyond stock Lua.
return {
    config = {
        dir = 'deps',
        subdir = false,
        flatten = true,
    },
    {
        -- Official lua.org amalgam (not the lua/lua git mirror): the archive's
        -- single top dir is auto-stripped, then the whole tree is vendored to
        -- deps/lua-5.5.0 so a -Ideps/lua-5.5.0/src path stays put.
        url = 'https://www.lua.org/ftp/lua-5.5.0.tar.gz',
        name = 'lua',
        dest = 'deps/lua-5.5.0',
    },
    {
        url = 'https://sqlite.org/2026/sqlite-amalgamation-3530300.zip',
        files = { 'sqlite3.c', 'sqlite3.h' },
    },
    {
        -- Pure-Lua JSON (David Kolf's dkjson): single file, no C deps, decode
        -- returns nil+err on bad input like cjson.safe. Lands next to jobs.lua
        -- (not deps/) — it's embedded into the binary at build time (see
        -- CMakeLists.txt), not require()'d off disk at runtime.
        'LuaDist/dkjson',
        files = { 'dkjson.lua' },
        dest = '.',
    },
    {
        -- embedc: embeds .lua source into C headers at build time (same
        -- pattern as base_sqlite/deps.lua). Local monorepo checkout via
        -- `dev`, not a real fetch.
        'timwmillard/cbase',
        dev = '..',
        name = 'cbase-tool',
        dir = '.',
        flatten = false,
        files = { 'tool/embedc/embedc.c', 'tool/embedc/CMakeLists.txt' },
    },
}
