/* map.h — hash map with `string` keys, arena-backed.
 *
 * Single-header, same pattern as base.h. Include after base.h:
 *
 *     #include "base.h"
 *     #include "map.h"
 *
 * and in exactly ONE translation unit, define the implementation macros
 * before the includes:
 *
 *     #define BASE_IMPLEMENTATION
 *     #define MAP_IMPLEMENTATION
 */

// >>header
#ifndef MAP_H
#define MAP_H

#ifndef BASE_H
#include "base.h"
#endif

// ---------------------------------------------------------------------------
// map — hash map with `string` keys, arena-backed.
// ---------------------------------------------------------------------------
// Design, following the conventions already in base.h:
//
//  - Declared with MAP(V), like LIST(T). Keys are always `string`; values may
//    be any type V. (String keys are the overwhelmingly common case, and the
//    library already has a strong string type. An integer-keyed map can be
//    added later as a sibling, not by complicating this one.)
//
//  - Backed by an arena. There is no map_free — reclaim memory by resetting
//    or releasing the arena, same as everything else here.
//
//  - Dense storage, Python-dict style: `keys[]` and `values[]` are parallel
//    arrays holding exactly `len` live entries, so iteration is an ordinary
//    for-loop over [0, len), just like a list — no "skip empty buckets"
//    iterator machinery. A separate open-addressed index table (opaque,
//    arena-allocated) maps hash(key) -> dense slot.
//
//  - Deletion swap-removes from the dense arrays: O(1), but iteration order
//    is insertion order only until the first delete.
//
//  - Keys are duplicated into the arena when first inserted, so inserting
//    with a temporary or stack-backed key is safe. Lookups never allocate
//    and never copy.
//
//  - Lookup results are *dense indices* (isize, -1 if absent), echoing
//    string_index_of. Values are then read/written directly through
//    m.values[i]. This avoids returning pointers that a rehash could
//    invalidate mid-expression, and needs no non-portable macro tricks.
//
// Usage:
//
//     typedef MAP(int) int_map;
//
//     int_map m = map_init(&a);
//     map_put(&m, S("one"), 1);
//     map_put(&m, S("two"), 2);
//
//     isize i = map_index(&m, S("one"));         // lookup
//     if (i >= 0) printf("%d\n", m.values[i]);
//
//     usize j = map_intern(&m, S("hits"));       // find-or-insert (zeroed)
//     m.values[j]++;                             // e.g. counters, caches
//
//     for (usize k = 0; k < m.len; k++)          // iterate like a list
//         printf(STR_FMT " = %d\n", STR_ARG(m.keys[k]), m.values[k]);
//
//     map_del(&m, S("one"));                     // O(1) swap-remove
//
// Pointer stability: values[] may move when the map grows (arena_realloc),
// so do not hold &m.values[i] across a map_put/map_intern. Indices are
// stable across growth, but NOT across map_del (swap-remove moves the last
// entry into the deleted slot).

// Declare a map type with value type V.
//     typedef MAP(int) int_map;
//     typedef MAP(string) string_map;
#define MAP(V)                                                                 \
   struct {                                                                    \
      map_idx *idx; /* opaque hash index: hash(key) -> dense slot */           \
      string *keys; /* dense, len live entries, parallel to values */          \
      V *values;    /* dense, len live entries */                              \
      usize len;    /* number of live entries */                               \
      usize cap;    /* capacity of keys/values arrays */                       \
      arena *arena; /* arena all storage grows into */                         \
   }

typedef struct map_idx map_idx; // internal open-addressed index table

#ifndef MAP_INIT_CAP
#define MAP_INIT_CAP 8
#endif

// Initialize an empty map (use in a declaration, like list_init):
//     int_map m = map_init(&a);
#define map_init(a) {.arena = (a)}

// Hash used by the map; exposed because it's independently useful.
// (FNV-1a; deterministic across runs — do not rely on it for adversarial
// inputs without an application-level seed.)
u64 string_hash(string s);

// -----------------------------------------------------------------------
// Lookup (never allocates)
// -----------------------------------------------------------------------

// Dense index of `key`, or -1 if absent.
//     isize i = map_index(&m, S("name"));
//     if (i >= 0) use(m.values[i]);
#define map_index(m, key) map_lookup((m)->idx, (m)->keys, (m)->len, (key))

