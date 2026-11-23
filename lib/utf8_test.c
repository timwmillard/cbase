#include <ctype.h>
#include <stdio.h>
#include <string.h> /* system string.h (strlen/strncmp) */

#define BASE_IMPLEMENTATION
#define UTF8_IMPLEMENTATION
#include "base.h"
#include "utf8.h"

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

#define ASSERT_RUNE(got, want)                                                 \
   ASSERT((got) == (want), "%s == U+%04X (got U+%04X)", #got, (want), (got))

/* ================================================================== */
/* case mapping                                                       */
/* ================================================================== */

void test_case_ascii(void) {
   const char *test_name = "case_ascii";
   for (rune c = 0; c < 0x80; c++) {
      ASSERT_RUNE(rune_tolower(c), (rune)tolower((int)c));
      ASSERT_RUNE(rune_toupper(c), (rune)toupper((int)c));
   }
   PASS(test_name);
}

void test_case_bmp(void) {
   const char *test_name = "case_bmp";
   ASSERT_RUNE(rune_tolower(0xC9), 0xE9u);     /* É -> é */
   ASSERT_RUNE(rune_toupper(0xE9), 0xC9u);     /* é -> É */
   ASSERT_RUNE(rune_tolower(0x100), 0x101u);   /* Ā -> ā (alternating) */
   ASSERT_RUNE(rune_tolower(0x101), 0x101u);   /* ā stays */
   ASSERT_RUNE(rune_toupper(0x101), 0x100u);   /* ā -> Ā */
   ASSERT_RUNE(rune_toupper(0x100), 0x100u);   /* Ā stays */
   ASSERT_RUNE(rune_tolower(0x3A3), 0x3C3u);   /* Σ -> σ */
   ASSERT_RUNE(rune_toupper(0x3C2), 0x3A3u);   /* final ς -> Σ */
   ASSERT_RUNE(rune_tolower(0x414), 0x434u);   /* Д -> д */
   ASSERT_RUNE(rune_toupper(0x434), 0x414u);   /* д -> Д */
   ASSERT_RUNE(rune_tolower(0x13A0), 0xAB70u); /* Cherokee Ꭰ -> ꭰ */
   ASSERT_RUNE(rune_toupper(0xFF41), 0xFF21u); /* fullwidth ａ -> Ａ */
   ASSERT_RUNE(rune_toupper(0xFF), 0x178u);    /* ÿ -> Ÿ */
   ASSERT_RUNE(rune_toupper(0xDF), 0xDFu);     /* ß has no simple upper */
   ASSERT_RUNE(rune_tolower(0x65E5), 0x65E5u); /* 日 unchanged */
   PASS(test_name);
}

/* all planes are covered, not just the BMP */
void test_case_above_bmp(void) {
   const char *test_name = "case_above_bmp";
   ASSERT_RUNE(rune_tolower(0x10400), 0x10428u); /* Deseret 𐐀 -> 𐐨 */
   ASSERT_RUNE(rune_toupper(0x10428), 0x10400u);
   ASSERT_RUNE(rune_tolower(0x1E900), 0x1E922u); /* Adlam 𞤀 -> 𞤢 */
   ASSERT_RUNE(rune_toupper(0x1F600), 0x1F600u); /* emoji unchanged */
   ASSERT_RUNE(rune_tolower(RUNE_MAX), (rune)RUNE_MAX);
   ASSERT_RUNE(rune_tolower(0xFFFFFFFF), 0xFFFFFFFFu); /* garbage in */
   PASS(test_name);
}

/* cases STC's table got wrong or missed; values from UnicodeData.txt */
void test_case_ucd_regressions(void) {
   const char *test_name = "case_ucd_regressions";
   ASSERT_RUNE(rune_toupper(0x1F52), 0x1F52u); /* ὒ has no simple upper */
   ASSERT_RUNE(rune_toupper(0x1F51), 0x1F59u); /* ὑ -> Ὑ */
   ASSERT_RUNE(rune_tolower(0x1C5), 0x1C6u);   /* titlecase ǅ -> ǆ */
   ASSERT_RUNE(rune_toupper(0x1C5), 0x1C4u);   /* ǅ -> Ǆ */
   ASSERT_RUNE(rune_tolower(0x1F88), 0x1F80u); /* ᾈ -> ᾀ */
   ASSERT_RUNE(rune_toupper(0x345), 0x399u);   /* ypogegrammeni -> Ι */
   ASSERT_RUNE(rune_tolower(0x2160), 0x2170u); /* Roman Ⅰ -> ⅰ */
   ASSERT_RUNE(rune_tolower(0x24B6), 0x24D0u); /* circled Ⓐ -> ⓐ */
   ASSERT_RUNE(rune_tolower(0x1E9E), 0xDFu);   /* ẞ -> ß, but not back */
   PASS(test_name);
}

void test_casefold(void) {
   const char *test_name = "casefold";
   ASSERT_RUNE(rune_casefold('A'), (rune)'a');
   ASSERT_RUNE(rune_casefold('a'), (rune)'a');
   ASSERT_RUNE(rune_casefold(0x17F), (rune)'s');  /* long s ſ */
   ASSERT_RUNE(rune_casefold(0x3C2), 0x3C3u);     /* final ς -> σ */
   ASSERT_RUNE(rune_casefold(0x3A3), 0x3C3u);     /* Σ -> σ */
   ASSERT_RUNE(rune_casefold(0x212A), (rune)'k'); /* Kelvin sign */
   ASSERT_RUNE(rune_casefold(0x1E9E), 0xDFu);     /* ẞ -> ß */
   ASSERT_RUNE(rune_casefold(0x130), 0x130u);     /* İ: Turkic-only fold */
   ASSERT_RUNE(rune_casefold(0xB5), 0x3BCu);      /* micro µ -> μ */
   PASS(test_name);
}

/* upper and lower forms of a letter fold to the same rune, across all
 * planes. The only exceptions Unicode makes are İ (U+0130) and ı (U+0131),
 * whose folds exist only in the Turkic tailoring we don't apply. */
void test_casefold_consistent(void) {
   const char *test_name = "casefold_consistent";
   usize bad = 0;
   rune first_bad = 0;
   for (rune c = 0; c <= RUNE_MAX; c++) {
      if (c == 0x130 || c == 0x131)
         continue;
      rune f = rune_casefold(c);
      if (rune_casefold(rune_tolower(c)) != f ||
          rune_casefold(rune_toupper(c)) != f) {
         if (!bad)
            first_bad = c;
         bad++;
      }
   }
   ASSERT(bad == 0, "%zu runes disagree, first U+%04X", bad, first_bad);
   ASSERT(rune_casefold(rune_tolower(0x130)) != rune_casefold(0x130),
          "İ is still the documented exception");
   PASS(test_name);
}

/* ================================================================== */
/* classification                                                     */
/* ================================================================== */

/* ASCII answers match the C locale <ctype.h> */
void test_class_ascii(void) {
   const char *test_name = "class_ascii";
   for (rune c = 0; c < 0x80; c++) {
      int i = (int)c;
      ASSERT(rune_is_upper(c) == !!isupper(i), "is_upper(%u)", c);
      ASSERT(rune_is_lower(c) == !!islower(i), "is_lower(%u)", c);
      ASSERT(rune_is_alpha(c) == !!isalpha(i), "is_alpha(%u)", c);
      ASSERT(rune_is_digit(c) == !!isdigit(i), "is_digit(%u)", c);
      ASSERT(rune_is_alnum(c) == !!isalnum(i), "is_alnum(%u)", c);
      ASSERT(rune_is_space(c) == !!isspace(i), "is_space(%u)", c);
      ASSERT(rune_is_blank(c) == !!isblank(i), "is_blank(%u)", c);
      ASSERT(rune_is_control(c) == !!iscntrl(i), "is_control(%u)", c);
      ASSERT(rune_is_word(c) == (isalnum(i) || c == '_'), "is_word(%u)", c);
   }
   PASS(test_name);
}

void test_class_unicode(void) {
   const char *test_name = "class_unicode";
   ASSERT(rune_is_alpha(0xE9), "é is alpha");
   ASSERT(rune_is_alpha(0x65E5), "日 is alpha");
   ASSERT(rune_is_alpha(0x3BB), "λ is alpha");
   ASSERT(!rune_is_alpha(0x663), "Arabic-Indic 3 is not alpha");
   ASSERT(rune_is_digit(0x663), "Arabic-Indic 3 is a digit");
   ASSERT(rune_is_digit(0xFF15), "fullwidth 5 is a digit");
   ASSERT(!rune_is_digit(0xB2), "superscript 2 is No, not Nd");
   ASSERT(rune_in_group(RUNE_GROUP_No, 0xB2), "superscript 2 is No");
   ASSERT(rune_is_upper(0xC9) && !rune_is_lower(0xC9), "É is upper");
   ASSERT(!rune_is_upper(0x1C5) && !rune_is_lower(0x1C5), "ǅ is neither");
   ASSERT(rune_is_lower(0xDF), "ß is lower despite no upper mapping");
   ASSERT(rune_is_upper(0x24B6) && rune_is_upper(0x2160), "Ⓐ and Ⅰ upper");
   ASSERT(rune_is_word(0x93F), "Devanagari vowel sign (Mc) is a word char");
   ASSERT(rune_is_digit(0x1D7CE), "math bold digit 0 is Nd");
   ASSERT(rune_is_lower(0xE9) && !rune_is_upper(0xE9), "é is lower");
   ASSERT(!rune_is_upper(0x65E5) && !rune_is_lower(0x65E5), "日 uncased");
   ASSERT(rune_is_space(0xA0), "NBSP is space");
   ASSERT(rune_is_space(0x3000), "ideographic space is space");
   ASSERT(rune_is_space(0x2028), "line separator is space");
   ASSERT(rune_is_space(0x85), "NEL is space");
   ASSERT(!rune_is_space(0x200B), "zero-width space is Cf, not space");
   ASSERT(rune_is_blank(0x2003) && !rune_is_blank(0x2028), "blank is Zs");
   ASSERT(rune_is_punct(0xBF), "¿ is punct");
   ASSERT(rune_is_punct(0xAB), "« is punct");
   ASSERT(rune_is_punct('!') && rune_is_punct('_'), "! and _ are punct");
   ASSERT(!rune_is_punct('$') && !rune_is_punct('+'), "$ + are symbols");
   ASSERT(rune_in_group(RUNE_GROUP_Sc, 0x20AC), "€ is currency");
   ASSERT(rune_is_word(0x203F), "undertie ‿ (Pc) is a word char");
   ASSERT(rune_is_control(0x9F) && !rune_is_control(0xA0), "C1 controls");
   PASS(test_name);
}

void test_groups(void) {
   const char *test_name = "groups";
   ASSERT(rune_in_group(RUNE_GROUP_LATIN, 'a'), "a is Latin");
   ASSERT(rune_in_group(RUNE_GROUP_LATIN, 0xE9), "é is Latin");
   ASSERT(rune_in_group(RUNE_GROUP_GREEK, 0x3BB), "λ is Greek");
   ASSERT(rune_in_group(RUNE_GROUP_CYRILLIC, 0x414), "Д is Cyrillic");
   ASSERT(rune_in_group(RUNE_GROUP_HAN, 0x65E5), "日 is Han");
   ASSERT(rune_in_group(RUNE_GROUP_HIRAGANA, 0x3042), "あ is Hiragana");
   ASSERT(rune_in_group(RUNE_GROUP_KATAKANA, 0x30A2), "ア is Katakana");
   ASSERT(rune_in_group(RUNE_GROUP_THAI, 0xE01), "ก is Thai");
   ASSERT(!rune_in_group(RUNE_GROUP_GREEK, 'a'), "a is not Greek");
   ASSERT(rune_in_group(RUNE_GROUP_L, 0x10400), "Deseret 𐐀 is a letter");
   ASSERT(rune_in_group(RUNE_GROUP_So, 0x1F600), "emoji is So");
   ASSERT(rune_in_group(RUNE_GROUP_HAN, 0x20000), "CJK Ext B is Han");
   ASSERT(rune_in_group(RUNE_GROUP_HANGUL, 0xAC00), "가 is Hangul");
   ASSERT(rune_in_group(RUNE_GROUP_HEBREW, 0x5D0), "א is Hebrew");
   ASSERT(rune_in_group(RUNE_GROUP_Cf, 0x200D), "ZWJ is Cf");
   ASSERT(rune_in_group(RUNE_GROUP_Mn, 0x301), "combining acute is Mn");
   ASSERT(!rune_in_group(RUNE_GROUP_COUNT, 'a'), "bad group id is false");
   ASSERT(!rune_in_group((rune_group)-1, 'a'), "negative group id is false");
   PASS(test_name);
}

/* every range table is sorted, non-overlapping and well-formed, so the
 * binary search in rune_in_group agrees with a linear scan */
void test_groups_match_linear(void) {
   const char *test_name = "groups_match_linear";
   usize bad = 0;
   for (int g = 0; g < RUNE_GROUP_COUNT; g++) {
      const utf8__group *grp = &utf8__groups[g];
      for (usize i = 0; i < grp->len; i++) {
         if (grp->ranges[i].lo > grp->ranges[i].hi)
            bad++;
         if (i > 0 && grp->ranges[i].lo <= grp->ranges[i - 1].hi)
            bad++;
      }
      for (rune c = 0; c <= RUNE_MAX; c += (c < 0x30000 ? 1 : 97)) {
         bool lin = false;
         for (usize i = 0; i < grp->len && !lin; i++)
            lin = grp->ranges[i].lo <= c && c <= grp->ranges[i].hi;
         if (lin != rune_in_group((rune_group)g, c))
            bad++;
      }
   }
   ASSERT(bad == 0, "%zu mismatches / malformed ranges", bad);
   PASS(test_name);
}

/* ================================================================== */
/* string operations                                                  */
/* ================================================================== */

void test_string_utf8_lower_upper(void) {
   const char *test_name = "string_utf8_lower_upper";
   arena a = {0};
   string mixed = S("\xC3\x80\xC3\x89 Stra\xC3\x9F" "e \xCE\xA3\xCE\xB1"); /* ÀÉ Straße Σα */
   string lo = string_utf8_lower(&a, mixed);
   string up = string_utf8_upper(&a, mixed);
   ASSERT_STR(lo, "\xC3\xA0\xC3\xA9 stra\xC3\x9F" "e \xCF\x83\xCE\xB1"); /* àé straße σα */
   ASSERT_STR(up, "\xC3\x80\xC3\x89 STRA\xC3\x9F" "E \xCE\xA3\xCE\x91"); /* ÀÉ STRAßE ΣΑ */
   ASSERT(lo.data[lo.len] == '\0', "result is NUL-terminated");

   /* length can change: Kelvin sign (3 bytes) lowers to 'k' (1 byte) */
   string k = string_utf8_lower(&a, S("\xE2\x84\xAA"));
   ASSERT_STR(k, "k");

   /* ill-formed bytes pass through untouched */
   string bad = string_utf8_upper(&a, S("a\xFF" "b\xE6\x97"));
   ASSERT_STR(bad, "A\xFF" "B\xE6\x97");

   string empty = string_utf8_lower(&a, S(""));
   ASSERT(empty.len == 0 && empty.data[0] == '\0', "empty in, empty out");
   arena_release(&a);
   PASS(test_name);
}

void test_string_utf8_casefold(void) {
   const char *test_name = "string_utf8_casefold";
   arena a = {0};
   /* ΣΟΦΟΣ (capital sigmas) and σοφος (final sigma) fold to the same */
   string f1 = string_utf8_casefold(&a, S("\xCE\xA3\xCE\x9F\xCE\xA6\xCE\x9F\xCE\xA3"));
   string f2 = string_utf8_casefold(&a, S("\xCF\x83\xCE\xBF\xCF\x86\xCE\xBF\xCF\x82"));
   ASSERT(string_eq(f1, f2), "ΣΟΦΟΣ and σοφος fold equal");
   arena_release(&a);
   PASS(test_name);
}

void test_string_utf8_cmp_nocase(void) {
   const char *test_name = "string_utf8_cmp_nocase";
   ASSERT(string_utf8_eq_nocase(S("Hello"), S("hELLO")), "ascii");
   ASSERT(string_utf8_eq_nocase(S("Stra\xC3\x9F" "e"), S("STRA\xC3\x9F" "E")),
          "Straße == STRAßE");
   ASSERT(string_utf8_eq_nocase(S("\xC3\x89t\xC3\xA9"), S("\xC3\xA9T\xC3\x89")),
          "Été == éTÉ");
   ASSERT(string_utf8_eq_nocase(S("\xE2\x84\xAA" "elvin"), S("kelvin")),
          "Kelvin sign: equal despite different byte lengths");
   ASSERT(string_utf8_eq_nocase(S(""), S("")), "empty == empty");
   ASSERT(!string_utf8_eq_nocase(S("abc"), S("abd")), "abc != abd");
   ASSERT(string_utf8_cmp_nocase(S("abc"), S("ABD")) < 0, "abc < ABD");
   ASSERT(string_utf8_cmp_nocase(S("ABD"), S("abc")) > 0, "ABD > abc");
   ASSERT(string_utf8_cmp_nocase(S("ab"), S("ABC")) < 0, "prefix sorts first");
   ASSERT(string_utf8_cmp_nocase(S("ABC"), S("ab")) > 0, "longer sorts last");
   ASSERT(string_utf8_cmp_nocase(S(""), S("a")) < 0, "empty sorts first");
   PASS(test_name);
}

void test_string_utf8_starts_with_nocase(void) {
   const char *test_name = "string_utf8_starts_with_nocase";
   ASSERT(string_utf8_starts_with_nocase(S("Content-Type: x"), S("content-type")),
          "ascii header prefix");
   ASSERT(string_utf8_starts_with_nocase(S("\xC3\x89t\xC3\xA9"), S("\xC3\xA9T")),
          "Été starts with éT");
   ASSERT(string_utf8_starts_with_nocase(S("abc"), S("")), "empty prefix");
   ASSERT(!string_utf8_starts_with_nocase(S("ab"), S("abc")), "prefix too long");
   ASSERT(!string_utf8_starts_with_nocase(S("abc"), S("abd")), "mismatch");
   PASS(test_name);
}

int main(void) {
   printf(COLOR_BOLD "\ncase mapping tests\n" COLOR_RESET "\n");
   test_case_ascii();
   test_case_bmp();
   test_case_above_bmp();
   test_case_ucd_regressions();
   test_casefold();
   test_casefold_consistent();

   printf(COLOR_BOLD "\nclassification tests\n" COLOR_RESET "\n");
   test_class_ascii();
   test_class_unicode();
   test_groups();
   test_groups_match_linear();

   printf(COLOR_BOLD "\nstring tests\n" COLOR_RESET "\n");
   test_string_utf8_lower_upper();
   test_string_utf8_casefold();
   test_string_utf8_cmp_nocase();
   test_string_utf8_starts_with_nocase();

   if (_failed == 0)
      printf("\n" COLOR_BOLD COLOR_GREEN "All tests passed." COLOR_RESET
             "\n\n");
   else
      printf("\n" COLOR_BOLD COLOR_RED "%d test(s) failed." COLOR_RESET "\n\n",
             _failed);
   return _failed != 0;
}
