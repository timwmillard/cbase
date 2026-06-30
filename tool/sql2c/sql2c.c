// sql2c — SQL→C code generator.
//
// Standalone: depends only on the SQLite amalgamation (sqlite3.c / sqlite3.h).
// The idea is to let SQLite itself analyze the schema and queries (see PLAN.md).

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

// ============================================================================
// Arena — block-backed bump allocator for the generator's own memory. Freed all
// at once at exit; we never individually free during a run.
// ============================================================================

#define ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGN 16

typedef struct ArenaBlock {
   struct ArenaBlock *next;
   size_t used;
   size_t cap;
   char data[];
} ArenaBlock;

typedef struct {
   ArenaBlock *head;
} Arena;

static ArenaBlock *arena_block_new(size_t cap) {
   if (cap < ARENA_BLOCK_SIZE) cap = ARENA_BLOCK_SIZE;
   ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
   if (!b) {
      fprintf(stderr, "sql2c: out of memory\n");
      exit(1);
   }
   b->next = NULL;
   b->used = 0;
   b->cap = cap;
   return b;
}

static void *arena_alloc(Arena *a, size_t size) {
   size = (size + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);
   if (!a->head || a->head->used + size > a->head->cap) {
      ArenaBlock *b = arena_block_new(size);
      b->next = a->head;
      a->head = b;
   }
   void *p = a->head->data + a->head->used;
   a->head->used += size;
   memset(p, 0, size);
   return p;
}

static void arena_free(Arena *a) {
   for (ArenaBlock *b = a->head; b;) {
      ArenaBlock *next = b->next;
      free(b);
      b = next;
   }
   a->head = NULL;
}

static char *arena_strdup(Arena *a, const char *s) {
   if (!s) return NULL;
   size_t n = strlen(s) + 1;
   char *p = arena_alloc(a, n);
   memcpy(p, s, n);
   return p;
}

// ============================================================================
// StrBuf — growable output buffer for emitted code.
// ============================================================================

typedef struct {
   char *data;
   size_t len;
   size_t cap;
} StrBuf;

static void sb_reserve(StrBuf *sb, size_t extra) {
   if (sb->len + extra + 1 <= sb->cap) return;
   size_t cap = sb->cap ? sb->cap : 256;
   while (cap < sb->len + extra + 1) cap *= 2;
   sb->data = realloc(sb->data, cap);
   if (!sb->data) {
      fprintf(stderr, "sql2c: out of memory\n");
      exit(1);
   }
   sb->cap = cap;
}

static void sb_puts(StrBuf *sb, const char *s) {
   size_t n = strlen(s);
   sb_reserve(sb, n);
   memcpy(sb->data + sb->len, s, n);
   sb->len += n;
   sb->data[sb->len] = 0;
}

static void sb_printf(StrBuf *sb, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   va_list probe;
   va_copy(probe, args);
   int n = vsnprintf(NULL, 0, fmt, probe);
   va_end(probe);
   if (n < 0) {
      va_end(args);
      return;
   }
   sb_reserve(sb, (size_t)n);
   vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, args);
   sb->len += (size_t)n;
   va_end(args);
}

static void sb_free(StrBuf *sb) {
   free(sb->data);
   sb->data = NULL;
   sb->len = sb->cap = 0;
}

// ============================================================================
// File IO
// ============================================================================

static char *read_file(Arena *a, const char *path) {
   FILE *f = fopen(path, "rb");
   if (!f) {
      fprintf(stderr, "sql2c: cannot open %s\n", path);
      exit(1);
   }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *buf = arena_alloc(a, (size_t)n + 1);
   if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
      fprintf(stderr, "sql2c: short read on %s\n", path);
      exit(1);
   }
   buf[n] = 0;
   fclose(f);
   return buf;
}

static void write_file(const char *path, const char *data, size_t len) {
   FILE *f = fopen(path, "wb");
   if (!f) {
      fprintf(stderr, "sql2c: cannot write %s\n", path);
      exit(1);
   }
   fwrite(data, 1, len, f);
   fclose(f);
   printf("Generated %s\n", path);
}

// ============================================================================
// Config — flat `key = value`, `#` comments. CLI flags override.
// ============================================================================

typedef struct {
   const char *schema;
   const char *queries;
   const char *output;
   const char *struct_style;
   const char *field_style;
   const char *func_style;
   const char *type_prefix;
   const char *func_prefix;
} Config;

static char *str_trim(char *s) {
   while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
   if (*s == 0) return s;
   char *end = s + strlen(s) - 1;
   while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
      *end-- = 0;
   }
   return s;
}

static int config_set(Config *c, const char *key, const char *value, Arena *a) {
   const char *v = arena_strdup(a, value);
   if (strcmp(key, "schema") == 0) c->schema = v;
   else if (strcmp(key, "queries") == 0) c->queries = v;
   else if (strcmp(key, "output") == 0) c->output = v;
   else if (strcmp(key, "struct-style") == 0) c->struct_style = v;
   else if (strcmp(key, "field-style") == 0) c->field_style = v;
   else if (strcmp(key, "func-style") == 0) c->func_style = v;
   else if (strcmp(key, "type-prefix") == 0) c->type_prefix = v;
   else if (strcmp(key, "func-prefix") == 0) c->func_prefix = v;
   else return 0;
   return 1;
}

static void config_load_file(Config *c, const char *path, Arena *a) {
   char *text = read_file(a, path);
   int lineno = 0;
   for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
      lineno++;
      char *t = str_trim(line);
      if (*t == 0 || *t == '#') continue;
      char *eq = strchr(t, '=');
      if (!eq) {
         fprintf(stderr, "sql2c: %s:%d: expected key = value\n", path, lineno);
         exit(1);
      }
      *eq = 0;
      char *key = str_trim(t);
      char *value = str_trim(eq + 1);
      if (!config_set(c, key, value, a)) {
         fprintf(stderr, "sql2c: %s:%d: unknown key '%s'\n", path, lineno, key);
      }
   }
}

static const char *args_config_path(int argc, char **argv) {
   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-config") == 0 && i + 1 < argc) return argv[i + 1];
      if (strncmp(argv[i], "-config=", 8) == 0) return argv[i] + 8;
   }
   return NULL;
}

