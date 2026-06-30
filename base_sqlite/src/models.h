// Generated from SQL - do not edit

#ifndef SQL_MODEL_H
#define SQL_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef unsigned char sql_byte;

typedef double sql_double;

typedef struct {
    sql_double value;
    bool null;
} sql_nulldouble;

typedef int sql_int;

typedef struct {
    sql_int value;
    bool null;
} sql_nullint;

typedef int64_t sql_int64;

typedef struct {
    sql_int64 value;
    bool null;
} sql_nullint64;

typedef double sql_numeric;

typedef struct {
    sql_numeric value;
    bool null;
} sql_nullnumeric;

typedef bool sql_bool;

typedef struct {
    sql_bool value;
    bool null;
} sql_nullbool;

typedef struct {
    sql_byte *data;
    size_t len;
} sql_blob;

typedef struct {
    sql_byte *data;
    size_t len;
    bool null;
} sql_nullblob;

typedef struct {
    sql_byte *data;
    size_t len;
} sql_text;

static inline sql_text to_sql_text(char *text) {
    return (sql_text){
        .data = (sql_byte*)text,
        .len = strlen(text),
    };
}

typedef struct {
    char *data;
    size_t len;
    bool null;
} sql_nulltext;

static inline sql_nulltext to_sql_nulltext(char *text, bool null) {
    return (sql_nulltext){
        .data = text,
        .len = strlen(text),
        .null = null,
    };
}

// ============ Allocator ============

// A single-function allocator: the owning query wrappers copy result rows
// (and their text/blob bytes) into whatever this points at. Cleanup is the
// caller's job (e.g. reset/release an arena); there is no per-row free.
typedef struct sql_allocator {
    void *(*alloc)(void *ctx, size_t size);
    void *ctx;
} sql_allocator;

static inline sql_text sql_dup_text(sql_allocator a, sql_text s) {
    if (s.data == NULL) return s;
    sql_byte *p = a.alloc(a.ctx, s.len + 1);
    memcpy(p, s.data, s.len);
    p[s.len] = 0;
    return (sql_text){ .data = p, .len = s.len };
}

static inline sql_nulltext sql_dup_nulltext(sql_allocator a, sql_nulltext s) {
    if (s.null || s.data == NULL) return s;
    char *p = a.alloc(a.ctx, s.len + 1);
    memcpy(p, s.data, s.len);
    p[s.len] = 0;
    return (sql_nulltext){ .data = p, .len = s.len, .null = false };
}

static inline sql_blob sql_dup_blob(sql_allocator a, sql_blob s) {
    if (s.data == NULL) return s;
    sql_byte *p = a.alloc(a.ctx, s.len);
    memcpy(p, s.data, s.len);
    return (sql_blob){ .data = p, .len = s.len };
}

static inline sql_nullblob sql_dup_nullblob(sql_allocator a, sql_nullblob s) {
    if (s.null || s.data == NULL) return s;
    sql_byte *p = a.alloc(a.ctx, s.len);
    memcpy(p, s.data, s.len);
    return (sql_nullblob){ .data = p, .len = s.len, .null = false };
}

// ============ Table Structs ============

typedef struct {
    sql_int64 id;
    sql_text name;
    sql_int64 age;
} Person;

typedef struct {
    sql_int64 id;
    sql_text name;
    sql_nullint64 owner_id;
} Pet;

#endif // SQL_MODEL_H
