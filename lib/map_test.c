#include <stdio.h>
#include <string.h> /* system string.h (strlen/strncmp) */

#define BASE_IMPLEMENTATION
#define MAP_IMPLEMENTATION
#include "base.h"
#include "map.h"

#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_BOLD  "\033[1m"
#define COLOR_RESET "\033[0m"

#define PASS(name) printf(COLOR_GREEN "  PASS" COLOR_RESET " %s\n", name)

static int _failed = 0;

/* ASSERT(expr, fmt, ...): on failure prints the owning test_name (an in-scope
 * string each test declares) plus a printf-style message and the expression. */
#define ASSERT(expr, ...)                                                      \
   do {                                                                        \
      if (!(expr)) {                                                           \
         printf(COLOR_RED "  FAIL" COLOR_RESET " %s:%d [%s] ", __FILE__,       \
                __LINE__, test_name);                                          \
         printf(__VA_ARGS__);                                                  \
         printf(" (%s)\n", #expr);                                             \
         _failed++;                                                            \
      }                                                                        \
   } while (0)

#define ASSERT_STR(s, cstr)                                                    \
   ASSERT((s).len == strlen(cstr) && strncmp((s).data, cstr, (s).len) == 0,    \
          "%s == \"%s\"", #s, cstr)

typedef MAP(int) int_map;

/* build a stack-backed "key-<n>" string; the map must not depend on it */
static string tmp_key(char *buf, usize bufsz, int n) {
   int len = snprintf(buf, bufsz, "key-%d", n);
   return string_view_n(buf, (usize)len);
}

/* ================================================================== */
/* map: init / put / lookup                                           */
/* ================================================================== */

/* map_init constructs an empty map wired to the arena */
void test_map_init(void) {
   const char *test_name = "map_init";
   arena a = {0};

   int_map m = map_init(&a);
   ASSERT(m.idx == NULL && m.keys == NULL && m.values == NULL,
          "map starts with no storage");
   ASSERT(m.len == 0 && m.cap == 0, "map starts empty");
   ASSERT(m.arena == &a, "map_init wires up the arena");

   /* lookups on an empty map are safe */
   ASSERT(map_index(&m, S("anything")) == -1, "index on empty map is -1");
   ASSERT(!map_contains(&m, S("anything")), "contains on empty map is false");
   ASSERT(map_at(&m, S("anything")) == NULL, "at on empty map is NULL");

   arena_release(&a);
   PASS(test_name);
}

/* put then read back via map_index and the dense values array */
void test_map_put_index(void) {
   const char *test_name = "map_put_index";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("one"), 1);
   map_put(&m, S("two"), 2);
   map_put(&m, S("three"), 3);
   ASSERT(m.len == 3, "len reflects all puts");

   isize i = map_index(&m, S("two"));
   ASSERT(i >= 0, "existing key is found");
   ASSERT(m.values[i] == 2, "index resolves to the right value");
   ASSERT_STR(m.keys[i], "two");

   ASSERT(map_index(&m, S("four")) == -1, "missing key returns -1");
   ASSERT(map_index(&m, S("on")) == -1, "prefix of a key is not a match");
   ASSERT(map_index(&m, S("onee")) == -1, "extension of a key is not a match");

   arena_release(&a);
   PASS(test_name);
}

/* putting an existing key overwrites in place, len unchanged */
void test_map_put_overwrite(void) {
   const char *test_name = "map_put_overwrite";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("k"), 1);
   isize before = map_index(&m, S("k"));
   map_put(&m, S("k"), 99);

   ASSERT(m.len == 1, "overwrite does not grow the map");
   ASSERT(map_index(&m, S("k")) == before, "overwrite keeps the same slot");
   ASSERT(m.values[before] == 99, "value is replaced");

   arena_release(&a);
   PASS(test_name);
}

/* map_at returns a live pointer, or NULL when absent */
void test_map_at(void) {
   const char *test_name = "map_at";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("answer"), 42);

   int *v = map_at(&m, S("answer"));
   ASSERT(v != NULL, "present key yields a pointer");
   ASSERT(*v == 42, "pointer reads the stored value");

   *v = 43; /* pointer is writable and aliases the dense array */
   ASSERT(m.values[map_index(&m, S("answer"))] == 43,
          "writes through the pointer land in the map");

   ASSERT(map_at(&m, S("question")) == NULL, "missing key yields NULL");

   arena_release(&a);
   PASS(test_name);
}