static void args_apply_overrides(Config *c, int argc, char **argv, Arena *a) {
   for (int i = 1; i < argc; i++) {
      if (argv[i][0] != '-') continue;
      char *flag = argv[i] + 1;
      char buf[256];
      const char *value = NULL;
      char *eq = strchr(flag, '=');
      if (eq) {
         size_t klen = (size_t)(eq - flag);
         if (klen >= sizeof(buf)) continue;
         memcpy(buf, flag, klen);
         buf[klen] = 0;
         flag = buf;
         value = eq + 1;
      } else if (i + 1 < argc) {
         value = argv[i + 1];
         i++;
      }
      if (strcmp(flag, "config") == 0) continue;
      if (value) config_set(c, flag, value, a);
   }
}

// ============================================================================
// Naming styles (ported from the Go generator for output parity)
// ============================================================================

typedef enum { STYLE_SNAKE, STYLE_CAMEL, STYLE_PASCAL } Style;

static Style parse_style(const char *s) {
   if (strcmp(s, "snake") == 0) return STYLE_SNAKE;
   if (strcmp(s, "camel") == 0) return STYLE_CAMEL;
   return STYLE_PASCAL;
}

static int is_sep(char c) { return c == '-' || c == '_' || c == ' '; }
static int is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static int is_lower(char c) { return c >= 'a' && c <= 'z'; }
static char to_lower(char c) { return is_upper(c) ? (char)(c - 'A' + 'a') : c; }
static char to_upper(char c) { return is_lower(c) ? (char)(c - 'a' + 'A') : c; }

// Split an identifier into words on separators and lower→upper humps.
// Writes word boundaries into out (start/len pairs); returns word count.
static int split_identifier(const char *s, int *starts, int *lens, int max) {
   int n = 0, cur = -1;
   int len = (int)strlen(s);
   for (int i = 0; i < len; i++) {
      char c = s[i];
      if (is_sep(c)) {
         if (cur >= 0) { lens[n] = i - cur; n++; cur = -1; }
         continue;
      }
      if (i > 0 && is_upper(c) && cur >= 0 && is_lower(s[i - 1])) {
         lens[n] = i - cur; n++; cur = -1;
      }
      if (cur < 0) { if (n >= max) break; cur = i; starts[n] = i; }
   }
   if (cur >= 0 && n < max) { lens[n] = len - cur; n++; }
   return n;
}

static char *to_pascal(Arena *a, const char *s) {
   int starts[64], lens[64];
   int n = split_identifier(s, starts, lens, 64);
   StrBuf sb = {0};
   for (int w = 0; w < n; w++) {
      for (int i = 0; i < lens[w]; i++) {
         char c = s[starts[w] + i];
         char outc = (i == 0) ? to_upper(c) : to_lower(c);
         sb_printf(&sb, "%c", outc);
      }
   }
   char *r = arena_strdup(a, sb.data ? sb.data : "");
   sb_free(&sb);
   return r;
}

static char *to_camel(Arena *a, const char *s) {
   char *p = to_pascal(a, s);
   if (p[0]) p[0] = to_lower(p[0]);
   return p;
}

static char *to_snake(Arena *a, const char *s) {
   StrBuf sb = {0};
   int prev_lower = 0;
   int len = (int)strlen(s);
   for (int i = 0; i < len; i++) {
      char c = s[i];
      if (is_sep(c)) {
         if (sb.len > 0) sb_puts(&sb, "_");
         prev_lower = 0;
         continue;
      }
      if (i > 0 && is_upper(c) && prev_lower) sb_puts(&sb, "_");
      sb_printf(&sb, "%c", to_lower(c));
      prev_lower = !is_upper(c);
   }
   char *r = arena_strdup(a, sb.data ? sb.data : "");
   sb_free(&sb);
   return r;
}

static char *apply_style(Arena *a, const char *s, Style style) {
   switch (style) {
      case STYLE_SNAKE: return to_snake(a, s);
      case STYLE_CAMEL: return to_camel(a, s);
      default: return to_pascal(a, s);
   }
}

// ============================================================================
// Type mapping: declared SQLite type → sql_* C type
// ============================================================================

static const char *sqlite_type_to_sqltype(Arena *a, const char *decltype, int nullable) {
   char lc[64] = {0};
   if (decltype) {
      size_t i = 0;
      for (; decltype[i] && i < sizeof(lc) - 1; i++) lc[i] = to_lower(decltype[i]);
   }
   const char *base;
   if (strstr(lc, "int")) base = "sql_int64";
   else if (strstr(lc, "text") || strstr(lc, "char")) base = "sql_text";
   else if (strstr(lc, "real") || strstr(lc, "doub") || strstr(lc, "floa")) base = "sql_double";
   else if (strstr(lc, "blob")) base = "sql_blob";
   else if (strstr(lc, "bool")) base = "sql_bool";
   else if (strstr(lc, "numeric")) base = "sql_numeric";
   else base = "sql_text";

   if (nullable) {
      StrBuf sb = {0};
      sb_printf(&sb, "sql_null%s", base + 4); // strip "sql_"
      const char *r = arena_strdup(a, sb.data);
      sb_free(&sb);
      return r;
   }
   return base;
}

// ============================================================================
// Schema model + introspection (via SQLite)
// ============================================================================

typedef struct {
   char *name;
   char *decltype;
   int notnull;
   int pk;
} Column;

typedef struct {
   char *name;
   Column *cols;
   int ncols;
} Table;

static void die_db(sqlite3 *db, const char *what) {
   fprintf(stderr, "sql2c: %s: %s\n", what, sqlite3_errmsg(db));
   exit(1);
}

// Read columns for one table via PRAGMA table_info.
static Column *introspect_columns(sqlite3 *db, Arena *a, const char *table, int *out_n) {
   char pragma[512];
   snprintf(pragma, sizeof(pragma), "PRAGMA table_info(\"%s\")", table);
   sqlite3_stmt *st;
   if (sqlite3_prepare_v2(db, pragma, -1, &st, NULL) != SQLITE_OK) die_db(db, "table_info");

   Column *cols = NULL;
   int n = 0, cap = 0;
   while (sqlite3_step(st) == SQLITE_ROW) {
      // columns: cid, name, type, notnull, dflt_value, pk
      if (n == cap) {
         cap = cap ? cap * 2 : 8;
         cols = realloc(cols, (size_t)cap * sizeof(Column));
      }
      cols[n].name = arena_strdup(a, (const char *)sqlite3_column_text(st, 1));
      cols[n].decltype = arena_strdup(a, (const char *)sqlite3_column_text(st, 2));
      cols[n].notnull = sqlite3_column_int(st, 3);
      cols[n].pk = sqlite3_column_int(st, 5);
      n++;
   }
   sqlite3_finalize(st);

   Column *out = arena_alloc(a, (size_t)n * sizeof(Column));
   memcpy(out, cols, (size_t)n * sizeof(Column));
   free(cols);
   *out_n = n;
   return out;
}

