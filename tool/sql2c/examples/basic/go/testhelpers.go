package mydb

/*
#include <stdlib.h>
#include "queries.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// execSchema applies raw schema SQL directly against the connection's
// sqlite3 handle, for test setup. The generated package only exposes the
// typed queries from queries.sql, not schema DDL. Named without a _test.go
// suffix because cgo isn't supported directly in Go test files; it's only
// ever called from tests.
func execSchema(db *DB, sql string) error {
	csql := C.CString(sql)
	defer C.free(unsafe.Pointer(csql))
	var errmsg *C.char
	rc := C.sqlite3_exec(db.db, csql, nil, nil, &errmsg)
	if rc != C.SQLITE_OK {
		msg := C.GoString(errmsg)
		C.sqlite3_free(unsafe.Pointer(errmsg))
		return fmt.Errorf("exec schema: %s", msg)
	}
	return nil
}
