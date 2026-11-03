// csql2go — generates a cgo-based Go package from a sql2c manifest.json.
//
// Standalone like sql2c/csql2lua: depends only on its own manifest.json
// input and, at build time, on the sql2c-generated queries.h/.c it wraps
// (linked in via the generated file's own "#cgo" pragmas — no external
// build tool is required to build the *result*, unlike the Lua module).
//
// Design notes:
//   - Each :one/:many query binds to sql2c's <func>_cb primitive rather than
//     the owning wrapper, exactly like csql2lua: there's no need to replay
//     sql2c's sql_allocator/deep-copy machinery here either. Instead, one
//     //export'd Go trampoline per distinct result struct type decodes the
//     borrowed row (via C.GoStringN/C.GoBytes, copying immediately) and
//     appends it to a Go slice reached through a runtime/cgo.Handle passed
//     as the callback's void *ctx.
//   - The Row* passed into a _cb callback aliases sqlite3_column_text/_blob
//     buffers owned by the statement — valid only for the duration of that
//     one callback call. Every decode copies data out immediately and never
//     retains the raw C pointer.
//   - Query methods hang off a *DB (Open(path) -> *DB; db.GetBoat(id);
//     db.Close()), mirroring csql2lua's connection-owning design. A closed
//     DB returns ErrClosed rather than dereferencing a stale handle.
//   - Go struct/method/field identifiers are derived from the manifest's
//     *raw* names (column.name, query.name) via csql2go's own snake_case ->
//     Pascal/camelCase conversion, not from sql2c's already-styled
//     field/func strings — this keeps the Go API idiomatic regardless of
//     whatever field-style/func-style the consumer's sql2c config uses.
//   - Assumes queries that share a result_type produce an identical column
//     shape (true for sql2c's own generation model: the same table querying
//     "select *"/"returning *" always yields the same projected columns).

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Arena — block-backed bump allocator. Freed all at once at exit.
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
      fprintf(stderr, "csql2go: out of memory\n");
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
// StrBuf — growable output buffer.
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
      fprintf(stderr, "csql2go: out of memory\n");
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

static void sb_putc(StrBuf *sb, char c) {
   sb_reserve(sb, 1);
   sb->data[sb->len++] = c;
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
      fprintf(stderr, "csql2go: cannot open %s\n", path);
      exit(1);
   }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *buf = arena_alloc(a, (size_t)n + 1);
   if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
      fprintf(stderr, "csql2go: short read on %s\n", path);
      exit(1);
   }
   buf[n] = 0;
   fclose(f);
   return buf;
}

