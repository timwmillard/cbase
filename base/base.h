/* base.h — 
 *
*/

// ██   ██ ███████  █████  ██████  ███████ ██████
// ██   ██ ██      ██   ██ ██   ██ ██      ██   ██
// ███████ █████   ███████ ██   ██ █████   ██████
// ██   ██ ██      ██   ██ ██   ██ ██      ██   ██
// ██   ██ ███████ ██   ██ ██████  ███████ ██   ██
//
// >>header
#ifndef BASE_H
#define BASE_H

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

typedef uint8_t  u8;
typedef size_t   usize;
typedef ptrdiff_t isize;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef __uint128_t u128;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef __int128_t i128;
typedef unsigned int uint;

typedef size_t usize;
typedef uintptr_t uptr;
typedef intptr_t iptr;
typedef ptrdiff_t isize;

typedef float f32;
typedef double f64;
typedef long double f28;

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

// ---------------------------------------------------------------------------
// arena (copied from base.h so this header stands alone)
// ---------------------------------------------------------------------------

#ifndef ARENA_REGION_DEFAULT_SIZE_BYTES
#define ARENA_REGION_DEFAULT_SIZE_BYTES 4096
#endif

typedef struct arena_region {
    struct arena_region *next;
    usize len;
    usize cap;
    uintptr_t data[];
} arena_region;

typedef struct arena {
    arena_region *start;
    arena_region *end;
} arena;

void *arena_alloc(arena *a, usize size_bytes);
void *arena_realloc(arena *a, void *oldptr, usize oldsz, usize newsz);
void arena_reset(arena *a);
void arena_release(arena *a);
char *arena_sprintf(arena *a, const char *format, ...);
char *arena_vsprintf(arena *a, const char *format, va_list args);

// ---------------------------------------------------------------------------
// string — immutable string slices over an arena allocator.
// ---------------------------------------------------------------------------
// Design (the "String8" / single-type model):
//
//   A `string` is ALWAYS just { data, len }. There is no view variant, no
//   capacity, no owner flag. An owned string and a borrowed slice are
//   bit-for-bit identical — ownership is a property of the arena that backs
//   the bytes, not of the string value. This makes slicing free and lets the
//   same `string` flow through every read-only function.
//
//   Strings are immutable. Nothing in this API mutates the bytes a `string`
//   points at. To build or grow text, use `string_builder`, then snapshot it
//   into a `string` with sb_string() / sb_cstr().
//
//   Strings are NOT guaranteed null-terminated (a slice points into the
//   middle of a buffer). Use string_cstr() / sb_cstr() when you need a C str.

typedef struct {
    char *data;
    usize len;
} string;

// printf helpers — printf(FMT_STR "\n", STR_ARG(s));
#define FMT_STR     "%.*s"
#define STR_ARG(s)  (int)(s).len, (s).data

// Build a `string` from a C string LITERAL at compile time, no copy.
//   string s = S("hello");
#define S(lit)  ((string){ (char *)(lit), sizeof(lit) - 1 })

// Views — borrow existing bytes, no allocation, no copy.
string string_view(const char *cstr);                 // strlen-terminated
string string_view_n(const char *bytes, usize len);   // explicit length

// Owned copies — bytes are duplicated into the arena.
string string_from(arena *a, const char *fmt, ...);    // printf-style
string string_vfrom(arena *a, const char *fmt, va_list args);
string string_from_n(arena *a, const char *bytes, usize len);
string string_dup(arena *a, string s);                 // copy of an existing slice

// Null-terminated C string copied into the arena (for APIs that need char*).
const char *string_cstr(arena *a, string s);

// ---------------------------------------------------------------------------
// Read-only operations (no allocation; results are slices INTO `s`)
// ---------------------------------------------------------------------------

string string_slice(string s, isize start, isize end); // negative = from end
string string_trim(string s);                          // strip leading/trailing ws
string string_trim_left(string s);
string string_trim_right(string s);

bool  string_eq(string a, string b);
bool  string_starts_with(string s, string prefix);
bool  string_ends_with(string s, string suffix);
bool  string_contains(string s, string needle);
isize string_index_of(string s, string needle);        // -1 if not found
isize string_index_of_char(string s, char c);          // -1 if not found

// ---------------------------------------------------------------------------
// Transforms (allocate a new string in the arena)
// ---------------------------------------------------------------------------

string string_upper(arena *a, string s);
string string_lower(arena *a, string s);
string string_concat(arena *a, string a1, string a2);
string string_replace(arena *a, string s, string from, string to);

// ---------------------------------------------------------------------------
// Arrays / split / join
// ---------------------------------------------------------------------------

typedef struct {
    string *items;
    usize   count;
    arena  *arena;
} string_array;

string_array string_split(arena *a, string s, string delim);
string       string_join(arena *a, string_array parts, string sep);

// ---------------------------------------------------------------------------
// string_builder — the only mutable type. Grows in an arena (or a fixed buffer).
// ---------------------------------------------------------------------------

typedef struct {
    char  *data;
    usize  len;
    usize  cap;
    arena *arena;   // NULL => fixed buffer, cannot grow past cap
} string_builder;

