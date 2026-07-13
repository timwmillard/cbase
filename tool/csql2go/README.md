# csql2go

Generates a cgo-based Go package from a [sql2c](../sql2c/README.md)
`manifest.json`. Given a manifest, it emits one `.go` file that, once built
alongside the sql2c-generated `queries.h`/`queries.c`, gives you a typed
`*DB` with a method per query (`db.GetBoat(id)`, `db.ListBoats()`,
`db.DeleteCompetitor(id)`, …) — no hand-written bindings, and (unlike the
Lua module) no separate build tool needed to produce the result: the
generated file builds with plain `go build`/`go test`.

It is a separate tool from `sql2c` on purpose, same as
[`csql2lua`](../csql2lua/README.md): `sql2c` stays dependency-free, while
this tool depends on `cgo` and a C compiler to build what it *generates*.

## How it works

Like `csql2lua`, `csql2go` doesn't touch SQL or SQLite metadata itself — it
only reads the JSON `sql2c` already produced. For each query it emits a Go
method that:

- binds to sql2c's `<func>_cb` callback primitive (not the owning wrapper)
  for `:one`/`:many` queries — a small `//export`ed Go trampoline (one per
  distinct result struct type, shared across every query returning it)
  decodes the borrowed C row into a Go struct field-by-field
  (`C.GoStringN`/`C.GoBytes`, copying immediately) and appends it to a Go
  slice, so there's no need to replay sql2c's `sql_allocator`/deep-copy
  machinery here either — the row is only valid for the duration of that one
  callback call, and every byte is copied out before it returns.
- for `:one`, returns `(*T, error)` — `nil, nil` if no row was found.
- for `:many`, returns `([]T, error)`.
- for `:exec`, calls sql2c's plain (non-`_cb`) function directly and returns
  just `error`.
- a real SQLite error (not just "no rows") is always returned as `error`,
  wrapped with `sqlite3_errmsg`.

Nullable SQL columns come back as Go pointers (`*string`, `*int64`, …) —
`nil` means SQL `NULL`. Nullable blobs are the one exception: they stay
`[]byte`, with `nil` representing `NULL` (no need for `*[]byte`).

Go identifiers (struct/field/method names) are derived from the manifest's
*raw* names (`column.name`, `query.name`), not from sql2c's already-styled
`field`/`func` strings — so the Go API stays idiomatic regardless of
whatever `field-style`/`func-style` the consumer's sql2c config uses.
`id`-named columns get the Go initialism treatment (`ID`, not `Id`).

## Building

```sh
cc -std=c11 -o csql2go csql2go.c
```

No dependencies beyond libc, same as `csql2lua` — a self-contained JSON
reader plus a Go-source emitter. A C compiler and `go` are only needed
later, to build what it *generates*.

## Usage

```sh
csql2go -config config.txt
```

### Config

```
# config.txt
manifest       = ../sql/manifest.json
package        = mydb
queries_header = queries.h
output         = mydb.go
cgo_cflags     =
cgo_ldflags    = -lsqlite3
```

| key | meaning | default |
|---|---|---|
| `manifest` | path to the sql2c manifest.json | `manifest.json` |
| `package` | Go package name — also becomes the prefix on generated `//export` trampoline symbols, to avoid collisions if more than one csql2go package links into the same binary | `queries` |
| `queries_header` | path `#include`d verbatim into the generated file's cgo preamble | `queries.h` |
| `output` | generated `.go` path | `<package>.go` |
| `cgo_cflags` | passthrough, embedded as a literal `#cgo CFLAGS: ...` line | (none) |
| `cgo_ldflags` | passthrough, embedded as a literal `#cgo LDFLAGS: ...` line | (none) |

Paths are resolved relative to the working directory `csql2go` runs in (and
`queries_header` is emitted as a literal `#include "..."`, so keep it
relative to `output`'s directory — see "Building the generated package"
below for why that's normally just a bare filename).

`cgo_cflags`/`cgo_ldflags` are opaque passthrough strings — `csql2go` stays
unopinionated about how you link SQLite/`queries.c`, same principle as
`csql2lua` leaving Lua discovery to CMake, just expressed through Go's own
`#cgo` pragma mechanism instead of an external build tool.

## The generated Go API

Given queries named `GetBoat` (`:one`), `ListBoats` (`:many`), and
`DeleteBoat` (`:exec`):

```go
import "mydb"

db, err := mydb.Open("boats.sqlite") // sqlite3_open under the hood
if err != nil { ... }
defer db.Close()

boat, err := db.GetBoat(1)   // (*mydb.Boat, error); boat == nil, err == nil if not found
boats, err := db.ListBoats() // ([]mydb.Boat, error)
err = db.DeleteBoat(1)       // error
```

- `mydb.Open(path)` opens a connection (`sqlite3_open`) and returns `*DB`.
- `(*DB).Close()` is idempotent.
- Every query becomes a `(*DB)` method, named after the manifest's raw query
  name (`GetBoat`, not sql2c's own `func-style`-derived `get_boat`).
- A query with 0–1 parameters takes a plain positional Go argument. A query
  with 2+ parameters takes a single generated `<QueryName>Params` struct
  argument instead (mirroring sql2c's own params-struct collapsing) —
  e.g. `db.CreateBoat(mydb.CreateBoatParams{Name: "...", Registration: "..."})`.
- Using a connection after `Close()` returns `mydb.ErrClosed` rather than
  crashing.

## Building the generated package

The generated `.go`'s cgo preamble carries its own `#cgo CFLAGS`/`#cgo
LDFLAGS` (from config) and `#include`s `queries_header` — so `go
build`/`go test` alone builds it, given:

- sql2c's generated `queries.h` **and `queries.c`** present in the *same
  directory* as the generated `.go` file (place them there, or symlink —
  see `../sql2c/examples/basic/go/` for a worked example wired up through
  CMake). This matters because cgo only auto-compiles `.c` files it finds
  sitting in a package's own directory; `#include`-ing `queries.c`'s
  function *definitions* into the preamble comment instead would get them
  compiled twice (cgo emits the preamble into more than one translation
  unit whenever the file has `//export` functions, which every csql2go
  output does) and fail to link with duplicate symbols.
- SQLite reachable however `cgo_ldflags` says — e.g. `-lsqlite3` against a
  system install, or point `cgo_cflags`/`cgo_ldflags` at a vendored
  amalgamation instead.

## Limitations

- Assumes every query sharing a `result_type`/`params_type` name produces an
  identical column shape (true for sql2c's own generation model: the same
  table queried with `select *`/`returning *` always yields the same
  projected columns) — `csql2go` emits one Go struct per distinct type name,
  not per query.
- Query methods take a fixed positional argument (0–1 params) or a single
  params struct (2+ params) — matching sql2c's own C signatures — not a
  fully named/optional-argument API.
- Text/blob bind parameters go through `C.CString`/`C.CBytes` (a copy) for
  simplicity and correctness, not a zero-copy binding.
