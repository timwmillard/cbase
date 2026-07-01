// Generated from SQL - do not edit

#include <stdlib.h>
#include <string.h>
#include "queries.h"

// CreatePerson :one
int createPerson_cb(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Person *out; } createPerson_ctx;
static void createPerson_collect(Person *row, void *vctx) {
    createPerson_ctx *c = (createPerson_ctx *)vctx;
    c->out = (Person *)c->a.alloc(c->a.ctx, sizeof(Person));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Person *createPerson(sql_allocator a, sqlite3 *db, CreatePersonParams *params, int *rc) {
    createPerson_ctx c = { a, NULL };
    int r = createPerson_cb(db, params, createPerson_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetPerson :one
int getPerson_cb(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Person *out; } getPerson_ctx;
static void getPerson_collect(Person *row, void *vctx) {
    getPerson_ctx *c = (getPerson_ctx *)vctx;
    c->out = (Person *)c->a.alloc(c->a.ctx, sizeof(Person));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Person *getPerson(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc) {
    getPerson_ctx c = { a, NULL };
    int r = getPerson_cb(db, id, getPerson_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetPeople :many
int getPeople_cb(sqlite3 *db, void (*cb)(Person*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Person *items; size_t len, cap; } getPeople_ctx;
static void getPeople_collect(Person *row, void *vctx) {
    getPeople_ctx *c = (getPeople_ctx *)vctx;
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
PersonList getPeople(sql_allocator a, sqlite3 *db, int *rc) {
    getPeople_ctx c = { a, NULL, 0, 0 };
    int r = getPeople_cb(db, getPeople_collect, &c);
    if (rc) *rc = r;
    return (PersonList){ c.items, c.len };
}

// DeletePerson :exec
int deletePerson(sqlite3 *db, sql_int64 id) {
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
int createPet_cb(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Pet *out; } createPet_ctx;
static void createPet_collect(Pet *row, void *vctx) {
    createPet_ctx *c = (createPet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *createPet(sql_allocator a, sqlite3 *db, CreatePetParams *params, int *rc) {
    createPet_ctx c = { a, NULL };
    int r = createPet_cb(db, params, createPet_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetPet :one
int getPet_cb(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Pet *out; } getPet_ctx;
static void getPet_collect(Pet *row, void *vctx) {
    getPet_ctx *c = (getPet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *getPet(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc) {
    getPet_ctx c = { a, NULL };
    int r = getPet_cb(db, id, getPet_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetPetsByOwner :many
int getPetsByOwner_cb(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Pet *items; size_t len, cap; } getPetsByOwner_ctx;
static void getPetsByOwner_collect(Pet *row, void *vctx) {
    getPetsByOwner_ctx *c = (getPetsByOwner_ctx *)vctx;
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
PetList getPetsByOwner(sql_allocator a, sqlite3 *db, sql_int64 owner_id, int *rc) {
    getPetsByOwner_ctx c = { a, NULL, 0, 0 };
    int r = getPetsByOwner_cb(db, owner_id, getPetsByOwner_collect, &c);
    if (rc) *rc = r;
    return (PetList){ c.items, c.len };
}

// UpdatePet :one
int updatePet_cb(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
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

typedef struct { sql_allocator a; Pet *out; } updatePet_ctx;
static void updatePet_collect(Pet *row, void *vctx) {
    updatePet_ctx *c = (updatePet_ctx *)vctx;
    c->out = (Pet *)c->a.alloc(c->a.ctx, sizeof(Pet));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
}
Pet *updatePet(sql_allocator a, sqlite3 *db, UpdatePetParams *params, int *rc) {
    updatePet_ctx c = { a, NULL };
    int r = updatePet_cb(db, params, updatePet_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

