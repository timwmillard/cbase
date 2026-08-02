# sql2c examples

## basic

A fishing-tournament schema (`boat` / `competitor` / `catch`) that exercises
`:one`, `:many`, and `:exec` queries, single- and multi-parameter queries, and
nullable result columns.

```
basic/
  sql/
    schema.sql    # tables sql2c introspects
    queries.sql   # named queries
    config.txt    # sql2c config (naming styles, paths)
    manifes.json  # generated: query manifest for other codegen (e.g. csql2lua)
  src/
    main.c        # demo program; queries.h/.c are generated here
  lua/
    config.txt    # csql2lua config
    basicdb.lua.c # generated: Lua C module source
    test.lua      # exercises the built basicdb.so from Lua
  CMakeLists.txt
```

Build and run:

```sh
cd basic
cmake -B build -S .
cmake --build build
./build/basic_example
```

`cmake --build` runs `sql2c` first to (re)generate `src/queries.h` and
`src/queries.c` from `sql/schema.sql` and `sql/queries.sql`, then builds
`basic_example` against them. `queries.h` contains everything — base types,
allocator interface, table structs, and query declarations — so the demo only
needs `#include "queries.h"`. Edit either SQL file and rebuild to see the
generated code change.

The example borrows the SQLite amalgamation already vendored under
`base_sqlite/deps/` in this repo. Outside this repo, point
`SQL2C_SQLITE_C`/`SQL2C_SQLITE_INCLUDE` at your own copy instead — see
`../README.md`.

If Lua is found via `pkg-config` at configure time, the build also runs
[`csql2lua`](../../csql2lua/README.md) against `sql/manifes.json` and builds
`basicdb.so`, a loadable Lua C module wrapping the same queries:

```sh
lua lua/test.lua
```

(See `../../csql2lua/README.md` for how the Lua-side API looks.) If Lua isn't
found, this step is skipped and `basic_example` still builds fine.
