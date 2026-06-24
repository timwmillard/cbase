#include <stdio.h>
#include <string.h>   /* system string.h (strlen/strncmp/strcmp) */

#define BASE_IMPLEMENTATION
#include "base.h"   /* our local base.h */

#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_RESET  "\033[0m"

#define PASS(name) printf(COLOR_GREEN "  PASS" COLOR_RESET "  %s\n", name)

static int _failed = 0;

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf(COLOR_RED "  FAIL" COLOR_RESET "  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        _failed++; \
    } \
} while(0)

#define ASSERT_STR(s, cstr) ASSERT((s).len == strlen(cstr) && strncmp((s).data, cstr, (s).len) == 0)

/* ------------------------------------------------------------------ */
/* Constructors / views                                               */
/* ------------------------------------------------------------------ */

/* S: compile-time literal view, length via sizeof */
void test_S_macro() {
    string s = S("hello");
    ASSERT(s.len == 5);
    ASSERT(strncmp(s.data, "hello", 5) == 0);
    string e = S("");
    ASSERT(e.len == 0);
    PASS("S_macro");
}

/* string_view: non-owning view of a C string, len via strlen */
void test_string_view() {
    const char *lit = "hello";
    string s = string_view(lit);
    ASSERT(s.len == 5);
    ASSERT(s.data == lit);
    PASS("string_view");
}

/* string_view_n: non-owning view of n bytes (not null-terminated) */
void test_string_view_n() {
    const char *lit = "hello world";
    string s = string_view_n(lit, 5);
    ASSERT(s.len == 5);
    ASSERT(s.data == lit);
    ASSERT_STR(s, "hello");
    PASS("string_view_n");
}

/* string_from: formatted, arena-owned copy */
void test_string_from() {
    arena a = {0};
    string s = string_from(&a, "hello %d", 42);
    ASSERT_STR(s, "hello 42");
    arena_release(&a);
    PASS("string_from");
}

/* string_from_n: copy n bytes into arena */
void test_string_from_n() {
    arena a = {0};
    string s = string_from_n(&a, "hello world", 5);
    ASSERT_STR(s, "hello");
    arena_release(&a);
    PASS("string_from_n");
}

/* string_dup: independent copy of an existing slice */
void test_string_dup() {
    arena a = {0};
    string view = string_view("hello");
    string copy = string_dup(&a, view);
    ASSERT_STR(copy, "hello");
    ASSERT(copy.data != view.data);   /* distinct backing bytes */
    arena_release(&a);
    PASS("string_dup");
}

/* string_cstr: null-terminated copy for C APIs */
void test_string_cstr() {
    arena a = {0};
    string s = string_view_n("hello world", 5);   /* slice, not terminated */
    const char *cs = string_cstr(&a, s);
    ASSERT(strcmp(cs, "hello") == 0);
    arena_release(&a);
    PASS("string_cstr");
}

/* ------------------------------------------------------------------ */
/* Read-only operations                                               */
/* ------------------------------------------------------------------ */

/* string_slice: positive and negative indices, clamping */
void test_string_slice() {
    string s = string_view("hello world");
    ASSERT_STR(string_slice(s, 6, 11), "world");
    ASSERT_STR(string_slice(s, -5, 11), "world");   /* -5 == len-5 == 6 */
    ASSERT_STR(string_slice(s, 0, 5), "hello");
    /* slices point INTO the source, no copy */
    ASSERT(string_slice(s, 6, 11).data == s.data + 6);
    PASS("string_slice");
}

/* string_trim family */
void test_string_trim() {
    ASSERT_STR(string_trim(string_view("  hello  ")), "hello");
    ASSERT_STR(string_trim(string_view("hello")), "hello");
    ASSERT(string_trim(string_view("   ")).len == 0);
    ASSERT_STR(string_trim_left(string_view("  hello  ")), "hello  ");
    ASSERT_STR(string_trim_right(string_view("  hello  ")), "  hello");
    PASS("string_trim");
}

/* string_eq */
void test_string_eq() {
    ASSERT(string_eq(string_view("hello"), S("hello")));
    ASSERT(!string_eq(string_view("hello"), S("world")));
    ASSERT(!string_eq(string_view("hello"), S("hell")));
    ASSERT(string_eq(S(""), S("")));
    PASS("string_eq");
}

/* string_starts_with / string_ends_with */
void test_string_starts_ends_with() {
    string s = string_view("hello world");
    ASSERT(string_starts_with(s, S("hello")));
    ASSERT(!string_starts_with(s, S("world")));
    ASSERT(string_ends_with(s, S("world")));
    ASSERT(!string_ends_with(s, S("hello")));
    ASSERT(string_starts_with(s, S("hello world")));
    ASSERT(string_ends_with(s, S("hello world")));
    PASS("string_starts_ends_with");
}

/* string_contains */
void test_string_contains() {
    string s = string_view("hello world");
    ASSERT(string_contains(s, S("lo wo")));
    ASSERT(string_contains(s, S("hello")));
    ASSERT(!string_contains(s, S("xyz")));
    PASS("string_contains");
}

/* string_index_of / string_index_of_char */
void test_string_index_of() {
    string s = string_view("hello world");
    ASSERT(string_index_of(s, S("world")) == 6);
    ASSERT(string_index_of(s, S("hello")) == 0);
    ASSERT(string_index_of(s, S("xyz")) == -1);
    ASSERT(string_index_of_char(s, 'w') == 6);
    ASSERT(string_index_of_char(s, 'z') == -1);
    PASS("string_index_of");
}

/* ------------------------------------------------------------------ */
/* Transforms (allocate)                                              */
/* ------------------------------------------------------------------ */

void test_string_upper_lower() {
    arena a = {0};
    string s = string_view("Hello World");
    ASSERT_STR(string_upper(&a, s), "HELLO WORLD");
    ASSERT_STR(string_lower(&a, s), "hello world");
    arena_release(&a);
    PASS("string_upper_lower");
}

void test_string_concat() {
    arena a = {0};
    string s = string_concat(&a, S("hello "), S("world"));
    ASSERT_STR(s, "hello world");
    arena_release(&a);
    PASS("string_concat");
}

void test_string_replace() {
    arena a = {0};
    string s = string_replace(&a, string_view("one fish two fish"), S("fish"), S("cat"));
    ASSERT_STR(s, "one cat two cat");
    arena_release(&a);
    PASS("string_replace");
}

/* ------------------------------------------------------------------ */
/* Split / join                                                       */
/* ------------------------------------------------------------------ */

void test_string_split() {
    arena a = {0};
    string_array arr = string_split(&a, string_view("one,two,three"), S(","));
    ASSERT(arr.count == 3);
    ASSERT_STR(arr.items[0], "one");
    ASSERT_STR(arr.items[1], "two");
    ASSERT_STR(arr.items[2], "three");
    /* no delimiter present: single element */
    string_array arr2 = string_split(&a, string_view("hello"), S(","));
    ASSERT(arr2.count == 1);
    ASSERT_STR(arr2.items[0], "hello");
    arena_release(&a);
    PASS("string_split");
}

void test_string_join() {
    arena a = {0};
    string_array arr = string_split(&a, string_view("one,two,three"), S(","));
    string joined = string_join(&a, arr, S(", "));
    ASSERT_STR(joined, "one, two, three");
    arena_release(&a);
    PASS("string_join");
}

/* ------------------------------------------------------------------ */
/* string_builder                                                     */
/* ------------------------------------------------------------------ */

void test_sb_append() {
    arena a = {0};
    string_builder sb = sb_init(&a);
    sb_append(&sb, S("hello"));
    sb_append_cstr(&sb, " world");
    sb_push(&sb, '!');
    ASSERT_STR(sb_string(&sb), "hello world!");
    ASSERT(strcmp(sb_cstr(&sb), "hello world!") == 0);
    arena_release(&a);
    PASS("sb_append");
}

void test_sb_appendf() {
    arena a = {0};
    string_builder sb = sb_init_cap(&a, 8);
    sb_appendf(&sb, "value=%d", 42);
    sb_appendf(&sb, " ok=%s", "yes");
    ASSERT_STR(sb_string(&sb), "value=42 ok=yes");
    arena_release(&a);
    PASS("sb_appendf");
}

void test_sb_reset() {
    arena a = {0};
    string_builder sb = sb_init(&a);
    sb_append(&sb, S("discard me"));
    sb_reset(&sb);
    ASSERT(sb_string(&sb).len == 0);
    sb_append(&sb, S("fresh"));
    ASSERT_STR(sb_string(&sb), "fresh");
    arena_release(&a);
    PASS("sb_reset");
}

/* sb_init_fixed: fixed buffer, no arena, must not grow past cap */
void test_sb_fixed() {
    char buf[16];
    string_builder sb = sb_init_fixed(buf, sizeof(buf));
    sb_append(&sb, S("hello"));
    ASSERT_STR(sb_string(&sb), "hello");
    ASSERT(sb_string(&sb).data == buf);
    ASSERT(sb.arena == NULL);
    PASS("sb_fixed");
}

int main(void) {
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
        printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET "\n\n");
    else
        printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n", _failed);
    return _failed != 0;
}
