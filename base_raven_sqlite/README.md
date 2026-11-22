# base_raven_sqlite

A local desktop app template: a [Raven](https://github.com/oscar-soirey/Raven)
UI backed by a SQLite database. Combines `base_raven` and `base_sqlite`.

- `sql/schema.sql` — embedded into `src/schema.h` and applied on startup
- `sql/queries.sql` — compiled by `sql2c` into typed C functions in `src/queries.h`
- `src/gui.xml` — embedded into `src/gui_xml.h` and loaded as the main window

```sh
make run            # opens ./app.db (created if missing)
./build/app my.db   # use a different database file
```