static void write_file(const char *path, const char *data, size_t len) {
   FILE *f = fopen(path, "wb");
   if (!f) {
      fprintf(stderr, "csql2go: cannot write %s\n", path);
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
   const char *manifest;
   const char *package;
   const char *queries_header;
   const char *output;
   const char *cgo_cflags;  // optional passthrough, embedded as "#cgo CFLAGS: ..."
   const char *cgo_ldflags; // optional passthrough, embedded as "#cgo LDFLAGS: ..."
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
   if (strcmp(key, "manifest") == 0) c->manifest = v;
   else if (strcmp(key, "package") == 0) c->package = v;
   else if (strcmp(key, "queries_header") == 0) c->queries_header = v;
   else if (strcmp(key, "output") == 0) c->output = v;
   else if (strcmp(key, "cgo_cflags") == 0) c->cgo_cflags = v;
   else if (strcmp(key, "cgo_ldflags") == 0) c->cgo_ldflags = v;
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
         fprintf(stderr, "csql2go: %s:%d: expected key = value\n", path, lineno);
         exit(1);
      }
      *eq = 0;
      char *key = str_trim(t);
      char *value = str_trim(eq + 1);
      if (!config_set(c, key, value, a)) {
         fprintf(stderr, "csql2go: %s:%d: unknown key '%s'\n", path, lineno, key);
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
// JSON — minimal reader tailored to sql2c's manifest.json shape (objects,
// arrays, strings with the usual escapes, true/false/null). Not a general
// JSON library: we only ever read what sql2c itself wrote.
// ============================================================================

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonType;
typedef struct JsonValue JsonValue;

typedef struct {
   char *key;
   JsonValue *val;
} JsonMember;

struct JsonValue {
   JsonType type;
   int b;
   char *s;
   JsonValue **items;
   int nitems;
   JsonMember *members;
   int nmembers;
};

typedef struct {
   const char *p;
   const char *path;
   Arena *a;
} JsonParser;

static void json_error(JsonParser *jp, const char *msg) {
   fprintf(stderr, "csql2go: %s: %s near '%.20s'\n", jp->path, msg, jp->p);
   exit(1);
}

static void json_skip_ws(JsonParser *jp) {
   while (*jp->p == ' ' || *jp->p == '\t' || *jp->p == '\n' || *jp->p == '\r') jp->p++;
}

static JsonValue *json_alloc(JsonParser *jp, JsonType t) {
   JsonValue *v = arena_alloc(jp->a, sizeof(JsonValue));
   v->type = t;
   return v;
}

static void json_utf8_append(StrBuf *sb, unsigned int cp) {
   if (cp < 0x80) {
      sb_putc(sb, (char)cp);
   } else if (cp < 0x800) {
      sb_putc(sb, (char)(0xC0 | (cp >> 6)));
      sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
   } else {
      sb_putc(sb, (char)(0xE0 | (cp >> 12)));
      sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
      sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
   }
}

static char *json_parse_string_raw(JsonParser *jp) {
   jp->p++; // opening quote
   StrBuf sb = {0};
   while (*jp->p && *jp->p != '"') {
      char c = *jp->p;
      if (c == '\\') {
         jp->p++;
         switch (*jp->p) {
            case '"': sb_putc(&sb, '"'); break;
            case '\\': sb_putc(&sb, '\\'); break;
            case '/': sb_putc(&sb, '/'); break;
            case 'n': sb_putc(&sb, '\n'); break;
            case 't': sb_putc(&sb, '\t'); break;
            case 'r': sb_putc(&sb, '\r'); break;
            case 'b': sb_putc(&sb, '\b'); break;
            case 'f': sb_putc(&sb, '\f'); break;
            case 'u': {
               unsigned int cp = 0;
               for (int i = 0; i < 4; i++) {
                  jp->p++;
                  char h = *jp->p;
                  cp <<= 4;
                  if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                  else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                  else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                  else json_error(jp, "bad \\u escape");
               }
               json_utf8_append(&sb, cp);
               break;
            }
            default: json_error(jp, "bad escape");
         }
         jp->p++;
      } else {
         sb_putc(&sb, c);
         jp->p++;
      }
   }
   if (*jp->p != '"') json_error(jp, "unterminated string");
   jp->p++;
   char *r = arena_strdup(jp->a, sb.data ? sb.data : "");
   sb_free(&sb);
   return r;
}

static JsonValue *json_parse_value(JsonParser *jp) {
   json_skip_ws(jp);
   char c = *jp->p;
   if (c == '"') {
      JsonValue *v = json_alloc(jp, JSON_STR);
      v->s = json_parse_string_raw(jp);
      return v;
   }
   if (c == '{') {
      jp->p++;
      JsonValue *v = json_alloc(jp, JSON_OBJ);
      JsonMember *members = NULL;
      int n = 0, cap = 0;
      json_skip_ws(jp);
      if (*jp->p == '}') { jp->p++; return v; }
      for (;;) {
         json_skip_ws(jp);
         if (*jp->p != '"') json_error(jp, "expected string key");
         char *key = json_parse_string_raw(jp);
         json_skip_ws(jp);
         if (*jp->p != ':') json_error(jp, "expected ':'");
         jp->p++;
         JsonValue *val = json_parse_value(jp);
         if (n == cap) { cap = cap ? cap * 2 : 8; members = realloc(members, (size_t)cap * sizeof(JsonMember)); }
         members[n].key = key;
         members[n].val = val;
         n++;
         json_skip_ws(jp);
         if (*jp->p == ',') { jp->p++; continue; }
         if (*jp->p == '}') { jp->p++; break; }
         json_error(jp, "expected ',' or '}'");
      }
      v->members = arena_alloc(jp->a, (size_t)n * sizeof(JsonMember));
      memcpy(v->members, members, (size_t)n * sizeof(JsonMember));
      free(members);
      v->nmembers = n;
      return v;
   }
   if (c == '[') {
      jp->p++;
      JsonValue *v = json_alloc(jp, JSON_ARR);
      JsonValue **items = NULL;
      int n = 0, cap = 0;
      json_skip_ws(jp);
      if (*jp->p == ']') { jp->p++; return v; }
      for (;;) {
         JsonValue *val = json_parse_value(jp);
         if (n == cap) { cap = cap ? cap * 2 : 8; items = realloc(items, (size_t)cap * sizeof(JsonValue *)); }
         items[n++] = val;
         json_skip_ws(jp);
         if (*jp->p == ',') { jp->p++; continue; }
         if (*jp->p == ']') { jp->p++; break; }
         json_error(jp, "expected ',' or ']'");
      }
      v->items = arena_alloc(jp->a, (size_t)n * sizeof(JsonValue *));
      memcpy(v->items, items, (size_t)n * sizeof(JsonValue *));
      free(items);
      v->nitems = n;
      return v;
   }
   if (strncmp(jp->p, "true", 4) == 0) { jp->p += 4; JsonValue *v = json_alloc(jp, JSON_BOOL); v->b = 1; return v; }
   if (strncmp(jp->p, "false", 5) == 0) { jp->p += 5; return json_alloc(jp, JSON_BOOL); }
   if (strncmp(jp->p, "null", 4) == 0) { jp->p += 4; return json_alloc(jp, JSON_NULL); }
   if (c == '-' || (c >= '0' && c <= '9')) {
      const char *start = jp->p;
      jp->p++;
      while (*jp->p && ((*jp->p >= '0' && *jp->p <= '9') || *jp->p == '.' || *jp->p == 'e' ||
                         *jp->p == 'E' || *jp->p == '+' || *jp->p == '-'))
         jp->p++;
      JsonValue *v = json_alloc(jp, JSON_NUM);
      size_t len = (size_t)(jp->p - start);
      char *buf = arena_alloc(jp->a, len + 1);
      memcpy(buf, start, len);
      v->s = buf;
      return v;
   }
   json_error(jp, "unexpected character");
   return NULL;
}

static JsonValue *json_parse(Arena *a, const char *path, const char *text) {
   JsonParser jp = { text, path, a };
   return json_parse_value(&jp);
}

static JsonValue *jv_get(JsonValue *obj, const char *key) {
   if (!obj || obj->type != JSON_OBJ) return NULL;
   for (int i = 0; i < obj->nmembers; i++)
      if (strcmp(obj->members[i].key, key) == 0) return obj->members[i].val;
   return NULL;
}

static const char *jv_str(JsonValue *v) { return (v && v->type == JSON_STR) ? v->s : NULL; }
static int jv_bool(JsonValue *v) { return v && v->type == JSON_BOOL && v->b; }
static int jv_arr_len(JsonValue *v) { return (v && v->type == JSON_ARR) ? v->nitems : 0; }
static JsonValue *jv_arr_at(JsonValue *v, int i) { return v->items[i]; }

// ============================================================================
// Manifest model — mirrors sql2c's manifest.json shape.
// ============================================================================

typedef struct {
   char *name;
   char *field;
   char *sql_type;
   char *c_type;
   int nullable;
   int pk;
} MColumn;

typedef struct {
   char *table;
   char *type;
   MColumn *cols;
   int ncols;
} Model;

typedef struct {
   char *name;
   char *kind; // "one" | "many" | "exec"
   char *sql;
   char *func;
   char *params_type; // NULL if <= 1 param
   MColumn *params;
   int nparams;
   char *result_type;      // NULL for :exec
   char *result_list_type; // non-NULL only for :many
   MColumn *result;
   int nresult;
} MQuery;

typedef struct {
   Model *models;
   int nmodels;
   MQuery *queries;
   int nqueries;
} Manifest;

static MColumn *mcolumns_from_json(Arena *a, JsonValue *arr, int *out_n) {
   int n = jv_arr_len(arr);
   MColumn *cols = arena_alloc(a, (size_t)n * sizeof(MColumn));
   for (int i = 0; i < n; i++) {
      JsonValue *item = jv_arr_at(arr, i);
      cols[i].name = arena_strdup(a, jv_str(jv_get(item, "name")));
      cols[i].field = arena_strdup(a, jv_str(jv_get(item, "field")));
      cols[i].sql_type = arena_strdup(a, jv_str(jv_get(item, "sql_type")));
      cols[i].c_type = arena_strdup(a, jv_str(jv_get(item, "c_type")));
      cols[i].nullable = jv_bool(jv_get(item, "nullable"));
      cols[i].pk = jv_bool(jv_get(item, "pk"));
   }
   *out_n = n;
   return cols;
}

static Manifest *manifest_load(Arena *a, const char *path) {
   char *text = read_file(a, path);
   JsonValue *root = json_parse(a, path, text);

   Manifest *m = arena_alloc(a, sizeof(Manifest));

   JsonValue *models = jv_get(root, "models");
   m->nmodels = jv_arr_len(models);
   m->models = arena_alloc(a, (size_t)(m->nmodels ? m->nmodels : 1) * sizeof(Model));
   for (int i = 0; i < m->nmodels; i++) {
      JsonValue *item = jv_arr_at(models, i);
      m->models[i].table = arena_strdup(a, jv_str(jv_get(item, "table")));
      m->models[i].type = arena_strdup(a, jv_str(jv_get(item, "type")));
      m->models[i].cols = mcolumns_from_json(a, jv_get(item, "columns"), &m->models[i].ncols);
   }

   JsonValue *queries = jv_get(root, "queries");
   m->nqueries = jv_arr_len(queries);
   m->queries = arena_alloc(a, (size_t)(m->nqueries ? m->nqueries : 1) * sizeof(MQuery));
   for (int i = 0; i < m->nqueries; i++) {
      JsonValue *item = jv_arr_at(queries, i);
      MQuery *q = &m->queries[i];
      q->name = arena_strdup(a, jv_str(jv_get(item, "name")));
      q->kind = arena_strdup(a, jv_str(jv_get(item, "kind")));
      q->sql = arena_strdup(a, jv_str(jv_get(item, "sql")));
      q->func = arena_strdup(a, jv_str(jv_get(item, "func")));
      q->params_type = arena_strdup(a, jv_str(jv_get(item, "params_type")));
      q->params = mcolumns_from_json(a, jv_get(item, "params"), &q->nparams);
      q->result_type = arena_strdup(a, jv_str(jv_get(item, "result_type")));
      q->result_list_type = arena_strdup(a, jv_str(jv_get(item, "result_list_type")));
      JsonValue *result = jv_get(item, "result");
      if (result && result->type == JSON_ARR) q->result = mcolumns_from_json(a, result, &q->nresult);
      else { q->result = NULL; q->nresult = 0; }
   }

   return m;
}

// ============================================================================
// Identifier casing — Go identifiers are derived from the manifest's *raw*
// names (column.name / query param names), not sql2c's already-styled
// field/func strings, so the Go API stays idiomatic regardless of the
// consumer's sql2c field-style/func-style config. "id" is special-cased to
// the initialism "ID" (Pascal) / "id" (camel, first word) / "ID" (camel,
// non-first word), matching common Go style.
// ============================================================================

static char *go_case(Arena *a, const char *s, int camel) {
   StrBuf sb = {0};
   const char *p = s;
   int first_word = 1;
   while (*p) {
      const char *start = p;
      while (*p && *p != '_') p++;
      size_t wlen = (size_t)(p - start);
      if (wlen > 0) {
         char word[128];
         if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
         memcpy(word, start, wlen);
         word[wlen] = 0;
         char lower[128];
         size_t i;
         for (i = 0; i < wlen; i++) lower[i] = (char)tolower((unsigned char)word[i]);
         lower[wlen] = 0;

         if (strcmp(lower, "id") == 0) {
            sb_puts(&sb, (camel && first_word) ? "id" : "ID");
         } else {
            char out[128];
            out[0] = (char)((camel && first_word) ? tolower((unsigned char)word[0]) : toupper((unsigned char)word[0]));
            for (i = 1; i < wlen; i++) out[i] = (char)tolower((unsigned char)word[i]);
            out[wlen] = 0;
            sb_puts(&sb, out);
         }
         first_word = 0;
      }
      if (*p == '_') p++;
   }
   char *r = arena_strdup(a, sb.data ? sb.data : "");
   sb_free(&sb);
   return r;
}

static char *go_pascal(Arena *a, const char *s) { return go_case(a, s, 0); }
static char *go_camel(Arena *a, const char *s) { return go_case(a, s, 1); }

// ============================================================================
// c_type -> Go type mapping
// ============================================================================

// Go struct field type (nullable c_types become pointers, except blob which
// stays []byte with nil representing NULL).
static const char *go_field_type(const char *c_type) {
   if (strcmp(c_type, "sql_int64") == 0) return "int64";
   if (strcmp(c_type, "sql_nullint64") == 0) return "*int64";
   if (strcmp(c_type, "sql_int") == 0) return "int32";
   if (strcmp(c_type, "sql_nullint") == 0) return "*int32";
   if (strcmp(c_type, "sql_double") == 0) return "float64";
   if (strcmp(c_type, "sql_nulldouble") == 0) return "*float64";
   if (strcmp(c_type, "sql_numeric") == 0) return "float64";
   if (strcmp(c_type, "sql_nullnumeric") == 0) return "*float64";
   if (strcmp(c_type, "sql_bool") == 0) return "bool";
   if (strcmp(c_type, "sql_nullbool") == 0) return "*bool";
   if (strcmp(c_type, "sql_text") == 0) return "string";
   if (strcmp(c_type, "sql_nulltext") == 0) return "*string";
   if (strcmp(c_type, "sql_blob") == 0) return "[]byte";
   if (strcmp(c_type, "sql_nullblob") == 0) return "[]byte";
   fprintf(stderr, "csql2go: unknown c_type '%s'\n", c_type);
   exit(1);
}

// Go function-argument type for a bound parameter. sql2c never types a bind
// parameter as nullable, so only base types are handled (mirrors
// csql2lua's emit_bind_param).
static const char *go_param_type(const char *c_type, const char *name) {
   if (strcmp(c_type, "sql_int64") == 0) return "int64";
   if (strcmp(c_type, "sql_int") == 0) return "int32";
   if (strcmp(c_type, "sql_double") == 0 || strcmp(c_type, "sql_numeric") == 0) return "float64";
   if (strcmp(c_type, "sql_bool") == 0) return "bool";
   if (strcmp(c_type, "sql_text") == 0) return "string";
   if (strcmp(c_type, "sql_blob") == 0) return "[]byte";
   fprintf(stderr, "csql2go: param '%s' has unsupported type '%s'\n", name, c_type);
   exit(1);
}

// ============================================================================
// Go source emit
// ============================================================================

// Decode a C row field (row.<c_field>) into a Go struct field (out.<go_field>).
static void emit_decode_field(StrBuf *sb, const char *c_type, const char *c_field, const char *go_field) {
   char e[300];
   snprintf(e, sizeof(e), "row.%s", c_field);

   if (strcmp(c_type, "sql_int64") == 0) {
      sb_printf(sb, "\tout.%s = int64(%s)\n", go_field, e);
   } else if (strcmp(c_type, "sql_int") == 0) {
      sb_printf(sb, "\tout.%s = int32(%s)\n", go_field, e);
   } else if (strcmp(c_type, "sql_double") == 0 || strcmp(c_type, "sql_numeric") == 0) {
      sb_printf(sb, "\tout.%s = float64(%s)\n", go_field, e);
   } else if (strcmp(c_type, "sql_bool") == 0) {
      sb_printf(sb, "\tout.%s = bool(%s)\n", go_field, e);
   } else if (strcmp(c_type, "sql_text") == 0) {
      sb_printf(sb, "\tout.%s = C.GoStringN((*C.char)(unsafe.Pointer(%s.data)), C.int(%s.len))\n", go_field, e, e);
   } else if (strcmp(c_type, "sql_blob") == 0) {
      sb_printf(sb, "\tout.%s = C.GoBytes(unsafe.Pointer(%s.data), C.int(%s.len))\n", go_field, e, e);
   } else if (strcmp(c_type, "sql_nullint64") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tv := int64(%s.value)\n\t\tout.%s = &v\n\t}\n", e, e, go_field);
   } else if (strcmp(c_type, "sql_nullint") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tv := int32(%s.value)\n\t\tout.%s = &v\n\t}\n", e, e, go_field);
   } else if (strcmp(c_type, "sql_nulldouble") == 0 || strcmp(c_type, "sql_nullnumeric") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tv := float64(%s.value)\n\t\tout.%s = &v\n\t}\n", e, e, go_field);
   } else if (strcmp(c_type, "sql_nullbool") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tv := bool(%s.value)\n\t\tout.%s = &v\n\t}\n", e, e, go_field);
   } else if (strcmp(c_type, "sql_nulltext") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tv := C.GoStringN(%s.data, C.int(%s.len))\n\t\tout.%s = &v\n\t}\n", e, e, e, go_field);
   } else if (strcmp(c_type, "sql_nullblob") == 0) {
      sb_printf(sb, "\tif !%s.null {\n\t\tout.%s = C.GoBytes(unsafe.Pointer(%s.data), C.int(%s.len))\n\t}\n", e, go_field, e, e);
   } else {
      fprintf(stderr, "csql2go: unknown c_type '%s'\n", c_type);
      exit(1);
   }
}