// Read all user tables (alphabetical, matching the Go tool's sorted output).
static Table *introspect_schema(sqlite3 *db, Arena *a, int *out_n) {
   const char *sql =
       "SELECT name FROM sqlite_master "
       "WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";
   sqlite3_stmt *st;
   if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) die_db(db, "sqlite_master");

   Table *tables = NULL;
   int n = 0, cap = 0;
   while (sqlite3_step(st) == SQLITE_ROW) {
      if (n == cap) {
         cap = cap ? cap * 2 : 8;
         tables = realloc(tables, (size_t)cap * sizeof(Table));
      }
      const char *name = (const char *)sqlite3_column_text(st, 0);
      tables[n].name = arena_strdup(a, name);
      tables[n].cols = introspect_columns(db, a, name, &tables[n].ncols);
      n++;
   }
   sqlite3_finalize(st);

   Table *out = arena_alloc(a, (size_t)n * sizeof(Table));
   memcpy(out, tables, (size_t)n * sizeof(Table));
   free(tables);
   *out_n = n;
   return out;
}

// A column's nullable-ness for type selection. SQLite reports notnull=0 for
// INTEGER PRIMARY KEY (rowid alias), so treat any PK as non-null like the Go
// tool did.
static int col_nullable(const Column *c) { return c->notnull == 0 && c->pk == 0; }

// ============================================================================
// models.h emit
// ============================================================================

static const char *MODELS_PRELUDE =
    "typedef unsigned char sql_byte;\n"
    "\n"
    "typedef double sql_double;\n"
    "\n"
    "typedef struct {\n"
    "    sql_double value;\n"
    "    bool null;\n"
    "} sql_nulldouble;\n"
    "\n"
    "typedef int sql_int;\n"
    "\n"
    "typedef struct {\n"
    "    sql_int value;\n"
    "    bool null;\n"
    "} sql_nullint;\n"
    "\n"
    "typedef int64_t sql_int64;\n"
    "\n"
    "typedef struct {\n"
    "    sql_int64 value;\n"
    "    bool null;\n"
    "} sql_nullint64;\n"
    "\n"
    "typedef double sql_numeric;\n"
    "\n"
    "typedef struct {\n"
    "    sql_numeric value;\n"
    "    bool null;\n"
    "} sql_nullnumeric;\n"
    "\n"
    "typedef bool sql_bool;\n"
    "\n"
    "typedef struct {\n"
    "    sql_bool value;\n"
    "    bool null;\n"
    "} sql_nullbool;\n"
    "\n"
    "typedef struct {\n"
    "    sql_byte *data;\n"
    "    size_t len;\n"
    "} sql_blob;\n"
    "\n"
    "typedef struct {\n"
    "    sql_byte *data;\n"
    "    size_t len;\n"
    "    bool null;\n"
    "} sql_nullblob;\n"
    "\n"
    "typedef struct {\n"
    "    sql_byte *data;\n"
    "    size_t len;\n"
    "} sql_text;\n"
    "\n"
    "static inline sql_text to_sql_text(char *text) {\n"
    "    return (sql_text){\n"
    "        .data = (sql_byte*)text,\n"
    "        .len = strlen(text),\n"
    "    };\n"
    "}\n"
    "\n"
    "typedef struct {\n"
    "    char *data;\n"
    "    size_t len;\n"
    "    bool null;\n"
    "} sql_nulltext;\n"
    "\n"
    "static inline sql_nulltext to_sql_nulltext(char *text, bool null) {\n"
    "    return (sql_nulltext){\n"
    "        .data = text,\n"
    "        .len = strlen(text),\n"
    "        .null = null,\n"
    "    };\n"
    "}\n"
    "\n";

// Allocator interface + deep-copy helpers used by the owning query wrappers.
static const char *MODELS_ALLOCATOR =
    "// ============ Allocator ============\n"
    "\n"
    "// A single-function allocator: the owning query wrappers copy result rows\n"
    "// (and their text/blob bytes) into whatever this points at. Cleanup is the\n"
    "// caller's job (e.g. reset/release an arena); there is no per-row free.\n"
    "typedef struct sql_allocator {\n"
    "    void *(*alloc)(void *ctx, size_t size);\n"
    "    void *ctx;\n"
    "} sql_allocator;\n"
    "\n"
    "static inline sql_text sql_dup_text(sql_allocator a, sql_text s) {\n"
    "    if (s.data == NULL) return s;\n"
    "    sql_byte *p = a.alloc(a.ctx, s.len + 1);\n"
    "    memcpy(p, s.data, s.len);\n"
    "    p[s.len] = 0;\n"
    "    return (sql_text){ .data = p, .len = s.len };\n"
    "}\n"
    "\n"
    "static inline sql_nulltext sql_dup_nulltext(sql_allocator a, sql_nulltext s) {\n"
    "    if (s.null || s.data == NULL) return s;\n"
    "    char *p = a.alloc(a.ctx, s.len + 1);\n"
    "    memcpy(p, s.data, s.len);\n"
    "    p[s.len] = 0;\n"
    "    return (sql_nulltext){ .data = p, .len = s.len, .null = false };\n"
    "}\n"
    "\n"
    "static inline sql_blob sql_dup_blob(sql_allocator a, sql_blob s) {\n"
    "    if (s.data == NULL) return s;\n"
    "    sql_byte *p = a.alloc(a.ctx, s.len);\n"
    "    memcpy(p, s.data, s.len);\n"
    "    return (sql_blob){ .data = p, .len = s.len };\n"
    "}\n"
    "\n"
    "static inline sql_nullblob sql_dup_nullblob(sql_allocator a, sql_nullblob s) {\n"
    "    if (s.null || s.data == NULL) return s;\n"
    "    sql_byte *p = a.alloc(a.ctx, s.len);\n"
    "    memcpy(p, s.data, s.len);\n"
    "    return (sql_nullblob){ .data = p, .len = s.len, .null = false };\n"
    "}\n"
    "\n";

