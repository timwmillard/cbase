/* base.h —
 *
 */

// >>header
#ifndef BASE_H
#define BASE_H

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef unsigned int uint;

// 128-bit integers are a GCC/Clang extension, absent on MSVC and on targets
// without native 128-bit support. __SIZEOF_INT128__ is defined only where the
// type actually exists; gate use of u128/i128 on BASE_HAS_INT128.
#if defined(__SIZEOF_INT128__)
#define BASE_HAS_INT128 1
typedef __uint128_t u128;
typedef __int128_t i128;
#endif

typedef size_t usize;
typedef uintptr_t uptr;
typedef intptr_t iptr;
typedef ptrdiff_t isize;

typedef float f32;
typedef double f64;
typedef long double f128;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

// Marks a code path as unreachable. Asserts in debug builds (giving a clear
// diagnostic if actually hit) and tells the compiler the path is dead
// otherwise, so it can drop the code and won't warn about a missing return.
#if defined(__GNUC__) || defined(__clang__)
#define UNREACHABLE() (assert(!"unreachable"), __builtin_unreachable())
#elif defined(_MSC_VER)
#define UNREACHABLE() (assert(!"unreachable"), __assume(0))
#else
#define UNREACHABLE() (assert(!"unreachable"), abort())
#endif

// Marks a function as taking a printf-style format string, so the compiler
// checks the format against the arguments at each call site. fmt_idx is the
// 1-based index of the format parameter; arg_idx is the first variadic
// argument, or 0 for va_list functions (only the format itself is checked).
#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(fmt_idx, arg_idx)                                          \
   __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define PRINTF_LIKE(fmt_idx, arg_idx)
#endif

// ---------------------------------------------------------------------------
// arena (copied from base.h so this header stands alone)
// ---------------------------------------------------------------------------

#ifndef ARENA_REGION_DEFAULT_SIZE_BYTES
#define ARENA_REGION_DEFAULT_SIZE_BYTES 4096
#endif

typedef struct arena_region arena_region;

typedef struct arena {
   arena_region *start;
   arena_region *end;
} arena;

void *arena_alloc(arena *a, usize size_bytes);  // uninitialized
void *arena_calloc(arena *a, usize size_bytes); // zeroed
void *arena_realloc(arena *a, void *oldptr, usize oldsz, usize newsz);
void arena_reset(arena *a);
void arena_release(arena *a);

// Allocate-and-format into the arena, returning a NUL-terminated C string.
// (Unlike libc sprintf, the arena owns the buffer, so it is returned rather
// than passed in. For pointer+length in one value, use string_from below.)
char *arena_sprintf(arena *a, const char *fmt, ...) PRINTF_LIKE(2, 3);
char *arena_vsprintf(arena *a, const char *fmt, va_list args) PRINTF_LIKE(2, 0);

// Temp (scratch) scopes — checkpoint the arena, do throwaway work, then roll
// back to the mark, reclaiming everything allocated in between. Anything you
// want to keep must be allocated in a DIFFERENT (outer) arena.
//   arena_temp t = arena_temp_begin(scratch);
//   ... allocate transient data in `scratch` ...
//   arena_temp_end(t); // reclaims it all
typedef struct {
   arena *arena;
   arena_region *region; // a->end at mark time
   usize len;            // region->len at mark time
} arena_temp;

arena_temp arena_temp_begin(arena *a);
void arena_temp_end(arena_temp t);

// Typed allocation helpers. Both return zeroed memory (the common case). If you
// want uninitialized memory, call arena_alloc directly.
//   Foo *f  = arena_new(a, Foo);       // one zeroed Foo
//   Foo *xs = arena_array(a, Foo, 16); // 16 zeroed Foo
#define arena_new(a, T)      ((T *)arena_calloc((a), sizeof(T)))
#define arena_array(a, T, n) ((T *)arena_calloc((a), sizeof(T) * (usize)(n)))

// ---------------------------------------------------------------------------
// list — growable dynamic array, backed by an arena.
// ---------------------------------------------------------------------------
// A "list" is any struct with these four fields:
//
//      T     *items;  // backing storage (grows as needed)
//      usize  len;    // number of items in use
//      usize  cap;    // number of items allocated
//      arena *arena;  // arena the storage grows into
//
// Use LIST(T) to declare one for a given element type:
//
//   typedef LIST(int)    int_list;
//   typedef LIST(string) string_list;
//
// Unlike a `string`/slice (a borrowed view) or a fixed `*_array`, a list owns
// growable storage. Append doubles capacity in the arena when full. Because the
// arena never frees the old buffer, each growth leaves the previous block dead
// until the arena is reset — doubling keeps that waste bounded and appends
// amortized O(1).
//
//   int_list xs = list_init(&a);
//   list_append(&xs, 42);
//   for (usize i = 0; i < xs.len; i++) use(xs.items[i]);

// Declare a list type with element type T.
//   typedef LIST(int) int_list;
#define LIST(T)                                                                \
   struct {                                                                    \
      T *items;                                                                \
      usize len;                                                               \
      usize cap;                                                               \
      arena *arena;                                                            \
   }

#ifndef LIST_INIT_CAP
#define LIST_INIT_CAP 8
#endif

