// Generated from SQL - do not edit

#include <stdlib.h>
#include <string.h>
#include "queries.h"

// CreatePerson :one
int create_person(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "insert into person (name, age) values (?, ?) returning *;\n";
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

// GetPerson :one
int get_person(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "select * from person where id = ?;\n";
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

// GetPeople :many
int get_people(sqlite3 *db, void (*cb)(Person*, void*), void *ctx) {
    const char *sql = "select * from person;\n";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    size_t capacity = 16;
    size_t n = 0;
    Person *arr = malloc(capacity * sizeof(Person));
    if (arr == NULL) { sqlite3_finalize(stmt); return SQLITE_NOMEM; }

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

// DeletePerson :exec
int delete_person(sqlite3 *db, sql_int64 id) {
    const char *sql = "delete from person where id = ?;\n";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

// CreatePet :one
int create_pet(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "insert into pet (name, owner_id) values (?, ?) returning *;\n";
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

// GetPet :one
int get_pet(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "select * from pet where id = ?;\n";
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

// GetPetsByOwner :many
int get_pets_by_owner(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "select * from pet where owner_id = ?;\n";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, owner_id);

    size_t capacity = 16;
    size_t n = 0;
    Pet *arr = malloc(capacity * sizeof(Pet));
    if (arr == NULL) { sqlite3_finalize(stmt); return SQLITE_NOMEM; }

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

// UpdatePet :one
int update_pet(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx) {
    const char *sql = "update pet set name = ? where id = ? returning *;\n";
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

