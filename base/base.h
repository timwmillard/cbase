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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

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
char *arena_sprintf(arena *a, const char *format, ...);
char *arena_vsprintf(arena *a, const char *format, va_list args);

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
   char *data;
   usize len;
} string;

// printf helpers — printf(STR_FMT "\n", STR_ARG(s));
#define STR_FMT    "%.*s"
#define STR_ARG(s) (int)(s).len, (s).data

// Build a `string` from a C string LITERAL at compile time, no copy.
//   string s = S("hello");
#define S(lit) ((string){(char *)(lit), sizeof(lit) - 1})

// Views — borrow existing bytes, no allocation, no copy.
string string_view(const char *cstr);               // strlen-terminated
string string_view_n(const char *bytes, usize len); // explicit length

// Owned copies — bytes are duplicated into the arena.
string string_from(arena *a, const char *fmt, ...); // printf-style
string string_vfrom(arena *a, const char *fmt, va_list args);
string string_from_n(arena *a, const char *bytes, usize len);
string string_dup(arena *a, string s); // copy of an existing slice

// Null-terminated C string copied into the arena (for APIs that need char*).
const char *string_cstr(arena *a, string s);

// ---------------------------------------------------------------------------
// Read-only operations (no allocation; results are slices INTO `s`)
// ---------------------------------------------------------------------------

string string_slice(string s, isize start, isize end); // negative = from end
string string_trim(string s); // strip leading/trailing ws
string string_trim_left(string s);
string string_trim_right(string s);

bool string_eq(string a, string b);
bool string_starts_with(string s, string prefix);
bool string_ends_with(string s, string suffix);
bool string_contains(string s, string needle);
isize string_index_of(string s, string needle); // -1 if not found
isize string_index_of_char(string s, char c);   // -1 if not found

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
   usize len;
   arena *arena;
} string_array;

string_array string_split(arena *a, string s, string delim);
string string_join(arena *a, string_array parts, string sep);

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
void sb_append_cstr(string_builder *sb, const char *cstr);
void sb_appendf(string_builder *sb, const char *fmt, ...);
void sb_vappendf(string_builder *sb, const char *fmt, va_list args);
void sb_reset(string_builder *sb); // len = 0, keep capacity

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
   usize region_cap = ARENA_REGION_DEFAULT_SIZE_BYTES / sizeof(uptr);
   if (region_cap < size)
      region_cap = size;
   usize size_bytes = sizeof(uptr) * region_cap;
   arena_region *r = (arena_region *)malloc(size_bytes);
   assert(r);
   r->next = NULL;
   r->len = 0;
   r->cap = region_cap - sizeof(arena_region) / sizeof(uptr);
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
   while (r) {
      arena_region *r0 = r;
      r = r->next;
      arena_free_region(r0);
   }
   a->start = NULL;
   a->end = NULL;
}

char *arena_vsprintf(arena *a, const char *format, va_list args) {
   va_list args_copy;
   va_copy(args_copy, args);
   int n = vsnprintf(NULL, 0, format, args_copy);
   va_end(args_copy);

   assert(n >= 0);
   char *result = (char *)arena_alloc(a, n + 1);
   vsnprintf(result, n + 1, format, args);

   return result;
}