// Initialize an empty list (use in a declaration; the brace form is not an
// expression). To pre-allocate capacity, follow with list_reserve:
//   int_list xs = list_init(&a);
//   list_reserve(&xs, 16); // optional: avoid regrowth for ~16 items
#define list_init(a) {.arena = (a)}

// Ensure capacity for at least `n` items (no-op if already large enough).
#define list_reserve(lst, n)                                                   \
   do {                                                                        \
      usize need_ = (n);                                                       \
      if (need_ > (lst)->cap) {                                                \
         usize cap_ = (lst)->cap ? (lst)->cap : LIST_INIT_CAP;                 \
         while (cap_ < need_)                                                  \
            cap_ *= 2;                                                         \
         (lst)->items = arena_realloc((lst)->arena, (lst)->items,              \
                                      (lst)->cap * sizeof(*(lst)->items),      \
                                      cap_ * sizeof(*(lst)->items));           \
         (lst)->cap = cap_;                                                    \
      }                                                                        \
   } while (0)

// Append one item, growing the backing storage in the list's arena as needed.
#define list_append(lst, item)                                                 \
   do {                                                                        \
      list_reserve((lst), (lst)->len + 1);                                     \
      (lst)->items[(lst)->len++] = (item);                                     \
   } while (0)

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
   const char *data; // immutable; never written through (see string_builder)
   usize len;
} string;

// printf helpers — printf(STR_FMT "\n", STR_ARG(s));
#define STR_FMT    "%.*s"
#define STR_ARG(s) (int)(s).len, (s).data

// Build a `string` from a C string LITERAL at compile time, no copy.
//   string s = S("hello");
#define S(lit) ((string){(lit), sizeof(lit) - 1})

// Views — borrow existing bytes, no allocation, no copy.
string string_view(const char *cstr);                   // strlen-terminated
string string_view_bytes(const char *bytes, usize len); // explicit length

// Owned copies — bytes are duplicated into the arena.
string stringf(arena *a, const char *fmt, ...) PRINTF_LIKE(2, 3);
string vstringf(arena *a, const char *fmt, va_list args) PRINTF_LIKE(2, 0);
string string_from(arena *a, const char *cstr); // null-terminated C string
string string_from_bytes(arena *a, const char *bytes, usize len);
string string_dup(arena *a, string s); // copy of an existing slice
// Copies are NUL-terminated; an empty copy is the literal "" (no allocation).

// Null-terminated C string copied into the arena (for APIs that need char*).
const char *string_cstr(arena *a, string s);

// ---------------------------------------------------------------------------
// Read-only operations (no allocation; results are slices INTO `s`)
// ---------------------------------------------------------------------------

string string_slice(string s, isize start, isize end); // negative = from end
string string_trim(string s); // strip leading/trailing ws
string string_trim_left(string s);
string string_trim_right(string s);
string string_trim_prefix(string s, string prefix);
string string_trim_suffix(string s, string suffix);

// First / last n bytes of s, clamped to s.len (clearer than string_slice
// with negative indices).
string string_prefix(string s, usize n);
string string_suffix(string s, usize n);

// Comparison. string_cmp orders bytewise (memcmp), and a string sorts before
// any longer string it is a prefix of; returns <0, 0 or >0, so it suits
// qsort and sorted containers. The _nocase variants fold ASCII letters only;
// for Unicode-aware caseless comparison see utf8.h.
bool string_eq(string a, string b);
int string_cmp(string a, string b);
bool string_eq_nocase(string a, string b);
bool string_starts_with(string s, string prefix);
bool string_starts_with_nocase(string s, string prefix);
bool string_ends_with(string s, string suffix);
bool string_contains(string s, string needle);

// Searching. All return a byte offset, or -1 if not found. An empty needle
// matches at 0 (index_of) or at s.len (last_index_of).
isize string_index_of(string s, string needle);
isize string_index_of_char(string s, char c);
isize string_last_index_of(string s, string needle);
isize string_last_index_of_char(string s, char c);
// First byte of s that is any of the bytes in `chars` (a strtok-style
// delimiter set): string_index_of_any(s, S(" \t,")).
isize string_index_of_any(string s, string chars);

// ---------------------------------------------------------------------------
// Transforms (allocate a new string in the arena)
// ---------------------------------------------------------------------------
// Results are NUL-terminated in the arena (not counted in len), though the
// `string` type itself makes no such promise.

string string_upper(arena *a, string s);
string string_lower(arena *a, string s);
string string_concat(arena *a, string a1, string a2);
string string_replace(arena *a, string s, string from, string to);
// Replace at most `count` occurrences, left to right; count < 0 means all.
string string_replace_n(arena *a, string s, string from, string to,
                        isize count);

// ---------------------------------------------------------------------------
// Splitting (no allocation; tokens are slices INTO the input)
// ---------------------------------------------------------------------------

// Go-style cut around the first sep. Returns true if sep was found; then
// *before and *after are the parts either side of it. Otherwise *before is
// s and *after is empty. Either out pointer may be NULL.
//   string key, val;
//   if (string_cut(line, S("="), &key, &val)) ...
bool string_cut(string s, string sep, string *before, string *after);

