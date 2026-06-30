# sql2c

A SQL→C code generator for SQLite. Given a schema and a set of named queries, it
emits typed C structs and query functions — like `sqlc`, but for C.

It is **standalone**: the only dependency is the SQLite amalgamation
(`sqlite3.c` / `sqlite3.h`). There is no third-party SQL parser, no scripting
runtime, no build tooling beyond a C compiler.

## How it works

`sql2c` doesn't parse SQL itself — **SQLite does.** It loads your schema into an
in-memory database and then asks SQLite to describe everything:

- schema → `sqlite_master` + `PRAGMA table_info` (column names, types, nullness, PK)
- queries → `sqlite3_prepare_v2` + column metadata
  (`sqlite3_column_table_name` / `_origin_name`) for result columns, and
  `sqlite3_bind_parameter_name` for parameters.

A query that doesn't compile against the schema is a hard error at generation
time, not a mystery at compile time. Result columns are typed from their true
origin table, so joins, column subsets, and `RETURNING` all work.

> Building requires `-DSQLITE_ENABLE_COLUMN_METADATA` so the column-origin APIs
> are available. The bundled `CMakeLists.txt` sets this for you.

## Building

The tool needs an SQLite amalgamation. The CMake resolves it in order:

1. `SQL2C_SQLITE_C` / `SQL2C_SQLITE_INCLUDE` set by the consumer, or
2. a vendored copy under `tool/sql2c/sqlite/` (`sqlite3.c`, `sqlite3.h`).

Standalone:

```sh
# point at an existing amalgamation...
cmake -B build -DSQL2C_SQLITE_C=/path/to/sqlite3.c -DSQL2C_SQLITE_INCLUDE=/path/to
cmake --build build
./build/sql2c -config config.txt
```

Or just compile it directly:

```sh
cc -DSQLITE_ENABLE_COLUMN_METADATA -I/path/to sql2c.c /path/to/sqlite3.c -o sql2c
```

## Usage

```sh
sql2c -config config.txt
```

Any config key can be overridden on the command line (`-output ...`,
`-field-style camel`, …).

### Config

A flat `key = value` file; `#` starts a comment.

```
# config.txt
schema       = schema.sql
queries      = queries.sql
output       = ../src/queries.h
struct-style = pascal
field-style  = snake
func-style   = snake
type-prefix  =
func-prefix  =
```

| key | meaning | default |
|---|---|---|
| `schema` | path to the schema SQL | `schema.sql` |
| `queries` | path to the queries SQL | `queries.sql` |
| `output` | path of the generated header (`.c` and `models.h` go alongside) | `queries.h` |
| `struct-style` | `snake` \| `camel` \| `pascal` for struct names | `pascal` |
| `field-style` | naming for struct fields / params | `pascal` |
| `func-style` | naming for function names | `snake` |
| `type-prefix` | prefix prepended to type names | (none) |
| `func-prefix` | prefix prepended to function names | (none) |

Paths in the config are resolved relative to the working directory `sql2c` runs
in.

### Queries

Each query is introduced by a `-- name:` annotation giving a name and a kind:

```sql
-- name: CreatePerson :one
insert into person (name, age) values (:name, :age) returning *;

-- name: GetPeople :many
select * from person;

-- name: DeletePerson :exec
delete from person where id = :id;
```

- `:one` — returns a single row (must produce result columns; use a `SELECT` or
  a `RETURNING` clause).
- `:many` — returns zero or more rows.
- `:exec` — runs a statement with no result.

**Use named parameters** (`:name`, `:id`). SQLite reports their names, so
`sql2c` can type them by looking the name up in the schema. Positional `?`
parameters carry no name and default to `sql_text` with a warning.

## Generated output

Three files are produced next to `output`:

- **`models.h`** — the `sql_*` base types (`sql_text`, `sql_int64`, nullable
  variants, …), one struct per table, and the allocator interface.
- **`queries.h`** — result-slice typedefs, parameter structs, and function
  declarations.
- **`queries.c`** — the implementations.

For each query you get two functions.

### Callback primitive (`<name>_cb`)

Zero allocation; streams each row to a callback. String/blob fields are
**borrowed** — valid only for the duration of the callback.

```c
int get_people_cb(sqlite3 *db, void (*cb)(Person *, void *), void *ctx);

static void print_person(Person *p, void *ctx) {
    (void)ctx;
    printf("%.*s (%lld)\n", (int)p->name.len, (char *)p->name.data, (long long)p->age);
}
get_people_cb(db, print_person, NULL);
```

### Owning wrapper (`<name>`)

Built on the `_cb` primitive: copies rows — and deep-copies their text/blob
bytes — into an allocator you supply, so the results outlive the statement.

```c
Person     *get_person(sql_allocator a, sqlite3 *db, sql_int64 id); // NULL if no row
PersonSlice get_people(sql_allocator a, sqlite3 *db);               // { items, len }
```

`:exec` queries have no result and so get only the plain function:

```c
int delete_person(sqlite3 *db, sql_int64 id);
```

### The allocator

`sql_allocator` is a single function plus a context pointer (defined in
`models.h`). Adapt any allocator with a few lines — here, an arena:

```c
static void *arena_alloc_fn(void *ctx, size_t n) { return arena_alloc((arena *)ctx, n); }

arena scratch = {0};
sql_allocator a = { arena_alloc_fn, &scratch };

PersonSlice people = get_people(a, db);   // owned; survives sqlite3_finalize/close
// ... use people ...
arena_release(&scratch);                  // frees everything at once; no per-row free
```

Cleanup is the caller's job (reset/release the arena). There is no `free` in the
interface by design.

## CMake integration

The `CMakeLists.txt` works as a subdirectory and exposes a helper so a consuming
project can run codegen as part of its build:

```cmake
set(SQL2C_SQLITE_C       ${CMAKE_SOURCE_DIR}/deps/sqlite3.c CACHE FILEPATH "")
set(SQL2C_SQLITE_INCLUDE ${CMAKE_SOURCE_DIR}/deps           CACHE PATH "")
add_subdirectory(tool/sql2c)

sql2c_generate(
    CONFIG  ${CMAKE_SOURCE_DIR}/sql/config.txt
    WORKDIR ${CMAKE_SOURCE_DIR}/sql
    OUTPUTS ${CMAKE_SOURCE_DIR}/src/queries.h
            ${CMAKE_SOURCE_DIR}/src/queries.c
            ${CMAKE_SOURCE_DIR}/src/models.h
    DEPENDS ${CMAKE_SOURCE_DIR}/sql/schema.sql
            ${CMAKE_SOURCE_DIR}/sql/queries.sql
)
```

`sql2c_generate` wires up `OUTPUT`/`DEPENDS` so the generator re-runs only when
the schema, queries, or tool change.

## Limitations

- Positional `?` parameters can't be typed from SQLite metadata; prefer named
  parameters. Positionals default to `sql_text`.
- A multi-table join result is currently typed as the first column's table
  struct rather than a synthesized row struct.

See `PLAN.md` for design rationale and the implementation roadmap.
