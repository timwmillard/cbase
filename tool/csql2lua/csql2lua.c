// csql2lua — generates a traditional Lua C module (luaopen_*, lua_CFunction
// wrappers) from a sql2c manifest.json.
//
// Standalone like sql2c: depends only on its own manifest.json input and, at
// build time, on Lua's headers (lua.h/lauxlib.h/lualib.h) and sqlite3.h/the
// sql2c-generated queries.h it wraps.
//
// Design notes:
//   - Each :one/:many query binds to sql2c's <func>_cb primitive rather than
//     the owning wrapper: Lua's C API copies strings the moment they're
//     pushed (lua_pushlstring), so there's no need to replay sql2c's
//     sql_allocator/deep-copy machinery here — the callback pushes straight
//     into a Lua table while the borrowed row is still valid.
//   - Query methods hang off a userdata "connection" object (module.open(path)
//     -> conn; conn:get_boat(id); conn:close()), not bare functions taking a
//     raw db handle — this tool owns the connection's Lua-side lifecycle.

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
      fprintf(stderr, "csql2lua: out of memory\n");
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
      fprintf(stderr, "csql2lua: out of memory\n");
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
      fprintf(stderr, "csql2lua: cannot open %s\n", path);
      exit(1);
   }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *buf = arena_alloc(a, (size_t)n + 1);
   if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
      fprintf(stderr, "csql2lua: short read on %s\n", path);
      exit(1);
   }
   buf[n] = 0;
   fclose(f);
   return buf;
}

