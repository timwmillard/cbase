# String improvements for `lib/base.h`

Ideas from comparing cbase's `string` with STC's `cstr` / `csview` / `csviewz`
(`vendor/STC`). The core design stays as it is: one immutable `{data, len}`
type, arena ownership, and `string_builder` as the only mutable type. STC
needs separate owned/view/zero-terminated types because it has no arena;
cbase doesn't, so most of what's worth borrowing is small additions, not
structural change.

## Done

UTF-8, in `base.h` and the new `utf8.h` (branch `utf8`):

- [x] Strict decoder (rejects overlongs, surrogates, > U+10FFFF, stray and
      truncated sequences → `RUNE_INVALID`, resyncs on the next byte)
- [x] `utf8_encode`, `sb_push_rune`, `string_utf8_valid`
- [x] `string_each_rune(it, s)` iterator (`it.r`, `it.pos`, `it.size`)
- [x] `utf8.h`: Unicode case mapping, categories/scripts, caseless compare,
      generated from the UCD by `utf8_gen.c` (`make utf8-tables`)

## To do

### 1. Splitting without allocating

`string_split` always allocates an array and makes two passes. Most splitting
is "loop over the tokens", which needs no allocation.
(STC: `c_each_split_sv`, `csview_split`)

```c
// Go-style cut: true if sep found; before/after are slices into s.
bool string_cut(string s, string sep, string *before, string *after);

// Consuming iterator: returns next token, advances *s. False when exhausted.
bool string_split_next(string *s, string sep, string *token);

#define string_each_split(tok, sep, input) \
   for (string _rest = (input), tok; string_split_next(&_rest, (sep), &tok);)
```

`string_cut` covers most small parsing jobs (`key=value`, `host:port`,
headers). Keep `string_split`, built on `string_split_next`.

### 2. Ordering and ASCII case-insensitive comparison

There's no way to sort strings (`qsort`) or use them as sorted-map keys.
(STC: `csview_cmp`, `csview_icmp`, `iequals`, `istarts_with`)

```c
int  string_cmp(string a, string b);        // memcmp, then length
bool string_eq_nocase(string a, string b);  // ASCII
bool string_starts_with_nocase(string s, string prefix);
```

The Unicode-aware versions already exist in `utf8.h`
(`string_utf8_cmp_nocase` etc.); these are the fast ASCII defaults.

### 3. Join over any slice

`string_join` only accepts `string_array`, so a `string_list` must be
converted first. (STC: `cstr_join` takes any container)

```c
string string_join_n(arena *a, const string *items, usize n, string sep);
#define string_join(a, xs, sep) string_join_n((a), (xs).items, (xs).len, (sep))
```

This works for both `string_array` and `string_list`.

### 4. Small slice helpers

(STC: `csview_tail`, `csview_strtok`, find-at)

- `string_last_index_of` / `string_last_index_of_char`
- `string_index_of_any(s, S(" \t,"))`, a strtok-style delimiter set
- `string_prefix(s, n)` / `string_suffix(s, n)`, clamped, clearer than
  `string_slice(s, -n, s.len)`
- `string_replace_n(a, s, from, to, count)`, like STC's `count` parameter
- `string_utf8_prefix(s, n_runes)`, the one rune-indexed helper worth having
  (avoid general rune indexing: it's O(n) disguised as O(1))

### 5. File and line input

(STC: `cstr_from_file`, `cstr_getline`)

```c
string string_read_file(arena *a, const char *path);
```

Combined with #1, line iteration is free:
`string_each_split(line, S("\n"), contents)`.

## Implementation fixes

- **`string_index_of`** runs `memcmp` at every position. Use `memchr` to find
  the first byte and only `memcmp` there (as STC's `c_strnstrn` does).
  `string_index_of_char` should just be `memchr`.
- **`string_replace`** pushes one byte at a time between matches. Find the
  next match and `sb_append` the whole chunk before it.
- **`string_eq`** compares in a hand-written loop; use `memcmp`.
- **NULL with length 0**: `memcmp` / `memcpy` with a NULL pointer is undefined
  behaviour even for length 0 (before C2y). A zero-initialised `string{0}`
  reaching `string_starts_with`, `string_ends_with` or `sb_append` hits this.
  Guard with `len == 0`.

## Not adopting from STC

- **A zero-terminated view type (`csviewz`).** It breaks the one-type model.
  Most arena transforms already write a trailing `'\0'`, but the type can't
  show it; `string_cstr` copying is the honest API.
- **`const char *` overloads** (`csview_find(sv, "x")` alongside `_sv`
  variants). `S("x")` makes them unnecessary and keeps the API half the size.
- **In-place mutation** (`cstr_insert`, `erase`, `u8_replace`). This belongs
  in `string_builder` if anywhere; `sb_truncate(sb, len)` and maybe `sb_pop`
  would cover the real needs.
- **Rune-position indexing** (`cutf8_at`, `u8_to_index`, `u8_subview`). Each
  is an O(n) scan disguised as an index; use byte offsets plus
  `string_each_rune`.

## Suggested order

1. `string_cut` + `string_split_next` / `string_each_split`
2. `string_cmp` + ASCII `_nocase` comparisons
3. The `index_of` / `replace` / `eq` fixes and NULL guards
4. `string_join_n`, slice helpers, `string_read_file`
