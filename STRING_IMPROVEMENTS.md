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

- [x] `string_cut`, `string_split_next`, `string_each_split` (allocation-free
      splitting; `string_split` is now built on `string_split_next`)
- [x] `string_cmp`, `string_eq_nocase`, `string_starts_with_nocase` (ASCII)
- [x] `string_join_n`; `string_join` is a macro over any `.items`/`.len` slice
      (`string_array`, `string_list`)
- [x] `string_last_index_of[_char]`, `string_index_of_any`, `string_prefix`,
      `string_suffix`, `string_replace_n`, `string_utf8_prefix`
- [x] `string_read_file` (NULL `data` on failure, NUL-terminated)
- [x] Fixes: `memchr`-driven `string_index_of` / `string_index_of_char`,
      chunked `string_replace`, `memcmp` in `string_eq`, zero-length guards
      on every `memcmp` / `memcpy` / `memchr` (a `string{0}` is now safe)

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