// Emit the temp-variable statements that build a C-side value for one bound
// parameter, and return the Go expression to pass as the call argument
// (e.g. "cId" or "&cparams"'s field is set in place). `go_src` is the Go
// expression holding the source value (a bare arg name, or "params.Field").
// `dest` is where the C value should end up: a fresh local var name.
static void emit_bind_param(StrBuf *sb, const char *c_type, const char *go_src, const char *dest, const char *tmp_prefix) {
   if (strcmp(c_type, "sql_int64") == 0) {
      sb_printf(sb, "\t%s := C.sql_int64(%s)\n", dest, go_src);
   } else if (strcmp(c_type, "sql_int") == 0) {
      sb_printf(sb, "\t%s := C.sql_int(%s)\n", dest, go_src);
   } else if (strcmp(c_type, "sql_double") == 0 || strcmp(c_type, "sql_numeric") == 0) {
      sb_printf(sb, "\t%s := C.sql_double(%s)\n", dest, go_src);
   } else if (strcmp(c_type, "sql_bool") == 0) {
      sb_printf(sb, "\t%s := C.sql_bool(%s)\n", dest, go_src);
   } else if (strcmp(c_type, "sql_text") == 0) {
      sb_printf(sb, "\t%sCstr := C.CString(%s)\n", tmp_prefix, go_src);
      sb_printf(sb, "\tdefer C.free(unsafe.Pointer(%sCstr))\n", tmp_prefix);
      sb_printf(sb, "\t%s := C.sql_text{data: (*C.sql_byte)(unsafe.Pointer(%sCstr)), len: C.size_t(len(%s))}\n", dest, tmp_prefix, go_src);
   } else if (strcmp(c_type, "sql_blob") == 0) {
      sb_printf(sb, "\t%sCptr := C.CBytes(%s)\n", tmp_prefix, go_src);
      sb_printf(sb, "\tdefer C.free(%sCptr)\n", tmp_prefix);
      sb_printf(sb, "\t%s := C.sql_blob{data: (*C.sql_byte)(%sCptr), len: C.size_t(len(%s))}\n", dest, tmp_prefix, go_src);
   } else {
      fprintf(stderr, "csql2go: unsupported param c_type '%s'\n", c_type);
      exit(1);
   }
}