#define map_contains(m, key) (map_index((m), (key)) >= 0)

// Pointer to the value for `key`, or NULL if absent. One hash lookup.
// The pointer is invalidated by any map_put/map_intern/map_del (growth can
// move values[]; delete swap-moves entries) — use immediately, don't hold.
// Note: returns void*, so the value type is not checked at the call site;
// prefer map_index when you want the compiler on your side.
#define map_at(m, key)                                                         \
   map_lookup_ptr((m)->idx, (m)->keys, (char *)(m)->values,                    \
                  sizeof(*(m)->values), (m)->len, (key))

// -----------------------------------------------------------------------
// Insert / update
// -----------------------------------------------------------------------

// Find `key`, inserting a new entry with a ZEROED value if absent. Always
// returns a valid dense index. This is the primitive for upserts, counters,
// and build-once caches — one hash lookup, no get-then-put dance.
//     usize i = map_intern(&m, key);
//     m.values[i] += 1;
#define map_intern(m, key)                                                     \
   map_insert(&(m)->idx, &(m)->keys, (char **)&(m)->values,                    \
              sizeof(*(m)->values), &(m)->len, &(m)->cap, (m)->arena, (key))

// Insert `key` -> `value`, overwriting the value if the key exists.
#define map_put(m, key, value)                                                 \
   do {                                                                        \
      usize map_i_ = map_intern((m), (key));                                   \
      (m)->values[map_i_] = (value);                                           \
   } while (0)

// Pre-size for at least `n` entries (avoids rehashing while filling).
#define map_reserve(m, n)                                                      \
   map_grow(&(m)->idx, &(m)->keys, (char **)&(m)->values,                      \
            sizeof(*(m)->values), &(m)->cap, (m)->arena, (n))

// -----------------------------------------------------------------------
// Delete
// -----------------------------------------------------------------------

// Remove `key` if present; returns true if an entry was removed. The last
// dense entry is swapped into the vacated slot (indices/iteration order for
// that entry change; everything else is untouched).
#define map_del(m, key)                                                        \
   map_remove((m)->idx, (m)->keys, (char *)(m)->values, sizeof(*(m)->values),  \
              &(m)->len, (key))

// Remove all entries but keep capacity (no allocation on refill).
#define map_clear(m) map_reset((m)->idx, &(m)->len)

// -----------------------------------------------------------------------
// Engine — one type-erased implementation shared by every MAP(V). Prefer
// the macros above, which bind the field layout, value size, and arena;
// these are external only because the macros must reach them. Defined in
// the implementation section below.
// -----------------------------------------------------------------------

isize map_lookup(const map_idx *idx, const string *keys, usize len, string key);
void *map_lookup_ptr(const map_idx *idx, const string *keys, char *values,
                     usize value_size, usize len, string key);
usize map_insert(map_idx **idx, string **keys, char **values, usize value_size,
                 usize *len, usize *cap, arena *a, string key);
void map_grow(map_idx **idx, string **keys, char **values, usize value_size,
              usize *cap, arena *a, usize n);
bool map_remove(map_idx *idx, string *keys, char *values, usize value_size,
                usize *len, string key);
void map_reset(map_idx *idx, usize *len);

#endif // MAP_H

// >>implementation
#ifdef MAP_IMPLEMENTATION

// Layout recap: the user-visible MAP(V) holds dense keys[] / values[] arrays
// with `len` live entries. This section owns the opaque map_idx: an open-
// addressed, linear-probing hash table whose buckets store (hash, dense slot).
// The index table never stores keys or values — it only points into the dense
// arrays — so all the logic here is type-erased and compiles once.
//
// Invariants:
//   - idx->cap is a power of two; probe step is 1 (cache friendly).
//   - bucket.slot >= 0        -> live, points at keys[slot]/values[slot]
//   - bucket.slot == -1       -> empty (probe chains stop here)
//   - bucket.slot == -2       -> tombstone (probe chains continue)
//   - live + tombstones <= 3/4 * cap; violating inserts trigger a rebuild,
//     which re-seats live buckets and drops all tombstones.
//   - deletion swap-removes in the dense arrays and repoints the bucket of
//     the entry that moved.

