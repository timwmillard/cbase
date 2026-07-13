/*
 * singleh — combine multiple .c/.h files (inlined, local #includes resolved
 * recursively, system #includes hoisted + deduped to the top) and raw data
 * files (stringified — same job as embedc, logic copied from there) into one
 * amalgamated, stb-style single header.
 *
 * Usage: singleh -manifest <path>
 *
 * Manifest format: flat text, '#' comments, blank lines ignored.
 *
 *   output = <path>              output header to write
 *   guard  = <MACRO>              optional #ifndef/#define/#endif wrapper
 *
 *   include <path>                inline this .c/.h. Its own local
 *                                 #include "..." lines are resolved
 *                                 recursively and inlined in place (each
 *                                 file inlined at most once, even if
 *                                 several inputs include it); #include <...>
 *                                 lines are collected and hoisted, deduped,
 *                                 to one block at the very top of the
 *                                 output instead of being inlined.
 *                                 Entries are emitted in manifest order —
 *                                 singleh does not topologically sort, so
 *                                 list a dependency before its dependents.
 *
 *   embed <path> [name=<id>] [mode=text|binary]
 *                                 stringify this file, same as embedc:
 *                                 text -> null-terminated char <name>_data[]
 *                                 (a C string); binary (default) -> a raw
 *                                 unsigned char <name>_data[] byte array.
 *                                 Either way also emits
 *                                 `unsigned int <name>_len`. name defaults
 *                                 to the basename with non-identifier
 *                                 characters replaced by '_'.
 *
 * include and embed entries may be interleaved in any order; each is
 * appended to the output in the order it appears in the manifest.
 *
 * Known limitation: the #include scanner is line-based — it does not parse
 * comments or string literals, so a line that merely *looks* like an
 * #include directive (e.g. one commented out) would be misread. Real
 * source doesn't tend to do this in practice.
 */

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Arena — block-backed bump allocator for the generator's own memory. Freed
 * all at once at exit; we never individually free during a run.
 * ===========================================================================
 */

#define ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGN 16

typedef struct ArenaBlock
{
    struct ArenaBlock *next;
    size_t used;
    size_t cap;
    char data[];
} ArenaBlock;

typedef struct
{
    ArenaBlock *head;
} Arena;

static ArenaBlock *arena_block_new(size_t cap)
{
    if (cap < ARENA_BLOCK_SIZE)
        cap = ARENA_BLOCK_SIZE;
    ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
    if (!b) {
        fprintf(stderr, "singleh: out of memory\n");
        exit(1);
    }
    b->next = NULL;
    b->used = 0;
    b->cap = cap;
    return b;
}

static void *arena_alloc(Arena *a, size_t size)
{
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

static char *arena_strdup(Arena *a, const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = arena_alloc(a, n);
    memcpy(p, s, n);
    return p;
}

static char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ===========================================================================
 * StrBuf — growable output buffer.
 * ===========================================================================
 */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_reserve(StrBuf *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t cap = sb->cap ? sb->cap : 256;
    while (cap < sb->len + extra + 1) cap *= 2;
    sb->data = realloc(sb->data, cap);
    if (!sb->data) {
        fprintf(stderr, "singleh: out of memory\n");
        exit(1);
    }
    sb->cap = cap;
}

static void sb_puts(StrBuf *sb, const char *s)
{
    size_t n = strlen(s);
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = 0;
}

static void sb_printf(StrBuf *sb, const char *fmt, ...)
{
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

/* ===========================================================================
 * StrList — small growable set of arena-owned strings (linear search; these
 * lists top out at "number of files in an amalgamation", not a scale where
 * anything fancier pays for itself).
 * ===========================================================================
 */

typedef struct
{
    const char **items;
    int n, cap;
} StrList;

static int strlist_has(StrList *l, const char *s)
{
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->items[i], s) == 0) return 1;
    return 0;
}

static void strlist_add(StrList *l, const char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->items = realloc(l->items, (size_t)l->cap * sizeof *l->items);
    }
    l->items[l->n++] = s;
}

/* ===========================================================================
 * File IO
 * ===========================================================================
 */

static char *read_file(Arena *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "singleh: cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = arena_alloc(a, (size_t)n + 1);
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "singleh: short read on %s\n", path);
        exit(1);
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static char *str_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        *end-- = 0;
    return s;
}

/* ===========================================================================
 * embed — file -> C data array. Copied from tool/embedc/embedc.c (kept as
 * its own small standalone tool; this just reuses the same emit logic,
 * writing into a StrBuf instead of straight to a file, since singleh has to
 * buffer the body before it knows the final hoisted #include block).
 * ===========================================================================
 */

/* Derive a C identifier from a path: basename, non-alnum -> '_'. */
static void derive_name(const char *path, char *out, size_t cap)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;

    size_t i = 0;
    for (; base[i] && i + 1 < cap; i++) {
        unsigned char c = (unsigned char)base[i];
        out[i] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    out[i] = '\0';

    /* A C identifier may not start with a digit. */
    if (i > 0 && isdigit((unsigned char)out[0]) && i + 1 < cap) {
        memmove(out + 1, out, i + 1);
        out[0] = '_';
    }
}