// Go function-signature args + return type helper text for a query.
static void emit_func_args(StrBuf *sb, MQuery *q, Arena *a) {
   if (q->nparams <= 1) {
      for (int i = 0; i < q->nparams; i++) {
         char *arg = go_camel(a, q->params[i].name);
         sb_printf(sb, "%s %s", arg, go_param_type(q->params[i].c_type, q->params[i].name));
      }
   } else {
      sb_printf(sb, "params %s", q->params_type);
   }
}

// Emit the C-side call-argument binding for a query's params, and append
// the resulting call arguments (each prefixed with ", ") to `callargs`.
static void emit_param_binding(StrBuf *sb, StrBuf *callargs, MQuery *q, Arena *a) {
   if (q->nparams == 0) return;
   if (q->nparams == 1) {
      char *arg = go_camel(a, q->params[0].name);
      char dest[64];
      snprintf(dest, sizeof(dest), "c%s", go_pascal(a, q->params[0].name));
      emit_bind_param(sb, q->params[0].c_type, arg, dest, arg);
      sb_printf(callargs, ", %s", dest);
      return;
   }
   sb_printf(sb, "\tvar cparams C.%s\n", q->params_type);
   for (int i = 0; i < q->nparams; i++) {
      char src[300];
      snprintf(src, sizeof(src), "params.%s", go_pascal(a, q->params[i].name));
      char dest[64];
      snprintf(dest, sizeof(dest), "f%d", i);
      char tmp[64];
      snprintf(tmp, sizeof(tmp), "p%d", i);
      emit_bind_param(sb, q->params[i].c_type, src, dest, tmp);
      sb_printf(sb, "\tcparams.%s = %s\n", q->params[i].field, dest);
   }
   sb_puts(callargs, ", &cparams");
}