string_builder sb_init(arena *a);                       // growable, empty
string_builder sb_init_cap(arena *a, usize cap);        // growable, pre-reserved
string_builder sb_init_fixed(char *buf, usize cap);     // fixed buffer (arena == NULL)

void sb_reserve(string_builder *sb, usize cap);
void sb_push(string_builder *sb, char c);
void sb_append(string_builder *sb, string s);
void sb_append_cstr(string_builder *sb, const char *cstr);
void sb_appendf(string_builder *sb, const char *fmt, ...);
void sb_vappendf(string_builder *sb, const char *fmt, va_list args);
void sb_reset(string_builder *sb);                      // len = 0, keep capacity

// Snapshot the builder's current contents.
string      sb_string(string_builder *sb);              // slice over sb's buffer
const char *sb_cstr(string_builder *sb);                // null-terminated view

#endif // BASE_H


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------


// ██ ███    ███ ██████  ██      ███████ ███    ███ ███████ ███    ██ ████████  █████  ████████ ██  ██████  ███    ██
// ██ ████  ████ ██   ██ ██      ██      ████  ████ ██      ████   ██    ██    ██   ██    ██    ██ ██    ██ ████   ██
// ██ ██ ████ ██ ██████  ██      █████   ██ ████ ██ █████   ██ ██  ██    ██    ███████    ██    ██ ██    ██ ██ ██  ██
// ██ ██  ██  ██ ██      ██      ██      ██  ██  ██ ██      ██  ██ ██    ██    ██   ██    ██    ██ ██    ██ ██  ██ ██
// ██ ██      ██ ██      ███████ ███████ ██      ██ ███████ ██   ████    ██    ██   ██    ██    ██  ██████  ██   ████
//
// >>implementation
#ifdef BASE_IMPLEMENTATION

#include <ctype.h>

// -----------------------------------------------------------------------------
// arena
// -----------------------------------------------------------------------------

arena_region *_arena_new_region(usize size) {
    usize region_cap = ARENA_REGION_DEFAULT_SIZE_BYTES / sizeof(uintptr_t);
    if (region_cap < size) region_cap = size;
    usize size_bytes = sizeof(uptr)*region_cap;
    arena_region *r = (arena_region*)malloc(size_bytes);
    assert(r);
    r->next = NULL;
    r->len = 0;
    r->cap = region_cap - sizeof(arena_region) / sizeof(uptr);
    return r;
}

void _arena_free_region(arena_region *r) {
    free(r);
}

void *arena_alloc(arena *a, usize size_bytes) {
    usize size = (size_bytes + sizeof(uptr) - 1)/sizeof(uptr);

    if (a->end == NULL) {
        assert(a->start == NULL);
        a->end = _arena_new_region(size);
        a->start = a->end;
    }

    while (a->end->len + size > a->end->cap && a->end->next != NULL) {
        a->end = a->end->next;
    }

    if (a->end->len + size > a->end->cap) {
        assert(a->end->next == NULL);
        a->end->next = _arena_new_region(size);
        a->end = a->end->next;
    }

    void *result = &a->end->data[a->end->len];
    a->end->len += size;
    return result;
}

void *arena_realloc(arena *a, void *oldptr, usize oldsz, usize newsz) {
    if (newsz <= oldsz) return oldptr;
    void *newptr = arena_alloc(a, newsz);
    char *newptr_char = (char*)newptr;
    char *oldptr_char = (char*)oldptr;
    for (usize i = 0; i < oldsz; ++i) {
        newptr_char[i] = oldptr_char[i];
    }
    return newptr;
}

void arena_reset(arena *a) {
    for (arena_region *r = a->start; r != NULL; r = r->next)
        r->len = 0;
    a->end = a->start;
}

void arena_release(arena *a) {
    arena_region *r = a->start;
    while (r) {
        arena_region *r0 = r;
        r = r->next;
        _arena_free_region(r0);
    }
    a->start = NULL;
    a->end = NULL;
}

char *arena_vsprintf(arena *a, const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    assert(n >= 0);
    char *result = (char*)arena_alloc(a, n + 1);
    vsnprintf(result, n + 1, format, args);

    return result;
}

char *arena_sprintf(arena *a, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *result = arena_vsprintf(a, format, args);
    va_end(args);

    return result;
}

// -----------------------------------------------------------------------------
// string
// -----------------------------------------------------------------------------

// Nits / design notes
//
// - string_slice signedness (line 358): start += s.len mixes isize/usize. It works via modular wraparound, but cast for clarity: start += (isize)s.len. Also you clamp end but not a too-large positive start; it's harmless (the end < start →
// end = start guard forces len = 0), but the .data pointer ends up past the buffer. Clamping start to s.len too would be tidier.
// - string_view const-cast (line 308): (char*)cstr discards const. That's inherent to the single char *data design (your S() macro does it too), and fine as long as the immutability contract holds — just know that writing through a view
// over a literal is UB.
// - string_vfrom double length pass (line 328): arena_vsprintf already computed n via vsnprintf(NULL,0,...) then throws it away, and you recompute with strlen. Minor; only worth it if you want arena_vsprintf to hand back the length.

