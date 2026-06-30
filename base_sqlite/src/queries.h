// Generated from SQL - do not edit

#ifndef QUERIES_H
#define QUERIES_H

#include "sqlite3.h"

#include "models.h"

// ============ Result Slices ============

typedef struct { Person *items; size_t len; } PersonSlice;
typedef struct { Pet *items; size_t len; } PetSlice;

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

// insert into person (name, age) values (:name, :age) returning *;
int create_person_cb(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx);
Person *create_person(sql_allocator a, sqlite3 *db, CreatePersonParams *params);
// select * from person where id = :id;
int get_person_cb(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx);
Person *get_person(sql_allocator a, sqlite3 *db, sql_int64 id);
// select * from person;
int get_people_cb(sqlite3 *db, void (*cb)(Person*, void*), void *ctx);
PersonSlice get_people(sql_allocator a, sqlite3 *db);
// delete from person where id = :id;
int delete_person(sqlite3 *db, sql_int64 id);
// insert into pet (name, owner_id) values (:name, :owner_id) returning *;
int create_pet_cb(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx);
Pet *create_pet(sql_allocator a, sqlite3 *db, CreatePetParams *params);
// select * from pet where id = :id;
int get_pet_cb(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx);
Pet *get_pet(sql_allocator a, sqlite3 *db, sql_int64 id);
// select * from pet where owner_id = :owner_id;
int get_pets_by_owner_cb(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx);
PetSlice get_pets_by_owner(sql_allocator a, sqlite3 *db, sql_int64 owner_id);
// update pet set name = :name where id = :id returning *;
int update_pet_cb(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx);
Pet *update_pet(sql_allocator a, sqlite3 *db, UpdatePetParams *params);

#endif // QUERIES_H