// One //export trampoline per distinct result struct type, shared by every
// :one/:many query that returns it.
static void emit_trampoline(StrBuf *sb, const char *pkg, const char *type, MColumn *cols, int ncols, Arena *a) {
   sb_printf(sb, "//export %s_%s_cb\n", pkg, type);
   sb_printf(sb, "func %s_%s_cb(row *C.%s, ctx unsafe.Pointer) {\n", pkg, type, type);
   sb_puts(sb, "\th := cgo.Handle(*(*C.uintptr_t)(ctx))\n");
   sb_printf(sb, "\tacc := h.Value().(*[]%s)\n", type);
   sb_printf(sb, "\tvar out %s\n", type);
   for (int i = 0; i < ncols; i++) {
      char *gofield = go_pascal(a, cols[i].name);
      emit_decode_field(sb, cols[i].c_type, cols[i].field, gofield);
   }
   sb_puts(sb, "\t*acc = append(*acc, out)\n");
   sb_puts(sb, "}\n\n");
}

static void emit_model_struct(StrBuf *sb, const char *type, MColumn *cols, int ncols, Arena *a) {
   sb_printf(sb, "type %s struct {\n", type);
   for (int i = 0; i < ncols; i++) {
      sb_printf(sb, "\t%s %s\n", go_pascal(a, cols[i].name), go_field_type(cols[i].c_type));
   }
   sb_puts(sb, "}\n\n");
}