typedef struct {
   Arena *a;
   Config *cfg;
   Style struct_style;
   Style field_style;
   Style func_style;
   Table *tables;
   int ntables;
} Gen;

static char *type_name(Gen *g, const char *name) {
   char *styled = apply_style(g->a, name, g->struct_style);
   if (g->cfg->type_prefix && g->cfg->type_prefix[0]) {
      StrBuf sb = {0};
      sb_printf(&sb, "%s%s", g->cfg->type_prefix, styled);
      char *r = arena_strdup(g->a, sb.data);
      sb_free(&sb);
      return r;
   }
   return styled;
}

static void emit_models(Gen *g, StrBuf *sb) {
   sb_puts(sb, "// Generated from SQL - do not edit\n\n");
   sb_puts(sb, "#ifndef SQL_MODEL_H\n");
   sb_puts(sb, "#define SQL_MODEL_H\n\n");
   sb_puts(sb, "#include <stdint.h>\n");
   sb_puts(sb, "#include <stdbool.h>\n");
   sb_puts(sb, "#include <stddef.h>\n");
   sb_puts(sb, "#include <string.h>\n\n");
   sb_puts(sb, MODELS_PRELUDE);
   sb_puts(sb, MODELS_ALLOCATOR);

   sb_puts(sb, "// ============ Table Structs ============\n\n");
   for (int t = 0; t < g->ntables; t++) {
      Table *tbl = &g->tables[t];
      sb_puts(sb, "typedef struct {\n");
      for (int c = 0; c < tbl->ncols; c++) {
         Column *col = &tbl->cols[c];
         const char *ctype = sqlite_type_to_sqltype(g->a, col->decltype, col_nullable(col));
         char *fname = apply_style(g->a, col->name, g->field_style);
         sb_printf(sb, "    %s %s;\n", ctype, fname);
      }
      sb_printf(sb, "} %s;\n\n", type_name(g, tbl->name));
   }

   sb_puts(sb, "#endif // SQL_MODEL_H\n");
}

// Directory portion of a path ("a/b/c.h" -> "a/b"), or "." if none.
static char *path_dir(Arena *a, const char *path) {
   const char *slash = strrchr(path, '/');
   if (!slash) return arena_strdup(a, ".");
   size_t n = (size_t)(slash - path);
   char *d = arena_alloc(a, n + 1);
   memcpy(d, path, n);
   d[n] = 0;
   return d;
}

static char *func_name(Gen *g, const char *name) {
   char *styled = apply_style(g->a, name, g->func_style);
   if (g->cfg->func_prefix && g->cfg->func_prefix[0]) {
      StrBuf sb = {0};
      sb_printf(&sb, "%s%s", g->cfg->func_prefix, styled);
      char *r = arena_strdup(g->a, sb.data);
      sb_free(&sb);
      return r;
   }
   return styled;
}

// ============================================================================
// Query model + introspection (via SQLite prepare/metadata)
// ============================================================================

typedef enum { Q_ONE, Q_MANY, Q_EXEC } QueryKind;

typedef struct {
   char *name;        // bind name (':' stripped), or "argN" for positional
   const char *type;  // sql_* C type
} Param;

typedef struct {
   Column col;   // resolved schema column (or synthesized for expressions)
   char *field;  // styled field name
} ResCol;

typedef struct {
   char *name;            // query name, e.g. CreatePerson
   QueryKind kind;
   char *sql;             // statement text
   Param *params;
   int nparams;
   ResCol *results;
   int nresults;
   const char *result_type; // struct type name, NULL if no result columns
} Query;

static Column *find_schema_col(Gen *g, const char *table, const char *col) {
   for (int t = 0; t < g->ntables; t++) {
      if (strcasecmp(g->tables[t].name, table) != 0) continue;
      for (int c = 0; c < g->tables[t].ncols; c++) {
         if (strcasecmp(g->tables[t].cols[c].name, col) == 0) return &g->tables[t].cols[c];
      }
   }
   return NULL;
}

// Type a named parameter by finding a matching column across all tables. If the
// name matches columns of differing type (or none), default to text.
static const char *param_type(Gen *g, const char *name) {
   const char *result = NULL;
   int conflict = 0;
   for (int t = 0; t < g->ntables; t++) {
      for (int c = 0; c < g->tables[t].ncols; c++) {
         if (strcasecmp(g->tables[t].cols[c].name, name) != 0) continue;
         const char *ty = sqlite_type_to_sqltype(g->a, g->tables[t].cols[c].decltype, 0);
         if (!result) result = ty;
         else if (strcmp(result, ty) != 0) conflict = 1;
      }
   }
   if (!result) {
      fprintf(stderr, "sql2c: param ':%s' matches no column; defaulting to sql_text\n", name);
      return "sql_text";
   }
   if (conflict) {
      fprintf(stderr, "sql2c: param ':%s' ambiguous type; defaulting to sql_text\n", name);
      return "sql_text";
   }
   return result;
}

static void introspect_query(Gen *g, sqlite3 *db, Query *q) {
   sqlite3_stmt *st;
   if (sqlite3_prepare_v2(db, q->sql, -1, &st, NULL) != SQLITE_OK) {
      fprintf(stderr, "sql2c: query %s: %s\n", q->name, sqlite3_errmsg(db));
      exit(1);
   }

   int np = sqlite3_bind_parameter_count(st);
   q->params = arena_alloc(g->a, (size_t)np * sizeof(Param));
   q->nparams = np;
   for (int i = 1; i <= np; i++) {
      const char *pn = sqlite3_bind_parameter_name(st, i);
      char namebuf[128];
      if (pn) {
         if (*pn == ':' || *pn == '@' || *pn == '$') pn++;
         snprintf(namebuf, sizeof(namebuf), "%s", pn);
      } else {
         snprintf(namebuf, sizeof(namebuf), "arg%d", i);
      }
      q->params[i - 1].name = arena_strdup(g->a, namebuf);
      q->params[i - 1].type = pn ? param_type(g, namebuf) : "sql_text";
   }

   int nc = sqlite3_column_count(st);
   q->results = arena_alloc(g->a, (size_t)nc * sizeof(ResCol));
   q->nresults = nc;
   for (int i = 0; i < nc; i++) {
      const char *tbl = sqlite3_column_table_name(st, i);
      const char *org = sqlite3_column_origin_name(st, i);
      const char *cn = sqlite3_column_name(st, i);
      Column *sc = (tbl && org) ? find_schema_col(g, tbl, org) : NULL;
      if (sc) {
         q->results[i].col = *sc;
         q->results[i].field = apply_style(g->a, sc->name, g->field_style);
         if (!q->result_type) q->result_type = type_name(g, tbl);
      } else {
         // Expression / computed column: synthesize a column from metadata.
         const char *decl = sqlite3_column_decltype(st, i);
         q->results[i].col.name = arena_strdup(g->a, cn ? cn : "col");
         q->results[i].col.decltype = arena_strdup(g->a, decl ? decl : "text");
         q->results[i].col.notnull = 0;
         q->results[i].col.pk = 0;
         q->results[i].field = apply_style(g->a, cn ? cn : "col", g->field_style);
      }
   }
   sqlite3_finalize(st);

   if (q->kind != Q_EXEC && q->nresults == 0) {
      fprintf(stderr,
              "sql2c: query %s is :%s but returns no columns; add a RETURNING "
              "clause or mark it :exec\n",
              q->name, q->kind == Q_ONE ? "one" : "many");
      exit(1);
   }
}

