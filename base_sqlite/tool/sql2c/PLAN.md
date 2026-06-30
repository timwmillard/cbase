# sql2c — plan

A SQL→C code generator (the successor to the Go `sql2c_go`), rewritten in C. It
reads a schema and a set of named queries and emits typed C structs and query
functions for SQLite.

The defining idea: **SQLite is the parser.** Instead of vendoring a third-party
SQL grammar (the Go version's `rqlite/sql`), we load the schema into an in-memory
database and ask SQLite to analyze everything via its introspection APIs. This
was validated by `tool/spike/spike.c` — see "Spike findings" below.

## Goals

- Drop the Go toolchain dependency entirely.
- **Standalone**: the tool's only dependency is `sqlite3.c` + `sqlite3.h`. No
  `base.h`, no third-party libraries, no scripting runtime.
- Ship its own `CMakeLists.txt` that builds the tool *and* exposes a helper the
  parent project can call to run codegen.
- More correct output than `sql2c_go`: joins, column subsets, aliases, and
  expression columns handled via real SQLite column metadata.
- Preserve the existing generated-code shape (`models.h` / `queries.h` /
  `queries.c`, the `sql_*` type model) so existing consumers keep working.

## Non-goals

- General-purpose SQL parsing. If SQLite can't prepare it, we don't support it.
- A configuration language. Config is a flat `key = value` file (below).
- Backwards-compat with the Go tool's internals. `sql2c_go` will be removed once
  `sql2c` reaches parity.

## Spike findings (already validated)

From `tool/spike/spike.c` run against `sql/schema.sql` + `sql/queries.sql`:

- `PRAGMA table_info` + `sqlite_master` fully replace CREATE TABLE parsing
  (names, declared types, `notnull`, `pk`, defaults) — and the regex hack that
  stripped triggers/DML disappears.
- `sqlite3_column_{name,decltype,table_name,origin_name}` give exact per-column
  source info, including for `RETURNING *`, subset selects, and **joins**
  (each result column reports its true origin table). Requires the amalgamation
  be compiled with `-DSQLITE_ENABLE_COLUMN_METADATA`.
- Expression columns (`count(*)`, `max(age)`) report a name but no origin table
  and `decltype = NULL` → the signal for "computed scalar, not a table field."
- Params: `sqlite3_bind_parameter_count` gives the count; **named** params
  (`:age`) are returned by `sqlite3_bind_parameter_name`, positional `?` are not.
  → Named params are the path to typed bindings (see "Parameters").

## Dependencies

- `sqlite3.c`, `sqlite3.h` — provided by the consumer (or vendored locally for
  standalone builds). Compiled with `SQLITE_ENABLE_COLUMN_METADATA`.
- A C11 compiler. Nothing else.

The generator carries a ~50-line internal arena + dynamic string builder so it
needs no external memory/string helpers. (This is the tool's *own* memory
management; it is unrelated to whatever the generated code uses.)

## Directory layout

```
tool/sql2c/
  PLAN.md            # this file
  CMakeLists.txt     # builds the tool + exposes sql2c_generate()
  sql2c.c            # the generator (single translation unit)
  README.md          # usage (later)
  sqlite/            # OPTIONAL vendored sqlite3.c/.h for standalone builds
```

Single translation unit on purpose — keeps "standalone" literally true and the
build trivial. Internal sections: arena/strbuf, config, schema introspection,
query introspection, type mapping, emit-models, emit-header, emit-impl, main.

## Pipeline

1. **Load config** (flat `key = value`). Resolve input/output paths.
2. **Open** an in-memory db; `sqlite3_exec` the schema file.
3. **Introspect schema**: `SELECT name FROM sqlite_master WHERE type='table'
   AND name NOT LIKE 'sqlite_%'`, then `PRAGMA table_info(<t>)` per table →
   build `Table{ Column{ name, decltype, notnull, pk } }`.
4. **Parse queries file**: split on `-- name:` markers (same convention as
   today), extract `name`, `:type` (one|many|exec), and the SQL body.
5. **Introspect each query**: `sqlite3_prepare_v2` against the loaded schema.
   - Result columns ← `column_count` + `column_name/decltype/table_name/
     origin_name`. Columns with an origin table map to that table's typed field;
     columns without one are computed scalars (typed from `decltype`/affinity,
     defaulting to text).
   - Params ← `bind_parameter_count` + `bind_parameter_name` (see below).
   - Prepare failure = hard error with `sqlite3_errmsg` (a real SQL error,
     surfaced at generation time instead of compile time).
6. **Emit** `models.h`, `<output>.h`, `<output>.c`.

## Parameters

SQLite ties a param to a name but not to a column. Strategy:

- **Named params (`:age`) — recommended.** Strip the leading `:`, look the name
  up in the involved tables' `table_info` to get the type. If the name is unique
  across the query's tables, use it; on ambiguity, support a qualified form
  (`:person_age` → table `person`, column `age`) before falling back to text.
- **Positional `?`** — type is unknown from the API. Inferred from context where
  cheap (INSERT/UPDATE bind in column order, so map by position to the target
  column list); otherwise default to `sql_text` and emit a `// TODO: type` note.

The generated function signatures are unchanged from `sql2c_go`: 0 params →
bare, 1 param → inline arg, >1 → a `<Name>Params` struct.

## Type mapping

Same `sql_*` model as `sql2c_go` (keep `models.h` byte-compatible where
possible): declared type substring → base type (`int`→`sql_int64`,
`text/char`→`sql_text`, `real/double/float`→`sql_double`, `blob`→`sql_blob`,
`bool`→`sql_bool`, `numeric`→`sql_numeric`, default `sql_text`); `notnull == 0`
→ nullable (`sql_null*` tagged struct).

## Generated code

Reproduce the current output shape and fix the known bugs found in review:

- Drop the dead/leaking `malloc` array in the `:many` callback path.
- Cast `sqlite3_column_text` consistently for the nullable-text path.
- Keep the borrowed-pointer semantics for the callback style, but document in
  the generated header that `sql_text`/`sql_blob` views are valid only for the
  duration of the callback.

### Result styles — always generate both (no config toggle)

Every query emits two functions: a callback *primitive* and an allocating
*wrapper* built on top of it. There is no `result-style` config option.

- **`<name>_cb`** — the primitive: streams rows to `void (*cb)(Row*, void*)`.
  Zero allocation, borrowed string views (valid only for the duration of the
  callback). This is the single source of truth for the SQL/step logic.
- **`<name>`** — the wrapper: installs a callback that copies each row into a
  caller-supplied allocator and returns owned data. Deep-copies `sql_text`/
  `sql_blob` bytes so rows outlive `finalize`.

  ```c
  // :many  — primitive + owning wrapper
  int         get_people_cb(sqlite3 *db, void (*cb)(Person*, void*), void *ctx);
  PersonSlice get_people(sql_allocator a, sqlite3 *db);

  // :one
  int     get_person_cb(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx);
  Person *get_person(sql_allocator a, sqlite3 *db, sql_int64 id); // NULL = no row
  ```

The wrapper's callback (1) appends the struct to a growing array and (2)
deep-copies each text/blob field into the allocator (`alloc(len+1)`, copy,
NUL-terminate). `:many` grows the array by doubling via the allocator — the same
bounded-waste pattern `base.h`'s `list` uses in an arena.

### The allocator interface

The wrapper takes a generic single-function allocator, defined once in the
generated `models.h`:

```c
typedef struct sql_allocator {
    void *(*alloc)(void *ctx, size_t size);
    void *ctx;
} sql_allocator;
```

Passed by value (two pointers), allocator-first by convention. One function is
enough: array growth uses alloc-and-`memcpy`, string copies use alloc; no
`free`/`realloc` needed because the cleanup model is "caller resets/releases
their own arena" (matches `base.h`).

Rationale for the function-pointer interface over a config-defined arena
(`arena-include`/`arena-type`/`arena-alloc`): allocators differ in call shape
(`arena_alloc(a,n)` vs `xalloc(n,a)` vs alignment args), so a config binding
would need a call *template* — reintroducing the config-as-language complexity
this design rejects — and would make generated code `#include` and depend on a
header the generator cannot verify. The function-pointer interface keeps
generated code self-contained (only the tiny typedef above) and works with any
allocator via a 3-line adapter:

```c
static void *arena_alloc_fn(void *ctx, size_t n) { return arena_alloc((arena *)ctx, n); }
sql_allocator A = { arena_alloc_fn, my_arena };
PersonSlice ppl = get_people(A, db);
```

Cost: one indirect call per allocation, amortized by bulk (doubling) array
growth — O(log n) array allocs + one per string. For a monomorphic hot path,
call the `_cb` primitive directly with your own arena-writing callback.

Caveat: this assumes arena/reset-style cleanup. A `malloc`-per-row /
free-each-row model would need a `free` in the interface (or generated
`free_*` helpers) — out of scope; not the `base.h` memory model.

## Config format

Flat `key = value`, `#` comments, whitespace-trimmed. ~30-line parser, no deps.
Keys mirror today's YAML:

```
schema       = schema.sql
queries      = queries.sql
output       = ../src/queries.h
struct-style = pascal
field-style  = snake
func-style   = snake
type-prefix  =
func-prefix  =
```

CLI flags may override any key (`-schema`, `-output`, …) for ad-hoc runs.

## CMake integration

`tool/sql2c/CMakeLists.txt` works both standalone and as a subdirectory.

- Cache variables let the consumer point at its sqlite amalgamation:
  - `SQL2C_SQLITE_C`      — path to `sqlite3.c` (default: `sqlite/sqlite3.c`).
  - `SQL2C_SQLITE_INCLUDE`— dir with `sqlite3.h` (default: `sqlite/`).
- Builds `add_executable(sql2c sql2c.c ${SQL2C_SQLITE_C})` with
  `SQLITE_ENABLE_COLUMN_METADATA` defined on the sqlite source.
- Exposes a helper function so the parent drives codegen without knowing
  internals:

  ```cmake
  # Runs the built tool; wires DEPENDS/OUTPUT for incremental builds.
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

Parent `CMakeLists.txt` usage:

```cmake
set(SQL2C_SQLITE_C       ${CMAKE_SOURCE_DIR}/deps/sqlite3.c CACHE FILEPATH "")
set(SQL2C_SQLITE_INCLUDE ${CMAKE_SOURCE_DIR}/deps           CACHE PATH "")
add_subdirectory(tool/sql2c)
sql2c_generate(CONFIG ... OUTPUTS ... DEPENDS ...)
```

This replaces both the `go build` step and the current hand-written
`add_custom_command` blocks for codegen.

## Implementation phases

1. **Skeleton + CMake.** Single-file tool that opens an in-memory db, builds,
   and prints "ok". `CMakeLists.txt` builds standalone and as a subdir.
   Internal arena + strbuf.
2. **Config parser** (flat key=value + CLI overrides).
3. **Schema introspection** via `sqlite_master` + `table_info`; emit
   `models.h`. Diff against current `models.h` for parity.
4. **Query introspection** + `_cb` primitive emit for `models.h`/`queries.h`/
   `queries.c`. Diff the generated `queries.*` against `sql2c_go` output on the
   demo and on `tool/sql2c_go/example`.
5. **Allocator wrappers**: emit the `sql_allocator` typedef + the owning
   `<name>` wrappers (slice for `:many`, pointer for `:one`) over the `_cb`
   primitives. Verify with a `base.h` arena adapter.
6. **Wire into parent CMake**; remove the Go build step. Verify `make run`.
7. **Remove `sql2c_go`** once parity is confirmed.

## Testing / parity

- Golden comparison: run both `sql2c_go` and `sql2c` on `sql/` and on
  `tool/sql2c_go/example/`, diff the generated files. Differences should only be
  improvements (joins/subsets/exprs) — review each.
- `make run` on `base_sqlite` must still print the expected row.
- Error cases: a query that fails to prepare must abort with a clear message.

## Open questions / risks

- **Param type inference for positional `?`** is the one genuinely fuzzy area.
  Mitigation: recommend named params; infer INSERT/UPDATE positionals from
  target columns; default the rest to text with a visible TODO.
- **`models.h` exact byte-parity** with `sql2c_go` may not be achievable if
  column ordering/typing improves; acceptable as long as consumers compile.
- **sqlite source location** for truly standalone builds — vendor a copy under
  `sqlite/`, or require the consumer to set `SQL2C_SQLITE_C`. Default to the
  vendored copy so `cmake . && make` works with zero configuration.
```
