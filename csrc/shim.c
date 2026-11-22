/* novo-treesitter shim.c — see shim.h for design notes. */

#include "shim.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

/* Last dlopen/dlsym error message.  A single static buffer, so the
 * error is only meaningful to the caller that just failed and only
 * before any other thread calls a loader entry point. */
static char g_last_load_err[512] = "";

const TSLanguage *novo_ts_load_grammar(const char *path, const char *symbol) {
    g_last_load_err[0] = '\0';

    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        if (e) {
            strncpy(g_last_load_err, e, sizeof g_last_load_err - 1);
            g_last_load_err[sizeof g_last_load_err - 1] = '\0';
        }
        return NULL;
    }

    /* Grammar exports a `const TSLanguage *tree_sitter_<name>(void)` */
    typedef const TSLanguage *(*ctor_fn)(void);
    dlerror();   /* clear any prior error */
    ctor_fn ctor = (ctor_fn)dlsym(h, symbol);
    const char *e = dlerror();
    if (e || !ctor) {
        if (e) {
            strncpy(g_last_load_err, e, sizeof g_last_load_err - 1);
            g_last_load_err[sizeof g_last_load_err - 1] = '\0';
        } else {
            strncpy(g_last_load_err, "dlsym returned NULL",
                    sizeof g_last_load_err - 1);
        }
        /* Intentionally leak `h` — see header comment about
         * grammar lifetimes. */
        return NULL;
    }
    return ctor();
}

const char *novo_ts_load_grammar_error(void) {
    return g_last_load_err;
}

/* Reading from a 32-byte buffer back into a TSNode is just memcpy.
 * Doing it via a single-field struct copy lets the C compiler emit a
 * single load on most ABIs. */
static inline TSNode load_node(const void *p) {
    TSNode n;
    memcpy(&n, p, sizeof n);
    return n;
}

static inline void store_node(void *p, TSNode n) {
    memcpy(p, &n, sizeof n);
}

void novo_ts_tree_root_node(const TSTree *tree, void *out_node) {
    store_node(out_node, ts_tree_root_node(tree));
}

void novo_ts_node_child(const void *node, uint32_t i, void *out_node) {
    store_node(out_node, ts_node_child(load_node(node), i));
}

void novo_ts_node_named_child(const void *node, uint32_t i, void *out_node) {
    store_node(out_node, ts_node_named_child(load_node(node), i));
}

uint32_t novo_ts_node_child_count(const void *node) {
    return ts_node_child_count(load_node(node));
}

uint32_t novo_ts_node_named_child_count(const void *node) {
    return ts_node_named_child_count(load_node(node));
}

uint32_t novo_ts_node_start_byte(const void *node) {
    return ts_node_start_byte(load_node(node));
}

uint32_t novo_ts_node_end_byte(const void *node) {
    return ts_node_end_byte(load_node(node));
}

const char *novo_ts_node_type(const void *node) {
    return ts_node_type(load_node(node));
}

bool novo_ts_node_is_named(const void *node) {
    return ts_node_is_named(load_node(node));
}

bool novo_ts_node_is_missing(const void *node) {
    return ts_node_is_missing(load_node(node));
}

bool novo_ts_node_has_error(const void *node) {
    return ts_node_has_error(load_node(node));
}

char *novo_ts_node_string(const void *node) {
    return ts_node_string(load_node(node));
}

void novo_ts_free_string(char *s) {
    free(s);
}

/* ── Query API ────────────────────────────────────────────────── */

TSQuery *novo_ts_query_new(
    const TSLanguage *language, const char *source, uint32_t source_len
) {
    uint32_t err_offset = 0;
    TSQueryError err_type = TSQueryErrorNone;
    TSQuery *q = ts_query_new(language, source, source_len,
                              &err_offset, &err_type);
    /* err_offset / err_type are discarded: NULL is the whole contract
     * this wrapper offers, and the caller turns it into a single
     * "query failed to compile" message.  Reporting *where* in the
     * .scm the parse failed would need two more out-params here. */
    (void)err_offset; (void)err_type;
    return q;
}

void novo_ts_query_delete(TSQuery *query) {
    if (query) ts_query_delete(query);
}

TSQueryCursor *novo_ts_query_cursor_new(void) {
    return ts_query_cursor_new();
}

void novo_ts_query_cursor_delete(TSQueryCursor *cursor) {
    if (cursor) ts_query_cursor_delete(cursor);
}

