#include <stdio.h>

#include "base.h"
#include "queries.h"
#include "schema.h"
#include "sqlite3.h"

static void get_person_cb(Person *person, void *ctx) {
   (void)ctx;
   printf("Person{id=%lld, name=%.*s, age=%lld}\n",
          (long long)person->id,
          (int)person->name.len, (char *)person->name.data,
          (long long)person->age);
}

int main(int argc, char *argv[]) {
   sqlite3 *db;
   int rc = sqlite3_open("test.db", &db);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "open: %s\n", sqlite3_errmsg(db));
      return rc;
   }

   char *errmsg = NULL;
   rc = sqlite3_exec(db, (const char *)schema_data, NULL, NULL, &errmsg);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "schema: %s\n", errmsg);
      sqlite3_free(errmsg);
      return rc;
   }

   CreatePersonParams person = {
       .name = to_sql_text("Tim"),
       .age = 42,
   };

   rc = create_person(db, &person, NULL, NULL);
   if (rc != SQLITE_OK)
      return rc;

   rc = get_person(db, 1, get_person_cb, NULL);
   if (rc != SQLITE_OK)
      return rc;

   sqlite3_close(db);

   return 0;
}