// Consuming split iterator: stores the next token in *token, advances *s
// past it and its separator, and returns true; returns false once the
// input is exhausted. Like string_split, n separators give n + 1 tokens,
// so "a,b," yields "a", "b", "" and "" yields one empty token. An empty sep
// yields s whole. Exhaustion is marked by setting s->data to NULL, so a
// string whose data is NULL yields nothing (string_each_split treats it as
// "" instead).
bool string_split_next(string *s, string sep, string *token);

// Loop over the tokens of a string without allocating:
//   string_each_split(line, S("\n"), contents) { ... line ... }
// input and sep are evaluated once / per token respectively; loops nest as
// long as the token names differ.
#define string_each_split(tok, sep, input)                                     \
   for (string tok##_rest_ = string_split_init(input), tok;                    \
        string_split_next(&tok##_rest_, (sep), &tok);)

static inline string string_split_init(string s) {
   if (s.data == NULL)
      s.data = "";
   return s;
}

// ---------------------------------------------------------------------------
// Arrays / split / join
// ---------------------------------------------------------------------------

typedef struct {
   string *items;
   usize len;
} string_array;

// Growable list of strings (see the `list` section: list_init/append/reserve).
typedef LIST(string) string_list;

// Split into an arena-allocated array (built on string_split_next; prefer
// string_each_split when you only need to loop over the tokens).
string_array string_split(arena *a, string s, string delim);

// Join n strings with sep between them.
string string_join_n(arena *a, const string *items, usize n, string sep);
// Join any slice with .items/.len: a string_array, a string_list, ...
// (xs is evaluated twice).
#define string_join(a, xs, sep) string_join_n((a), (xs).items, (xs).len, (sep))

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

// Read a whole file into the arena (NUL-terminated, not counted in len).
// On failure returns a string with data == NULL; an empty file gives a
// non-NULL empty string. Lines: string_each_split(line, S("\n"), contents).
string string_read_file(arena *a, const char *path);

// ---------------------------------------------------------------------------
// UTF8
// ---------------------------------------------------------------------------

// Encoding-level UTF-8 only. For Unicode case mapping, character classes
// and caseless comparison, include utf8.h (built on these primitives).

typedef u32 rune; // a decoded Unicode codepoint

#define RUNE_INVALID 0xFFFD // U+FFFD REPLACEMENT CHARACTER
#define RUNE_MAX     0x10FFFF

// Decoding is strict (RFC 3629): overlong encodings, surrogates
// (U+D800..U+DFFF), codepoints above U+10FFFF, stray continuation bytes and
// truncated sequences are all rejected. Each ill-formed sequence decodes to
// RUNE_INVALID and consumes its "maximal subpart" — the longest prefix that
// could have started a valid sequence, minimum 1 byte — so decoding resyncs
// on the next byte that can start a codepoint (the Unicode/WHATWG practice).

// Decode the codepoint at the start of s, writing it to *codepoint
// (optional — pass NULL to just skip a codepoint without decoding it).
// Returns the number of bytes consumed, never more than s.len.
// Empty input returns 0 with *codepoint 0, distinct from a real NUL byte
// (which returns 1 with *codepoint 0) — check the return value, not just
// the codepoint, to tell them apart. A literal U+FFFD in the input also
// decodes to RUNE_INVALID; use string_utf8_valid to tell them apart.
usize string_utf8_decode(string s, rune *codepoint);

// Number of codepoints (each ill-formed sequence counts as one).
usize string_utf8_len(string s);

// As string_utf8_len, and if s ends in a truncated-but-so-far-valid sequence
// (e.g. a read buffer cut mid-codepoint), *bytes_overrun is how many more
// bytes it needs; otherwise 0. The partial sequence is counted as one
// codepoint. bytes_overrun may be NULL.
usize string_utf8_len_overrun(string s, usize *bytes_overrun);

// True if s is entirely well-formed UTF-8.
bool string_utf8_valid(string s);

// The first n codepoints of s (each ill-formed sequence counts as one), or
// all of s if it has fewer. Never splits a codepoint. This is the one
// rune-counted helper: everything else uses byte offsets, since a rune
// index is an O(n) scan.
string string_utf8_prefix(string s, usize n);

// Encode r as UTF-8 into out. Returns the number of bytes written (1-4), or
// 0 (writing nothing) if r is a surrogate or above RUNE_MAX.
usize utf8_encode(char out[4], rune r);

// Iterate the codepoints of a string:
//   string_each_rune(it, s) {
//      it.r     — the codepoint (RUNE_INVALID for ill-formed bytes)
//      it.pos   — byte offset of this codepoint in s
//      it.size  — its length in bytes, so string_slice(s, it.pos,
//                 it.pos + it.size) is the codepoint's own bytes
//   }
// s is evaluated once; loops nest as long as the iterator names differ.
typedef struct {
   string s;
   usize pos;
   usize size;
   rune r;
} rune_iter;

bool rune_iter_next(rune_iter *it);

#define string_each_rune(it, str)                                              \
   for (rune_iter it = {.s = (str)}; rune_iter_next(&it);)

// ---------------------------------------------------------------------------
// string_builder — the only mutable type. Grows in an arena (or a fixed
// buffer).
// ---------------------------------------------------------------------------

typedef struct {
   char *data;
   usize len;
   usize cap;
   arena *arena; // NULL => fixed buffer, cannot grow past cap
} string_builder;

string_builder sb_init(arena *a);                // growable, empty
string_builder sb_init_cap(arena *a, usize cap); // growable, pre-reserved
string_builder sb_init_fixed(char *buf,
                             usize cap); // fixed buffer (arena == NULL)

void sb_reserve(string_builder *sb, usize cap);
void sb_push(string_builder *sb, char c);
void sb_append(string_builder *sb, string s);
void sb_append_from(string_builder *sb, const char *cstr);
void sb_append_from_bytes(string_builder *sb, const char *bytes, usize len);
void sb_appendf(string_builder *sb, const char *fmt, ...) PRINTF_LIKE(2, 3);
void sb_vappendf(string_builder *sb, const char *fmt, va_list args)
    PRINTF_LIKE(2, 0);
void sb_push_rune(string_builder *sb, rune r); // invalid r => U+FFFD
void sb_reset(string_builder *sb);             // len = 0, keep capacity

// Snapshot the builder's current contents.
string sb_string(string_builder *sb);    // slice over sb's buffer
const char *sb_cstr(string_builder *sb); // null-terminated view

#endif // BASE_H

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

// >>implementation
#ifdef BASE_IMPLEMENTATION

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// arena
// -----------------------------------------------------------------------------

struct arena_region {
   arena_region *next;
   usize len;
   usize cap;
   uptr data[];
};

static arena_region *arena_new_region(usize size) {
   // region_cap counts uptr words for the WHOLE block (header + data). The
   // header occupies header_words; usable capacity is region_cap -
   // header_words. We must size the block so usable cap >= the requested
   // `size`, otherwise a single oversized request overflows the region it's
   // placed in.
   usize header_words =
       (sizeof(arena_region) + sizeof(uptr) - 1) / sizeof(uptr);
   usize region_cap = ARENA_REGION_DEFAULT_SIZE_BYTES / sizeof(uptr);
   if (region_cap < size + header_words)
      region_cap = size + header_words;
   usize size_bytes = sizeof(uptr) * region_cap;
   arena_region *r = (arena_region *)malloc(size_bytes);
   assert(r);
   r->next = NULL;
   r->len = 0;
   r->cap = region_cap - header_words;
   return r;
}

static void arena_free_region(arena_region *r) {
   free(r);
}

void *arena_alloc(arena *a, usize size_bytes) {
   usize size = (size_bytes + sizeof(uptr) - 1) / sizeof(uptr);

   if (a->end == NULL) {
      assert(a->start == NULL);
      a->end = arena_new_region(size);
      a->start = a->end;
   }

   while (a->end->len + size > a->end->cap && a->end->next != NULL) {
      a->end = a->end->next;
   }

   if (a->end->len + size > a->end->cap) {
      assert(a->end->next == NULL);
      a->end->next = arena_new_region(size);
      a->end = a->end->next;
   }

   void *result = &a->end->data[a->end->len];
   a->end->len += size;
   return result;
}

void *arena_calloc(arena *a, usize size_bytes) {
   void *p = arena_alloc(a, size_bytes);
   memset(p, 0, size_bytes);
   return p;
}

void *arena_realloc(arena *a, void *oldptr, usize oldsz, usize newsz) {
   if (newsz <= oldsz)
      return oldptr;
   void *newptr = arena_alloc(a, newsz);
   char *newptr_char = (char *)newptr;
   char *oldptr_char = (char *)oldptr;
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
   a->start = NULL;
   a->end = NULL;
   while (r) {
      arena_region *r0 = r;
      r = r->next;
      arena_free_region(r0);
   }
}

arena_temp arena_temp_begin(arena *a) {
   return (arena_temp){
       .arena = a, .region = a->end, .len = a->end ? a->end->len : 0};
}

void arena_temp_end(arena_temp t) {
   arena *a = t.arena;
   if (t.region == NULL) { // arena was empty at the mark
      arena_reset(a);
      return;
   }
   // regions filled after the mark go back to empty (kept for reuse)
   for (arena_region *r = t.region->next; r != NULL; r = r->next)
      r->len = 0;
   t.region->len = t.len; // partial rewind within the mark's region
   a->end = t.region;
}

// Thin C-string wrapper over the vstringf primitive (which keeps the length
// vsnprintf already computed). The arena-owned buffer is genuinely mutable;
// the const on string.data is only the string layer's immutability contract.
char *arena_vsprintf(arena *a, const char *fmt, va_list args) {
   return (char *)vstringf(a, fmt, args).data;
}

char *arena_sprintf(arena *a, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   char *result = arena_vsprintf(a, fmt, args);
   va_end(args);

   return result;
}

// -----------------------------------------------------------------------------
// string
// -----------------------------------------------------------------------------

// Constructors

// A view borrows `cstr` without copying. `string.data` is const, so the view
// cannot be written through -- matching the immutability contract. To get a
// mutable, owned copy, use string_from, string_from_bytes, or string_dup.
string string_view(const char *cstr) {
   usize len = strlen(cstr);
   return (string){
       .data = cstr,
       .len = len,
   };
}

string string_view_bytes(const char *bytes, usize len) {
   return (string){
       .data = bytes,
       .len = len,
   };
}

string stringf(arena *a, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   string s = vstringf(a, fmt, args);
   va_end(args);
   return s;
}

// The formatting primitive: vsnprintf tells us the length, so we keep it
// rather than strlen the result. arena_vsprintf/arena_sprintf wrap this.
string vstringf(arena *a, const char *fmt, va_list args) {
   va_list args_copy;
   va_copy(args_copy, args);
   int n = vsnprintf(NULL, 0, fmt, args_copy);
   va_end(args_copy);

   assert(n >= 0);
   char *buf = (char *)arena_alloc(a, (usize)n + 1);
   vsnprintf(buf, (usize)n + 1, fmt, args);

   return (string){
       .data = buf,
       .len = (usize)n,
   };
}

string string_from(arena *a, const char *cstr) {
   return string_from_bytes(a, cstr, strlen(cstr));
}

string string_from_bytes(arena *a, const char *bytes, usize len) {
   if (len == 0)
      return S(""); // no allocation; still non-NULL and NUL-terminated
   char *buf = (char *)arena_alloc(a, len + 1);
   if (len)
      memcpy(buf, bytes, len);
   buf[len] = '\0';
   return (string){
       .data = buf,
       .len = len,
   };
}

string string_dup(arena *a, string s) {
   return string_from_bytes(a, s.data, s.len);
}

const char *string_cstr(arena *a, string s) {
   if (s.len == 0)
      return "";
   char *buf = (char *)arena_alloc(a, s.len + 1);
   if (s.len)
      memcpy(buf, s.data, s.len);
   buf[s.len] = '\0';
   return buf;
}

// Slicing / inspection
string string_slice(string s, isize start, isize end) {
   if (start < 0)
      start += s.len;
   if (end < 0)
      end += s.len;
   if (start < 0)
      start = 0;
   if (start > (isize)s.len)
      start = (isize)s.len; // keep &s.data[start] in-bounds (one past end ok)
   if (end > (isize)s.len)
      end = s.len;
   if (end < start)
      end = start; // empty slice, not negative len
   return (string){
       .data = &s.data[start],
       .len = (usize)(end - start),
   };
}

string string_trim(string s) {
   return string_trim_left(string_trim_right(s));
}

string string_trim_left(string s) {
   usize i = 0;
   while (i < s.len && isspace((unsigned char)s.data[i]))
      i++;
   return (string){.data = s.data + i, .len = s.len - i};
}

string string_trim_right(string s) {
   usize len = s.len;
   while (len > 0 && isspace((unsigned char)s.data[len - 1]))
      len--;
   return (string){.data = s.data, .len = len};
}

string string_trim_prefix(string s, string prefix) {
   if (!string_starts_with(s, prefix))
      return s;
   return (string){.data = s.data + prefix.len, .len = s.len - prefix.len};
}

string string_trim_suffix(string s, string suffix) {
   if (!string_ends_with(s, suffix))
      return s;
   return (string){.data = s.data, .len = s.len - suffix.len};
}

string string_prefix(string s, usize n) {
   if (n >= s.len)
      return s;
   return (string){.data = s.data, .len = n};
}

string string_suffix(string s, usize n) {
   if (n >= s.len)
      return s;
   return (string){.data = s.data + (s.len - n), .len = n};
}

// memcmp/memchr with a NULL pointer are undefined even for length 0 (before
// C2y), and a zero-initialised string has data == NULL, so every call below
// is guarded by a non-zero length.

bool string_eq(string a, string b) {
   return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

int string_cmp(string a, string b) {
   usize n = a.len < b.len ? a.len : b.len;
   int c = n ? memcmp(a.data, b.data, n) : 0;
   if (c != 0)
      return c;
   return (a.len > b.len) - (a.len < b.len);
}

static inline u8 ascii_lower(u8 c) {
   return (c >= 'A' && c <= 'Z') ? (u8)(c + ('a' - 'A')) : c;
}

bool string_eq_nocase(string a, string b) {
   if (a.len != b.len)
      return false;
   for (usize i = 0; i < a.len; i++)
      if (ascii_lower((u8)a.data[i]) != ascii_lower((u8)b.data[i]))
         return false;
   return true;
}

bool string_starts_with(string s, string prefix) {
   if (prefix.len > s.len)
      return false;
   return prefix.len == 0 || memcmp(s.data, prefix.data, prefix.len) == 0;
}

bool string_starts_with_nocase(string s, string prefix) {
   return prefix.len <= s.len &&
          string_eq_nocase(string_prefix(s, prefix.len), prefix);
}

bool string_ends_with(string s, string suffix) {
   if (suffix.len > s.len)
      return false;
   return suffix.len == 0 ||
          memcmp(s.data + (s.len - suffix.len), suffix.data, suffix.len) == 0;
}

bool string_contains(string s, string needle) {
   return string_index_of(s, needle) >= 0;
}

// memchr for the needle's first byte, then memcmp only at those candidates.
isize string_index_of(string s, string needle) {
   if (needle.len == 0)
      return 0; // empty needle matches at the start
   if (needle.len > s.len)
      return -1;
   const char *p = s.data;
   const char *last = s.data + (s.len - needle.len); // last possible start
   char first = needle.data[0];
   while (p <= last) {
      p = (const char *)memchr(p, first, (usize)(last - p) + 1);
      if (p == NULL)
         return -1;
      if (memcmp(p + 1, needle.data + 1, needle.len - 1) == 0)
         return (isize)(p - s.data);
      p++;
   }
   return -1;
}

isize string_index_of_char(string s, char c) {
   if (s.len == 0)
      return -1;
   const char *p = (const char *)memchr(s.data, c, s.len);
   return p ? (isize)(p - s.data) : -1;
}

isize string_last_index_of(string s, string needle) {
   if (needle.len == 0)
      return (isize)s.len; // empty needle matches at the end
   if (needle.len > s.len)
      return -1;
   for (usize i = s.len - needle.len + 1; i-- > 0;)
      if (s.data[i] == needle.data[0] &&
          memcmp(s.data + i + 1, needle.data + 1, needle.len - 1) == 0)
         return (isize)i;
   return -1;
}

isize string_last_index_of_char(string s, char c) {
   for (usize i = s.len; i-- > 0;)
      if (s.data[i] == c)
         return (isize)i;
   return -1;
}

isize string_index_of_any(string s, string chars) {
   if (chars.len == 1)
      return string_index_of_char(s, chars.data[0]);
   u8 set[32] = {0}; // 256-bit membership set
   for (usize i = 0; i < chars.len; i++) {
      u8 c = (u8)chars.data[i];
      set[c >> 3] |= (u8)(1u << (c & 7));
   }
   for (usize i = 0; i < s.len; i++) {
      u8 c = (u8)s.data[i];
      if (set[c >> 3] & (1u << (c & 7)))
         return (isize)i;
   }
   return -1;
}

// Transforms
string string_upper(arena *a, string s) {
   char *buf = (char *)arena_alloc(a, s.len + 1);
   for (usize i = 0; i < s.len; i++)
      buf[i] = (char)toupper((unsigned char)s.data[i]);
   buf[s.len] = '\0';
   return (string){.data = buf, .len = s.len};
}

string string_lower(arena *a, string s) {
   char *buf = (char *)arena_alloc(a, s.len + 1);
   for (usize i = 0; i < s.len; i++)
      buf[i] = (char)tolower((unsigned char)s.data[i]);
   buf[s.len] = '\0';
   return (string){.data = buf, .len = s.len};
}

string string_concat(arena *a, string a1, string a2) {
   usize len = a1.len + a2.len;
   char *buf = (char *)arena_alloc(a, len + 1);
   if (a1.len)
      memcpy(buf, a1.data, a1.len);
   if (a2.len)
      memcpy(buf + a1.len, a2.data, a2.len);
   buf[len] = '\0';
   return (string){.data = buf, .len = len};
}

string string_replace(arena *a, string s, string from, string to) {
   return string_replace_n(a, s, from, to, -1);
}

// Copies the whole run before each match in one append, rather than a byte
// at a time.
string string_replace_n(arena *a, string s, string from, string to,
                        isize count) {
   if (from.len == 0 || count == 0)
      return string_dup(a, s); // nothing to match; avoid looping
   string_builder sb = sb_init_cap(a, s.len + 1);
   string rest = s;
   for (; count != 0; count = count > 0 ? count - 1 : count) {
      isize i = string_index_of(rest, from);
      if (i < 0)
         break;
      sb_append(&sb, string_prefix(rest, (usize)i));
      sb_append(&sb, to);
      usize skip = (usize)i + from.len;
      rest = (string){.data = rest.data + skip, .len = rest.len - skip};
   }
   sb_append(&sb, rest);
   const char *data = sb_cstr(&sb);
   return (string){.data = data, .len = sb.len};
}

// Split / join

bool string_cut(string s, string sep, string *before, string *after) {
   isize i = string_index_of(s, sep);
   if (i < 0) {
      if (before)
         *before = s;
      if (after)
         *after = S("");
      return false;
   }
   if (before)
      *before = (string){.data = s.data, .len = (usize)i};
   if (after)
      *after = (string){.data = s.data + i + sep.len,
                        .len = s.len - (usize)i - sep.len};
   return true;
}

bool string_split_next(string *s, string sep, string *token) {
   if (s->data == NULL)
      return false; // exhausted
   isize i = sep.len ? string_index_of(*s, sep) : -1;
   if (i < 0) { // last token: the rest of the input
      *token = *s;
      *s = (string){.data = NULL, .len = 0};
      return true;
   }
   *token = (string){.data = s->data, .len = (usize)i};
   usize skip = (usize)i + sep.len;
   *s = (string){.data = s->data + skip, .len = s->len - skip};
   return true;
}

string_array string_split(arena *a, string s, string delim) {
   s = string_split_init(s);
   string rest, tok;

   // First pass: count tokens. Second pass: fill in slices INTO s.
   usize count = 0;
   for (rest = s; string_split_next(&rest, delim, &tok);)
      count++;

   string_array arr = {
       .items = (string *)arena_alloc(a, sizeof(string) * count),
       .len = count,
   };
   usize i = 0;
   for (rest = s; string_split_next(&rest, delim, &tok);)
      arr.items[i++] = tok;
   return arr;
}

string string_join_n(arena *a, const string *items, usize n, string sep) {
   usize len = 0;
   for (usize i = 0; i < n; i++)
      len += items[i].len;
   if (n > 1)
      len += sep.len * (n - 1);

   char *buf = (char *)arena_alloc(a, len + 1);
   char *p = buf;
   for (usize i = 0; i < n; i++) {
      if (i > 0 && sep.len) {
         memcpy(p, sep.data, sep.len);
         p += sep.len;
      }
      if (items[i].len) {
         memcpy(p, items[i].data, items[i].len);
         p += items[i].len;
      }
   }
   *p = '\0';
   return (string){.data = buf, .len = len};
}

// Files

string string_read_file(arena *a, const char *path) {
   FILE *f = fopen(path, "rb");
   if (f == NULL)
      return (string){0};

   // Size the buffer from the file length when the stream is seekable (+1
   // so the first read hits EOF and there is room for the NUL); otherwise
   // (pipes, character devices) grow as we go.
   usize cap = 4096;
   if (fseek(f, 0, SEEK_END) == 0) {
      long n = ftell(f);
      if (n >= 0)
         cap = (usize)n + 1;
      if (fseek(f, 0, SEEK_SET) != 0) {
         fclose(f);
         return (string){0};
      }
   }

   string_builder sb = sb_init_cap(a, cap);
   for (;;) {
      if (sb.len == sb.cap)
         sb_reserve(&sb, sb.cap * 2);
      usize want = sb.cap - sb.len;
      usize got = fread(sb.data + sb.len, 1, want, f);
      sb.len += got;
      if (got < want)
         break; // EOF or error
   }
   bool failed = ferror(f) != 0;
   fclose(f);
   if (failed)
      return (string){0};
   const char *data = sb_cstr(&sb);
   return (string){.data = data, .len = sb.len};
}

// One strict decode step over p[0..len), len >= 1. Returns bytes consumed.
// *ok is false for an ill-formed sequence (and *out is RUNE_INVALID);
// *missing is nonzero only when the input ran out partway through a
// sequence that was valid so far, and says how many bytes it still needs.
//
// The legal byte ranges are those of RFC 3629 / Unicode Table 3-7: the
// second byte's range is narrowed after E0, ED, F0 and F4 to exclude
// overlongs, surrogates and codepoints above U+10FFFF; C0, C1 and F5..FF
// never appear.
static usize utf8_step(const u8 *p, usize len, rune *out, bool *ok,
                             usize *missing) {
   u8 b0 = p[0];
   *missing = 0;
   if (b0 < 0x80) {
      *out = b0;
      *ok = true;
      return 1;
   }

   usize need;
   rune r;
   u8 lo = 0x80, hi = 0xBF; // legal range for the next continuation byte
   if (b0 >= 0xC2 && b0 <= 0xDF) {
      need = 2;
      r = b0 & 0x1F;
   } else if (b0 >= 0xE0 && b0 <= 0xEF) {
      need = 3;
      r = b0 & 0x0F;
      if (b0 == 0xE0)
         lo = 0xA0; // overlong
      else if (b0 == 0xED)
         hi = 0x9F; // surrogates
   } else if (b0 >= 0xF0 && b0 <= 0xF4) {
      need = 4;
      r = b0 & 0x07;
      if (b0 == 0xF0)
         lo = 0x90; // overlong
      else if (b0 == 0xF4)
         hi = 0x8F; // > U+10FFFF
   } else {
      // continuation byte, C0/C1 (always overlong) or F5..FF
      *out = RUNE_INVALID;
      *ok = false;
      return 1;
   }

   for (usize i = 1; i < need; i++) {
      if (i >= len) { // truncated: valid so far, input ran out
         *out = RUNE_INVALID;
         *ok = false;
         *missing = need - i;
         return i;
      }
      u8 b = p[i];
      if (b < lo || b > hi) { // maximal subpart ends before the bad byte
         *out = RUNE_INVALID;
         *ok = false;
         return i;
      }
      lo = 0x80;
      hi = 0xBF;
      r = (r << 6) | (b & 0x3F);
   }
   *out = r;
   *ok = true;
   return need;
}

usize string_utf8_decode(string s, rune *codepoint) {
   rune r = 0;
   usize n = 0;
   if (s.len > 0) {
      bool ok;
      usize missing;
      n = utf8_step((const u8 *)s.data, s.len, &r, &ok, &missing);
   }
   if (codepoint)
      *codepoint = r;
   return n;
}

usize string_utf8_len_overrun(string s, usize *bytes_overrun) {
   const u8 *p = (const u8 *)s.data;
   usize i = 0, count = 0, missing = 0;
   while (i < s.len) {
      rune r;
      bool ok;
      i += utf8_step(p + i, s.len - i, &r, &ok, &missing);
      count++;
   }
   // Only the final step can run out of input, so `missing` is its verdict.
   if (bytes_overrun)
      *bytes_overrun = missing;
   return count;
}

usize string_utf8_len(string s) {
   return string_utf8_len_overrun(s, NULL);
}

string string_utf8_prefix(string s, usize n) {
   const u8 *p = (const u8 *)s.data;
   usize i = 0;
   for (; n > 0 && i < s.len; n--) {
      rune r;
      bool ok;
      usize missing;
      i += utf8_step(p + i, s.len - i, &r, &ok, &missing);
   }
   return (string){.data = s.data, .len = i};
}

bool string_utf8_valid(string s) {
   const u8 *p = (const u8 *)s.data;
   usize i = 0;
   while (i < s.len) {
      // ASCII fast path
      if (p[i] < 0x80) {
         i++;
         continue;
      }
      rune r;
      bool ok;
      usize missing;
      i += utf8_step(p + i, s.len - i, &r, &ok, &missing);
      if (!ok)
         return false;
   }
   return true;
}

usize utf8_encode(char out[4], rune r) {
   if (r < 0x80) {
      out[0] = (char)r;
      return 1;
   }
   if (r < 0x800) {
      out[0] = (char)(0xC0 | (r >> 6));
      out[1] = (char)(0x80 | (r & 0x3F));
      return 2;
   }
   if (r < 0x10000) {
      if (r >= 0xD800 && r <= 0xDFFF)
         return 0; // surrogates are not encodable
      out[0] = (char)(0xE0 | (r >> 12));
      out[1] = (char)(0x80 | ((r >> 6) & 0x3F));
      out[2] = (char)(0x80 | (r & 0x3F));
      return 3;
   }
   if (r <= RUNE_MAX) {
      out[0] = (char)(0xF0 | (r >> 18));
      out[1] = (char)(0x80 | ((r >> 12) & 0x3F));
      out[2] = (char)(0x80 | ((r >> 6) & 0x3F));
      out[3] = (char)(0x80 | (r & 0x3F));
      return 4;
   }
   return 0;
}

bool rune_iter_next(rune_iter *it) {
   it->pos += it->size;
   if (it->pos >= it->s.len) {
      it->size = 0;
      return false;
   }
   bool ok;
   usize missing;
   it->size = utf8_step((const u8 *)it->s.data + it->pos,
                              it->s.len - it->pos, &it->r, &ok, &missing);
   return true;
}

// -----------------------------------------------------------------------------
// string_builder
// -----------------------------------------------------------------------------

string_builder sb_init(arena *a) {
   return (string_builder){.data = NULL, .len = 0, .cap = 0, .arena = a};
}

string_builder sb_init_cap(arena *a, usize cap) {
   string_builder sb = sb_init(a);
   sb_reserve(&sb, cap);
   return sb;
}

string_builder sb_init_fixed(char *buf, usize cap) {
   return (string_builder){.data = buf, .len = 0, .cap = cap, .arena = NULL};
}

void sb_reserve(string_builder *sb, usize cap) {
   if (cap <= sb->cap)
      return;
   // Fixed buffers (no arena) cannot grow.
   assert(sb->arena != NULL && "string_builder fixed buffer overflow");
   usize newcap = sb->cap ? sb->cap : 16;
   while (newcap < cap)
      newcap *= 2;
   sb->data = (char *)arena_realloc(sb->arena, sb->data, sb->len, newcap);
   sb->cap = newcap;
}

void sb_push(string_builder *sb, char c) {
   sb_reserve(sb, sb->len + 1);
   sb->data[sb->len++] = c;
}

void sb_append(string_builder *sb, string s) {
   if (s.len == 0)
      return;
   sb_reserve(sb, sb->len + s.len);
   memcpy(sb->data + sb->len, s.data, s.len);
   sb->len += s.len;
}

void sb_append_from(string_builder *sb, const char *cstr) {
   sb_append(sb, string_view(cstr));
}

void sb_append_from_bytes(string_builder *sb, const char *bytes, usize len) {
   sb_append(sb, string_view_bytes(bytes, len));
}

void sb_appendf(string_builder *sb, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   sb_vappendf(sb, fmt, args);
   va_end(args);
}

void sb_vappendf(string_builder *sb, const char *fmt, va_list args) {
   va_list copy;
   va_copy(copy, args);
   int n = vsnprintf(NULL, 0, fmt, copy);
   va_end(copy);
   if (n <= 0)
      return;
   // +1 so vsnprintf has room for its NUL; len only advances by n.
   sb_reserve(sb, sb->len + (usize)n + 1);
   vsnprintf(sb->data + sb->len, (usize)n + 1, fmt, args);
   sb->len += (usize)n;
}

void sb_push_rune(string_builder *sb, rune r) {
   char buf[4];
   usize n = utf8_encode(buf, r);
   if (n == 0)
      n = utf8_encode(buf, RUNE_INVALID);
   sb_append_from_bytes(sb, buf, n);
}

void sb_reset(string_builder *sb) {
   sb->len = 0;
}

string sb_string(string_builder *sb) {
   assert(sb->data != NULL || sb->len == 0);
   return (string){
       .data = sb->data ? sb->data : "",
       .len = sb->len,
   };
}

const char *sb_cstr(string_builder *sb) {
   sb_reserve(sb, sb->len + 1);
   sb->data[sb->len] = '\0';
   return sb->data;
}

#endif