// Split queries.sql on "-- name:" markers into Query records.
static Query *parse_queries(Gen *g, const char *text, int *out_n) {
   Query *qs = NULL;
   int n = 0, cap = 0;
   Query *cur = NULL;
   StrBuf sql = {0};
   char *buf = arena_strdup(g->a, text);

   char *p = buf;
   while (*p) {
      char *eol = strchr(p, '\n');
      char *line;
      if (eol) { *eol = 0; line = p; p = eol + 1; }
      else { line = p; p += strlen(p); }
      size_t ll = strlen(line);
      if (ll && line[ll - 1] == '\r') line[ll - 1] = 0;

      char tmp[1024];
      snprintf(tmp, sizeof(tmp), "%s", line);
      char *trimmed = str_trim(tmp);

      if (strncmp(trimmed, "-- name:", 8) == 0) {
         if (cur) { cur->sql = arena_strdup(g->a, str_trim(sql.data ? sql.data : "")); sql.len = 0; if (sql.data) sql.data[0] = 0; }
         char namebuf[128] = {0}, kindbuf[32] = {0};
         sscanf(trimmed + 8, " %127s :%31s", namebuf, kindbuf);
         if (n == cap) { cap = cap ? cap * 2 : 8; qs = realloc(qs, (size_t)cap * sizeof(Query)); }
         memset(&qs[n], 0, sizeof(Query));
         qs[n].name = arena_strdup(g->a, namebuf);
         qs[n].kind = strcmp(kindbuf, "many") == 0 ? Q_MANY : strcmp(kindbuf, "exec") == 0 ? Q_EXEC : Q_ONE;
         cur = &qs[n];
         n++;
      } else if (cur) {
         sb_puts(&sql, line);
         sb_puts(&sql, "\n");
      }
   }
   if (cur) cur->sql = arena_strdup(g->a, str_trim(sql.data ? sql.data : ""));
   sb_free(&sql);

   Query *out = arena_alloc(g->a, (size_t)n * sizeof(Query));
   memcpy(out, qs, (size_t)n * sizeof(Query));
   free(qs);
   *out_n = n;
   return out;
}

// ============================================================================
// queries.h / queries.c emit
// ============================================================================

static void emit_sql_literal(StrBuf *sb, const char *sql) {
   sb_puts(sb, "\"");
   for (const char *p = sql; *p; p++) {
      switch (*p) {
         case '\\': sb_puts(sb, "\\\\"); break;
         case '"': sb_puts(sb, "\\\""); break;
         case '\n': sb_puts(sb, "\\n"); break;
         case '\t': sb_puts(sb, "\\t"); break;
         case '\r': break;
         default: sb_printf(sb, "%c", *p);
      }
   }
   sb_puts(sb, "\"");
}

static void emit_sql_comment(StrBuf *sb, const char *sql) {
   sb_puts(sb, "// ");
   int space = 0;
   for (const char *p = sql; *p; p++) {
      char c = *p;
      if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
         space = 1;
         continue;
      }
      if (space) { sb_puts(sb, " "); space = 0; }
      sb_printf(sb, "%c", c);
   }
   sb_puts(sb, "\n");
}

static char *params_type_name(Gen *g, Query *q) {
   StrBuf sb = {0};
   sb_printf(&sb, "%sParams", q->name);
   char *concat = arena_strdup(g->a, sb.data);
   sb_free(&sb);
   return type_name(g, concat);
}

// Write the parameter portion of a signature (leading ", " included), excluding
// the trailing callback/closing.
static void emit_param_args(Gen *g, StrBuf *sb, Query *q) {
   if (q->nparams == 0) return;
   if (q->nparams == 1) {
      sb_printf(sb, ", %s %s", q->params[0].type, apply_style(g->a, q->params[0].name, g->field_style));
   } else {
      sb_printf(sb, ", %s *params", params_type_name(g, q));
   }
}

static void emit_param_struct(Gen *g, StrBuf *sb, Query *q) {
   sb_puts(sb, "typedef struct {\n");
   for (int i = 0; i < q->nparams; i++) {
      sb_printf(sb, "    %s %s;\n", q->params[i].type, apply_style(g->a, q->params[i].name, g->field_style));
   }
   sb_printf(sb, "} %s;\n\n", params_type_name(g, q));
}

static void emit_query_decl(Gen *g, StrBuf *sb, Query *q) {
   const char *fn = func_name(g, q->name);
   emit_sql_comment(sb, q->sql);
   if (q->kind == Q_EXEC) {
      sb_printf(sb, "int %s(sqlite3 *db", fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ");\n");
   } else {
      sb_printf(sb, "int %s_cb(sqlite3 *db", fn);
      emit_param_args(g, sb, q);
      sb_printf(sb, ", void (*cb)(%s*, void*), void *ctx);\n", q->result_type);
   }
}

