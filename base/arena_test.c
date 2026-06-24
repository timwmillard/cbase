#include <stdio.h>
#include <string.h>

#define BASE_IMPLEMENTATION
#include "base.h"

#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_RESET  "\033[0m"

#define PASS(name) printf(COLOR_GREEN "  PASS" COLOR_RESET "  %s\n", name)

static int _failed = 0;

/* ASSERT(expr, fmt, ...): on failure prints the owning test_name (an in-scope
 * string each test declares) plus a printf-style message and the expression. */
#define ASSERT(expr, ...) do { \
    if (!(expr)) { \
        printf(COLOR_RED "  FAIL" COLOR_RESET "  %s:%d  [%s] ", __FILE__, __LINE__, test_name); \
        printf(__VA_ARGS__); \
        printf("  (%s)\n", #expr); \
        _failed++; \
    } \
} while(0)

/* basic alloc returns non-null and is writable */
void test_alloc_basic() {
    const char *test_name = "alloc basic";
    arena a = {0};
    u8 *mem = arena_alloc(&a, 1024);
    ASSERT(mem != NULL, "alloc returns non-null");
    memset(mem, 0xcb, 1024);
    ASSERT(mem[0] == (u8)0xcb && mem[1023] == (u8)0xcb, "memory is writable end to end");
    arena_release(&a);
    PASS(test_name);
}

/* multiple allocations don't overlap */
void test_alloc_no_overlap() {
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
void test_alloc_alignment() {
    const char *test_name = "alloc alignment";
    arena a = {0};
    /* vary sizes to stress alignment of subsequent allocs */
    (void)arena_alloc(&a, 1);
    int    *p2 = arena_alloc(&a, sizeof(int));
    double *p3 = arena_alloc(&a, sizeof(double));
    (void)arena_alloc(&a, 3);
    int    *p4 = arena_alloc(&a, sizeof(int));
    ASSERT(((uptr)p2 % sizeof(uptr)) == 0, "p2 is word-aligned");
    ASSERT(((uptr)p3 % sizeof(uptr)) == 0, "p3 is word-aligned");
    ASSERT(((uptr)p4 % sizeof(uptr)) == 0, "p4 is word-aligned");
    arena_release(&a);
    PASS(test_name);
}

/* force region growth by exceeding the default region size */
void test_alloc_multi_region() {
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
void test_alloc_large() {
    const char *test_name = "alloc large";
    arena a = {0};
    usize big = ARENA_REGION_DEFAULT_SIZE_BYTES * 4;
    u8 *p = arena_alloc(&a, big);
    ASSERT(p != NULL, "oversized alloc succeeds");
    memset(p, 0xAB, big);
    ASSERT(p[0] == (u8)0xAB && p[big - 1] == (u8)0xAB, "full %zu bytes are writable", big);
    arena_release(&a);
    PASS(test_name);
}

/* realloc grows and copies existing data */
void test_realloc_grow() {
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
void test_realloc_no_shrink() {
    const char *test_name = "realloc no shrink";
    arena a = {0};
    char *p = arena_alloc(&a, 64);
    char *p2 = arena_realloc(&a, p, 64, 32);
    ASSERT(p == p2, "shrink returns same pointer");
    arena_release(&a);
    PASS(test_name);
}

/* release zeroes the arena; it can be reused immediately after */
void test_release_and_reuse() {
    const char *test_name = "release and reuse";
    arena a = {0};
    for (int i = 0; i < 50; i++) arena_alloc(&a, 256);
    arena_release(&a);
    ASSERT(a.start == NULL && a.end == NULL, "release zeroes the arena");
    void *p = arena_alloc(&a, 64);
    ASSERT(p != NULL, "arena is usable after release");
    arena_release(&a);
    PASS(test_name);
}

/* reset keeps regions but resets len; memory can be reused without malloc */
void test_reset() {
    const char *test_name = "reset";
    arena a = {0};
    for (int i = 0; i < 50; i++) arena_alloc(&a, 256);
    arena_region *first = a.start;   /* remember the region pointer */
    arena_reset(&a);
    ASSERT(a.start == first, "regions are retained after reset");
    ASSERT(a.start->len == 0, "region len is reset to 0");
    void *p = arena_alloc(&a, 64);
    ASSERT(p != NULL, "arena is usable after reset");
    arena_release(&a);
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

    if (_failed == 0)
        printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET "\n\n");
    else
        printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n", _failed);
    return _failed != 0;
}
