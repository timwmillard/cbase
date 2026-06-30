// Spike: can SQLite itself replace the Go SQL parser?
//
// Loads schema.sql into an in-memory db, then prepares each query from
// queries.sql and dumps what SQLite tells us about it:
//   - result columns: name, decltype, source table, source column
//   - bind parameters: count + names
//
// Build (column metadata APIs need the compile flag):
//   cc -DSQLITE_ENABLE_COLUMN_METADATA -I../../deps \
//      spike.c ../../deps/sqlite3.c -o spike
//   ./spike ../../sql/schema.sql ../../sql/queries.sql

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

static char *read_file(const char *path) {
   FILE *f = fopen(path, "rb");
   if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *buf = malloc(n + 1);
   fread(buf, 1, n, f);
   buf[n] = 0;
   fclose(f);
   return buf;
}

static void dump_query(sqlite3 *db, const char *name, const char *type,
                       const char *sql) {
   printf("== %s (:%s) ==\n", name, type);

   sqlite3_stmt *stmt;
   int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
   if (rc != SQLITE_OK) {
      printf("   PREPARE ERROR: %s\n\n", sqlite3_errmsg(db));
      return;
   }

   int np = sqlite3_bind_parameter_count(stmt);
   printf("   params: %d\n", np);
   for (int i = 1; i <= np; i++) {
      const char *pn = sqlite3_bind_parameter_name(stmt, i);
      printf("     #%d  name=%s\n", i, pn ? pn : "(positional ?)");
   }

   int nc = sqlite3_column_count(stmt);
   printf("   result columns: %d\n", nc);
   for (int i = 0; i < nc; i++) {
      const char *cname  = sqlite3_column_name(stmt, i);
      const char *decl   = sqlite3_column_decltype(stmt, i);
      const char *tbl    = sqlite3_column_table_name(stmt, i);
      const char *origin = sqlite3_column_origin_name(stmt, i);
      printf("     [%d] name=%-12s decltype=%-10s table=%-10s origin=%s\n",
             i, cname ? cname : "?", decl ? decl : "(expr)",
             tbl ? tbl : "(none)", origin ? origin : "(none)");
   }
   printf("\n");
   sqlite3_finalize(stmt);
}

int main(int argc, char **argv) {
   if (argc != 3) {
      fprintf(stderr, "usage: %s schema.sql queries.sql\n", argv[0]);
      return 1;
   }

   sqlite3 *db;
   if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
      fprintf(stderr, "open: %s\n", sqlite3_errmsg(db));
      return 1;
   }

   char *schema = read_file(argv[1]);
   char *err = NULL;
   if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
      fprintf(stderr, "schema: %s\n", err);
      return 1;
   }

   // Split queries.sql on "-- name:" markers (same convention as sql2c).
   char *queries = read_file(argv[2]);
   char *p = strstr(queries, "-- name:");
   while (p) {
      char *next = strstr(p + 8, "-- name:");
      if (next) *next = 0; // terminate this block

      // Header line: "-- name: QueryName :type"
      char name[128] = {0}, type[32] = {0};
      char *nl = strchr(p, '\n');
      if (nl) {
         *nl = 0;
         sscanf(p + 8, " %127s :%31s", name, type);
         char *sql = nl + 1;
         dump_query(db, name, type, sql);
         if (next) *nl = '\n';
      }

      if (next) *next = '-';
      p = next;
   }

   sqlite3_close(db);
   return 0;
}