static void emit_bind_params(Gen *g, StrBuf *sb, Query *q) {
   for (int i = 0; i < q->nparams; i++) {
      int idx = i + 1;
      char access[256];
      const char *field = apply_style(g->a, q->params[i].name, g->field_style);
      if (q->nparams == 1) snprintf(access, sizeof(access), "%s", field);
      else snprintf(access, sizeof(access), "params->%s", field);

      const char *ty = q->params[i].type;
      if (strcmp(ty, "sql_int64") == 0)
         sb_printf(sb, "    sqlite3_bind_int64(stmt, %d, %s);\n", idx, access);
      else if (strcmp(ty, "sql_int") == 0)
         sb_printf(sb, "    sqlite3_bind_int(stmt, %d, %s);\n", idx, access);
      else if (strcmp(ty, "sql_text") == 0)
         sb_printf(sb, "    sqlite3_bind_text(stmt, %d, (char*)%s.data, %s.len, SQLITE_STATIC);\n", idx, access, access);
      else if (strcmp(ty, "sql_double") == 0)
         sb_printf(sb, "    sqlite3_bind_double(stmt, %d, %s);\n", idx, access);
      else if (strcmp(ty, "sql_blob") == 0)
         sb_printf(sb, "    sqlite3_bind_blob(stmt, %d, %s.data, %s.len, SQLITE_STATIC);\n", idx, access, access);
      else if (strcmp(ty, "sql_bool") == 0)
         sb_printf(sb, "    sqlite3_bind_int(stmt, %d, %s ? 1 : 0);\n", idx, access);
   }
   if (q->nparams > 0) sb_puts(sb, "\n");
}

static void emit_extract(Gen *g, StrBuf *sb, Query *q) {
   for (int i = 0; i < q->nresults; i++) {
      Column *col = &q->results[i].col;
      const char *f = q->results[i].field;
      const char *ty = sqlite_type_to_sqltype(g->a, col->decltype, col_nullable(col));

      if (strncmp(ty, "sql_nullint", 11) == 0) {
         sb_printf(sb, "        if (sqlite3_column_type(stmt, %d) != SQLITE_NULL) {\n", i);
         sb_printf(sb, "            result.%s.value = sqlite3_column_int64(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.null = false;\n", f);
         sb_printf(sb, "        } else { result.%s.null = true; }\n", f);
      } else if (strcmp(ty, "sql_int64") == 0) {
         sb_printf(sb, "        result.%s = sqlite3_column_int64(stmt, %d);\n", f, i);
      } else if (strcmp(ty, "sql_int") == 0) {
         sb_printf(sb, "        result.%s = sqlite3_column_int(stmt, %d);\n", f, i);
      } else if (strncmp(ty, "sql_nulltext", 12) == 0) {
         sb_printf(sb, "        if (sqlite3_column_type(stmt, %d) != SQLITE_NULL) {\n", i);
         sb_printf(sb, "            result.%s.data = (char*)sqlite3_column_text(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.len = sqlite3_column_bytes(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.null = false;\n", f);
         sb_printf(sb, "        } else { result.%s.null = true; }\n", f);
      } else if (strcmp(ty, "sql_text") == 0) {
         sb_printf(sb, "        result.%s.data = (sql_byte*)sqlite3_column_text(stmt, %d);\n", f, i);
         sb_printf(sb, "        result.%s.len = sqlite3_column_bytes(stmt, %d);\n", f, i);
      } else if (strncmp(ty, "sql_nulldouble", 14) == 0 || strncmp(ty, "sql_nullnumeric", 15) == 0) {
         sb_printf(sb, "        if (sqlite3_column_type(stmt, %d) != SQLITE_NULL) {\n", i);
         sb_printf(sb, "            result.%s.value = sqlite3_column_double(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.null = false;\n", f);
         sb_printf(sb, "        } else { result.%s.null = true; }\n", f);
      } else if (strcmp(ty, "sql_double") == 0 || strcmp(ty, "sql_numeric") == 0) {
         sb_printf(sb, "        result.%s = sqlite3_column_double(stmt, %d);\n", f, i);
      } else if (strncmp(ty, "sql_nullbool", 12) == 0) {
         sb_printf(sb, "        if (sqlite3_column_type(stmt, %d) != SQLITE_NULL) {\n", i);
         sb_printf(sb, "            result.%s.value = sqlite3_column_int(stmt, %d) != 0;\n", f, i);
         sb_printf(sb, "            result.%s.null = false;\n", f);
         sb_printf(sb, "        } else { result.%s.null = true; }\n", f);
      } else if (strcmp(ty, "sql_bool") == 0) {
         sb_printf(sb, "        result.%s = sqlite3_column_int(stmt, %d) != 0;\n", f, i);
      } else if (strncmp(ty, "sql_nullblob", 12) == 0) {
         sb_printf(sb, "        if (sqlite3_column_type(stmt, %d) != SQLITE_NULL) {\n", i);
         sb_printf(sb, "            result.%s.data = (sql_byte*)sqlite3_column_blob(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.len = sqlite3_column_bytes(stmt, %d);\n", f, i);
         sb_printf(sb, "            result.%s.null = false;\n", f);
         sb_printf(sb, "        } else { result.%s.null = true; }\n", f);
      } else if (strcmp(ty, "sql_blob") == 0) {
         sb_printf(sb, "        result.%s.data = (sql_byte*)sqlite3_column_blob(stmt, %d);\n", f, i);
         sb_printf(sb, "        result.%s.len = sqlite3_column_bytes(stmt, %d);\n", f, i);
      } else {
         sb_printf(sb, "        result.%s.data = (sql_byte*)sqlite3_column_text(stmt, %d);\n", f, i);
         sb_printf(sb, "        result.%s.len = sqlite3_column_bytes(stmt, %d);\n", f, i);
      }
   }
}