#include <string.h> // memset, memcpy

#define MAP_SLOT_EMPTY    (-1)
#define MAP_SLOT_TOMB     (-2)
#define MAP_MAX_LOAD(cap) ((cap) - (cap) / 4) // 3/4, in integer math

typedef struct {
   u64 hash;   // cached so growth/rebuild never re-hashes keys
   isize slot; // dense index, MAP_SLOT_EMPTY, or MAP_SLOT_TOMB
} map_bucket;

struct map_idx {
   usize cap;            // bucket count, power of two
   usize used;           // live + tombstones (drives the load factor)
   usize tombs;          // tombstones only
   map_bucket buckets[]; // flexible array member, arena-allocated
};

// --- hash ------------------------------------------------------------------

// FNV-1a, 64-bit. Deterministic across runs and platforms.
u64 string_hash(string s) {
   u64 h = 14695981039346656037ULL;
   for (usize i = 0; i < s.len; i++) {
      h ^= (u64)(unsigned char)s.data[i];
      h *= 1099511628211ULL;
   }
   return h;
}

// --- index table -----------------------------------------------------------

static map_idx *map_idx_new(arena *a, usize cap /* power of two */) {
   map_idx *idx = arena_alloc(a, sizeof(map_idx) + cap * sizeof(map_bucket));
   idx->cap = cap;
   idx->used = 0;
   idx->tombs = 0;
   for (usize i = 0; i < cap; i++) {
      idx->buckets[i].slot = MAP_SLOT_EMPTY;
   }
   return idx;
}

// Insert into a table known to have room, for a key known to be absent.
// Used by rebuild and by intern's insert path.
static void map_idx_insert(map_idx *idx, u64 hash, isize slot) {
   usize mask = idx->cap - 1;
   usize i = (usize)hash & mask;
   while (idx->buckets[i].slot >= 0) {
      i = (i + 1) & mask;
   }
   if (idx->buckets[i].slot == MAP_SLOT_TOMB) {
      idx->tombs--; // reusing a grave
   } else {
      idx->used++;
   }
   idx->buckets[i].hash = hash;
   idx->buckets[i].slot = slot;
}

// Grow (or first-allocate) the index and re-seat every live bucket.
// Tombstones are dropped, which is what keeps long-lived maps healthy.
static map_idx *map_idx_rebuild(map_idx *old, arena *a, usize min_live) {
   usize cap = MAP_INIT_CAP * 2; // buckets, not entries; stays ahead of load
   while (MAP_MAX_LOAD(cap) < min_live + 1) {
      cap *= 2;
   }
   map_idx *idx = map_idx_new(a, cap);
   if (old) {
      for (usize i = 0; i < old->cap; i++) {
         if (old->buckets[i].slot >= 0) {
            map_idx_insert(idx, old->buckets[i].hash, old->buckets[i].slot);
         }
      }
   }
   return idx;
}

// Probe for `key`. Returns the bucket holding it, or NULL if absent.
// If `first_free` is non-NULL, it receives the first reusable bucket
// (tombstone or empty) seen on the probe path — the insert position.
static map_bucket *map_idx_find(const map_idx *idx, const string *keys,
                                string key, u64 hash, map_bucket **first_free) {
   if (first_free)
      *first_free = NULL;
   if (!idx)
      return NULL;

   usize mask = idx->cap - 1;
   usize i = (usize)hash & mask;
   for (;;) {
      map_bucket *b = (map_bucket *)&idx->buckets[i];
      if (b->slot == MAP_SLOT_EMPTY) {
         if (first_free && !*first_free)
            *first_free = b;
         return NULL;
      }
      if (b->slot == MAP_SLOT_TOMB) {
         if (first_free && !*first_free)
            *first_free = b;
      } else if (b->hash == hash && string_eq(keys[b->slot], key)) {
         return b;
      }
      i = (i + 1) & mask;
   }
}

// --- dense array growth ----------------------------------------------------

