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
  src/
    main.c        # demo program; queries.h/.c and models.h are generated here
  CMakeLists.txt
```

Build and run:

```sh
cd basic
cmake -B build -S .
cmake --build build
./build/basic_example
```

`cmake --build` runs `sql2c` first to (re)generate `src/queries.h`,
`src/queries.c`, and `src/models.h` from `sql/schema.sql` and
`sql/queries.sql`, then builds `basic_example` against them. Edit either SQL
file and rebuild to see the generated code change.

The example borrows the SQLite amalgamation already vendored under
`base_sqlite/deps/` in this repo. Outside this repo, point
`SQL2C_SQLITE_C`/`SQL2C_SQLITE_INCLUDE` at your own copy instead — see
`../README.md`.
