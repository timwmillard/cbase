# csql2lua

Generates a traditional Lua C module (`luaopen_*`, `lua_CFunction` wrappers)
from a [sql2c](../sql2c/README.md) `manifest.json`. Given a manifest, it emits
one `.c` file that, once compiled and linked against Lua, SQLite, and the
sql2c-generated `queries.c`, gives you a `require()`-able module exposing
every `:one`/`:many`/`:exec` query as a method on a connection object.

It is a separate tool from `sql2c` on purpose: `sql2c` stays dependency-free
(only the SQLite amalgamation), while this tool depends on Lua's headers —
splitting them keeps either one usable without the other.

## How it works

`csql2lua` doesn't touch SQL or SQLite metadata itself — it only reads the
JSON `sql2c` already produced. For each query it emits a `lua_CFunction` that:

- binds to sql2c's `<func>_cb` callback primitive (not the owning wrapper) —
  Lua's C API copies strings the instant they're pushed
  (`lua_pushlstring`), so there's no need to replay sql2c's
  `sql_allocator`/deep-copy machinery here; the callback pushes straight into
  a Lua table while the row from SQLite is still valid.
- for `:one`, returns a table of the row, or `nil` if no row was found.
- for `:many`, returns a 1-based array table of row tables.
- for `:exec`, returns `true`, or raises a Lua error via `sqlite3_errmsg` on
  failure.
- for `:one`/`:many`, a genuine SQLite error (not just "no rows") also raises.

Nullable SQL columns come back as Lua `nil` for that field — which, assigned
into a table, just means the key is absent (`pairs()` won't see it).

## Building

```sh
cc -std=c11 -o csql2lua csql2lua.c
```

No dependencies beyond libc — it's a self-contained JSON reader (tailored to
`sql2c`'s manifest shape, not a general-purpose parser) plus a C code emitter.
Lua/SQLite headers are only needed later, to compile what it *generates*.

## Usage

```sh
csql2lua -config config.txt
```

### Config

```
# config.txt
manifest       = ../sql/manifest.json
module         = mydb
queries_header = ../src/queries.h
output         = mydb.lua.c
```

| key | meaning | default |
|---|---|---|
| `manifest` | path to the sql2c manifest.json | `manifest.json` |
| `module` | Lua module name — becomes `luaopen_<module>` and the identifier prefix for every generated symbol | `queries` |
| `queries_header` | path to the sql2c-generated header, `#include`d verbatim in the output | `queries.h` |
| `output` | generated `.c` path | `<module>.lua.c` |

Paths are resolved relative to the working directory `csql2lua` runs in (and
`queries_header` is emitted as a literal `#include "..."`, so keep it
relative to `output`'s directory).

## The generated Lua API

Given queries named `GetBoat` (`:one`), `ListBoats` (`:many`), and
`DeleteBoat` (`:exec`) with `func-style = snake` in sql2c's own config (so
`sql2c` itself names them `get_boat` / `list_boats` / `delete_boat`):

```lua
local mydb = require("mydb")

local db = mydb.open("boats.sqlite")   -- sqlite3_open; raises on failure

local boat = db:get_boat(1)             -- table, or nil if not found
local boats = db:list_boats()           -- array of tables
local ok = db:delete_boat(1)            -- true, or raises

db:close()                              -- also runs automatically via __gc
```

- `module.open(path)` opens a connection (`sqlite3_open` under the hood) and
  returns a userdata with a metatable — `tostring(db)` and garbage-collection
  (`__gc` closes it) both work.
- Every query becomes a method, named after sql2c's own generated function
  name (so it already reflects whatever `func-style`/`func-prefix` you
  configured in `sql2c`).
- Multi-parameter queries take positional Lua arguments in the same order
  sql2c bound them in (the order the `:name` placeholders first appear in the
  SQL) — not a table. Check the manifest's `params` array if you're unsure of
  the order for a given query.
- Using a connection after `close()` raises a Lua error rather than
  segfaulting.

## Building the generated module

The generated `.c` needs to be compiled and linked with:

- Lua's headers/library (`lua.h`, `lauxlib.h`, `lualib.h`)
- `sqlite3.h` / the SQLite amalgamation
- the sql2c-generated `queries.c` it wraps

as a Lua **loadable module** (`.so`/`.dylib`/`.dll`, no `lib` prefix,
`luaopen_<module>` as the entry point) — see `CMakeLists.txt` for a
`csql2lua_module()` helper, or `../sql2c/examples/basic/CMakeLists.txt` for a
complete example wired up end-to-end (`basicdb.so`, tested from
`examples/basic/lua/test.lua`).

## Limitations

- Targets Lua 5.2+ (`luaL_setfuncs`); no 5.1 compatibility shims.
- Query methods take a fixed positional argument list — there's no named/
  optional-argument support, matching sql2c's own C signatures.
- No LuaJIT `ffi.cdef` mode (yet) — this tool only emits the traditional
  `lua_CFunction` module style. A `queries.h` header is already close to
  `ffi.cdef`-ready on its own, if you want to go that route instead.