void novo_ts_query_cursor_exec(
    TSQueryCursor *cursor, const TSQuery *query, const void *node
) {
    ts_query_cursor_exec(cursor, query, load_node(node));
}

const char *novo_ts_ansi_esc(void) {
    static const char esc[] = "\x1b";
    return esc;
}

/* SGR escape sequences for the highlight CLI.  Returned as
 * static C strings; Novo reads them via std.ptr.read_str.
 *
 * The full sequence is built in C because Novo's string literal
 * escapes don't include `\x` or `\u`, so the ESC byte (0x1b)
 * cannot be put into a Novo string at the source level.
 */
const char *novo_ts_ansi_for_capture(const char *capture_name) {
    static const char dim_white[] = "\x1b[2;37m";
    static const char red[]       = "\x1b[31m";
    static const char green[]     = "\x1b[32m";
    static const char yellow[]    = "\x1b[33m";
    static const char blue[]      = "\x1b[34m";
    static const char magenta[]   = "\x1b[35m";
    static const char cyan[]      = "\x1b[36m";

    if (!capture_name) return "";

    /* Match on the dotted capture FAMILY, not the exact name.  Grammars keep
     * inventing leaf captures (@string.special.url, @number.float,
     * @function.method.call, …) and every one of them used to fall through
     * this table to "" — uncoloured.  Prefix matching means a grammar we have
     * never seen still highlights sensibly.
     *
     * The most-specific prefixes must be tested first: variable.* splits
     * three ways, and markup.raw/quote/link/list differ from bare markup. */
#define FAM(p) (strncmp(capture_name, (p), sizeof(p) - 1) == 0)

    if (FAM("comment"))              return dim_white;
    if (FAM("string") ||
        FAM("character"))            return green;
    if (FAM("number") ||
        FAM("boolean") ||
        FAM("constant"))             return yellow;
    if (FAM("variable.builtin"))     return red;
    if (FAM("variable.parameter") ||
        FAM("variable.member"))      return yellow;
    if (FAM("type") ||
        FAM("module") ||
        FAM("property") ||
        FAM("tag"))                  return cyan;
    if (FAM("function") ||
        FAM("constructor"))          return blue;
    if (FAM("keyword") ||
        FAM("operator") ||
        FAM("attribute") ||
        FAM("label"))                return magenta;

    /* The @markup.* family — nvim-treesitter's vocabulary for prose grammars.
     * markdown's highlights.scm is almost entirely @markup.* + @label, so
     * without these a markdown buffer parses fine and renders with no colour
     * at all. */
    if (FAM("markup.heading"))       return magenta;
    if (FAM("markup.raw") ||
        FAM("markup.quote"))         return green;
    if (FAM("markup.link") ||
        FAM("markup.list"))          return yellow;
    if (FAM("markup"))               return cyan;

    /* The legacy @text.* vocabulary that @markup.* replaced.  Still shipped by
     * plenty of grammars: tree-sitter-markdown vendors a copy of the old
     * nvim-treesitter queries, so its highlights.scm is entirely @text.title /
     * @text.literal / @text.uri and rendered with no colour at all until these
     * were mapped. */
    if (FAM("text.title"))           return magenta;
    if (FAM("text.literal"))         return green;
    if (FAM("text.uri") ||
        FAM("text.reference"))       return yellow;
    if (FAM("text"))                 return cyan;

#undef FAM
    return "";
}

const char *novo_ts_ansi_reset(void) {
    static const char r[] = "\x1b[0m";
    return r;
}

int novo_ts_next_capture(
    TSQueryCursor *cursor,
    const TSQuery *query,
    const char **out_name_addr,
    uint64_t *out_start_byte,
    uint64_t *out_end_byte
) {
    TSQueryMatch match;
    uint32_t cap_index = 0;
    if (!ts_query_cursor_next_capture(cursor, &match, &cap_index)) {
        return 0;
    }
    if (cap_index >= match.capture_count) {
        return 0;
    }
    TSQueryCapture cap = match.captures[cap_index];
    uint32_t name_len = 0;
    *out_name_addr = ts_query_capture_name_for_id(query, cap.index, &name_len);
    *out_start_byte = (uint64_t)ts_node_start_byte(cap.node);
    *out_end_byte = (uint64_t)ts_node_end_byte(cap.node);
    return 1;
}