// Constructors
string string_view(const char *cstr) {
    usize len = strlen(cstr);
    return (string){
        .data = (char*)cstr,
        .len = len,
    };
}

string string_view_n(const char *bytes, usize len) {
    return (string){
        .data = (char*)bytes,
        .len = len,
    };
}

string string_from(arena *a, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    string s = string_vfrom(a, fmt, args);
    va_end(args);
    return s;
}

string string_vfrom(arena *a, const char *fmt, va_list args) {
    char *cstr = arena_vsprintf(a, fmt, args);
    usize len = strlen(cstr);
    return (string){
        .data = cstr,
        .len = len,
    };
}

string string_from_n(arena *a, const char *bytes, usize len) {
    char *buf = (char*)arena_alloc(a, len + 1);
    memcpy(buf, bytes, len);
    buf[len] = '\0';
    return (string){
        .data = buf,
        .len = len,
    };
}

string string_dup(arena *a, string s) {
    return string_from_n(a, s.data, s.len);
}

const char *string_cstr(arena *a, string s) {
    char *buf = (char*)arena_alloc(a, s.len + 1);
    memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';
    return buf;
}

// Slicing / inspection
string string_slice(string s, isize start, isize end) {
    if (start < 0) start += s.len;
    if (end   < 0) end   += s.len;
    if (start < 0) start = 0;
    if (end > (isize)s.len) end = s.len;
    if (end < start) end = start;          // empty slice, not negative len
    return (string){
        .data = &s.data[start],
        .len  = (usize)(end - start),
    };
}

string string_trim(string s) {
    (void)s;
    return (string){0}; // TODO: implement
}

string string_trim_left(string s) {
    (void)s;
    return (string){0}; // TODO: implement
}

string string_trim_right(string s) {
    (void)s;
    return (string){0}; // TODO: implement
}

bool string_eq(string a, string b) {
    (void)a; (void)b;
    return false; // TODO: implement
}

bool string_starts_with(string s, string prefix) {
    (void)s; (void)prefix;
    return false; // TODO: implement
}

bool string_ends_with(string s, string suffix) {
    (void)s; (void)suffix;
    return false; // TODO: implement
}

bool string_contains(string s, string needle) {
    (void)s; (void)needle;
    return false; // TODO: implement
}

isize string_index_of(string s, string needle) {
    (void)s; (void)needle;
    return -1; // TODO: implement
}

isize string_index_of_char(string s, char c) {
    (void)s; (void)c;
    return -1; // TODO: implement
}

// Transforms
string string_upper(arena *a, string s) {
    (void)a; (void)s;
    return (string){0}; // TODO: implement
}

string string_lower(arena *a, string s) {
    (void)a; (void)s;
    return (string){0}; // TODO: implement
}

string string_concat(arena *a, string a1, string a2) {
    (void)a; (void)a1; (void)a2;
    return (string){0}; // TODO: implement
}

string string_replace(arena *a, string s, string from, string to) {
    (void)a; (void)s; (void)from; (void)to;
    return (string){0}; // TODO: implement
}

// Split / join
string_array string_split(arena *a, string s, string delim) {
    (void)a; (void)s; (void)delim;
    return (string_array){0}; // TODO: implement
}

string string_join(arena *a, string_array parts, string sep) {
    (void)a; (void)parts; (void)sep;
    return (string){0}; // TODO: implement
}

// -----------------------------------------------------------------------------
// string_builder
// -----------------------------------------------------------------------------

string_builder sb_init(arena *a) {
    (void)a;
    return (string_builder){0}; // TODO: implement
}

string_builder sb_init_cap(arena *a, usize cap) {
    (void)a; (void)cap;
    return (string_builder){0}; // TODO: implement
}

string_builder sb_init_fixed(char *buf, usize cap) {
    (void)buf; (void)cap;
    return (string_builder){0}; // TODO: implement
}

void sb_reserve(string_builder *sb, usize cap) {
    (void)sb; (void)cap;
    // TODO: implement
}

void sb_push(string_builder *sb, char c) {
    (void)sb; (void)c;
    // TODO: implement
}

void sb_append(string_builder *sb, string s) {
    (void)sb; (void)s;
    // TODO: implement
}

void sb_append_cstr(string_builder *sb, const char *cstr) {
    (void)sb; (void)cstr;
    // TODO: implement
}

void sb_appendf(string_builder *sb, const char *fmt, ...) {
    (void)sb; (void)fmt;
    // TODO: implement
}

void sb_vappendf(string_builder *sb, const char *fmt, va_list args) {
    (void)sb; (void)fmt; (void)args;
    // TODO: implement
}

void sb_reset(string_builder *sb) {
    (void)sb;
    // TODO: implement
}

string sb_string(string_builder *sb) {
    (void)sb;
    return (string){0}; // TODO: implement
}

const char *sb_cstr(string_builder *sb) {
    (void)sb;
    return NULL; // TODO: implement
}

#endif