static void emit_query_impl(Gen *g, StrBuf *sb, Query *q) {
   const char *fn = func_name(g, q->name);
   const char *kind = q->kind == Q_ONE ? "one" : q->kind == Q_MANY ? "many" : "exec";
   sb_printf(sb, "// %s :%s\n", q->name, kind);

   if (q->kind == Q_EXEC) {
      sb_printf(sb, "int %s(sqlite3 *db", fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ") {\n");
   } else {
      sb_printf(sb, "int %s_cb(sqlite3 *db", fn);
      emit_param_args(g, sb, q);
      sb_printf(sb, ", void (*cb)(%s*, void*), void *ctx) {\n", q->result_type);
   }

   sb_puts(sb, "    const char *sql = ");
   emit_sql_literal(sb, q->sql);
   sb_puts(sb, ";\n");
   sb_puts(sb, "    sqlite3_stmt *stmt;\n");
   sb_puts(sb, "    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);\n");
   sb_puts(sb, "    if (rc != SQLITE_OK) return rc;\n\n");
   emit_bind_params(g, sb, q);

   if (q->kind == Q_ONE) {
      sb_puts(sb, "    rc = sqlite3_step(stmt);\n");
      sb_puts(sb, "    if (rc == SQLITE_ROW) {\n");
      sb_printf(sb, "        %s result = {0};\n", q->result_type);
      emit_extract(g, sb, q);
      sb_puts(sb, "        if (cb) cb(&result, ctx);\n");
      sb_puts(sb, "        rc = SQLITE_OK;\n");
      sb_puts(sb, "    } else if (rc == SQLITE_DONE) {\n");
      sb_puts(sb, "        rc = SQLITE_NOTFOUND;\n");
      sb_puts(sb, "    }\n");
      sb_puts(sb, "    sqlite3_finalize(stmt);\n");
      sb_puts(sb, "    return rc;\n");
      sb_puts(sb, "}\n\n");
   } else if (q->kind == Q_MANY) {
      sb_puts(sb, "    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {\n");
      sb_printf(sb, "        %s result = {0};\n", q->result_type);
      emit_extract(g, sb, q);
      sb_puts(sb, "        if (cb) cb(&result, ctx);\n");
      sb_puts(sb, "    }\n\n");
      sb_puts(sb, "    sqlite3_finalize(stmt);\n");
      sb_puts(sb, "    if (rc == SQLITE_DONE) rc = SQLITE_OK;\n");
      sb_puts(sb, "    return rc;\n");
      sb_puts(sb, "}\n\n");
   } else { // exec
      sb_puts(sb, "    rc = sqlite3_step(stmt);\n");
      sb_puts(sb, "    sqlite3_finalize(stmt);\n");
      sb_puts(sb, "    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;\n");
      sb_puts(sb, "}\n\n");
   }
}

// Argument list passed when the wrapper calls the _cb primitive (leading ", ").
static void emit_param_call(Gen *g, StrBuf *sb, Query *q) {
   if (q->nparams == 0) return;
   if (q->nparams == 1) sb_printf(sb, ", %s", apply_style(g->a, q->params[0].name, g->field_style));
   else sb_puts(sb, ", params");
}

// Deep-copy each text/blob result field from borrowed storage into the
// allocator, so returned rows survive sqlite3_finalize.
static void emit_deepcopy(Gen *g, StrBuf *sb, Query *q, const char *target, const char *aexpr) {
   for (int i = 0; i < q->nresults; i++) {
      Column *col = &q->results[i].col;
      const char *f = q->results[i].field;
      const char *ty = sqlite_type_to_sqltype(g->a, col->decltype, col_nullable(col));
      const char *dup = NULL;
      if (strncmp(ty, "sql_nulltext", 12) == 0) dup = "sql_dup_nulltext";
      else if (strcmp(ty, "sql_text") == 0) dup = "sql_dup_text";
      else if (strncmp(ty, "sql_nullblob", 12) == 0) dup = "sql_dup_nullblob";
      else if (strcmp(ty, "sql_blob") == 0) dup = "sql_dup_blob";
      if (dup) sb_printf(sb, "    %s%s = %s(%s, %s%s);\n", target, f, dup, aexpr, target, f);
   }
}

static const char *slice_type(Gen *g, const char *result_type) {
   StrBuf sb = {0};
   sb_printf(&sb, "%sSlice", result_type);
   char *r = arena_strdup(g->a, sb.data);
   sb_free(&sb);
   return r;
}

static void emit_wrapper_decl(Gen *g, StrBuf *sb, Query *q) {
   const char *fn = func_name(g, q->name);
   if (q->kind == Q_ONE) {
      sb_printf(sb, "%s *%s(sql_allocator a, sqlite3 *db", q->result_type, fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ");\n");
   } else if (q->kind == Q_MANY) {
      sb_printf(sb, "%s %s(sql_allocator a, sqlite3 *db", slice_type(g, q->result_type), fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ");\n");
   }
}

static void emit_wrapper_impl(Gen *g, StrBuf *sb, Query *q) {
   if (q->kind == Q_EXEC) return;
   const char *fn = func_name(g, q->name);
   const char *rt = q->result_type;

   if (q->kind == Q_ONE) {
      sb_printf(sb, "typedef struct { sql_allocator a; %s *out; } %s_ctx;\n", rt, fn);
      sb_printf(sb, "static void %s_collect(%s *row, void *vctx) {\n", fn, rt);
      sb_printf(sb, "    %s_ctx *c = (%s_ctx *)vctx;\n", fn, fn);
      sb_printf(sb, "    c->out = (%s *)c->a.alloc(c->a.ctx, sizeof(%s));\n", rt, rt);
      sb_puts(sb, "    *c->out = *row;\n");
      emit_deepcopy(g, sb, q, "c->out->", "c->a");
      sb_puts(sb, "}\n");
      sb_printf(sb, "%s *%s(sql_allocator a, sqlite3 *db", rt, fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ") {\n");
      sb_printf(sb, "    %s_ctx c = { a, NULL };\n", fn);
      sb_printf(sb, "    %s_cb(db", fn);
      emit_param_call(g, sb, q);
      sb_printf(sb, ", %s_collect, &c);\n", fn);
      sb_puts(sb, "    return c.out;\n");
      sb_puts(sb, "}\n\n");
   } else { // many
      const char *st = slice_type(g, rt);
      sb_printf(sb, "typedef struct { sql_allocator a; %s *items; size_t len, cap; } %s_ctx;\n", rt, fn);
      sb_printf(sb, "static void %s_collect(%s *row, void *vctx) {\n", fn, rt);
      sb_printf(sb, "    %s_ctx *c = (%s_ctx *)vctx;\n", fn, fn);
      sb_puts(sb, "    if (c->len == c->cap) {\n");
      sb_puts(sb, "        size_t ncap = c->cap ? c->cap * 2 : 8;\n");
      sb_printf(sb, "        %s *ni = (%s *)c->a.alloc(c->a.ctx, ncap * sizeof(%s));\n", rt, rt, rt);
      sb_printf(sb, "        if (c->items) memcpy(ni, c->items, c->len * sizeof(%s));\n", rt);
      sb_puts(sb, "        c->items = ni; c->cap = ncap;\n");
      sb_puts(sb, "    }\n");
      sb_printf(sb, "    %s *dst = &c->items[c->len++];\n", rt);
      sb_puts(sb, "    *dst = *row;\n");
      emit_deepcopy(g, sb, q, "dst->", "c->a");
      sb_puts(sb, "}\n");
      sb_printf(sb, "%s %s(sql_allocator a, sqlite3 *db", st, fn);
      emit_param_args(g, sb, q);
      sb_puts(sb, ") {\n");
      sb_printf(sb, "    %s_ctx c = { a, NULL, 0, 0 };\n", fn);
      sb_printf(sb, "    %s_cb(db", fn);
      emit_param_call(g, sb, q);
      sb_printf(sb, ", %s_collect, &c);\n", fn);
      sb_printf(sb, "    return (%s){ c.items, c.len };\n", st);
      sb_puts(sb, "}\n\n");
   }
}