char *arena_sprintf(arena *a, const char *format, ...) {
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
// - string_slice signedness (line 358): start += s.len mixes isize/usize. It
// works via modular wraparound, but cast for clarity: start += (isize)s.len.
// Also you clamp end but not a too-large positive start; it's harmless (the end
// < start → end = start guard forces len = 0), but the .data pointer ends up
// past the buffer. Clamping start to s.len too would be tidier.
// - string_view const-cast (line 308): (char*)cstr discards const. That's
// inherent to the single char *data design (your S() macro does it too), and
// fine as long as the immutability contract holds — just know that writing
// through a view over a literal is UB.
// - string_vfrom double length pass (line 328): arena_vsprintf already computed
// n via vsnprintf(NULL,0,...) then throws it away, and you recompute with
// strlen. Minor; only worth it if you want arena_vsprintf to hand back the
// length.

// Constructors
string string_view(const char *cstr) {
   usize len = strlen(cstr);
   return (string){
       .data = (char *)cstr,
       .len = len,
   };
}

string string_view_n(const char *bytes, usize len) {
   return (string){
       .data = (char *)bytes,
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
   char *buf = (char *)arena_alloc(a, len + 1);
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
   char *buf = (char *)arena_alloc(a, s.len + 1);
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
   while (i < s.len && isspace(s.data[i]))
      i++;
   return (string){.data = s.data + i, .len = s.len - i};
}

string string_trim_right(string s) {
   usize len = s.len;
   while (len > 0 && isspace(s.data[len - 1]))
      len--;
   return (string){.data = s.data, .len = len};
}

bool string_eq(string a, string b) {
   if (a.len != b.len)
      return false;
   for (usize i = 0; i < a.len; i++)
      if (a.data[i] != b.data[i])
         return false;
   return true;
}

bool string_starts_with(string s, string prefix) {
   if (prefix.len > s.len)
      return false;
   return memcmp(s.data, prefix.data, prefix.len) == 0;
}

bool string_ends_with(string s, string suffix) {
   if (suffix.len > s.len)
      return false;
   return memcmp(s.data + (s.len - suffix.len), suffix.data, suffix.len) == 0;
}

bool string_contains(string s, string needle) {
   return string_index_of(s, needle) >= 0;
}

isize string_index_of(string s, string needle) {
   if (needle.len == 0)
      return 0; // empty needle matches at the start
   if (needle.len > s.len)
      return -1;
   for (usize i = 0; i + needle.len <= s.len; i++)
      if (memcmp(s.data + i, needle.data, needle.len) == 0)
         return (isize)i;
   return -1;
}

isize string_index_of_char(string s, char c) {
   for (usize i = 0; i < s.len; i++)
      if (s.data[i] == c)
         return (isize)i;
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
   memcpy(buf, a1.data, a1.len);
   memcpy(buf + a1.len, a2.data, a2.len);
   buf[len] = '\0';
   return (string){.data = buf, .len = len};
}

string string_replace(arena *a, string s, string from, string to) {
   if (from.len == 0)
      return string_dup(a, s); // nothing to match; avoid looping
   string_builder sb = sb_init(a);
   usize i = 0;
   while (i < s.len) {
      if (i + from.len <= s.len &&
          memcmp(s.data + i, from.data, from.len) == 0) {
         sb_append(&sb, to);
         i += from.len;
      } else {
         sb_push(&sb, s.data[i]);
         i++;
      }
   }
   return sb_string(&sb);
}

// Split / join
string_array string_split(arena *a, string s, string delim) {
   string_array arr = {.items = NULL, .len = 0, .arena = a};

   if (delim.len == 0) { // no delimiter => whole string
      arr.items = (string *)arena_alloc(a, sizeof(string));
      arr.items[0] = s;
      arr.len = 1;
      return arr;
   }

   // First pass: count segments (delimiters + 1).
   usize count = 1;
   for (usize i = 0; i + delim.len <= s.len;) {
      if (memcmp(s.data + i, delim.data, delim.len) == 0) {
         count++;
         i += delim.len;
      } else {
         i++;
      }
   }

   // Second pass: fill in slices that point INTO s (no copy).
   arr.items = (string *)arena_alloc(a, sizeof(string) * count);
   arr.len = count;
   usize idx = 0, start = 0, i = 0;
   while (i + delim.len <= s.len) {
      if (memcmp(s.data + i, delim.data, delim.len) == 0) {
         arr.items[idx++] = (string){.data = s.data + start, .len = i - start};
         i += delim.len;
         start = i;
      } else {
         i++;
      }
   }
   arr.items[idx] = (string){.data = s.data + start, .len = s.len - start};
   return arr;
}

string string_join(arena *a, string_array parts, string sep) {
   string_builder sb = sb_init(a);
   for (usize i = 0; i < parts.len; i++) {
      if (i > 0)
         sb_append(&sb, sep);
      sb_append(&sb, parts.items[i]);
   }
   return sb_string(&sb);
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

void sb_append_cstr(string_builder *sb, const char *cstr) {
   sb_append(sb, string_view(cstr));
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

void sb_reset(string_builder *sb) {
   sb->len = 0;
}

string sb_string(string_builder *sb) {
   return (string){.data = sb->data, .len = sb->len};
}

const char *sb_cstr(string_builder *sb) {
   sb_reserve(sb, sb->len + 1);
   sb->data[sb->len] = '\0';
   return sb->data;
}

#endif