// SQL text from the manifest can span multiple lines; prefix every line
// with "// " so it stays a valid Go comment instead of spilling raw SQL
// into the source.
static void emit_sql_comment(StrBuf *sb, const char *sql) {
   sb_puts(sb, "// ");
   for (const char *p = sql; *p; p++) {
      sb_putc(sb, *p);
      if (*p == '\n') sb_puts(sb, "// ");
   }
   sb_putc(sb, '\n');
}

static void emit_query_method(StrBuf *sb, const char *pkg, MQuery *q, Arena *a) {
   int is_one = strcmp(q->kind, "one") == 0;
   int is_many = strcmp(q->kind, "many") == 0;

   emit_sql_comment(sb, q->sql);
   if (!is_one && !is_many) {
      // :exec — no callback, no result.
      sb_printf(sb, "func (d *DB) %s(", q->name);
      emit_func_args(sb, q, a);
      sb_puts(sb, ") error {\n");
      sb_puts(sb, "\tif d.closed {\n\t\treturn ErrClosed\n\t}\n");
      StrBuf callargs = {0};
      emit_param_binding(sb, &callargs, q, a);
      sb_printf(sb, "\trc := C.%s(d.db%s)\n", q->func, callargs.data ? callargs.data : "");
      sb_free(&callargs);
      sb_puts(sb, "\tif rc != C.SQLITE_OK {\n\t\treturn d.err()\n\t}\n");
      sb_puts(sb, "\treturn nil\n");
      sb_puts(sb, "}\n\n");
      return;
   }

   sb_printf(sb, "func (d *DB) %s(", q->name);
   emit_func_args(sb, q, a);
   if (is_one) sb_printf(sb, ") (*%s, error) {\n", q->result_type);
   else sb_printf(sb, ") ([]%s, error) {\n", q->result_type);

   sb_printf(sb, "\tif d.closed {\n\t\treturn nil, ErrClosed\n\t}\n");
   StrBuf callargs = {0};
   emit_param_binding(sb, &callargs, q, a);
   sb_printf(sb, "\tvar acc []%s\n", q->result_type);
   sb_puts(sb, "\th := cgo.NewHandle(&acc)\n");
   sb_puts(sb, "\tdefer h.Delete()\n");
   // ctx must be a genuine pointer to Go memory holding no Go pointers (a
   // cgo.Handle is just a uintptr key, not a real pointer, so it can't be
   // cast to unsafe.Pointer directly — that fails checkptr under -race).
   // Passing the address of a plain C.uintptr_t local satisfies cgo's
   // pointer-passing rule instead.
   sb_puts(sb, "\thctx := C.uintptr_t(h)\n");
   sb_printf(sb, "\trc := C.%s_cb(d.db%s, (*[0]byte)(C.%s_%s_cb), unsafe.Pointer(&hctx))\n",
             q->func, callargs.data ? callargs.data : "", pkg, q->result_type);
   sb_free(&callargs);
   if (is_one) {
      sb_puts(sb, "\tif rc == C.SQLITE_NOTFOUND {\n\t\treturn nil, nil\n\t}\n");
      sb_puts(sb, "\tif rc != C.SQLITE_OK {\n\t\treturn nil, d.err()\n\t}\n");
      sb_puts(sb, "\treturn &acc[0], nil\n");
   } else {
      sb_puts(sb, "\tif rc != C.SQLITE_OK {\n\t\treturn nil, d.err()\n\t}\n");
      sb_puts(sb, "\treturn acc, nil\n");
   }
   sb_puts(sb, "}\n\n");
}