/* the empty string is a valid key, distinct from every other key */
void test_map_empty_key(void) {
   const char *test_name = "map_empty_key";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S(""), 7);
   map_put(&m, S("x"), 8);

   ASSERT(map_contains(&m, S("")), "empty key is stored");
   ASSERT(*(int *)map_at(&m, S("")) == 7, "empty key holds its own value");
   ASSERT(*(int *)map_at(&m, S("x")) == 8, "empty key doesn't shadow others");
   ASSERT(m.len == 2, "empty key counts as a normal entry");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: key ownership                                                 */
/* ================================================================== */

/* keys are duplicated on insert; the caller's buffer can be reused */
void test_map_key_dup(void) {
   const char *test_name = "map_key_dup";
   arena a = {0};

   int_map m = map_init(&a);
   char buf[32];
   for (int i = 0; i < 10; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i); /* same buf every time */
   memset(buf, 'Z', sizeof(buf)); /* clobber the source buffer */

   ASSERT(m.len == 10, "each distinct key got its own entry");
   char fresh[32];
   for (int i = 0; i < 10; i++) {
      string k = tmp_key(fresh, sizeof(fresh), i);
      isize at = map_index(&m, k);
      ASSERT(at >= 0 && m.values[at] == i, "key %d survives buffer reuse", i);
      ASSERT(m.keys[at].data != fresh, "stored key has its own bytes");
   }

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: intern (find-or-insert)                                       */
/* ================================================================== */

/* intern inserts a zeroed value for a new key */
void test_map_intern_new(void) {
   const char *test_name = "map_intern_new";
   arena a = {0};

   int_map m = map_init(&a);
   usize i = map_intern(&m, S("count"));
   ASSERT(m.len == 1, "intern inserts a missing key");
   ASSERT(m.values[i] == 0, "new value is zero-initialized");
   ASSERT_STR(m.keys[i], "count");

   arena_release(&a);
   PASS(test_name);
}

/* intern on an existing key returns its slot without touching the value */
void test_map_intern_existing(void) {
   const char *test_name = "map_intern_existing";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("count"), 5);
   usize i = map_intern(&m, S("count"));

   ASSERT(m.len == 1, "intern of an existing key does not insert");
   ASSERT(m.values[i] == 5, "existing value is untouched");

   /* the counter idiom: one lookup per increment */
   m.values[map_intern(&m, S("count"))]++;
   m.values[map_intern(&m, S("count"))]++;
   ASSERT(*(int *)map_at(&m, S("count")) == 7, "counter idiom works");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: growth / rehash                                               */
/* ================================================================== */

/* enough inserts to force several dense growths and index rebuilds */
void test_map_growth(void) {
   const char *test_name = "map_growth";
   arena a = {0};

   int_map m = map_init(&a);
   char buf[32];
   int total = 5000; /* well past MAP_INIT_CAP and many rehashes */
   for (int i = 0; i < total; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i);

   ASSERT(m.len == (usize)total, "len tracks all inserts across growth");
   ASSERT(m.cap >= (usize)total, "dense cap grew to fit");

   int all_found = 1;
   for (int i = 0; i < total; i++) {
      int *v = map_at(&m, tmp_key(buf, sizeof(buf), i));
      if (!v || *v != i)
         all_found = 0;
   }
   ASSERT(all_found, "every key survives rehashing");

   arena_release(&a);
   PASS(test_name);
}

/* map_reserve pre-sizes; filling within it never moves the arrays */
void test_map_reserve(void) {
   const char *test_name = "map_reserve";
   arena a = {0};

   int_map m = map_init(&a);
   map_reserve(&m, 100);
   ASSERT(m.cap >= 100, "reserve grows cap to fit");
   ASSERT(m.len == 0, "reserve does not change len");
   ASSERT(m.keys != NULL && m.values != NULL, "reserve pre-allocates storage");

   int *values_before = m.values;
   string *keys_before = m.keys;
   char buf[32];
   for (int i = 0; i < 100; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i);
   ASSERT(m.values == values_before, "values pointer stable when reserved");
   ASSERT(m.keys == keys_before, "keys pointer stable when reserved");

   map_reserve(&m, 10); /* smaller than current: no-op */
   ASSERT(m.cap >= 100, "reserve below cap is a no-op");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: delete                                                        */
/* ================================================================== */

/* delete removes the key, decrements len, and reports what it did */
void test_map_del(void) {
   const char *test_name = "map_del";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("a"), 1);
   map_put(&m, S("b"), 2);
   map_put(&m, S("c"), 3);

   ASSERT(map_del(&m, S("b")), "deleting a present key returns true");
   ASSERT(m.len == 2, "len shrinks by one");
   ASSERT(!map_contains(&m, S("b")), "deleted key is gone");
   ASSERT(*(int *)map_at(&m, S("a")) == 1, "other keys unaffected");
   ASSERT(*(int *)map_at(&m, S("c")) == 3, "other keys unaffected");

   ASSERT(!map_del(&m, S("b")), "deleting a missing key returns false");
   ASSERT(!map_del(&m, S("zzz")), "deleting an unknown key returns false");
   ASSERT(m.len == 2, "failed delete does not change len");

   arena_release(&a);
   PASS(test_name);
}

/* delete swap-removes: the last entry moves into the hole and stays findable */
void test_map_del_swap(void) {
   const char *test_name = "map_del_swap";
   arena a = {0};

   int_map m = map_init(&a);
   map_put(&m, S("first"), 1);
   map_put(&m, S("middle"), 2);
   map_put(&m, S("last"), 3);

   isize hole = map_index(&m, S("middle"));
   map_del(&m, S("middle"));

   /* the previously-last entry now occupies the vacated dense slot */
   ASSERT(map_index(&m, S("last")) == hole, "last entry moved into the hole");
   ASSERT(*(int *)map_at(&m, S("last")) == 3, "moved entry keeps its value");
   ASSERT(*(int *)map_at(&m, S("first")) == 1, "untouched entry still intact");

   /* deleting the (new) final entry exercises the i == last path */
   ASSERT(map_del(&m, S("last")), "deleting the final dense entry works");
   ASSERT(m.len == 1 && map_contains(&m, S("first")),
          "only the untouched entry remains");

   arena_release(&a);
   PASS(test_name);
}

/* heavy delete/reinsert churn: tombstones must be reused and cleaned up */
void test_map_del_churn(void) {
   const char *test_name = "map_del_churn";
   arena a = {0};

   int_map m = map_init(&a);
   char buf[32];
   int n = 1000;
   for (int i = 0; i < n; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i);

   for (int i = 0; i < n; i += 2) /* delete evens */
      ASSERT(map_del(&m, tmp_key(buf, sizeof(buf), i)), "delete key %d", i);
   ASSERT(m.len == (usize)n / 2, "half the entries remain");

   int correct = 1;
   for (int i = 0; i < n; i++) {
      int *v = map_at(&m, tmp_key(buf, sizeof(buf), i));
      if (i % 2 == 0 ? v != NULL : (!v || *v != i))
         correct = 0;
   }
   ASSERT(correct, "evens are gone, odds are intact");

   for (int i = 0; i < n; i += 2) /* reinsert into tombstoned territory */
      map_put(&m, tmp_key(buf, sizeof(buf), i), -i);
   ASSERT(m.len == (usize)n, "reinserts restore the full count");

   correct = 1;
   for (int i = 0; i < n; i++) {
      int *v = map_at(&m, tmp_key(buf, sizeof(buf), i));
      int want = (i % 2 == 0) ? -i : i;
      if (!v || *v != want)
         correct = 0;
   }
   ASSERT(correct, "all values correct after churn");

   /* churn in place at constant size: load factor must stay healthy */
   for (int round = 0; round < 20; round++) {
      for (int i = 1; i < n; i += 2) {
         map_del(&m, tmp_key(buf, sizeof(buf), i));
         map_put(&m, tmp_key(buf, sizeof(buf), i), i + round);
      }
   }
   ASSERT(m.len == (usize)n, "constant-size churn preserves len");
   ASSERT(*(int *)map_at(&m, tmp_key(buf, sizeof(buf), 1)) == 1 + 19,
          "last churn round's value wins");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: clear / iteration                                             */
/* ================================================================== */

/* clear empties the map but keeps capacity for reuse */
void test_map_clear(void) {
   const char *test_name = "map_clear";
   arena a = {0};

   int_map m = map_init(&a);
   char buf[32];
   for (int i = 0; i < 100; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i);

   usize cap_before = m.cap;
   int *values_before = m.values;
   map_clear(&m);

   ASSERT(m.len == 0, "clear empties the map");
   ASSERT(!map_contains(&m, S("key-5")), "cleared keys are gone");
   ASSERT(m.cap == cap_before, "clear keeps dense capacity");

   map_put(&m, S("again"), 42);
   ASSERT(m.values == values_before, "refill reuses existing storage");
   ASSERT(*(int *)map_at(&m, S("again")) == 42, "map is usable after clear");

   arena_release(&a);
   PASS(test_name);
}

/* iteration is a plain loop over the dense arrays */
void test_map_iteration(void) {
   const char *test_name = "map_iteration";
   arena a = {0};

   int_map m = map_init(&a);
   char buf[32];
   int n = 100;
   for (int i = 0; i < n; i++)
      map_put(&m, tmp_key(buf, sizeof(buf), i), i);

   /* insertion order holds until the first delete */
   int in_order = 1;
   for (usize k = 0; k < m.len; k++)
      if (m.values[k] != (int)k)
         in_order = 0;
   ASSERT(in_order, "iteration is in insertion order before deletes");

   /* every (key, value) pair seen during iteration agrees with lookup */
   int consistent = 1;
   long sum = 0;
   for (usize k = 0; k < m.len; k++) {
      if (map_index(&m, m.keys[k]) != (isize)k)
         consistent = 0;
      sum += m.values[k];
   }
   ASSERT(consistent, "iterated keys resolve back to their own slot");
   ASSERT(sum == (long)n * (n - 1) / 2, "iteration visits each entry once");

   /* after deletes, iteration still covers exactly the live entries */
   map_del(&m, S("key-10"));
   map_del(&m, S("key-20"));
   sum = 0;
   for (usize k = 0; k < m.len; k++)
      sum += m.values[k];
   ASSERT(sum == (long)n * (n - 1) / 2 - 10 - 20,
          "deleted entries are not visited");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* map: non-int values / hash                                         */
/* ================================================================== */

/* MAP(V) works with aggregate value types */
void test_map_struct_values(void) {
   const char *test_name = "map_struct_values";
   arena a = {0};

   typedef struct {
      double x, y;
      int id;
   } point;
   typedef MAP(point) point_map;

   point_map m = map_init(&a);
   map_put(&m, S("origin"), ((point){0, 0, 1}));
   map_put(&m, S("unit"), ((point){1, 1, 2}));

   point *p = map_at(&m, S("unit"));
   ASSERT(p != NULL, "struct value is retrievable");
   ASSERT(p->x == 1 && p->y == 1 && p->id == 2, "struct fields round-trip");

   usize i = map_intern(&m, S("fresh"));
   ASSERT(m.values[i].x == 0 && m.values[i].y == 0 && m.values[i].id == 0,
          "interned struct value is fully zeroed");

   arena_release(&a);
   PASS(test_name);
}

/* string values compose with base.h's own string type */
void test_map_string_values(void) {
   const char *test_name = "map_string_values";
   arena a = {0};

   typedef MAP(string) string_map;
   string_map m = map_init(&a);
   map_put(&m, S("greeting"), S("hello"));
   map_put(&m, S("subject"), stringf(&a, "world %d", 42));

   string *s = map_at(&m, S("subject"));
   ASSERT(s != NULL, "string value is retrievable");
   ASSERT_STR(*s, "world 42");
   ASSERT_STR(*(string *)map_at(&m, S("greeting")), "hello");

   arena_release(&a);
   PASS(test_name);
}

/* string_hash: deterministic, length-sensitive, empty-safe */
void test_string_hash(void) {
   const char *test_name = "string_hash";

   ASSERT(string_hash(S("hello")) == string_hash(S("hello")),
          "hash is deterministic");
   ASSERT(string_hash(S("hello")) != string_hash(S("world")),
          "different strings hash differently (sanity)");
   ASSERT(string_hash(S("a")) != string_hash(S("aa")),
          "hash is length-sensitive");
   (void)string_hash(S("")); /* must not crash */
   ASSERT(string_hash(S("")) == string_hash(S("")),
          "empty string hashes consistently");

   PASS(test_name);
}

int main(void) {
   printf(COLOR_BOLD "\nmap tests\n" COLOR_RESET "\n");
   test_map_init();
   test_map_put_index();
   test_map_put_overwrite();
   test_map_at();
   test_map_empty_key();
   test_map_key_dup();
   test_map_intern_new();
   test_map_intern_existing();
   test_map_growth();
   test_map_reserve();
   test_map_del();
   test_map_del_swap();
   test_map_del_churn();
   test_map_clear();
   test_map_iteration();
   test_map_struct_values();
   test_map_string_values();
   test_string_hash();

   if (_failed == 0)
      printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET
             "\n\n");
   else
      printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n",
             _failed);
   return _failed != 0;
}
