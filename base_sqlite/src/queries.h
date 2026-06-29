// Generated from SQL - do not edit

#ifndef QUERIES_H
#define QUERIES_H

#include "sqlite3.h"

#include "models.h"

// ============ Param Structs ============

typedef struct {
    sql_text name;
    sql_int64 age;
} CreatePersonParams;

typedef struct {
    sql_text name;
    sql_int64 owner_id;
} CreatePetParams;

typedef struct {
    sql_text name;
    sql_int64 id;
} UpdatePetParams;

// ============ Query Functions ============

// insert into person (name, age) values (?, ?) returning *;
int create_person(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx);
// select * from person where id = ?;
int get_person(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx);
// select * from person;
int get_people(sqlite3 *db, void (*cb)(Person*, void*), void *ctx);
// delete from person where id = ?;
int delete_person(sqlite3 *db, sql_int64 id);

// insert into pet (name, owner_id) values (?, ?) returning *;
int create_pet(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx);
// select * from pet where id = ?;
int get_pet(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx);
// select * from pet where owner_id = ?;
int get_pets_by_owner(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx);
// update pet set name = ? where id = ? returning *;
int update_pet(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx);

#endif // QUERIES_H
