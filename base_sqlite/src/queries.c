// Generated from SQL - do not edit

#include <stdlib.h>
#include <string.h>
#include "queries.h"

// CreatePerson :one
int create_person_cb(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "insert into person (name, age) values (:name, :age) returning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->name.data, params->name.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, params->age);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Person result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.age = sqlite3_column_int64(stmt, 2);
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Person *out; } create_person_ctx;
static void create_person_collect(Person *row, void *vctx) {
    create_person_ctx *c = (create_person_ctx *)vctx;
    c->out = (Person *)c->a.alloc(c->a.ctx, sizeof(Person));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Person *create_person(sql_allocator a, sqlite3 *db, CreatePersonParams *params) {
    create_person_ctx c = { a, NULL };
    create_person_cb(db, params, create_person_collect, &c);
    return c.out;
}

// GetPerson :one
int get_person_cb(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "select * from person where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Person result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.age = sqlite3_column_int64(stmt, 2);
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Person *out; } get_person_ctx;
static void get_person_collect(Person *row, void *vctx) {
    get_person_ctx *c = (get_person_ctx *)vctx;
    c->out = (Person *)c->a.alloc(c->a.ctx, sizeof(Person));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Person *get_person(sql_allocator a, sqlite3 *db, sql_int64 id) {
    get_person_ctx c = { a, NULL };
    get_person_cb(db, id, get_person_collect, &c);
    return c.out;
}

// GetPeople :many
int get_people_cb(sqlite3 *db, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "select * from person;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Person result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.age = sqlite3_column_int64(stmt, 2);
        if (cb) cb(&result, ctx);
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    return rc;
}

typedef struct { sql_allocator a; Person *items; size_t len, cap; } get_people_ctx;
static void get_people_collect(Person *row, void *vctx) {
    get_people_ctx *c = (get_people_ctx *)vctx;
    if (c->len == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        Person *ni = (Person *)c->a.alloc(c->a.ctx, ncap * sizeof(Person));
        if (c->items) memcpy(ni, c->items, c->len * sizeof(Person));
        c->items = ni; c->cap = ncap;
    }
    Person *dst = &c->items[c->len++];
    *dst = *row;
    dst->name = sql_dup_text(c->a, dst->name);
}
PersonSlice get_people(sql_allocator a, sqlite3 *db) {
    get_people_ctx c = { a, NULL, 0, 0 };
    get_people_cb(db, get_people_collect, &c);
    return (PersonSlice){ c.items, c.len };
}

// DeletePerson :exec
int delete_person(sqlite3 *db, sql_int64 id) {
    const char *sql = "delete from person where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

// CreatePet :one
int create_pet_cb(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "insert into pet (name, owner_id) values (:name, :owner_id) returning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->name.data, params->name.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, params->owner_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Pet result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            result.owner_id.value = sqlite3_column_int64(stmt, 2);
            result.owner_id.null = false;
        } else { result.owner_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Pet *out; } create_pet_ctx;
static void create_pet_collect(Pet *row, void *vctx) {
    create_pet_ctx *c = (create_pet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *create_pet(sql_allocator a, sqlite3 *db, CreatePetParams *params) {
    create_pet_ctx c = { a, NULL };
    create_pet_cb(db, params, create_pet_collect, &c);
    return c.out;
}

// GetPet :one
int get_pet_cb(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "select * from pet where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Pet result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            result.owner_id.value = sqlite3_column_int64(stmt, 2);
            result.owner_id.null = false;
        } else { result.owner_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Pet *out; } get_pet_ctx;
static void get_pet_collect(Pet *row, void *vctx) {
    get_pet_ctx *c = (get_pet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *get_pet(sql_allocator a, sqlite3 *db, sql_int64 id) {
    get_pet_ctx c = { a, NULL };
    get_pet_cb(db, id, get_pet_collect, &c);
    return c.out;
}

// GetPetsByOwner :many
int get_pets_by_owner_cb(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "select * from pet where owner_id = :owner_id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, owner_id);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Pet result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            result.owner_id.value = sqlite3_column_int64(stmt, 2);
            result.owner_id.null = false;
        } else { result.owner_id.null = true; }
        if (cb) cb(&result, ctx);
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    return rc;
}

typedef struct { sql_allocator a; Pet *items; size_t len, cap; } get_pets_by_owner_ctx;
static void get_pets_by_owner_collect(Pet *row, void *vctx) {
    get_pets_by_owner_ctx *c = (get_pets_by_owner_ctx *)vctx;
    if (c->len == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        Pet *ni = (Pet *)c->a.alloc(c->a.ctx, ncap * sizeof(Pet));
        if (c->items) memcpy(ni, c->items, c->len * sizeof(Pet));
        c->items = ni; c->cap = ncap;
    }
    Pet *dst = &c->items[c->len++];
    *dst = *row;
    dst->name = sql_dup_text(c->a, dst->name);
}
PetSlice get_pets_by_owner(sql_allocator a, sqlite3 *db, sql_int64 owner_id) {
    get_pets_by_owner_ctx c = { a, NULL, 0, 0 };
    get_pets_by_owner_cb(db, owner_id, get_pets_by_owner_collect, &c);
    return (PetSlice){ c.items, c.len };
}

// UpdatePet :one
int update_pet_cb(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "update pet set name = :name where id = :id returning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->name.data, params->name.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, params->id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Pet result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            result.owner_id.value = sqlite3_column_int64(stmt, 2);
            result.owner_id.null = false;
        } else { result.owner_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Pet *out; } update_pet_ctx;
static void update_pet_collect(Pet *row, void *vctx) {
    update_pet_ctx *c = (update_pet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *update_pet(sql_allocator a, sqlite3 *db, UpdatePetParams *params) {
    update_pet_ctx c = { a, NULL };
    update_pet_cb(db, params, update_pet_collect, &c);
    return c.out;
}