static void emit_binary(FILE *in, StrBuf *out, const char *name)
{
    unsigned long len = 0;
    int c;

    sb_printf(out, "const unsigned char %s_data[] = {", name);
    while ((c = fgetc(in)) != EOF) {
        if (len % 12 == 0)
            sb_puts(out, "\n\t");
        sb_printf(out, "0x%02x, ", (unsigned char)c);
        len++;
    }
    sb_puts(out, "\n};\n");
    sb_printf(out, "const unsigned int %s_len = %lu;\n", name, len);
}

static void emit_text(FILE *in, StrBuf *out, const char *name)
{
    unsigned long len = 0;
    int c;

    sb_printf(out, "const char %s_data[] =\n\t\"", name);
    while ((c = fgetc(in)) != EOF) {
        unsigned char ch = (unsigned char)c;
        len++;
        switch (ch) {
        case '\\': sb_puts(out, "\\\\"); break;
        case '"':  sb_puts(out, "\\\""); break;
        case '\t': sb_puts(out, "\\t");  break;
        case '\r': sb_puts(out, "\\r");  break;
        case '\n': sb_puts(out, "\\n\"\n\t\""); break;
        default:
            if (ch >= 0x20 && ch < 0x7f)
                sb_printf(out, "%c", ch);
            else
                /* 3-digit octal: unambiguous regardless of the next char. */
                sb_printf(out, "\\%03o", ch);
        }
    }
    sb_puts(out, "\";\n");
    sb_printf(out, "const unsigned int %s_len = %lu;\n", name, len);
}

/* ===========================================================================
 * include — inline a .c/.h file, resolving its local #includes recursively.
 * ===========================================================================
 */

typedef struct
{
    int is_system;      /* <...> vs "..." */
    char inner[512];
} IncludeMatch;

/* Recognizes `#include "foo.h"` / `#include <foo.h>`, with optional
 * leading/internal whitespace (`# include`). Line-based: doesn't know about
 * comments or string literals. */
static int match_include(const char *line, IncludeMatch *out)
{
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (*p != '#') return 0;
    p++;
    while (isspace((unsigned char)*p)) p++;
    if (strncmp(p, "include", 7) != 0) return 0;
    p += 7;
    if (!isspace((unsigned char)*p)) return 0;
    while (isspace((unsigned char)*p)) p++;

    char close = 0;
    if (*p == '"') { out->is_system = 0; close = '"'; }
    else if (*p == '<') { out->is_system = 1; close = '>'; }
    else return 0;

    const char *start = p + 1;
    const char *end = strchr(start, close);
    if (!end) return 0;
    size_t n = (size_t)(end - start);
    if (n >= sizeof out->inner) n = sizeof(out->inner) - 1;
    memcpy(out->inner, start, n);
    out->inner[n] = 0;
    return 1;
}

static void dirname_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, cap, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n >= cap) n = cap - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

/* from_path/from_line are only for error messages (the include site that
 * pulled this file in); pass NULL/0 for a manifest-level root. */
static void inline_file(Arena *a, StrBuf *body, StrList *seen_local,
                         StrList *sys_includes, const char *path,
                         const char *from_path, int from_line)
{
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        if (from_path)
            fprintf(stderr, "singleh: %s:%d: cannot resolve include \"%s\"\n",
                    from_path, from_line, path);
        else
            fprintf(stderr, "singleh: cannot resolve %s\n", path);
        exit(1);
    }
    if (strlist_has(seen_local, resolved))
        return; /* already inlined once */
    strlist_add(seen_local, arena_strdup(a, resolved));

    char dir[PATH_MAX];
    dirname_of(path, dir, sizeof dir);

    char *text = read_file(a, path);
    int lineno = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        lineno++;
        IncludeMatch m;
        if (match_include(line, &m)) {
            if (m.is_system) {
                if (!strlist_has(sys_includes, line))
                    strlist_add(sys_includes, arena_strdup(a, line));
            } else {
                char child[PATH_MAX];
                if (m.inner[0] == '/')
                    snprintf(child, sizeof child, "%s", m.inner);
                else
                    snprintf(child, sizeof child, "%s/%s", dir, m.inner);
                inline_file(a, body, seen_local, sys_includes, child, path, lineno);
            }
            continue; /* don't copy the #include line itself into body */
        }
        sb_puts(body, line);
        sb_puts(body, "\n");
    }
    sb_printf(body, "/* -- end %s -- */\n", path);
}

/* ===========================================================================
 * Manifest
 * ===========================================================================
 */

typedef enum { ENTRY_INCLUDE, ENTRY_EMBED } EntryKind;

typedef struct
{
    EntryKind kind;
    const char *path;
    const char *name;   /* EMBED only; NULL -> derive from path */
    int text_mode;      /* EMBED only: 1 text, 0 binary (embedc's default) */
} Entry;

typedef struct
{
    const char *output;
    const char *guard;  /* NULL -> no #ifndef/#define/#endif wrapper */
    Entry *entries;
    int n_entries, cap_entries;
} Manifest;

