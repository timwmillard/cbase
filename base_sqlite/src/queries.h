// Generated from SQL - do not edit

#ifndef QUERIES_H
#define QUERIES_H

#include "sqlite3.h"

#include "models.h"

// ============ Result Lists ============

typedef struct { Person *items; size_t len; } PersonList;
typedef struct { Pet *items; size_t len; } PetList;

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
int createPerson_cb(sqlite3 *db, CreatePersonParams *params, void (*cb)(Person*, void*), void *ctx);
Person *createPerson(sql_allocator a, sqlite3 *db, CreatePersonParams *params);
// select * from person where id = :id;
int getPerson_cb(sqlite3 *db, sql_int64 id, void (*cb)(Person*, void*), void *ctx);
Person *getPerson(sql_allocator a, sqlite3 *db, sql_int64 id);
// select * from person;
int getPeople_cb(sqlite3 *db, void (*cb)(Person*, void*), void *ctx);
PersonList getPeople(sql_allocator a, sqlite3 *db);
// delete from person where id = :id;
int deletePerson(sqlite3 *db, sql_int64 id);
// insert into pet (name, owner_id) values (:name, :owner_id) returning *;
int createPet_cb(sqlite3 *db, CreatePetParams *params, void (*cb)(Pet*, void*), void *ctx);
Pet *createPet(sql_allocator a, sqlite3 *db, CreatePetParams *params);
// select * from pet where id = :id;
int getPet_cb(sqlite3 *db, sql_int64 id, void (*cb)(Pet*, void*), void *ctx);
Pet *getPet(sql_allocator a, sqlite3 *db, sql_int64 id);
// select * from pet where owner_id = :owner_id;
int getPetsByOwner_cb(sqlite3 *db, sql_int64 owner_id, void (*cb)(Pet*, void*), void *ctx);
PetList getPetsByOwner(sql_allocator a, sqlite3 *db, sql_int64 owner_id);
// update pet set name = :name where id = :id returning *;
int updatePet_cb(sqlite3 *db, UpdatePetParams *params, void (*cb)(Pet*, void*), void *ctx);
Pet *updatePet(sql_allocator a, sqlite3 *db, UpdatePetParams *params);

#endif // QUERIES_H
