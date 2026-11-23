/* utf8_gen.c — generate the Unicode data tables in utf8.h.
 *
 * Usage:  ./utf8_gen <ucd-dir> <utf8.h>
 *
 * Reads UnicodeData.txt, CaseFolding.txt, Scripts.txt and license.txt from
 * <ucd-dir> (see the `ucd` target in the Makefile) and rewrites, in place,
 * the regions of <utf8.h> between these marker lines:
 *
 *     // utf8_gen:enum begin    ...  // utf8_gen:enum end
 *     // utf8_gen:tables begin  ...  // utf8_gen:tables end
 *
 * Everything outside the markers is hand-written and left untouched, so
 * utf8.h stays a normal, editable single header. Normal builds never run
 * this; run `make utf8-tables` only to move to a new Unicode version or to
 * change the set of groups below.
 *
 * Tables produced:
 *   - one sorted range list per rune_group (general categories, the
 *     umbrella categories L/M/N/P/S/Z, and a few scripts), all planes;
 *   - simple (1:1) tolower / toupper / casefold mappings, run-length
 *     compressed as (lo, hi, delta, stride) — stride 2 covers the common
 *     alternating Upper/lower blocks such as U+0100..U+017F.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCP 0x110000u

// ---------------------------------------------------------------------------
// Groups. Order here is the order of the rune_group enum.
// ---------------------------------------------------------------------------

typedef enum { G_CATEGORY, G_SCRIPT } group_kind;

typedef struct {
   const char *enum_name; // suffix after RUNE_GROUP_
   const char *match;     // category list ("Lu Ll") or script name
   group_kind kind;
   const char *comment;
} group_def;

static const group_def groups[] = {
    {"L", "Lu Ll Lt Lm Lo", G_CATEGORY, "letter"},
    {"Lu", "Lu", G_CATEGORY, "uppercase letter"},
    {"Ll", "Ll", G_CATEGORY, "lowercase letter"},
    {"Lt", "Lt", G_CATEGORY, "titlecase letter (digraphs like U+01C5)"},
    {"Lm", "Lm", G_CATEGORY, "modifier letter"},
    {"Lo", "Lo", G_CATEGORY, "other letter (CJK, Arabic, ...)"},
    {"M", "Mn Mc Me", G_CATEGORY, "mark"},
    {"Mn", "Mn", G_CATEGORY, "nonspacing (combining) mark"},
    {"Mc", "Mc", G_CATEGORY, "spacing mark"},
    {"Me", "Me", G_CATEGORY, "enclosing mark"},
    {"N", "Nd Nl No", G_CATEGORY, "number"},
    {"Nd", "Nd", G_CATEGORY, "decimal digit"},
    {"Nl", "Nl", G_CATEGORY, "letter number (Roman numerals)"},
    {"No", "No", G_CATEGORY, "other number (superscripts, fractions)"},
    {"P", "Pc Pd Ps Pe Pi Pf Po", G_CATEGORY, "punctuation"},
    {"Pc", "Pc", G_CATEGORY, "connector punctuation ('_')"},
    {"Pd", "Pd", G_CATEGORY, "dash"},
    {"Ps", "Ps", G_CATEGORY, "open bracket"},
    {"Pe", "Pe", G_CATEGORY, "close bracket"},
    {"Pi", "Pi", G_CATEGORY, "initial quote"},
    {"Pf", "Pf", G_CATEGORY, "final quote"},
    {"Po", "Po", G_CATEGORY, "other punctuation"},
    {"S", "Sm Sc Sk So", G_CATEGORY, "symbol"},
    {"Sm", "Sm", G_CATEGORY, "math symbol"},
    {"Sc", "Sc", G_CATEGORY, "currency symbol"},
    {"Sk", "Sk", G_CATEGORY, "modifier symbol"},
    {"So", "So", G_CATEGORY, "other symbol (emoji, ...)"},
    {"Z", "Zs Zl Zp", G_CATEGORY, "separator"},
    {"Zs", "Zs", G_CATEGORY, "space separator"},
    {"Zl", "Zl", G_CATEGORY, "line separator"},
    {"Zp", "Zp", G_CATEGORY, "paragraph separator"},
    {"Cc", "Cc", G_CATEGORY, "control"},
    {"Cf", "Cf", G_CATEGORY, "format (ZWJ, BOM, soft hyphen, ...)"},
    {"Co", "Co", G_CATEGORY, "private use"},
    {"ARABIC", "Arabic", G_SCRIPT, NULL},
    {"ARMENIAN", "Armenian", G_SCRIPT, NULL},
    {"BENGALI", "Bengali", G_SCRIPT, NULL},
    {"CYRILLIC", "Cyrillic", G_SCRIPT, NULL},
    {"DEVANAGARI", "Devanagari", G_SCRIPT, NULL},
    {"GEORGIAN", "Georgian", G_SCRIPT, NULL},
    {"GREEK", "Greek", G_SCRIPT, NULL},
    {"HAN", "Han", G_SCRIPT, NULL},
    {"HANGUL", "Hangul", G_SCRIPT, NULL},
    {"HEBREW", "Hebrew", G_SCRIPT, NULL},
    {"HIRAGANA", "Hiragana", G_SCRIPT, NULL},
    {"KATAKANA", "Katakana", G_SCRIPT, NULL},
    {"LATIN", "Latin", G_SCRIPT, NULL},
    {"THAI", "Thai", G_SCRIPT, NULL},
};
#define NGROUPS (sizeof(groups) / sizeof(groups[0]))

// ---------------------------------------------------------------------------
// Per-codepoint data
// ---------------------------------------------------------------------------

static char (*category)[3]; // "Lu", or "" if unassigned
static int *script;          // index into groups[], or -1
static uint32_t *lower, *upper, *fold;

static void die(const char *msg, const char *arg) {
   fprintf(stderr, "utf8_gen: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
   exit(1);
}

static FILE *open_in(const char *dir, const char *name) {
   char path[4096];
   snprintf(path, sizeof path, "%s/%s", dir, name);
   FILE *f = fopen(path, "r");
   if (!f)
      die("cannot open", path);
   return f;
}

// Split line on ';' into at most max fields (in place). Returns count.
static int split_fields(char *line, char **fields, int max) {
   int n = 0;
   char *p = line;
   while (n < max) {
      fields[n++] = p;
      char *semi = strchr(p, ';');
      if (!semi)
         break;
      *semi = '\0';
      p = semi + 1;
   }
   return n;
}

static char *trim(char *s) {
   while (*s == ' ' || *s == '\t')
      s++;
   char *e = s + strlen(s);
   while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' ||
                    e[-1] == '\r'))
      *--e = '\0';
   return s;
}

static void read_unicode_data(const char *dir) {
   FILE *f = open_in(dir, "UnicodeData.txt");
   char line[1024];
   long first = -1; // start of a <..., First> / <..., Last> range
   while (fgets(line, sizeof line, f)) {
      char *fl[15];
      if (split_fields(line, fl, 15) < 15)
         continue;
      uint32_t cp = (uint32_t)strtoul(fl[0], NULL, 16);
      const char *name = fl[1], *cat = fl[2];
      if (strstr(name, ", First>")) {
         first = cp;
         continue;
      }
      uint32_t lo = cp;
      if (strstr(name, ", Last>")) {
         if (first < 0)
            die("Last without First in UnicodeData.txt", name);
         lo = (uint32_t)first;
         first = -1;
      }
      for (uint32_t c = lo; c <= cp; c++)
         memcpy(category[c], cat, 3);
      // simple mappings (fields 12 upper, 13 lower) apply to cp itself
      if (*trim(fl[12]))
         upper[cp] = (uint32_t)strtoul(fl[12], NULL, 16);
      if (*trim(fl[13]))
         lower[cp] = (uint32_t)strtoul(fl[13], NULL, 16);
   }
   fclose(f);
}

static void read_case_folding(const char *dir) {
   FILE *f = open_in(dir, "CaseFolding.txt");
   char line[1024];
   while (fgets(line, sizeof line, f)) {
      if (line[0] == '#' || line[0] == '\n')
         continue;
      char *fl[4];
      if (split_fields(line, fl, 4) < 3)
         continue;
      const char *status = trim(fl[1]);
      // C = common, S = simple. F (full, 1:n) and T (Turkic) are skipped.
      if (strcmp(status, "C") != 0 && strcmp(status, "S") != 0)
         continue;
      uint32_t cp = (uint32_t)strtoul(fl[0], NULL, 16);
      fold[cp] = (uint32_t)strtoul(trim(fl[2]), NULL, 16);
   }
   fclose(f);
}

static void read_scripts(const char *dir, char *version, size_t vcap) {
   FILE *f = open_in(dir, "Scripts.txt");
   char line[1024];
   version[0] = '\0';
   while (fgets(line, sizeof line, f)) {
      if (!version[0] && sscanf(line, "# Scripts-%31[0-9.]", version) == 1) {
         size_t n = strlen(version);
         if (n && version[n - 1] == '.') // strip the "." of ".txt"
            version[n - 1] = '\0';
         if (n >= vcap)
            die("version string too long", NULL);
         continue;
      }
      char *hash = strchr(line, '#');
      if (hash)
         *hash = '\0';
      char *fl[2];
      if (split_fields(line, fl, 2) < 2)
         continue;
      const char *name = trim(fl[1]);
      int g = -1;
      for (size_t i = 0; i < NGROUPS; i++)
         if (groups[i].kind == G_SCRIPT && strcmp(groups[i].match, name) == 0)
            g = (int)i;
      if (g < 0)
         continue;
      char *range = trim(fl[0]);
      char *dots = strstr(range, "..");
      uint32_t lo = (uint32_t)strtoul(range, NULL, 16);
      uint32_t hi = dots ? (uint32_t)strtoul(dots + 2, NULL, 16) : lo;
      for (uint32_t c = lo; c <= hi; c++)
         script[c] = g;
   }
   fclose(f);
   if (!version[0])
      die("could not find the Unicode version in Scripts.txt", NULL);
}

static bool in_group(size_t g, uint32_t c) {
   if (groups[g].kind == G_SCRIPT)
      return script[c] == (int)g;
   if (!category[c][0])
      return false;
   // match is a space-separated list of two-letter categories
   for (const char *m = groups[g].match; *m; m += (m[2] ? 3 : 2))
      if (m[0] == category[c][0] && m[1] == category[c][1])
         return true;
   return false;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

typedef struct {
   char *data;
   size_t len, cap;
} buf;

static void bprintf(buf *b, const char *fmt, ...) {
   va_list ap, ap2;
   va_start(ap, fmt);
   va_copy(ap2, ap);
   int n = vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);
   if (n < 0)
      die("vsnprintf failed", NULL);
   if (b->len + (size_t)n + 1 > b->cap) {
      size_t cap = b->cap ? b->cap : 4096;
      while (b->len + (size_t)n + 1 > cap)
         cap *= 2;
      b->data = realloc(b->data, cap);
      if (!b->data)
         die("out of memory", NULL);
      b->cap = cap;
   }
   vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
   va_end(ap2);
   b->len += (size_t)n;
}

static void emit_enum(buf *b, const char *version) {
   bprintf(b, "#define UTF8_UNICODE_VERSION \"%s\"\n\n", version);
   bprintf(b, "// General categories (standard Unicode abbreviations; the one-"
              "letter\n// groups are unions) and a few scripts.\n");
   bprintf(b, "typedef enum {\n");
   for (size_t g = 0; g < NGROUPS; g++) {
      if (groups[g].comment)
         bprintf(b, "   RUNE_GROUP_%s, // %s\n", groups[g].enum_name,
                 groups[g].comment);
      else
         bprintf(b, "   RUNE_GROUP_%s,\n", groups[g].enum_name);
   }
   bprintf(b, "   RUNE_GROUP_COUNT\n} rune_group;\n");
}

static size_t emit_ranges(buf *b, size_t g) {
   bprintf(b, "static const utf8__range utf8__%s_ranges[] = {\n",
           groups[g].enum_name);
   size_t n = 0, col = 0;
   uint32_t c = 0;
   while (c < NCP) {
      if (!in_group(g, c)) {
         c++;
         continue;
      }
      uint32_t lo = c;
      while (c + 1 < NCP && in_group(g, c + 1))
         c++;
      if (col == 0)
         bprintf(b, "   ");
      bprintf(b, " {0x%04X, 0x%04X},", lo, c);
      if (++col == 4) {
         bprintf(b, "\n");
         col = 0;
      }
      n++;
      c++;
   }
   if (col)
      bprintf(b, "\n");
   bprintf(b, "};\n");
   if (n == 0)
      die("group has no codepoints", groups[g].enum_name);
   return n;
}

// Run-length compress a mapping into (lo, hi, delta, stride) entries. A run
// extends while mapped codepoints share a delta and are evenly spaced 1 or
// 2 apart; codepoints between stride-2 members are never mapped by the same
// run (if one were, the spacing would have been 1 and the run would end).
static size_t emit_case(buf *b, const char *name, const uint32_t *map) {
   bprintf(b, "static const utf8__case_range utf8__%s_table[] = {\n", name);
   size_t n = 0;
   bool open = false;
   uint32_t lo = 0, hi = 0, stride = 0;
   int64_t delta = 0;
   for (uint32_t c = 0; c <= NCP; c++) {
      bool mapped = c < NCP && map[c] != c;
      int64_t d = mapped ? (int64_t)map[c] - (int64_t)c : 0;
      if (open && mapped && d == delta) {
         uint32_t gap = c - hi;
         if (stride == 0 && (gap == 1 || gap == 2)) {
            stride = gap;
            hi = c;
            continue;
         }
         if (gap == stride) {
            hi = c;
            continue;
         }
      }
      if (open && (mapped || c == NCP || c - hi > 2)) {
         bprintf(b, "    {0x%04X, 0x%04X, %6lld, %u},\n", lo, hi,
                 (long long)delta, stride ? stride : 1);
         n++;
         open = false;
      }
      if (mapped) {
         open = true;
         lo = hi = c;
         delta = d;
         stride = 0;
      }
   }
   bprintf(b, "};\n");
   return n;
}

static void emit_tables(buf *b, const char *dir, const char *version) {
   // Unicode License V3 requires the notice to travel with the data.
   bprintf(b, "/* Generated by utf8_gen.c from the Unicode Character Database"
              " %s.\n * Do not edit by hand; run `make utf8-tables`.\n *\n",
           version);
   FILE *lf = open_in(dir, "license.txt");
   char line[1024];
   while (fgets(line, sizeof line, lf)) {
      char *t = line;
      t[strcspn(t, "\r\n")] = '\0';
      if (strstr(t, "*/"))
         die("license text contains a comment terminator", NULL);
      bprintf(b, *t ? " * %s\n" : " *\n", t);
   }
   fclose(lf);
   bprintf(b, " */\n\n");

   bprintf(b, "typedef struct {\n   u32 lo, hi;\n} utf8__range;\n\n");
   bprintf(b, "typedef struct {\n   u32 lo, hi;\n   i32 delta;\n"
              "   u32 stride; // 1 = every codepoint in [lo, hi]; 2 = lo, "
              "lo+2, ...\n} utf8__case_range;\n\n");

   bprintf(b, "typedef struct {\n   const utf8__range *ranges;\n   usize len;\n} utf8__group;\n\n");

   size_t total = 0;
   for (size_t g = 0; g < NGROUPS; g++)
      total += emit_ranges(b, g);
   bprintf(b, "\n#define UTF8__GROUP(name)                                  "
              "                    \\\n"
              "   {utf8__##name##_ranges, ARRAY_SIZE(utf8__##name##_ranges)}"
              "\n\n");
   bprintf(b, "static const utf8__group utf8__groups[RUNE_GROUP_COUNT] = {\n");
   for (size_t g = 0; g < NGROUPS; g++)
      bprintf(b, "    [RUNE_GROUP_%s] = UTF8__GROUP(%s),\n",
              groups[g].enum_name, groups[g].enum_name);
   bprintf(b, "};\n\n");

   size_t nl = emit_case(b, "tolower", lower);
   bprintf(b, "\n");
   size_t nu = emit_case(b, "toupper", upper);
   bprintf(b, "\n");
   size_t nf = emit_case(b, "casefold", fold);
   fprintf(stderr,
           "utf8_gen: Unicode %s — %zu group ranges, case runs: lower %zu, "
           "upper %zu, fold %zu\n",
           version, total, nl, nu, nf);
}

