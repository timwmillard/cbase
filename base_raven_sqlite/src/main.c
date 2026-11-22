#include <stdio.h>

#include <Raven.h>

#include "base.h"
#include "gui_xml.h"
#include "queries.h"
#include "schema.h"
#include "sqlite3.h"

#define DB_PATH "app.db"

static sqlite3 *db;
static arena scratch;

static Rid name_input;
static Rid age_input;
static Rid people_list;
static Rid status_label;

// Ids of the rows shown in people_list, indexed the same as its items.
static sql_int64 *people_ids;
static usize people_len;

// Adapt base.h's arena to the generated sql_allocator interface.
static void *arena_alloc_fn(void *ctx, size_t n) {
   return arena_alloc((arena *)ctx, n);
}

static void set_status(const char *text) {
   Rlabel_set_text(status_label, text);
}

static void refresh_people(void) {
   arena_reset(&scratch);
   sql_allocator alloc = {arena_alloc_fn, &scratch};

   int rc;
   PersonList people = getPeople(alloc, db, &rc);
   if (rc != SQLITE_OK) {
      set_status(sqlite3_errmsg(db));
      return;
   }

   Rlist_view_clear_items(people_list);
   people_ids = arena_alloc(&scratch, (people.len + 1) * sizeof(*people_ids));
   people_len = people.len;
   for (usize i = 0; i < people.len; i++) {
      Person *p = &people.items[i];
      people_ids[i] = p->id;
      char *text = arena_sprintf(&scratch, "%.*s (%lld)", (int)p->name.len,
                                 (char *)p->name.data, (long long)p->age);
      Rlist_view_add_item(people_list, text);
   }

   char status[64];
   snprintf(status, sizeof(status), "%zu people", people.len);
   set_status(status);
}

static void on_add(Rid sender, const char *action, void *user_data) {
   (void)sender;
   (void)action;
   (void)user_data;

   const char *name = Rtext_input_get_text(name_input);
   if (!name || !*name) {
      set_status("Enter a name");
      return;
   }

   sql_allocator alloc = {arena_alloc_fn, &scratch};
   CreatePersonParams params = {
       .name = to_sql_text((char *)name),
       .age = (sql_int64)Rspin_box_get_value(age_input),
   };
   int rc;
   createPerson(alloc, db, &params, &rc);
   if (rc != SQLITE_OK) {
      set_status(sqlite3_errmsg(db));
      return;
   }

   Rtext_input_set_text(name_input, "");
   refresh_people();
}

static void on_delete(Rid sender, const char *action, void *user_data) {
   (void)sender;
   (void)action;
   (void)user_data;

   int index = Rlist_view_get_selected(people_list);
   if (index < 0 || (usize)index >= people_len) {
      set_status("Select a person to delete");
      return;
   }

   int rc = deletePerson(db, people_ids[index]);
   if (rc != SQLITE_OK) {
      set_status(sqlite3_errmsg(db));
      return;
   }
   refresh_people();
}

static int open_db(const char *path) {
   int rc = sqlite3_open(path, &db);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "open %s: %s\n", path, sqlite3_errmsg(db));
      return rc;
   }

   char *errmsg = NULL;
   rc = sqlite3_exec(db, (const char *)schema_data, NULL, NULL, &errmsg);
   if (rc != SQLITE_OK) {
      fprintf(stderr, "schema: %s\n", errmsg);
      sqlite3_free(errmsg);
      return rc;
   }
   return SQLITE_OK;
}

int main(int argc, char *argv[]) {
   const char *path = argc > 1 ? argv[1] : DB_PATH;
   if (open_db(path) != SQLITE_OK) {
      sqlite3_close(db);
      return 1;
   }

   Rinit(R_BACKEND_METAL);

   Raction_bind("person.add", on_add, NULL);
   Raction_bind("person.delete", on_delete, NULL);

   Rid window = Rwindow_create_from_xml(gui_xml_data, gui_xml_len);
   if (window == R_INVALID_ID) {
      Rshutdown();
      sqlite3_close(db);
      return 1;
   }

   name_input = Rwidget_find(window, "name");
   age_input = Rwidget_find(window, "age");
   people_list = Rwidget_find(window, "people");
   status_label = Rwidget_find(window, "status");

   refresh_people();

   while (Rwait_frame(-1.0)) {
   }

   Rshutdown();
   arena_release(&scratch);
   sqlite3_close(db);
   return 0;
}
