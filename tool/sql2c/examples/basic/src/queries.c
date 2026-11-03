// Generated from SQL - do not edit

#include <stdlib.h>
#include <string.h>
#include "queries.h"

// CreateBoat :one
int create_boat_cb(sqlite3 *db, CreateBoatParams *params, void (*cb)(Boat*, void*), void *ctx) {
    const char *sql = "insert into boat (name, registration)\nvalues (:name, :registration)\nreturning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->name.data, params->name.len, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, (char*)params->registration.data, params->registration.len, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Boat result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.registration.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.registration.len = sqlite3_column_bytes(stmt, 2);
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Boat *out; } create_boat_ctx;
static void create_boat_collect(Boat *row, void *vctx) {
    create_boat_ctx *c = (create_boat_ctx *)vctx;
    c->out = (Boat *)c->a.alloc(c->a.ctx, sizeof(Boat));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
    c->out->registration = sql_dup_text(c->a, c->out->registration);
}
Boat *create_boat(sql_allocator a, sqlite3 *db, CreateBoatParams *params, int *rc) {
    create_boat_ctx c = { a, NULL };
    int r = create_boat_cb(db, params, create_boat_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetBoat :one
int get_boat_cb(sqlite3 *db, sql_int64 id, void (*cb)(Boat*, void*), void *ctx) {
    const char *sql = "select * from boat where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Boat result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.registration.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.registration.len = sqlite3_column_bytes(stmt, 2);
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Boat *out; } get_boat_ctx;
static void get_boat_collect(Boat *row, void *vctx) {
    get_boat_ctx *c = (get_boat_ctx *)vctx;
    c->out = (Boat *)c->a.alloc(c->a.ctx, sizeof(Boat));
    *c->out = *row;
    c->out->name = sql_dup_text(c->a, c->out->name);
    c->out->registration = sql_dup_text(c->a, c->out->registration);
}
Boat *get_boat(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc) {
    get_boat_ctx c = { a, NULL };
    int r = get_boat_cb(db, id, get_boat_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// ListBoats :many
int list_boats_cb(sqlite3 *db, void (*cb)(Boat*, void*), void *ctx) {
    const char *sql = "select * from boat order by name;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Boat result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.name.len = sqlite3_column_bytes(stmt, 1);
        result.registration.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.registration.len = sqlite3_column_bytes(stmt, 2);
        if (cb) cb(&result, ctx);
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    return rc;
}

typedef struct { sql_allocator a; Boat *items; size_t len, cap; } list_boats_ctx;
static void list_boats_collect(Boat *row, void *vctx) {
    list_boats_ctx *c = (list_boats_ctx *)vctx;
    if (c->len == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        Boat *ni = (Boat *)c->a.alloc(c->a.ctx, ncap * sizeof(Boat));
        if (c->items) memcpy(ni, c->items, c->len * sizeof(Boat));
        c->items = ni; c->cap = ncap;
    }
    Boat *dst = &c->items[c->len++];
    *dst = *row;
    dst->name = sql_dup_text(c->a, dst->name);
    dst->registration = sql_dup_text(c->a, dst->registration);
}
BoatList list_boats(sql_allocator a, sqlite3 *db, int *rc) {
    list_boats_ctx c = { a, NULL, 0, 0 };
    int r = list_boats_cb(db, list_boats_collect, &c);
    if (rc) *rc = r;
    return (BoatList){ c.items, c.len };
}

// CreateCompetitor :one
int create_competitor_cb(sqlite3 *db, CreateCompetitorParams *params, void (*cb)(Competitor*, void*), void *ctx) {
    const char *sql = "insert into competitor (first_name, last_name, email, boat_id)\nvalues (:first_name, :last_name, :email, :boat_id)\nreturning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->first_name.data, params->first_name.len, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, (char*)params->last_name.data, params->last_name.len, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, (char*)params->email.data, params->email.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, params->boat_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Competitor result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.first_name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.first_name.len = sqlite3_column_bytes(stmt, 1);
        result.last_name.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.last_name.len = sqlite3_column_bytes(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            result.email.data = (char*)sqlite3_column_text(stmt, 3);
            result.email.len = sqlite3_column_bytes(stmt, 3);
            result.email.null = false;
        } else { result.email.null = true; }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            result.boat_id.value = sqlite3_column_int64(stmt, 4);
            result.boat_id.null = false;
        } else { result.boat_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Competitor *out; } create_competitor_ctx;
static void create_competitor_collect(Competitor *row, void *vctx) {
    create_competitor_ctx *c = (create_competitor_ctx *)vctx;
    c->out = (Competitor *)c->a.alloc(c->a.ctx, sizeof(Competitor));
    *c->out = *row;
    c->out->first_name = sql_dup_text(c->a, c->out->first_name);
    c->out->last_name = sql_dup_text(c->a, c->out->last_name);
    c->out->email = sql_dup_nulltext(c->a, c->out->email);
}
Competitor *create_competitor(sql_allocator a, sqlite3 *db, CreateCompetitorParams *params, int *rc) {
    create_competitor_ctx c = { a, NULL };
    int r = create_competitor_cb(db, params, create_competitor_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// CreateCompetitorNoContact :one
int create_competitor_no_contact_cb(sqlite3 *db, CreateCompetitorNoContactParams *params, void (*cb)(Competitor*, void*), void *ctx) {
    const char *sql = "insert into competitor (first_name, last_name)\nvalues (:first_name, :last_name)\nreturning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->first_name.data, params->first_name.len, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, (char*)params->last_name.data, params->last_name.len, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Competitor result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.first_name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.first_name.len = sqlite3_column_bytes(stmt, 1);
        result.last_name.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.last_name.len = sqlite3_column_bytes(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            result.email.data = (char*)sqlite3_column_text(stmt, 3);
            result.email.len = sqlite3_column_bytes(stmt, 3);
            result.email.null = false;
        } else { result.email.null = true; }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            result.boat_id.value = sqlite3_column_int64(stmt, 4);
            result.boat_id.null = false;
        } else { result.boat_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Competitor *out; } create_competitor_no_contact_ctx;
static void create_competitor_no_contact_collect(Competitor *row, void *vctx) {
    create_competitor_no_contact_ctx *c = (create_competitor_no_contact_ctx *)vctx;
    c->out = (Competitor *)c->a.alloc(c->a.ctx, sizeof(Competitor));
    *c->out = *row;
    c->out->first_name = sql_dup_text(c->a, c->out->first_name);
    c->out->last_name = sql_dup_text(c->a, c->out->last_name);
    c->out->email = sql_dup_nulltext(c->a, c->out->email);
}
Competitor *create_competitor_no_contact(sql_allocator a, sqlite3 *db, CreateCompetitorNoContactParams *params, int *rc) {
    create_competitor_no_contact_ctx c = { a, NULL };
    int r = create_competitor_no_contact_cb(db, params, create_competitor_no_contact_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// GetCompetitor :one
int get_competitor_cb(sqlite3 *db, sql_int64 id, void (*cb)(Competitor*, void*), void *ctx) {
    const char *sql = "select * from competitor where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Competitor result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.first_name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.first_name.len = sqlite3_column_bytes(stmt, 1);
        result.last_name.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.last_name.len = sqlite3_column_bytes(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            result.email.data = (char*)sqlite3_column_text(stmt, 3);
            result.email.len = sqlite3_column_bytes(stmt, 3);
            result.email.null = false;
        } else { result.email.null = true; }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            result.boat_id.value = sqlite3_column_int64(stmt, 4);
            result.boat_id.null = false;
        } else { result.boat_id.null = true; }
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Competitor *out; } get_competitor_ctx;
static void get_competitor_collect(Competitor *row, void *vctx) {
    get_competitor_ctx *c = (get_competitor_ctx *)vctx;
    c->out = (Competitor *)c->a.alloc(c->a.ctx, sizeof(Competitor));
    *c->out = *row;
    c->out->first_name = sql_dup_text(c->a, c->out->first_name);
    c->out->last_name = sql_dup_text(c->a, c->out->last_name);
    c->out->email = sql_dup_nulltext(c->a, c->out->email);
}
Competitor *get_competitor(sql_allocator a, sqlite3 *db, sql_int64 id, int *rc) {
    get_competitor_ctx c = { a, NULL };
    int r = get_competitor_cb(db, id, get_competitor_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// ListCompetitors :many
int list_competitors_cb(sqlite3 *db, void (*cb)(Competitor*, void*), void *ctx) {
    const char *sql = "select * from competitor order by last_name, first_name;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Competitor result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.first_name.data = (sql_byte*)sqlite3_column_text(stmt, 1);
        result.first_name.len = sqlite3_column_bytes(stmt, 1);
        result.last_name.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.last_name.len = sqlite3_column_bytes(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            result.email.data = (char*)sqlite3_column_text(stmt, 3);
            result.email.len = sqlite3_column_bytes(stmt, 3);
            result.email.null = false;
        } else { result.email.null = true; }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            result.boat_id.value = sqlite3_column_int64(stmt, 4);
            result.boat_id.null = false;
        } else { result.boat_id.null = true; }
        if (cb) cb(&result, ctx);
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    return rc;
}

typedef struct { sql_allocator a; Competitor *items; size_t len, cap; } list_competitors_ctx;
static void list_competitors_collect(Competitor *row, void *vctx) {
    list_competitors_ctx *c = (list_competitors_ctx *)vctx;
    if (c->len == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        Competitor *ni = (Competitor *)c->a.alloc(c->a.ctx, ncap * sizeof(Competitor));
        if (c->items) memcpy(ni, c->items, c->len * sizeof(Competitor));
        c->items = ni; c->cap = ncap;
    }
    Competitor *dst = &c->items[c->len++];
    *dst = *row;
    dst->first_name = sql_dup_text(c->a, dst->first_name);
    dst->last_name = sql_dup_text(c->a, dst->last_name);
    dst->email = sql_dup_nulltext(c->a, dst->email);
}
CompetitorList list_competitors(sql_allocator a, sqlite3 *db, int *rc) {
    list_competitors_ctx c = { a, NULL, 0, 0 };
    int r = list_competitors_cb(db, list_competitors_collect, &c);
    if (rc) *rc = r;
    return (CompetitorList){ c.items, c.len };
}

// UpdateCompetitorEmail :exec
int update_competitor_email(sqlite3 *db, UpdateCompetitorEmailParams *params) {
    const char *sql = "update competitor set email = :email where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, (char*)params->email.data, params->email.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, params->id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

// DeleteCompetitor :exec
int delete_competitor(sqlite3 *db, sql_int64 id) {
    const char *sql = "delete from competitor where id = :id;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

// CreateCatch :one
int create_catch_cb(sqlite3 *db, CreateCatchParams *params, void (*cb)(Catch*, void*), void *ctx) {
    const char *sql = "insert into catch (competitor_id, species, weight_grams, caught_at)\nvalues (:competitor_id, :species, :weight_grams, :caught_at)\nreturning *;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, params->competitor_id);
    sqlite3_bind_text(stmt, 2, (char*)params->species.data, params->species.len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, params->weight_grams);
    sqlite3_bind_text(stmt, 4, (char*)params->caught_at.data, params->caught_at.len, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        Catch result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.competitor_id = sqlite3_column_int64(stmt, 1);
        result.species.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.species.len = sqlite3_column_bytes(stmt, 2);
        result.weight_grams = sqlite3_column_int64(stmt, 3);
        result.caught_at.data = (sql_byte*)sqlite3_column_text(stmt, 4);
        result.caught_at.len = sqlite3_column_bytes(stmt, 4);
        if (cb) cb(&result, ctx);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(stmt);
    return rc;
}

typedef struct { sql_allocator a; Catch *out; } create_catch_ctx;
static void create_catch_collect(Catch *row, void *vctx) {
    create_catch_ctx *c = (create_catch_ctx *)vctx;
    c->out = (Catch *)c->a.alloc(c->a.ctx, sizeof(Catch));
    *c->out = *row;
    c->out->species = sql_dup_text(c->a, c->out->species);
    c->out->caught_at = sql_dup_text(c->a, c->out->caught_at);
}
Catch *create_catch(sql_allocator a, sqlite3 *db, CreateCatchParams *params, int *rc) {
    create_catch_ctx c = { a, NULL };
    int r = create_catch_cb(db, params, create_catch_collect, &c);
    if (rc) *rc = r;
    return c.out;
}

// ListCatchesByCompetitor :many
int list_catches_by_competitor_cb(sqlite3 *db, sql_int64 competitor_id, void (*cb)(Catch*, void*), void *ctx) {
    const char *sql = "select * from catch where competitor_id = :competitor_id order by caught_at;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, competitor_id);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Catch result = {0};
        result.id = sqlite3_column_int64(stmt, 0);
        result.competitor_id = sqlite3_column_int64(stmt, 1);
        result.species.data = (sql_byte*)sqlite3_column_text(stmt, 2);
        result.species.len = sqlite3_column_bytes(stmt, 2);
        result.weight_grams = sqlite3_column_int64(stmt, 3);
        result.caught_at.data = (sql_byte*)sqlite3_column_text(stmt, 4);
        result.caught_at.len = sqlite3_column_bytes(stmt, 4);
        if (cb) cb(&result, ctx);
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) rc = SQLITE_OK;
    return rc;
}

typedef struct { sql_allocator a; Catch *items; size_t len, cap; } list_catches_by_competitor_ctx;
static void list_catches_by_competitor_collect(Catch *row, void *vctx) {
    list_catches_by_competitor_ctx *c = (list_catches_by_competitor_ctx *)vctx;
    if (c->len == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        Catch *ni = (Catch *)c->a.alloc(c->a.ctx, ncap * sizeof(Catch));
        if (c->items) memcpy(ni, c->items, c->len * sizeof(Catch));
        c->items = ni; c->cap = ncap;
    }
    Catch *dst = &c->items[c->len++];
    *dst = *row;
    dst->species = sql_dup_text(c->a, dst->species);
    dst->caught_at = sql_dup_text(c->a, dst->caught_at);
}
CatchList list_catches_by_competitor(sql_allocator a, sqlite3 *db, sql_int64 competitor_id, int *rc) {
    list_catches_by_competitor_ctx c = { a, NULL, 0, 0 };
    int r = list_catches_by_competitor_cb(db, competitor_id, list_catches_by_competitor_collect, &c);
    if (rc) *rc = r;
    return (CatchList){ c.items, c.len };
}