// ---------------------------------------------------------------------------
// Splice generated text between marker lines
// ---------------------------------------------------------------------------

static char *read_file(const char *path, size_t *len) {
   FILE *f = fopen(path, "rb");
   if (!f)
      die("cannot open", path);
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *s = malloc((size_t)n + 1);
   if (!s || fread(s, 1, (size_t)n, f) != (size_t)n)
      die("cannot read", path);
   s[n] = '\0';
   fclose(f);
   *len = (size_t)n;
   return s;
}

// Replace the text between the line containing `// utf8_gen:<tag> begin`
// and the line containing `// utf8_gen:<tag> end` with gen.
static void splice(buf *out, const char *src, const char *tag,
                   const buf *gen) {
   char begin[64], end[64];
   snprintf(begin, sizeof begin, "// utf8_gen:%s begin\n", tag);
   snprintf(end, sizeof end, "// utf8_gen:%s end\n", tag);
   const char *b = strstr(src, begin);
   const char *e = b ? strstr(b, end) : NULL;
   if (!b || !e)
      die("missing marker pair in header", tag);
   if (strstr(e + strlen(end), begin))
      die("duplicate marker in header", tag);
   size_t keep = (size_t)(b - src) + strlen(begin);
   bprintf(out, "%.*s", (int)keep, src);
   bprintf(out, "%.*s", (int)gen->len, gen->data);
   bprintf(out, "%s", e);
}