static void manifest_add(Manifest *m, Entry e)
{
    if (m->n_entries == m->cap_entries) {
        m->cap_entries = m->cap_entries ? m->cap_entries * 2 : 16;
        m->entries = realloc(m->entries, (size_t)m->cap_entries * sizeof *m->entries);
    }
    m->entries[m->n_entries++] = e;
}

static void manifest_load(Manifest *m, const char *path, Arena *a)
{
    char *text = read_file(a, path);
    int lineno = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        lineno++;
        char *t = str_trim(line);
        if (*t == 0 || *t == '#') continue;

        if (strncmp(t, "include", 7) == 0 && isspace((unsigned char)t[7])) {
            Entry e = {0};
            e.kind = ENTRY_INCLUDE;
            e.path = arena_strdup(a, str_trim(t + 7));
            manifest_add(m, e);
            continue;
        }

        if (strncmp(t, "embed", 5) == 0 && isspace((unsigned char)t[5])) {
            char *rest = str_trim(t + 5);
            char *sp = rest;
            while (*sp && !isspace((unsigned char)*sp)) sp++;

            Entry e = {0};
            e.kind = ENTRY_EMBED;
            e.path = arena_strndup(a, rest, (size_t)(sp - rest));
            e.text_mode = 0;

            char *p = sp;
            while (*p) {
                while (isspace((unsigned char)*p)) p++;
                if (!*p) break;
                char *tok = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                char save = *p;
                *p = 0;
                char *eq = strchr(tok, '=');
                if (!eq) {
                    fprintf(stderr, "singleh: %s:%d: expected key=value in '%s'\n",
                            path, lineno, tok);
                    exit(1);
                }
                *eq = 0;
                const char *key = tok, *val = eq + 1;
                if (strcmp(key, "name") == 0) e.name = arena_strdup(a, val);
                else if (strcmp(key, "mode") == 0) e.text_mode = strcmp(val, "text") == 0;
                else {
                    fprintf(stderr, "singleh: %s:%d: unknown embed attribute '%s'\n",
                            path, lineno, key);
                    exit(1);
                }
                *p = save;
            }
            manifest_add(m, e);
            continue;
        }

        char *eq = strchr(t, '=');
        if (eq) {
            *eq = 0;
            char *key = str_trim(t);
            char *val = str_trim(eq + 1);
            if (strcmp(key, "output") == 0) m->output = arena_strdup(a, val);
            else if (strcmp(key, "guard") == 0) m->guard = arena_strdup(a, val);
            else {
                fprintf(stderr, "singleh: %s:%d: unknown key '%s'\n", path, lineno, key);
                exit(1);
            }
            continue;
        }

        fprintf(stderr,
                "singleh: %s:%d: expected 'include <path>', "
                "'embed <path> [name=] [mode=]', or 'key = value'\n",
                path, lineno);
        exit(1);
    }
}

/* ===========================================================================
 * main
 * ===========================================================================
 */

static const char *args_manifest_path(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-manifest") == 0 && i + 1 < argc) return argv[i + 1];
        if (strncmp(argv[i], "-manifest=", 10) == 0) return argv[i] + 10;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *manifest_path = args_manifest_path(argc, argv);
    if (!manifest_path) {
        fprintf(stderr, "usage: %s -manifest <path>\n", argv[0]);
        return 2;
    }

    Arena arena = {0};
    Manifest m = {0};
    manifest_load(&m, manifest_path, &arena);

    if (!m.output) {
        fprintf(stderr, "singleh: %s: missing 'output = <path>'\n", manifest_path);
        return 1;
    }

    StrBuf body = {0};
    StrList seen_local = {0};
    StrList sys_includes = {0};

    for (int i = 0; i < m.n_entries; i++) {
        Entry *e = &m.entries[i];
        if (e->kind == ENTRY_INCLUDE) {
            inline_file(&arena, &body, &seen_local, &sys_includes, e->path, NULL, 0);
            continue;
        }

        FILE *in = fopen(e->path, e->text_mode ? "r" : "rb");
        if (!in) {
            fprintf(stderr, "singleh: cannot open %s\n", e->path);
            return 1;
        }
        char derived[256];
        const char *name = e->name;
        if (!name) {
            derive_name(e->path, derived, sizeof derived);
            name = derived;
        }
        if (e->text_mode) emit_text(in, &body, name);
        else emit_binary(in, &body, name);
        fclose(in);
    }

    FILE *out = fopen(m.output, "w");
    if (!out) {
        fprintf(stderr, "singleh: cannot write %s\n", m.output);
        return 1;
    }
    if (m.guard) fprintf(out, "#ifndef %s\n#define %s\n\n", m.guard, m.guard);
    for (int i = 0; i < sys_includes.n; i++)
        fprintf(out, "%s\n", sys_includes.items[i]);
    if (sys_includes.n > 0) fprintf(out, "\n");
    fwrite(body.data, 1, body.len, out);
    if (m.guard) fprintf(out, "\n#endif\n");
    fclose(out);

    printf("Generated %s\n", m.output);
    return 0;
}