// Has `name` already been recorded in `seen`? If not, appends it and
// returns 0 (first sighting); returns 1 if it was already present.
static int seen_mark(const char **seen, int *nseen, const char *name) {
   for (int i = 0; i < *nseen; i++)
      if (strcmp(seen[i], name) == 0) return 1;
   seen[(*nseen)++] = name;
   return 0;
}

static void emit_package(StrBuf *sb, Manifest *m, const Config *cfg, Arena *a) {
   // Distinct result types needing a trampoline (one per :one/:many result
   // type). Computed up front: cgo can't resolve C.<name> for a same-file
   // //export'd function unless it's forward-declared as `extern` in the
   // preamble first (the auto-generated _cgo_export.h isn't available to
   // the preamble of the file that defines it).
   const char *tramp_types[512];
   int ntramp = 0;
   for (int i = 0; i < m->nqueries; i++) {
      MQuery *q = &m->queries[i];
      int is_one = strcmp(q->kind, "one") == 0;
      int is_many = strcmp(q->kind, "many") == 0;
      if (!is_one && !is_many) continue;
      int dup = 0;
      for (int j = 0; j < ntramp; j++)
         if (strcmp(tramp_types[j], q->result_type) == 0) { dup = 1; break; }
      if (!dup) tramp_types[ntramp++] = q->result_type;
   }

   sb_puts(sb, "// Generated by csql2go - do not edit\n\n");
   sb_printf(sb, "package %s\n\n", cfg->package);

   sb_puts(sb, "/*\n");
   if (cfg->cgo_cflags && cfg->cgo_cflags[0]) sb_printf(sb, "#cgo CFLAGS: %s\n", cfg->cgo_cflags);
   if (cfg->cgo_ldflags && cfg->cgo_ldflags[0]) sb_printf(sb, "#cgo LDFLAGS: %s\n", cfg->cgo_ldflags);
   // Only a declarations-only #include belongs in the preamble: cgo can
   // compile this comment block into more than one translation unit (it
   // duplicates it into a _cgo_export.c alongside the main one whenever the
   // file has //export functions, which every csql2go output does for its
   // row trampolines). #include-ing queries.c's *definitions* here would
   // get them compiled twice and fail to link with duplicate symbols.
   // Instead, place (or symlink) queries.h/queries.c inside this package's
   // own directory — cgo automatically compiles any .c file it finds there
   // exactly once, no matter how many Go files import "C".
   sb_puts(sb, "#include <stdlib.h>\n"); // for C.free / C.CString / C.CBytes
   sb_printf(sb, "#include \"%s\"\n", cfg->queries_header);
   for (int i = 0; i < ntramp; i++) {
      sb_printf(sb, "extern void %s_%s_cb(%s *row, void *ctx);\n", cfg->package, tramp_types[i], tramp_types[i]);
   }
   sb_puts(sb, "*/\n");
   sb_puts(sb, "import \"C\"\n\n");

   sb_puts(sb, "import (\n");
   sb_puts(sb, "\t\"errors\"\n");
   sb_puts(sb, "\t\"fmt\"\n");
   sb_puts(sb, "\t\"runtime/cgo\"\n");
   sb_puts(sb, "\t\"unsafe\"\n");
   sb_puts(sb, ")\n\n");

   sb_printf(sb, "var ErrClosed = errors.New(\"%s: use of closed database connection\")\n\n", cfg->package);

   // ---- model / params structs, deduped by type name (first sighting wins;
   // sql2c gives every query sharing a result_type/params_type the same
   // column shape). ----
   const char *seen[512];
   int nseen = 0;

   for (int i = 0; i < m->nqueries; i++) {
      MQuery *q = &m->queries[i];
      if (q->result_type && q->result_type[0] && !seen_mark(seen, &nseen, q->result_type)) {
         emit_model_struct(sb, q->result_type, q->result, q->nresult, a);
      }
   }
   for (int i = 0; i < m->nqueries; i++) {
      MQuery *q = &m->queries[i];
      if (q->params_type && q->params_type[0] && !seen_mark(seen, &nseen, q->params_type)) {
         emit_model_struct(sb, q->params_type, q->params, q->nparams, a);
      }
   }

   // ---- connection ----
   sb_puts(sb, "type DB struct {\n\tdb     *C.sqlite3\n\tclosed bool\n}\n\n");

   sb_puts(sb, "func Open(path string) (*DB, error) {\n");
   sb_puts(sb, "\tcpath := C.CString(path)\n");
   sb_puts(sb, "\tdefer C.free(unsafe.Pointer(cpath))\n");
   sb_puts(sb, "\tvar db *C.sqlite3\n");
   sb_puts(sb, "\trc := C.sqlite3_open(cpath, &db)\n");
   sb_puts(sb, "\tif rc != C.SQLITE_OK {\n");
   sb_puts(sb, "\t\tmsg := \"cannot open database\"\n");
   sb_puts(sb, "\t\tif db != nil {\n");
   sb_puts(sb, "\t\t\tmsg = C.GoString(C.sqlite3_errmsg(db))\n");
   sb_puts(sb, "\t\t\tC.sqlite3_close(db)\n");
   sb_puts(sb, "\t\t}\n");
   sb_printf(sb, "\t\treturn nil, fmt.Errorf(\"%s: open: %%s\", msg)\n", cfg->package);
   sb_puts(sb, "\t}\n");
   sb_puts(sb, "\treturn &DB{db: db}, nil\n");
   sb_puts(sb, "}\n\n");

   sb_puts(sb, "func (d *DB) Close() error {\n");
   sb_puts(sb, "\tif d.closed {\n\t\treturn nil\n\t}\n");
   sb_puts(sb, "\trc := C.sqlite3_close(d.db)\n");
   sb_puts(sb, "\td.closed = true\n");
   sb_puts(sb, "\tif rc != C.SQLITE_OK {\n");
   sb_printf(sb, "\t\treturn fmt.Errorf(\"%s: close: %%s\", C.GoString(C.sqlite3_errmsg(d.db)))\n", cfg->package);
   sb_puts(sb, "\t}\n\treturn nil\n");
   sb_puts(sb, "}\n\n");

   sb_puts(sb, "func (d *DB) err() error {\n");
   sb_printf(sb, "\treturn fmt.Errorf(\"%s: %%s\", C.GoString(C.sqlite3_errmsg(d.db)))\n", cfg->package);
   sb_puts(sb, "}\n\n");

   // ---- trampolines, one per distinct result_type used by :one/:many ----
   for (int i = 0; i < ntramp; i++) {
      for (int j = 0; j < m->nqueries; j++) {
         MQuery *q = &m->queries[j];
         if (strcmp(q->result_type ? q->result_type : "", tramp_types[i]) != 0) continue;
         emit_trampoline(sb, cfg->package, q->result_type, q->result, q->nresult, a);
         break;
      }
   }

   // ---- query methods ----
   for (int i = 0; i < m->nqueries; i++) emit_query_method(sb, cfg->package, &m->queries[i], a);
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
   Arena arena = {0};

   Config cfg = {
       .manifest = "manifest.json",
       .package = "queries",
       .queries_header = "queries.h",
       .output = "",
       .cgo_cflags = "",
       .cgo_ldflags = "",
   };

   const char *config_path = args_config_path(argc, argv);
   if (config_path) config_load_file(&cfg, config_path, &arena);
   args_apply_overrides(&cfg, argc, argv, &arena);

   if (!cfg.output[0]) {
      StrBuf ob = {0};
      sb_printf(&ob, "%s.go", cfg.package);
      cfg.output = arena_strdup(&arena, ob.data);
      sb_free(&ob);
   }

   Manifest *m = manifest_load(&arena, cfg.manifest);

   StrBuf out = {0};
   emit_package(&out, m, &cfg, &arena);
   write_file(cfg.output, out.data, out.len);
   sb_free(&out);

   arena_free(&arena);
   return 0;
}
