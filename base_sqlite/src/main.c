#include <stdio.h>

#include "base.h"
#include "queries.h"
#include "schema.h"
#include "sqlite3.h"

// Adapt base.h's arena to the generated sql_allocator interface.
static void *arena_alloc_fn(void *ctx, size_t n) {
   return arena_alloc((arena *)ctx, n);
}

static void print_person(Person *person, void *ctx) {
   (void)ctx;
   printf("Person{id=%lld, name=%.*s, age=%lld}\n", (long long)person->id,
          (int)person->name.len, (char *)person->name.data,
          (long long)person->age);
}

int main(int argc, char *argv[]) {
   (void)argc;
   (void)argv;

   sqlite3 *db;
   int rc = sqlite3_open(":memory:", &db);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "open: %s\n", sqlite3_errmsg(db));
      return rc;
   }

   // Schema is embedded from sql/schema.sql via bin2c (schema.h).
   char *errmsg = NULL;
   rc = sqlite3_exec(db, (const char *)schema_data, NULL, NULL, &errmsg);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "schema: %s\n", errmsg);
      sqlite3_free(errmsg);
      return rc;
   }

   arena scratch = {0};
   sql_allocator alloc = {arena_alloc_fn, &scratch};

   CreatePersonParams tim = {.name = to_sql_text("Tim"), .age = 42};
   CreatePersonParams ada = {.name = to_sql_text("Ada"), .age = 36};
   createPerson(alloc, db, &tim); // owning wrapper (returned row unused here)
   createPerson(alloc, db, &ada);

   // Callback primitive: stream a single row.
   printf("-- get_person(1) --\n");
   getPerson_cb(db, 1, print_person, NULL);

   // Arena wrapper: an owned slice that outlives the statement.
   PersonList people = getPeople(alloc, db);
   printf("-- get_people (%zu) --\n", people.len);
   for (size_t i = 0; i < people.len; i++)
      print_person(&people.items[i], NULL);

   arena_release(&scratch);
   sqlite3_close(db);
   return 0;
}
