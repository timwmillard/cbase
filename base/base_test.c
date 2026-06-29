#include <stdio.h>
#include <string.h> /* system string.h (strlen/strncmp/strcmp) */

#define BASE_IMPLEMENTATION
#include "base.h" /* our local base.h */

#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_BOLD  "\033[1m"
#define COLOR_RESET "\033[0m"

#define PASS(name) printf(COLOR_GREEN "  PASS" COLOR_RESET "  %s\n", name)

static int _failed = 0;

/* ASSERT(expr, fmt, ...): on failure prints the owning test_name (an in-scope
 * string each test declares) plus a printf-style message and the expression. */
#define ASSERT(expr, ...)                                                      \
   do {                                                                        \
      if (!(expr)) {                                                           \
         printf(COLOR_RED "  FAIL" COLOR_RESET "  %s:%d  [%s] ", __FILE__,     \
                __LINE__, test_name);                                          \
         printf(__VA_ARGS__);                                                  \
         printf("  (%s)\n", #expr);                                            \
         _failed++;                                                            \
      }                                                                        \
   } while (0)

#define ASSERT_STR(s, cstr)                                                    \
   ASSERT((s).len == strlen(cstr) && strncmp((s).data, cstr, (s).len) == 0,    \
          "%s == \"%s\"", #s, cstr)

/* ================================================================== */
/* arena                                                              */
/* ================================================================== */

/* basic alloc returns non-null and is writable */
void test_alloc_basic(void) {
   const char *test_name = "alloc basic";
   arena a = {0};
   u8 *mem = arena_alloc(&a, 1024);
   ASSERT(mem != NULL, "alloc returns non-null");
   memset(mem, 0xcb, 1024);
   ASSERT(mem[0] == (u8)0xcb && mem[1023] == (u8)0xcb,
          "memory is writable end to end");
   arena_release(&a);
   PASS(test_name);
}

/* multiple allocations don't overlap */
void test_alloc_no_overlap(void) {
   const char *test_name = "alloc no overlap";
   arena a = {0};
   u8 *a1 = arena_alloc(&a, 64);
   u8 *a2 = arena_alloc(&a, 64);
   u8 *a3 = arena_alloc(&a, 64);
   memset(a1, 0x11, 64);
   memset(a2, 0x22, 64);
   memset(a3, 0x33, 64);
   ASSERT(a1[0] == (u8)0x11, "first alloc unclobbered");
   ASSERT(a2[0] == (u8)0x22, "second alloc unclobbered");
   ASSERT(a3[0] == (u8)0x33, "third alloc unclobbered");
   arena_release(&a);
   PASS(test_name);
}

/* returned pointers are word-aligned */
void test_alloc_alignment(void) {
   const char *test_name = "alloc alignment";
   arena a = {0};
   /* vary sizes to stress alignment of subsequent allocs */
   (void)arena_alloc(&a, 1);
   int *p2 = arena_alloc(&a, sizeof(int));
   double *p3 = arena_alloc(&a, sizeof(double));
   (void)arena_alloc(&a, 3);
   int *p4 = arena_alloc(&a, sizeof(int));
   ASSERT(((uptr)p2 % sizeof(uptr)) == 0, "p2 is word-aligned");
   ASSERT(((uptr)p3 % sizeof(uptr)) == 0, "p3 is word-aligned");
   ASSERT(((uptr)p4 % sizeof(uptr)) == 0, "p4 is word-aligned");
   arena_release(&a);
   PASS(test_name);
}

/* force region growth by exceeding the default region size */
void test_alloc_multi_region(void) {
   const char *test_name = "alloc multi region";
   arena a = {0};
   int total = (ARENA_REGION_DEFAULT_SIZE_BYTES / 512) * 4;
   for (int i = 0; i < total; i++) {
      void *p = arena_alloc(&a, 512);
      ASSERT(p != NULL, "alloc %d of %d succeeds", i, total);
   }
   ASSERT(a.start != a.end, "arena grew beyond one region");
   arena_release(&a);
   PASS(test_name);
}

/* single allocation larger than the default region size */
void test_alloc_large(void) {
   const char *test_name = "alloc large";
   arena a = {0};
   usize big = ARENA_REGION_DEFAULT_SIZE_BYTES * 4;
   u8 *p = arena_alloc(&a, big);
   ASSERT(p != NULL, "oversized alloc succeeds");
   memset(p, 0xAB, big);
   ASSERT(p[0] == (u8)0xAB && p[big - 1] == (u8)0xAB,
          "full %zu bytes are writable", big);
   arena_release(&a);
   PASS(test_name);
}

/* realloc grows and copies existing data */
void test_realloc_grow(void) {
   const char *test_name = "realloc grow";
   arena a = {0};
   char *p = arena_alloc(&a, 16);
   memcpy(p, "hello world!!!!!", 16);
   char *p2 = arena_realloc(&a, p, 16, 32);
   ASSERT(p2 != NULL, "grown alloc is non-null");
   ASSERT(memcmp(p2, "hello world!!!!!", 16) == 0, "existing data preserved");
   arena_release(&a);
   PASS(test_name);
}

/* realloc with newsz <= oldsz returns the same pointer unchanged */
void test_realloc_no_shrink(void) {
   const char *test_name = "realloc no shrink";
   arena a = {0};
   char *p = arena_alloc(&a, 64);
   char *p2 = arena_realloc(&a, p, 64, 32);
   ASSERT(p == p2, "shrink returns same pointer");
   arena_release(&a);
   PASS(test_name);
}

/* release zeroes the arena; it can be reused immediately after */
void test_release_and_reuse(void) {
   const char *test_name = "release and reuse";
   arena a = {0};
   for (int i = 0; i < 50; i++)
      arena_alloc(&a, 256);
   arena_release(&a);
   ASSERT(a.start == NULL && a.end == NULL, "release zeroes the arena");
   void *p = arena_alloc(&a, 64);
   ASSERT(p != NULL, "arena is usable after release");
   arena_release(&a);
   PASS(test_name);
}

/* reset keeps regions but resets len; memory can be reused without malloc */
void test_reset(void) {
   const char *test_name = "reset";
   arena a = {0};
   for (int i = 0; i < 50; i++)
      arena_alloc(&a, 256);
   arena_region *first = a.start; /* remember the region pointer */
   arena_reset(&a);
   ASSERT(a.start == first, "regions are retained after reset");
   ASSERT(a.start->len == 0, "region len is reset to 0");
   void *p = arena_alloc(&a, 64);
   ASSERT(p != NULL, "arena is usable after reset");
   arena_release(&a);
   PASS(test_name);
}

/* arena_new / arena_array: correct size, typed, writable */
void test_arena_new_array(void) {
   const char *test_name = "arena_new_array";
   arena a = {0};

   typedef struct {
      int x;
      double y;
   } point;

   point *p = arena_new(&a, point);
   ASSERT(p != NULL, "arena_new returns non-null");
   ASSERT(((uptr)p % sizeof(uptr)) == 0, "arena_new result is word-aligned");
   p->x = 7;
   p->y = 1.5;
   ASSERT(p->x == 7 && p->y == 1.5, "struct is writable");

   point *xs = arena_array(&a, point, 16);
   ASSERT(xs != NULL, "arena_array returns non-null");
   for (int i = 0; i < 16; i++) {
      xs[i].x = i;
      xs[i].y = (double)i;
   }
   ASSERT(xs[0].x == 0 && xs[15].x == 15, "array is writable end to end");
   ASSERT((uptr)p != (uptr)xs, "distinct allocations don't alias");

   arena_release(&a);
   PASS(test_name);
}

/* arena_new / arena_array always return zeroed memory */
void test_arena_new_array_zero(void) {
   const char *test_name = "arena_new_array_zero";
   arena a = {0};

   /* dirty the arena so a fresh zeroed alloc has something to overwrite */
   u8 *dirty = arena_alloc(&a, 256);
   memset(dirty, 0xcb, 256);
   arena_reset(&a);

   typedef struct {
      int x;
      int y;
      char tag[8];
   } rec;

   rec *r = arena_new(&a, rec);
   ASSERT(r != NULL, "arena_new returns non-null");
   ASSERT(r->x == 0 && r->y == 0, "scalar fields zeroed");
   int tag_zero = 1;
   for (size_t i = 0; i < sizeof(r->tag); i++)
      if (r->tag[i] != 0)
         tag_zero = 0;
   ASSERT(tag_zero, "array field zeroed");

   u8 *bytes = arena_array(&a, u8, 128);
   ASSERT(bytes != NULL, "arena_array returns non-null");
   int all_zero = 1;
   for (int i = 0; i < 128; i++)
      if (bytes[i] != 0)
         all_zero = 0;
   ASSERT(all_zero, "all 128 bytes are zero");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* list: growable dynamic array                                       */
/* ================================================================== */

typedef LIST(int) int_list;

/* append grows from empty, preserves order, and tracks count */
void test_list_append(void) {
   const char *test_name = "list_append";
   arena a = {0};
   int_list xs = list_init(&a);

   ASSERT(xs.items == NULL && xs.len == 0 && xs.cap == 0,
          "list starts empty");

   for (int i = 0; i < 100; i++)
      list_append(&xs, i * 10);

   ASSERT(xs.len == 100, "len reflects all appends");
   ASSERT(xs.cap >= 100, "cap grew to fit");
   ASSERT(xs.items != NULL, "backing storage allocated");

   int in_order = 1;
   for (int i = 0; i < 100; i++)
      if (xs.items[i] != i * 10)
         in_order = 0;
   ASSERT(in_order, "items preserved in append order");

   arena_release(&a);
   PASS(test_name);
}

/* capacity grows by doubling from LIST_INIT_CAP */
void test_list_growth(void) {
   const char *test_name = "list_growth";
   arena a = {0};
   int_list xs = list_init(&a);

   list_append(&xs, 1);
   ASSERT(xs.cap == LIST_INIT_CAP, "first append reserves LIST_INIT_CAP");

   while (xs.len < LIST_INIT_CAP)
      list_append(&xs, (int)xs.len);
   ASSERT(xs.cap == LIST_INIT_CAP, "no growth until full");

   list_append(&xs, 999); /* trips the growth */
   ASSERT(xs.cap == LIST_INIT_CAP * 2, "cap doubles when full");

   arena_release(&a);
   PASS(test_name);
}

/* list_reserve pre-sizes capacity without changing count */
void test_list_reserve(void) {
   const char *test_name = "list_reserve";
   arena a = {0};
   int_list xs = list_init(&a);

   list_reserve(&xs, 50);
   ASSERT(xs.cap >= 50, "reserve grows cap to fit");
   ASSERT(xs.len == 0, "reserve does not change len");

   usize cap_before = xs.cap;
   int *items_before = xs.items;
   for (int i = 0; i < 50; i++)
      list_append(&xs, i);
   ASSERT(xs.cap == cap_before, "no realloc within reserved cap");
   ASSERT(xs.items == items_before, "backing pointer stable when reserved");

   list_reserve(&xs, 10); /* smaller than current: no-op */
   ASSERT(xs.cap == cap_before, "reserve below cap is a no-op");

   arena_release(&a);
   PASS(test_name);
}

/* list_init constructs an empty list; list_reserve pre-sizes it */
void test_list_init(void) {
   const char *test_name = "list_init";
   arena a = {0};

   int_list xs = list_init(&a);
   ASSERT(xs.items == NULL && xs.len == 0 && xs.cap == 0, "list_init is empty");
   ASSERT(xs.arena == &a, "list_init wires up the arena");

   /* pre-allocate with list_reserve, then fill without regrowth */
   int_list ys = list_init(&a);
   list_reserve(&ys, 32);
   ASSERT(ys.items != NULL, "reserve pre-allocates storage");
   ASSERT(ys.cap >= 32, "reserve gives capacity for 32 items");
   ASSERT(ys.len == 0, "reserve leaves the list empty");

   int *items_before = ys.items;
   for (int i = 0; i < 32; i++)
      list_append(&ys, i);
   ASSERT(ys.items == items_before, "no realloc within reserved cap");
   ASSERT(ys.len == 32, "all appends land in reserved storage");

   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* string: constructors / views                                       */
/* ================================================================== */

/* S: compile-time literal view, length via sizeof */
void test_S_macro(void) {
   const char *test_name = "S_macro";
   string s = S("hello");
   ASSERT(s.len == 5, "len is 5");
   ASSERT(strncmp(s.data, "hello", 5) == 0, "data is \"hello\"");
   string e = S("");
   ASSERT(e.len == 0, "empty literal has len 0");
   PASS(test_name);
}

/* string_view: non-owning view of a C string, len via strlen */
void test_string_view(void) {
   const char *test_name = "string_view";
   const char *lit = "hello";
   string s = string_view(lit);
   ASSERT(s.len == 5, "len is 5");
   ASSERT(s.data == lit, "view aliases the source pointer");
   PASS(test_name);
}

/* string_view_n: non-owning view of n bytes (not null-terminated) */
void test_string_view_n(void) {
   const char *test_name = "string_view_n";
   const char *lit = "hello world";
   string s = string_view_n(lit, 5);
   ASSERT(s.len == 5, "len is 5");
   ASSERT(s.data == lit, "view aliases the source pointer");
   ASSERT_STR(s, "hello");
   PASS(test_name);
}

/* string_from: formatted, arena-owned copy */
void test_string_from(void) {
   const char *test_name = "string_from";
   arena a = {0};
   string s = string_from(&a, "hello %d", 42);
   ASSERT_STR(s, "hello 42");
   arena_release(&a);
   PASS(test_name);
}

/* string_from_n: copy n bytes into arena */
void test_string_from_n(void) {
   const char *test_name = "string_from_n";
   arena a = {0};
   string s = string_from_n(&a, "hello world", 5);
   ASSERT_STR(s, "hello");
   arena_release(&a);
   PASS(test_name);
}

/* string_dup: independent copy of an existing slice */
void test_string_dup(void) {
   const char *test_name = "string_dup";
   arena a = {0};
   string view = string_view("hello");
   string copy = string_dup(&a, view);
   ASSERT_STR(copy, "hello");
   ASSERT(copy.data != view.data, "copy has distinct backing bytes");
   arena_release(&a);
   PASS(test_name);
}

/* string_cstr: null-terminated copy for C APIs */
void test_string_cstr(void) {
   const char *test_name = "string_cstr";
   arena a = {0};
   string s = string_view_n("hello world", 5); /* slice, not terminated */
   const char *cs = string_cstr(&a, s);
   ASSERT(strcmp(cs, "hello") == 0, "cstr is null-terminated \"hello\"");
   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* string: read-only operations                                       */
/* ================================================================== */

/* string_slice: positive and negative indices, clamping */
void test_string_slice(void) {
   const char *test_name = "string_slice";
   string s = S("hello world");
   ASSERT_STR(string_slice(s, 6, 11), "world");
   ASSERT_STR(string_slice(s, -5, 11), "world"); /* -5 == len-5 == 6 */
   ASSERT_STR(string_slice(s, 0, 5), "hello");
   /* slices point INTO the source, no copy */
   ASSERT(string_slice(s, 6, 11).data == s.data + 6,
          "slice points into source");
   PASS(test_name);
}

/* string_trim family */
void test_string_trim(void) {
   const char *test_name = "string_trim";
   ASSERT_STR(string_trim(S("  hello  ")), "hello");
   ASSERT_STR(string_trim(S("hello")), "hello");
   ASSERT(string_trim(S("   ")).len == 0, "all-space trims to empty");
   ASSERT_STR(string_trim_left(S("  hello  ")), "hello  ");
   ASSERT_STR(string_trim_right(S("  hello  ")), "  hello");
   PASS(test_name);
}

/* string_eq */
void test_string_eq(void) {
   const char *test_name = "string_eq";
   ASSERT(string_eq(S("hello"), S("hello")), "equal strings compare equal");
   ASSERT(!string_eq(S("hello"), S("world")), "different content not equal");
   ASSERT(!string_eq(S("hello"), S("hell")), "different length not equal");
   ASSERT(string_eq(S(""), S("")), "empty equals empty");
   PASS(test_name);
}

/* string_starts_with / string_ends_with */
void test_string_starts_ends_with(void) {
   const char *test_name = "string_starts_ends_with";
   string s = S("hello world");
   ASSERT(string_starts_with(s, S("hello")), "starts with \"hello\"");
   ASSERT(!string_starts_with(s, S("world")), "does not start with \"world\"");
   ASSERT(string_ends_with(s, S("world")), "ends with \"world\"");
   ASSERT(!string_ends_with(s, S("hello")), "does not end with \"hello\"");
   ASSERT(string_starts_with(s, S("hello world")), "starts with whole string");
   ASSERT(string_ends_with(s, S("hello world")), "ends with whole string");
   PASS(test_name);
}

/* string_contains */
void test_string_contains(void) {
   const char *test_name = "string_contains";
   string s = S("hello world");
   ASSERT(string_contains(s, S("lo wo")), "contains interior substring");
   ASSERT(string_contains(s, S("hello")), "contains leading substring");
   ASSERT(!string_contains(s, S("xyz")), "does not contain \"xyz\"");
   PASS(test_name);
}

/* string_index_of / string_index_of_char */
void test_string_index_of(void) {
   const char *test_name = "string_index_of";
   string s = S("hello world");
   ASSERT(string_index_of(s, S("world")) == 6, "\"world\" at index 6");
   ASSERT(string_index_of(s, S("hello")) == 0, "\"hello\" at index 0");
   ASSERT(string_index_of(s, S("xyz")) == -1, "missing substring returns -1");
   ASSERT(string_index_of_char(s, 'w') == 6, "'w' at index 6");
   ASSERT(string_index_of_char(s, 'z') == -1, "missing char returns -1");
   PASS(test_name);
}

/* ================================================================== */
/* string: transforms (allocate)                                      */
/* ================================================================== */

void test_string_upper_lower(void) {
   const char *test_name = "string_upper_lower";
   arena a = {0};
   string s = S("Hello World");
   ASSERT_STR(string_upper(&a, s), "HELLO WORLD");
   ASSERT_STR(string_lower(&a, s), "hello world");
   arena_release(&a);
   PASS(test_name);
}

void test_string_concat(void) {
   const char *test_name = "string_concat";
   arena a = {0};
   string s = string_concat(&a, S("hello "), S("world"));
   ASSERT_STR(s, "hello world");
   arena_release(&a);
   PASS(test_name);
}

void test_string_replace(void) {
   const char *test_name = "string_replace";
   arena a = {0};
   string s = string_replace(&a, S("one fish two fish"), S("fish"), S("cat"));
   ASSERT_STR(s, "one cat two cat");
   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* string: split / join                                               */
/* ================================================================== */

void test_string_split(void) {
   const char *test_name = "string_split";
   arena a = {0};
   string_array arr = string_split(&a, S("one,two,three"), S(","));
   ASSERT(arr.len == 3, "split into 3 parts");
   ASSERT_STR(arr.items[0], "one");
   ASSERT_STR(arr.items[1], "two");
   ASSERT_STR(arr.items[2], "three");
   /* no delimiter present: single element */
   string_array arr2 = string_split(&a, S("hello"), S(","));
   ASSERT(arr2.len == 1, "no delimiter yields single element");
   ASSERT_STR(arr2.items[0], "hello");
   arena_release(&a);
   PASS(test_name);
}

void test_string_join(void) {
   const char *test_name = "string_join";
   arena a = {0};
   string_array arr = string_split(&a, S("one,two,three"), S(","));
   string joined = string_join(&a, arr, S(", "));
   ASSERT_STR(joined, "one, two, three");
   arena_release(&a);
   PASS(test_name);
}

/* ================================================================== */
/* string_builder                                                     */
/* ================================================================== */

void test_sb_append(void) {
   const char *test_name = "sb_append";
   arena a = {0};
   string_builder sb = sb_init(&a);
   sb_append(&sb, S("hello"));
   sb_append_cstr(&sb, " world");
   sb_push(&sb, '!');
   ASSERT_STR(sb_string(&sb), "hello world!");
   ASSERT(strcmp(sb_cstr(&sb), "hello world!") == 0,
          "cstr matches built string");
   arena_release(&a);
   PASS(test_name);
}

void test_sb_appendf(void) {
   const char *test_name = "sb_appendf";
   arena a = {0};
   string_builder sb = sb_init_cap(&a, 8);
   sb_appendf(&sb, "value=%d", 42);
   sb_appendf(&sb, " ok=%s", "yes");
   ASSERT_STR(sb_string(&sb), "value=42 ok=yes");
   arena_release(&a);
   PASS(test_name);
}

void test_sb_reset(void) {
   const char *test_name = "sb_reset";
   arena a = {0};
   string_builder sb = sb_init(&a);
   sb_append(&sb, S("discard me"));
   sb_reset(&sb);
   ASSERT(sb_string(&sb).len == 0, "reset empties the builder");
   sb_append(&sb, S("fresh"));
   ASSERT_STR(sb_string(&sb), "fresh");
   arena_release(&a);
   PASS(test_name);
}

/* sb_init_fixed: fixed buffer, no arena, must not grow past cap */
void test_sb_fixed(void) {
   const char *test_name = "sb_fixed";
   char buf[16];
   string_builder sb = sb_init_fixed(buf, sizeof(buf));
   sb_append(&sb, S("hello"));
   ASSERT_STR(sb_string(&sb), "hello");
   ASSERT(sb_string(&sb).data == buf, "builder writes into the fixed buffer");
   ASSERT(sb.arena == NULL, "fixed builder has no arena");
   PASS(test_name);
}

int main(void) {
   printf(COLOR_BOLD "\narena tests\n" COLOR_RESET "\n");

   test_alloc_basic();
   test_alloc_no_overlap();
   test_alloc_alignment();
   test_alloc_multi_region();
   test_alloc_large();
   test_realloc_grow();
   test_realloc_no_shrink();
   test_release_and_reuse();
   test_reset();
   test_arena_new_array();
   test_arena_new_array_zero();

   printf(COLOR_BOLD "\nlist tests\n" COLOR_RESET "\n");

   test_list_append();
   test_list_growth();
   test_list_reserve();
   test_list_init();

   printf(COLOR_BOLD "\nstring tests\n" COLOR_RESET "\n");

   test_S_macro();
   test_string_view();
   test_string_view_n();
   test_string_from();
   test_string_from_n();
   test_string_dup();
   test_string_cstr();
   test_string_slice();
   test_string_trim();
   test_string_eq();
   test_string_starts_ends_with();
   test_string_contains();
   test_string_index_of();
   test_string_upper_lower();
   test_string_concat();
   test_string_replace();
   test_string_split();
   test_string_join();
   test_sb_append();
   test_sb_appendf();
   test_sb_reset();
   test_sb_fixed();

   if (_failed == 0)
      printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET
             "\n\n");
   else
      printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n",
             _failed);
   return _failed != 0;
}
