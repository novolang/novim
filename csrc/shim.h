/* novo-treesitter shim.h
 *
 * Pointer-form wrappers around libtree-sitter's TSNode-by-value API.
 * TSNode is a 32-byte aggregate (`{uint32_t context[4], const void *id,
 * const TSTree *tree}`) — Novo's @ffi surface only handles scalars and
 * pointers, so every function that takes or returns TSNode by value
 * gets a wrapper here that swaps the value for a `void*` to a 32-byte
 * caller-managed buffer.
 *
 * Caller layout for the buffer is exactly the C TSNode layout — Novo's
 * facade layer reads/writes the 4 64-bit words via std.ptr.read_word /
 * write_word.  Buffer ownership is the Novo caller's; allocate with
 * `ptr.alloc(32)`, free with `ptr.free`.
 *
 * The shim also bundles dlopen/dlsym for grammar loading so the Novo
 * package doesn't need a separate libdl binding.
 */
#ifndef NOVO_TREESITTER_SHIM_H_
#define NOVO_TREESITTER_SHIM_H_

#include <stdbool.h>
#include <stdint.h>
#include <tree_sitter/api.h>

/* ── grammar loading via dlopen ──────────────────────────────────────
 * `path` is a filesystem path to a tree-sitter grammar .so (e.g.
 * "tree-sitter-json.so").  `symbol` is the grammar's exported
 * constructor name (e.g. "tree_sitter_json").  Returns a pointer to
 * the resulting TSLanguage on success; NULL on dlopen / dlsym failure.
 *
 * The dlopen handle is intentionally leaked — grammar shared objects
 * are process-lifetime; releasing them invalidates every parser that
 * was set up against them, and the returned TSLanguage points into
 * the mapping.  There is no unload entry point precisely because a
 * caller holding only a TSLanguage cannot know whether some other
 * parser still needs it.
 */
const TSLanguage *novo_ts_load_grammar(const char *path, const char *symbol);

/* Last dlopen / dlsym error string for diagnostics.  Returns "" if
 * the most recent novo_ts_load_grammar succeeded. */
const char *novo_ts_load_grammar_error(void);

/* ── TSNode-by-value wrappers ────────────────────────────────────────
 * Every function that accepts a TSNode takes a `const void *node`
 * pointing at a 32-byte buffer; every function that returns a TSNode
 * takes a `void *out_node` pointing at a 32-byte caller buffer the
 * shim writes into.
 */

/* Read the tree's root node into `out_node` (32 bytes). */
void novo_ts_tree_root_node(const TSTree *tree, void *out_node);

/* Read the i-th child of `node` into `out_node` (32 bytes). */
void novo_ts_node_child(const void *node, uint32_t i, void *out_node);

/* Read the i-th NAMED child of `node` into `out_node` (32 bytes). */
void novo_ts_node_named_child(const void *node, uint32_t i, void *out_node);

/* Total + named child counts. */
uint32_t novo_ts_node_child_count(const void *node);
uint32_t novo_ts_node_named_child_count(const void *node);

/* Byte range covered by the node. */
uint32_t novo_ts_node_start_byte(const void *node);
uint32_t novo_ts_node_end_byte(const void *node);

/* Node "type" (kind) name — borrowed pointer into the grammar's
 * static string table.  Caller copies via std.ptr.read_str. */
const char *novo_ts_node_type(const void *node);

/* Whether the node is a named (non-anonymous) production. */
bool novo_ts_node_is_named(const void *node);

/* Whether the node is a missing (synthesised by the parser) leaf. */
bool novo_ts_node_is_missing(const void *node);

/* Whether the subtree rooted at the node contains any error / missing
 * node — useful for "is this parse clean" checks. */
bool novo_ts_node_has_error(const void *node);

/* S-expression serialisation of the subtree.  Returns a malloc'd
 * NUL-terminated string; caller must free via novo_ts_free_string
 * (NOT std.ptr.free directly — keeps the malloc/free pair on one
 * heap, which matters when the shim is built against musl while
 * Novo runtime is glibc, etc.). */
char *novo_ts_node_string(const void *node);

/* Free a string previously returned by novo_ts_node_string. */
void novo_ts_free_string(char *s);

/* ── Query API ────────────────────────────────────────────────────
 * Higher-level iterator interface over libtree-sitter's
 * ts_query_cursor_*.  Hides TSQueryMatch / TSQueryCapture struct-by-
 * value passing behind a simple "next capture" call that fills
 * out-params.  Novo has no by-value struct ABI for these types, so
 * the iterator shape is what makes the query API callable at all.
 */

/* Compile a query from `source` (typically highlights.scm).
 * Returns the TSQuery, or NULL on parse error. */
TSQuery *novo_ts_query_new(
    const TSLanguage *language,
    const char *source,
    uint32_t source_len);

void novo_ts_query_delete(TSQuery *query);

TSQueryCursor *novo_ts_query_cursor_new(void);
void novo_ts_query_cursor_delete(TSQueryCursor *cursor);

/* Run `query` against the subtree rooted at `node` (32-byte
 * buffer, same layout as the rest of the shim). */
void novo_ts_query_cursor_exec(
    TSQueryCursor *cursor, const TSQuery *query, const void *node);

/* Iterator step.  Returns 1 if a next capture is available, 0 at
 * end of stream.  When 1, the out-params get the capture's name
 * pointer (into the query's static table) + source byte range.
 *
 * Out-params (32-bit values) are returned as packed Int via the
 * shim's caller-allocated buffers; see Novo facade for marshalling.
 */
int novo_ts_next_capture(
    TSQueryCursor *cursor,
    const TSQuery *query,
    const char **out_name_addr,
    uint64_t *out_start_byte,
    uint64_t *out_end_byte);

/* Returns a static "\x1b" — Novo's string escape grammar doesn't
 * include hex/octal escapes, so the highlight CLI gets the ESC
 * byte through a C constant instead. */
const char *novo_ts_ansi_esc(void);

/* Static SGR escapes for highlight rendering.  Built in C because
 * Novo string-concat doesn't survive a non-ASCII first byte (the
 * `\x1b` ESC kills str.len downstream). */
const char *novo_ts_ansi_for_capture(const char *capture_name);
const char *novo_ts_ansi_reset(void);

#endif /* NOVO_TREESITTER_SHIM_H_ */
