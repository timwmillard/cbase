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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
void  arena_reset(arena *a);
void  arena_release(arena *a);
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
#endif