static void map_dense_grow(string **keys, char **values, usize value_size,
                           usize *cap, usize want, arena *a) {
   usize cap2 = *cap ? *cap : MAP_INIT_CAP;
   while (cap2 < want) {
      cap2 *= 2;
   }
   if (cap2 == *cap)
      return;

   *keys =
       arena_realloc(a, *keys, *cap * sizeof(string), cap2 * sizeof(string));
   *values = arena_realloc(a, *values, *cap * value_size, cap2 * value_size);
   *cap = cap2;
}

// --- public helpers (called via the macros in base_map.h) --------------------

isize map_lookup(const map_idx *idx, const string *keys, usize len,
                 string key) {
   if (len == 0)
      return -1;
   map_bucket *b = map_idx_find(idx, keys, key, string_hash(key), NULL);
   return b ? b->slot : -1;
}

void *map_lookup_ptr(const map_idx *idx, const string *keys, char *values,
                     usize value_size, usize len, string key) {
   isize i = map_lookup(idx, keys, len, key);
   return i >= 0 ? values + (usize)i * value_size : NULL;
}

usize map_insert(map_idx **idx, string **keys, char **values, usize value_size,
                 usize *len, usize *cap, arena *a, string key) {
   u64 hash = string_hash(key);

   // Fast path: already present.
   map_bucket *free_b = NULL;
   map_bucket *b = map_idx_find(*idx, *keys, key, hash, &free_b);
   if (b)
      return (usize)b->slot;

   // Make room in the index (also handles the first allocation). A rebuild
   // invalidates free_b, so re-resolve the insert bucket afterwards.
   if (!*idx || (*idx)->used + 1 > MAP_MAX_LOAD((*idx)->cap)) {
      *idx = map_idx_rebuild(*idx, a, *len);
      map_idx_find(*idx, *keys, key, hash, &free_b);
   }

   // Make room in the dense arrays.
   if (*len == *cap) {
      map_dense_grow(keys, values, value_size, cap, *len + 1, a);
   }

   usize slot = *len;
   // Duplicate the key into the arena so callers may pass temporaries.
   (*keys)[slot] = string_dup(a, key);
   memset(*values + slot * value_size, 0, value_size); // zeroed value
   *len = slot + 1;

   // free_b is the tombstone-or-empty bucket found on the probe path.
   if (free_b->slot == MAP_SLOT_TOMB) {
      (*idx)->tombs--;
   } else {
      (*idx)->used++;
   }
   free_b->hash = hash;
   free_b->slot = (isize)slot;
   return slot;
}

void map_grow(map_idx **idx, string **keys, char **values, usize value_size,
              usize *cap, arena *a, usize n) {
   // Dense side.
   if (n > *cap) {
      map_dense_grow(keys, values, value_size, cap, n, a);
   }
   // Index side: rebuild if n live entries would exceed the load factor.
   if (!*idx || MAP_MAX_LOAD((*idx)->cap) < n) {
      *idx = map_idx_rebuild(*idx, a, n);
   }
}

bool map_remove(map_idx *idx, string *keys, char *values, usize value_size,
                usize *len, string key) {
   if (*len == 0)
      return false;

   u64 hash = string_hash(key);
   map_bucket *b = map_idx_find(idx, keys, key, hash, NULL);
   if (!b)
      return false;

   isize i = b->slot;
   isize last = (isize)*len - 1;

   // Kill the bucket first; if i == last we're done after shrinking.
   b->slot = MAP_SLOT_TOMB;
   idx->tombs++;

   if (i != last) {
      // Swap-remove: move the last entry into the hole...
      keys[i] = keys[last];
      memcpy(values + (usize)i * value_size, values + (usize)last * value_size,
             value_size);
      // ...and repoint its bucket. Its hash is cached in the bucket, so
      // probe with the moved key's own hash, not the deleted key's.
      map_bucket *moved =
          map_idx_find(idx, keys, keys[i], string_hash(keys[i]), NULL);
      moved->slot = i;
   }

   *len = (usize)last;
   return true;
}

void map_reset(map_idx *idx, usize *len) {
   *len = 0;
   if (!idx)
      return;
   idx->used = 0;
   idx->tombs = 0;
   for (usize i = 0; i < idx->cap; i++) {
      idx->buckets[i].slot = MAP_SLOT_EMPTY;
   }
}

#endif // MAP_IMPLEMENTATION