static void emit_header(Gen *g, StrBuf *sb, Query *qs, int nq, const char *output) {
   const char *base = strrchr(output, '/');
   base = base ? base + 1 : output;
   StrBuf guard = {0};
   for (const char *p = base; *p; p++) sb_printf(&guard, "%c", *p == '.' ? '_' : to_upper(*p));

   sb_puts(sb, "// Generated from SQL - do not edit\n\n");
   sb_printf(sb, "#ifndef %s\n", guard.data);
   sb_printf(sb, "#define %s\n\n", guard.data);
   sb_puts(sb, "#include \"sqlite3.h\"\n\n");
   sb_puts(sb, "#include \"models.h\"\n\n");
   sb_free(&guard);

   // Result slices for :many wrappers (deduped by result type).
   const char *seen[256];
   int nseen = 0;
   sb_puts(sb, "// ============ Result Slices ============\n\n");
   for (int i = 0; i < nq; i++) {
      if (qs[i].kind != Q_MANY) continue;
      int dup = 0;
      for (int j = 0; j < nseen; j++)
         if (strcmp(seen[j], qs[i].result_type) == 0) dup = 1;
      if (dup || nseen >= 256) continue;
      seen[nseen++] = qs[i].result_type;
      sb_printf(sb, "typedef struct { %s *items; size_t len; } %s;\n",
                qs[i].result_type, slice_type(g, qs[i].result_type));
   }
   sb_puts(sb, "\n");

   sb_puts(sb, "// ============ Param Structs ============\n\n");
   for (int i = 0; i < nq; i++) {
      if (qs[i].nparams > 1) emit_param_struct(g, sb, &qs[i]);
   }

   sb_puts(sb, "// ============ Query Functions ============\n\n");
   for (int i = 0; i < nq; i++) {
      emit_query_decl(g, sb, &qs[i]);
      emit_wrapper_decl(g, sb, &qs[i]);
   }

   sb_puts(sb, "\n");
   const char *base2 = strrchr(output, '/');
   base2 = base2 ? base2 + 1 : output;
   StrBuf guard2 = {0};
   for (const char *p = base2; *p; p++) sb_printf(&guard2, "%c", *p == '.' ? '_' : to_upper(*p));
   sb_printf(sb, "#endif // %s\n", guard2.data);
   sb_free(&guard2);
}

static void emit_impl(Gen *g, StrBuf *sb, Query *qs, int nq, const char *header_base) {
   sb_puts(sb, "// Generated from SQL - do not edit\n\n");
   sb_puts(sb, "#include <stdlib.h>\n");
   sb_puts(sb, "#include <string.h>\n");
   sb_printf(sb, "#include \"%s\"\n\n", header_base);
   for (int i = 0; i < nq; i++) {
      emit_query_impl(g, sb, &qs[i]);
      emit_wrapper_impl(g, sb, &qs[i]);
   }
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
   Arena arena = {0};

   Config cfg = {
       .schema = "schema.sql",
       .queries = "queries.sql",
       .output = "queries.h",
       .struct_style = "pascal",
       .field_style = "pascal",
       .func_style = "snake",
       .type_prefix = "",
       .func_prefix = "",
   };

   const char *config_path = args_config_path(argc, argv);
   if (config_path) config_load_file(&cfg, config_path, &arena);
   args_apply_overrides(&cfg, argc, argv, &arena);

   sqlite3 *db = NULL;
   if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
      fprintf(stderr, "sql2c: cannot open in-memory db: %s\n", sqlite3_errmsg(db));
      sqlite3_close(db);
      return 1;
   }

   // Load the schema into the analyzer db.
   char *schema_sql = read_file(&arena, cfg.schema);
   char *errmsg = NULL;
   if (sqlite3_exec(db, schema_sql, NULL, NULL, &errmsg) != SQLITE_OK) {
      fprintf(stderr, "sql2c: schema: %s\n", errmsg);
      sqlite3_free(errmsg);
      return 1;
   }

   Gen g = {
       .a = &arena,
       .cfg = &cfg,
       .struct_style = parse_style(cfg.struct_style),
       .field_style = parse_style(cfg.field_style),
       .func_style = parse_style(cfg.func_style),
   };
   g.tables = introspect_schema(db, &arena, &g.ntables);

   // Parse + introspect queries.
   char *queries_text = read_file(&arena, cfg.queries);
   int nq = 0;
   Query *qs = parse_queries(&g, queries_text, &nq);
   for (int i = 0; i < nq; i++) introspect_query(&g, db, &qs[i]);

   char *dir = path_dir(&arena, cfg.output);
   const char *base = strrchr(cfg.output, '/');
   base = base ? base + 1 : cfg.output;

   // Emit models.h next to the output header.
   StrBuf models = {0};
   emit_models(&g, &models);
   char models_path[1024];
   snprintf(models_path, sizeof(models_path), "%s/models.h", dir);
   write_file(models_path, models.data, models.len);
   sb_free(&models);

   // Emit the query header (<output>) and implementation (<output>.c).
   StrBuf header = {0};
   emit_header(&g, &header, qs, nq, cfg.output);
   write_file(cfg.output, header.data, header.len);
   sb_free(&header);

   // Implementation path: output with extension replaced by .c.
   char impl_path[1024];
   snprintf(impl_path, sizeof(impl_path), "%s", cfg.output);
   char *dot = strrchr(impl_path, '.');
   char *slash = strrchr(impl_path, '/');
   if (dot && (!slash || dot > slash)) strcpy(dot, ".c");
   else strncat(impl_path, ".c", sizeof(impl_path) - strlen(impl_path) - 1);

   StrBuf impl = {0};
   emit_impl(&g, &impl, qs, nq, base);
   write_file(impl_path, impl.data, impl.len);
   sb_free(&impl);

   sqlite3_close(db);
   arena_free(&arena);
   return 0;
}