static void write_file(const char *path, const char *data, size_t len) {
   FILE *f = fopen(path, "wb");
   if (!f) {
      fprintf(stderr, "csql2lua: cannot write %s\n", path);
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
   const char *module;
   const char *queries_header;
   const char *output;
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
   else if (strcmp(key, "module") == 0) c->module = v;
   else if (strcmp(key, "queries_header") == 0) c->queries_header = v;
   else if (strcmp(key, "output") == 0) c->output = v;
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
         fprintf(stderr, "csql2lua: %s:%d: expected key = value\n", path, lineno);
         exit(1);
      }
      *eq = 0;
      char *key = str_trim(t);
      char *value = str_trim(eq + 1);
      if (!config_set(c, key, value, a)) {
         fprintf(stderr, "csql2lua: %s:%d: unknown key '%s'\n", path, lineno, key);
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
   fprintf(stderr, "csql2lua: %s: %s near '%.20s'\n", jp->path, msg, jp->p);
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
// Lua module emit
// ============================================================================

// Push a C struct field (row->field / dst.field) onto the Lua stack.
static void emit_push_field(StrBuf *sb, const char *c_type, const char *access, const char *field) {
   char e[256];
   snprintf(e, sizeof(e), "%s%s", access, field);

   if (strcmp(c_type, "sql_int64") == 0 || strcmp(c_type, "sql_int") == 0) {
      sb_printf(sb, "    lua_pushinteger(L, (lua_Integer)%s);\n", e);
   } else if (strcmp(c_type, "sql_double") == 0 || strcmp(c_type, "sql_numeric") == 0) {
      sb_printf(sb, "    lua_pushnumber(L, (lua_Number)%s);\n", e);
   } else if (strcmp(c_type, "sql_bool") == 0) {
      sb_printf(sb, "    lua_pushboolean(L, %s);\n", e);
   } else if (strcmp(c_type, "sql_text") == 0 || strcmp(c_type, "sql_blob") == 0) {
      sb_printf(sb, "    lua_pushlstring(L, (const char*)%s.data, %s.len);\n", e, e);
   } else if (strcmp(c_type, "sql_nullint64") == 0 || strcmp(c_type, "sql_nullint") == 0) {
      sb_printf(sb, "    if (%s.null) lua_pushnil(L); else lua_pushinteger(L, (lua_Integer)%s.value);\n", e, e);
   } else if (strcmp(c_type, "sql_nulldouble") == 0 || strcmp(c_type, "sql_nullnumeric") == 0) {
      sb_printf(sb, "    if (%s.null) lua_pushnil(L); else lua_pushnumber(L, (lua_Number)%s.value);\n", e, e);
   } else if (strcmp(c_type, "sql_nullbool") == 0) {
      sb_printf(sb, "    if (%s.null) lua_pushnil(L); else lua_pushboolean(L, %s.value);\n", e, e);
   } else if (strcmp(c_type, "sql_nulltext") == 0 || strcmp(c_type, "sql_nullblob") == 0) {
      sb_printf(sb, "    if (%s.null) lua_pushnil(L); else lua_pushlstring(L, (const char*)%s.data, %s.len);\n", e, e, e);
   } else {
      fprintf(stderr, "csql2lua: unknown c_type '%s'\n", c_type);
      exit(1);
   }
}

// Bind a Lua argument at stack index argi into dest (a plain local or
// "params.field"). sql2c never types a bind parameter as nullable, so only
// the base (non-null) sql_* types need handling here.
static void emit_bind_param(StrBuf *sb, const char *c_type, const char *dest, int argi, const char *name) {
   if (strcmp(c_type, "sql_int64") == 0) {
      sb_printf(sb, "    %s = (sql_int64)luaL_checkinteger(L, %d);\n", dest, argi);
   } else if (strcmp(c_type, "sql_int") == 0) {
      sb_printf(sb, "    %s = (sql_int)luaL_checkinteger(L, %d);\n", dest, argi);
   } else if (strcmp(c_type, "sql_double") == 0 || strcmp(c_type, "sql_numeric") == 0) {
      sb_printf(sb, "    %s = (sql_double)luaL_checknumber(L, %d);\n", dest, argi);
   } else if (strcmp(c_type, "sql_bool") == 0) {
      sb_printf(sb, "    %s = lua_toboolean(L, %d) ? true : false;\n", dest, argi);
   } else if (strcmp(c_type, "sql_text") == 0 || strcmp(c_type, "sql_blob") == 0) {
      sb_printf(sb, "    { size_t n; const char *s = luaL_checklstring(L, %d, &n); %s.data = (sql_byte*)s; %s.len = n; }\n",
                argi, dest, dest);
   } else {
      fprintf(stderr, "csql2lua: param '%s' has unsupported type '%s'\n", name, c_type);
      exit(1);
   }
}

// Trailing ", <arg>" / ", &params" to append to a call to <func>/<func>_cb,
// matching how the params were declared/bound above.
static void emit_param_call(StrBuf *sb, MQuery *q) {
   if (q->nparams == 0) return;
   if (q->nparams == 1) sb_printf(sb, ", %s", q->params[0].field);
   else sb_puts(sb, ", &params");
}

static void emit_query_function(StrBuf *sb, const char *mod, MQuery *q) {
   int is_one = strcmp(q->kind, "one") == 0;
   int is_many = strcmp(q->kind, "many") == 0;

   char lfn[256];
   snprintf(lfn, sizeof(lfn), "%s_l_%s", mod, q->func);

   sb_printf(sb, "// %s :%s\n", q->name, q->kind);

   if (!is_one && !is_many) {
      // :exec — no callback, no result.
      sb_printf(sb, "static int %s(lua_State *L) {\n", lfn);
      sb_printf(sb, "    %s_Handle *h = %s_checkhandle(L, 1);\n", mod, mod);
      if (q->nparams == 1) {
         sb_printf(sb, "    %s %s;\n", q->params[0].c_type, q->params[0].field);
         emit_bind_param(sb, q->params[0].c_type, q->params[0].field, 2, q->params[0].name);
      } else if (q->nparams > 1) {
         sb_printf(sb, "    %s params;\n", q->params_type);
         for (int i = 0; i < q->nparams; i++) {
            char dest[300];
            snprintf(dest, sizeof(dest), "params.%s", q->params[i].field);
            emit_bind_param(sb, q->params[i].c_type, dest, 2 + i, q->params[i].name);
         }
      }
      sb_printf(sb, "    int rc = %s(h->db", q->func);
      emit_param_call(sb, q);
      sb_puts(sb, ");\n");
      sb_puts(sb, "    if (rc != SQLITE_OK) return luaL_error(L, \"%s\", sqlite3_errmsg(h->db));\n");
      sb_puts(sb, "    lua_pushboolean(L, 1);\n");
      sb_puts(sb, "    return 1;\n");
      sb_puts(sb, "}\n\n");
      return;
   }

   char collect[300];
   snprintf(collect, sizeof(collect), "%s_collect", lfn);

   sb_printf(sb, "typedef struct { lua_State *L; int idx; } %s_ctx;\n", lfn);
   sb_printf(sb, "static void %s(%s *row, void *vctx) {\n", collect, q->result_type);
   sb_printf(sb, "    %s_ctx *c = (%s_ctx *)vctx;\n", lfn, lfn);
   sb_puts(sb, "    lua_State *L = c->L;\n");
   sb_puts(sb, "    lua_newtable(L);\n");
   for (int i = 0; i < q->nresult; i++) {
      MColumn *col = &q->result[i];
      emit_push_field(sb, col->c_type, "row->", col->field);
      sb_printf(sb, "    lua_setfield(L, -2, \"%s\");\n", col->field);
   }
   if (is_many) sb_puts(sb, "    lua_rawseti(L, -2, ++c->idx);\n");
   sb_puts(sb, "}\n\n");

   sb_printf(sb, "static int %s(lua_State *L) {\n", lfn);
   sb_printf(sb, "    %s_Handle *h = %s_checkhandle(L, 1);\n", mod, mod);
   if (q->nparams == 1) {
      sb_printf(sb, "    %s %s;\n", q->params[0].c_type, q->params[0].field);
      emit_bind_param(sb, q->params[0].c_type, q->params[0].field, 2, q->params[0].name);
   } else if (q->nparams > 1) {
      sb_printf(sb, "    %s params;\n", q->params_type);
      for (int i = 0; i < q->nparams; i++) {
         char dest[300];
         snprintf(dest, sizeof(dest), "params.%s", q->params[i].field);
         emit_bind_param(sb, q->params[i].c_type, dest, 2 + i, q->params[i].name);
      }
   }
   if (is_many) sb_puts(sb, "    lua_newtable(L);\n");
   sb_printf(sb, "    %s_ctx c = { L, 0 };\n", lfn);
   sb_printf(sb, "    int rc = %s_cb(h->db", q->func);
   emit_param_call(sb, q);
   sb_printf(sb, ", %s, &c);\n", collect);
   if (is_one) {
      sb_puts(sb, "    if (rc == SQLITE_NOTFOUND) { lua_pushnil(L); return 1; }\n");
      sb_puts(sb, "    if (rc != SQLITE_OK) return luaL_error(L, \"%s\", sqlite3_errmsg(h->db));\n");
   } else {
      sb_puts(sb, "    if (rc != SQLITE_OK) { lua_pop(L, 1); return luaL_error(L, \"%s\", sqlite3_errmsg(h->db)); }\n");
   }
   sb_puts(sb, "    return 1;\n");
   sb_puts(sb, "}\n\n");
}

static void emit_module(StrBuf *sb, Manifest *m, const char *mod, const char *queries_header) {
   sb_puts(sb, "// Generated by csql2lua - do not edit\n\n");
   sb_puts(sb, "#include <stdbool.h>\n");
   sb_puts(sb, "#include <stddef.h>\n");
   sb_puts(sb, "#include <stdio.h>\n");
   sb_puts(sb, "#include \"sqlite3.h\"\n");
   sb_puts(sb, "#include \"lua.h\"\n");
   sb_puts(sb, "#include \"lauxlib.h\"\n");
   sb_puts(sb, "#include \"lualib.h\"\n");
   sb_printf(sb, "#include \"%s\"\n\n", queries_header);

   sb_puts(sb, "#define MOD_MT \"");
   sb_puts(sb, mod);
   sb_puts(sb, ".Connection\"\n\n");

   sb_printf(sb, "typedef struct { sqlite3 *db; } %s_Handle;\n\n", mod);

   sb_printf(sb, "static %s_Handle *%s_checkhandle(lua_State *L, int idx) {\n", mod, mod);
   sb_printf(sb, "    %s_Handle *h = (%s_Handle *)luaL_checkudata(L, idx, MOD_MT);\n", mod, mod);
   sb_printf(sb, "    if (!h->db) luaL_error(L, \"attempt to use a closed '%s' database connection\");\n", mod);
   sb_puts(sb, "    return h;\n");
   sb_puts(sb, "}\n\n");

   sb_printf(sb, "static int %s_open(lua_State *L) {\n", mod);
   sb_puts(sb, "    const char *path = luaL_checkstring(L, 1);\n");
   sb_puts(sb, "    sqlite3 *db = NULL;\n");
   sb_puts(sb, "    int rc = sqlite3_open(path, &db);\n");
   sb_puts(sb, "    if (rc != SQLITE_OK) {\n");
   sb_puts(sb, "        char msg[256];\n");
   sb_puts(sb, "        snprintf(msg, sizeof(msg), \"%s\", db ? sqlite3_errmsg(db) : \"cannot open database\");\n");
   sb_puts(sb, "        if (db) sqlite3_close(db);\n");
   sb_puts(sb, "        return luaL_error(L, \"%s\", msg);\n");
   sb_puts(sb, "    }\n");
   sb_printf(sb, "    %s_Handle *h = (%s_Handle *)lua_newuserdata(L, sizeof(%s_Handle));\n", mod, mod, mod);
   sb_puts(sb, "    h->db = db;\n");
   sb_puts(sb, "    luaL_getmetatable(L, MOD_MT);\n");
   sb_puts(sb, "    lua_setmetatable(L, -2);\n");
   sb_puts(sb, "    return 1;\n");
   sb_puts(sb, "}\n\n");

   sb_printf(sb, "static int %s_close(lua_State *L) {\n", mod);
   sb_printf(sb, "    %s_Handle *h = (%s_Handle *)luaL_checkudata(L, 1, MOD_MT);\n", mod, mod);
   sb_puts(sb, "    if (h->db) { sqlite3_close(h->db); h->db = NULL; }\n");
   sb_puts(sb, "    return 0;\n");
   sb_puts(sb, "}\n\n");

   sb_printf(sb, "static int %s_tostring(lua_State *L) {\n", mod);
   sb_printf(sb, "    %s_Handle *h = (%s_Handle *)luaL_checkudata(L, 1, MOD_MT);\n", mod, mod);
   sb_printf(sb, "    lua_pushfstring(L, \"%s.Connection: %%p%%s\", (void*)h, h->db ? \"\" : \" (closed)\");\n", mod);
   sb_puts(sb, "    return 1;\n");
   sb_puts(sb, "}\n\n");

   for (int i = 0; i < m->nqueries; i++) emit_query_function(sb, mod, &m->queries[i]);

   sb_printf(sb, "static const luaL_Reg %s_methods[] = {\n", mod);
   sb_printf(sb, "    { \"close\", %s_close },\n", mod);
   for (int i = 0; i < m->nqueries; i++)
      sb_printf(sb, "    { \"%s\", %s_l_%s },\n", m->queries[i].func, mod, m->queries[i].func);
   sb_puts(sb, "    { NULL, NULL }\n");
   sb_puts(sb, "};\n\n");

   sb_printf(sb, "int luaopen_%s(lua_State *L) {\n", mod);
   sb_puts(sb, "    luaL_newmetatable(L, MOD_MT);\n");
   sb_printf(sb, "    lua_pushcfunction(L, %s_close);\n", mod);
   sb_puts(sb, "    lua_setfield(L, -2, \"__gc\");\n");
   sb_printf(sb, "    lua_pushcfunction(L, %s_tostring);\n", mod);
   sb_puts(sb, "    lua_setfield(L, -2, \"__tostring\");\n");
   sb_puts(sb, "    lua_newtable(L);\n");
   sb_printf(sb, "    luaL_setfuncs(L, %s_methods, 0);\n", mod);
   sb_puts(sb, "    lua_setfield(L, -2, \"__index\");\n");
   sb_puts(sb, "    lua_pop(L, 1);\n\n");
   sb_puts(sb, "    lua_newtable(L);\n");
   sb_printf(sb, "    lua_pushcfunction(L, %s_open);\n", mod);
   sb_puts(sb, "    lua_setfield(L, -2, \"open\");\n");
   sb_puts(sb, "    return 1;\n");
   sb_puts(sb, "}\n");
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
   Arena arena = {0};

   Config cfg = {
       .manifest = "manifest.json",
       .module = "queries",
       .queries_header = "queries.h",
       .output = "",
   };

   const char *config_path = args_config_path(argc, argv);
   if (config_path) config_load_file(&cfg, config_path, &arena);
   args_apply_overrides(&cfg, argc, argv, &arena);

   if (!cfg.output[0]) {
      StrBuf ob = {0};
      sb_printf(&ob, "%s.lua.c", cfg.module);
      cfg.output = arena_strdup(&arena, ob.data);
      sb_free(&ob);
   }

   Manifest *m = manifest_load(&arena, cfg.manifest);

   StrBuf out = {0};
   emit_module(&out, m, cfg.module, cfg.queries_header);
   write_file(cfg.output, out.data, out.len);
   sb_free(&out);

   arena_free(&arena);
   return 0;
}
