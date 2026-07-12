// Generated from SQL - do not edit

#ifndef QUERIES_H
#define QUERIES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "sqlite3.h"

// ============ Base Types ============

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
    sql_text registration;
} Boat;

typedef struct {
    sql_int64 id;
    sql_int64 competitor_id;
    sql_text species;
    sql_int64 weight_grams;
    sql_text caught_at;
} Catch;

typedef struct {
    sql_int64 id;
    sql_text first_name;
    sql_text last_name;
    sql_nulltext email;
    sql_nullint64 boat_id;
} Competitor;

// ============ Result Lists ============

typedef struct { Boat *items; size_t len; } BoatList;
typedef struct { Competitor *items; size_t len; } CompetitorList;
typedef struct { Catch *items; size_t len; } CatchList;

// ============ Param Structs ============

typedef struct {
    sql_text name;
    sql_text registration;
} CreateBoatParams;

typedef struct {
    sql_text first_name;
    sql_text last_name;
    sql_text email;
    sql_int64 boat_id;
} CreateCompetitorParams;

typedef struct {
    sql_text first_name;
    sql_text last_name;
} CreateCompetitorNoContactParams;

typedef struct {
    sql_text email;
    sql_int64 id;
} UpdateCompetitorEmailParams;

typedef struct {
    sql_int64 competitor_id;
    sql_text species;
    sql_int64 weight_grams;
    sql_text caught_at;
} CreateCatchParams;

// ============ Query Functions ============

// insert into boat (name, registration) values (:name, :registration) returning *;
int create_boat_cb(sqlite3 *db, CreateBoatParams *params, void (*cb)(Boat*, void*), void *ctx);
Boat *create_boat(sql_allocator a, sqlite3 *db, CreateBoatParams *params, int *rc);
// select * from boat where id = :id;
int get_boat_cb(sqlite3 *db, sql_int64 id, void (*cb)(Boat*, void*), void *ctx);
Boat *get_boat(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc);
// select * from boat order by name;
int list_boats_cb(sqlite3 *db, void (*cb)(Boat*, void*), void *ctx);
BoatList list_boats(sql_allocator a, sqlite3 *db, int *rc);
// insert into competitor (first_name, last_name, email, boat_id) values (:first_name, :last_name, :email, :boat_id) returning *;
int create_competitor_cb(sqlite3 *db, CreateCompetitorParams *params, void (*cb)(Competitor*, void*), void *ctx);
Competitor *create_competitor(sql_allocator a, sqlite3 *db, CreateCompetitorParams *params, int *rc);
// insert into competitor (first_name, last_name) values (:first_name, :last_name) returning *;
int create_competitor_no_contact_cb(sqlite3 *db, CreateCompetitorNoContactParams *params, void (*cb)(Competitor*, void*), void *ctx);
Competitor *create_competitor_no_contact(sql_allocator a, sqlite3 *db, CreateCompetitorNoContactParams *params, int *rc);
// select * from competitor where id = :id;
int get_competitor_cb(sqlite3 *db, sql_int64 id, void (*cb)(Competitor*, void*), void *ctx);
Competitor *get_competitor(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc);
// select * from competitor order by last_name, first_name;
int list_competitors_cb(sqlite3 *db, void (*cb)(Competitor*, void*), void *ctx);
CompetitorList list_competitors(sql_allocator a, sqlite3 *db, int *rc);
// update competitor set email = :email where id = :id;
int update_competitor_email(sqlite3 *db, UpdateCompetitorEmailParams *params);
// delete from competitor where id = :id;
int delete_competitor(sqlite3 *db, sql_int64 id);
// insert into catch (competitor_id, species, weight_grams, caught_at) values (:competitor_id, :species, :weight_grams, :caught_at) returning *;
int create_catch_cb(sqlite3 *db, CreateCatchParams *params, void (*cb)(Catch*, void*), void *ctx);
Catch *create_catch(sql_allocator a, sqlite3 *db, CreateCatchParams *params, int *rc);
// select * from catch where competitor_id = :competitor_id order by caught_at;
int list_catches_by_competitor_cb(sqlite3 *db, sql_int64 competitor_id, void (*cb)(Catch*, void*), void *ctx);
CatchList list_catches_by_competitor(sql_allocator a, sqlite3 *db, sql_int64 competitor_id, int *rc);

#endif // QUERIES_H