int main(int argc, char **argv) {
   if (argc != 3) {
      fprintf(stderr, "usage: %s <ucd-dir> <utf8.h>\n", argv[0]);
      return 2;
   }
   const char *dir = argv[1], *header = argv[2];

   category = calloc(NCP, sizeof *category);
   script = malloc(NCP * sizeof *script);
   lower = malloc(NCP * sizeof *lower);
   upper = malloc(NCP * sizeof *upper);
   fold = malloc(NCP * sizeof *fold);
   if (!category || !script || !lower || !upper || !fold)
      die("out of memory", NULL);
   for (uint32_t c = 0; c < NCP; c++) {
      script[c] = -1;
      lower[c] = upper[c] = fold[c] = c;
   }

   char version[32];
   read_unicode_data(dir);
   read_case_folding(dir);
   read_scripts(dir, version, sizeof version);

   buf en = {0}, tb = {0};
   emit_enum(&en, version);
   emit_tables(&tb, dir, version);

   size_t len;
   char *src = read_file(header, &len);
   buf pass1 = {0}, pass2 = {0};
   splice(&pass1, src, "enum", &en);
   splice(&pass2, pass1.data, "tables", &tb);

   // write to a temp file, then rename, so a failure never truncates utf8.h
   char tmp[4096];
   snprintf(tmp, sizeof tmp, "%s.tmp", header);
   FILE *f = fopen(tmp, "wb");
   if (!f || fwrite(pass2.data, 1, pass2.len, f) != pass2.len || fclose(f))
      die("cannot write", tmp);
   if (rename(tmp, header) != 0)
      die("cannot rename over", header);
   return 0;
}
