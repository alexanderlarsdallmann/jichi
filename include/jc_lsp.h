/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lsp.h - a minimal Language Server Protocol client for diagnostics.
 *
 * Spawns configured language servers (config "lspServers"), speaks LSP's
 * Content-Length-framed JSON-RPC over their stdio, opens a file, and collects
 * the server's textDocument/publishDiagnostics. Used to surface compiler/type
 * errors to the agent after it edits a file and via the `lsp` CLI subcommand.
 * Scope is intentionally diagnostics-only (no completion/hover/rename).
 *
 * The framing and diagnostics formatting are pure and unit-tested offline; the
 * transport (fork/exec/pipe/select) is a thin shim, like the MCP stdio client.
 */
#ifndef JC_LSP_H
#define JC_LSP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_config.h"
#include "jc_str.h"

struct jc_app;       /* jc_app.h          */
struct jc_lsp_conn;  /* opaque (jc_lsp.c) */

/* ----- pure helpers (unit-tested offline) ------------------------------ */

/* Append an LSP frame ("Content-Length: N\r\n\r\n" + body) to `out`. */
void jc_lsp_frame_encode(const char *body, struct jc_sb *out);

/* Streaming frame decoder: push raw bytes, pop complete message bodies. */
/* Hard cap on one LSP message body (M472). The framing header is attacker-supplied
 * -- a language server is a third-party program -- and `Content-Length` had no
 * sanity bound at all, so `Content-Length: 4000000000` made jichi buffer without
 * limit waiting for a body that never arrives. On the low-RAM rows that is an OOM
 * kill; anywhere it is a hostile or broken server stalling the agent.
 *
 * 64 MB is far above any real LSP payload (a large `textDocument/didOpen` for a
 * generated file is single-digit MB) and far below any budget worth defending.
 * Same shape as JC_SSE_FIELD_MAX, which bounds the other framing layer -- copied
 * deliberately, because the two failures are the same failure.
 *
 * The audit also flagged the unchecked `v * 10 + digit` accumulation and the
 * unchecked `hdr_len + 4 + clen`. Both were TESTED against hostile headers and
 * neither corrupts memory on 64-bit -- but only because malloc fails first, and
 * because SIZE_MAX happens to collide with find_content_length's (jc_size)-1
 * error sentinel. Two accidents, not two guards. The cap below is what makes the
 * arithmetic safe by construction instead. */
#define JC_LSP_MAX_BODY (64L * 1024L * 1024L)

/* M609: hard cap on the HEADER block -- the bytes before the "\r\n\r\n" that
 * ends the headers. The M472 body cap only engages once a complete header has
 * been parsed; a server that streams bytes and never sends the terminator grew
 * f->buf without limit, the same OOM the body cap closes, one layer earlier and
 * found by the 2026-08-27 hardening survey (§5). A real header block is two
 * short lines; 64 KB is orders above that and orders below a budget worth
 * defending. On overflow the whole buffer is dropped (there is no frame
 * boundary to preserve yet) and the framer resyncs. */
#define JC_LSP_MAX_HEADER (64L * 1024L)

struct jc_lsp_framer { struct jc_sb buf; };
void jc_lsp_framer_init(struct jc_lsp_framer *f);
void jc_lsp_framer_free(struct jc_lsp_framer *f);
void jc_lsp_framer_push(struct jc_lsp_framer *f, const char *bytes, jc_size n);
/* Pop the next complete message body (malloc'd, NUL-terminated) -> 1, else 0. */
int  jc_lsp_framer_pop(struct jc_lsp_framer *f, char **body_out);

/* Build a "file://" URI for `path` (resolved against `cwd` if relative) into
 * `buf` (capacity `cap`). No percent-encoding (plain paths only). */
void jc_lsp_path_to_uri(const char *path, const char *cwd, char *buf,
                        jc_size cap);

/* LSP languageId for a file extension (without the dot); falls back to `ext`. */
const char *jc_lsp_language_id(const char *ext);

/* A recommended language server for a language (M114). */
struct jc_lsp_suggestion {
    const char *command;    /* the server binary, e.g. "clangd"          */
    const char *extensions; /* csv file extensions, e.g. "c,h,cpp,hpp"   */
    const char *install;    /* a short install hint                       */
};

/* Suggest a language server for a language name or file extension
 * (c/cpp/python/go/rust/zig/typescript/javascript/ruby/...). Fills *out and
 * returns 1 for a known language, else 0 (out untouched). Pure; unit-tested. */
int jc_lsp_suggest(const char *lang, struct jc_lsp_suggestion *out);

/* Parse a textDocument/publishDiagnostics params object. If its "uri" equals
 * `want_uri`, append each diagnostic ("<display_path>:<line>:<col>: <sev>:
 * <msg>") to `out`, store the count in *count, and return 1; otherwise return
 * 0. Lines/columns are converted from 0-based (LSP) to 1-based. */
int jc_lsp_format_diagnostics(const char *params_json, const char *want_uri,
                              const char *display_path, struct jc_sb *out,
                              int *count);

/* Format an LSP location result (the `result` of a textDocument/definition or
 * /references response) into `out`: one "<path>:<line>:<col>" per location
 * (1-based; "file://" stripped to a path). Accepts a single Location object, a
 * Location[] array, or a LocationLink[] (targetUri/targetRange), or null/[].
 * Returns 1 when the JSON parsed (even with 0 results) and stores the count. */
int jc_lsp_format_locations(const char *result_json, struct jc_sb *out,
                            int *count);

/* Format a symbol result (textDocument/documentSymbol or workspace/symbol) into
 * `out`. Handles hierarchical DocumentSymbol[] (indented by depth, "<kind>
 * <name>  (line N)") and flat SymbolInformation[]/WorkspaceSymbol[] ("<kind>
 * <name>  <path>:<line>"). Returns 1 when parsed; stores the count. */
int jc_lsp_format_symbols(const char *result_json, struct jc_sb *out,
                          int *count);

/* Format a textDocument/codeAction result ((CodeAction|Command)[]) into `out`,
 * one action per line as "<title>  [<kind>]" (the "  [<kind>]" suffix omitted
 * for a bare Command / kind-less action). Returns 1 when parsed; stores the
 * count. Pure (M44). */
int jc_lsp_format_code_actions(const char *result_json, struct jc_sb *out,
                               int *count);

/* Extract the workspace/executeCommand command from a code-action node
 * (`action_json` is a serialized CodeAction or Command object). A Command has a
 * string `command` (+ optional `arguments`); a CodeAction nests one under its
 * `command` field. On success returns 1 and sets *cmd_out (malloc'd command
 * string, caller frees) and *args_json_out (malloc'd JSON-array string of
 * arguments, or NULL if none; caller frees). Returns 0 when no command is
 * present (e.g. an edit-only action) or on malformed input. Pure (M50). */
int jc_lsp_action_command(const char *action_json, char **cmd_out,
                          char **args_json_out);

/* From a `publishDiagnostics` params object (`params_json`) whose `uri` equals
 * `want_uri`, return a malloc'd JSON array string of the verbatim Diagnostic
 * objects whose range covers the 0-based `line0` (`start.line <= line0 <=
 * end.line`). Returns `"[]"` on no match / uri mismatch / malformed input
 * (never NULL on success; NULL only on allocation failure). Used to populate
 * the codeAction request `context.diagnostics` so diagnostic-tied quick-fixes
 * are offered (M57). Pure + unit-tested. Caller frees. */
char *jc_lsp_diagnostics_for_line(const char *params_json, const char *want_uri,
                                  long line0);

/* Find the first word-boundary occurrence of `symbol` in `text`; write its
 * 0-based line and column into the two out-params and return 1, else 0. */
int jc_lsp_locate_symbol(const char *text, const char *symbol, long *line,
                         long *character);

/* From a workspace/symbol result (SymbolInformation[]/WorkspaceSymbol[]), find
 * the first entry whose name equals `want_name`; copy its location uri into
 * `uri_buf` and write its 0-based line/character. Returns 1 on a match. */
int jc_lsp_first_symbol_location(const char *result_json, const char *want_name,
                                 char *uri_buf, jc_size uri_cap, long *line,
                                 long *character);

/* Apply an LSP TextEdit[] (`edits_json`: a JSON array of {range:{start,end},
 * newText}) to `text`, appending the result to `out`. Edits are applied in
 * document order (sorted by start position); overlapping edits after the first
 * are skipped. `character` is treated as a byte offset within the line — exact
 * for ASCII/UTF-8 single-byte, an approximation where LSP counts UTF-16 units.
 * Returns the number of edits applied, or -1 on malformed `edits_json` (in which
 * case `out` is untouched, so the caller leaves the file unchanged). Pure. */
int jc_lsp_apply_text_edits(const char *text, const char *edits_json,
                            struct jc_sb *out);

/* ----- manager --------------------------------------------------------- */

struct jc_lsp_manager {
    struct jc_app *app;
    struct jc_vec  conns; /* of struct jc_lsp_conn* (lazily spawned) */
};

void jc_lsp_manager_init(struct jc_lsp_manager *m, struct jc_app *app);
void jc_lsp_manager_shutdown(struct jc_lsp_manager *m);

/* Non-zero if some configured server is matched to this file's extension. */
int jc_lsp_handles(struct jc_lsp_manager *m, const char *path);

/* Run diagnostics for `path`: lazily spawn + initialize the matching server,
 * (re)open the file, wait for diagnostics, and return a malloc'd report (caller
 * frees) — a formatted diagnostics list, "no diagnostics", or a short error
 * note. Returns NULL only when no server matches the extension. *count_out (if
 * non-NULL) receives the number of diagnostics. */
char *jc_lsp_diagnostics(struct jc_lsp_manager *m, const char *path,
                         int *count_out);

/* ----- navigation (definition / references / symbols) ------------------- */
/* Each returns a malloc'd report (caller frees), or NULL when no server matches
 * `path`'s extension. *count (if non-NULL) gets the number of results. */

/* Go-to-definition of `symbol`. With `path`, locate `symbol` in that file (or
 * use `line_hint`, 1-based, when > 0) and query textDocument/definition; with
 * `path == NULL`, query workspace/symbol and list matching definitions. */
char *jc_lsp_definition(struct jc_lsp_manager *m, const char *path,
                        const char *symbol, long line_hint, int *count);

/* Find references to `symbol`. With `path`, query textDocument/references at the
 * located position; with `path == NULL`, resolve the symbol via workspace/symbol
 * then query references at its definition. */
char *jc_lsp_references(struct jc_lsp_manager *m, const char *path,
                        const char *symbol, long line_hint, int *count);

/* List the document symbols (outline) of `path` via textDocument/documentSymbol. */
char *jc_lsp_symbols(struct jc_lsp_manager *m, const char *path, int *count);

/* ----- refactors (rename / format) — M40 ------------------------------- */

/* textDocument/rename: resolve the symbol's position in `path` (via `line_hint`
 * 1-based when > 0, else by locating `symbol`), request a rename to `new_name`,
 * and return the raw WorkspaceEdit `result` as a malloc'd JSON string (caller
 * frees). NULL when no server matches, the symbol isn't found, or the server
 * returns no edit. The caller applies the WorkspaceEdit to the workspace. */
char *jc_lsp_rename(struct jc_lsp_manager *m, const char *path, long line_hint,
                    const char *symbol, const char *new_name);

/* textDocument/formatting: return the server's TextEdit[] `result` for `path`
 * as a malloc'd JSON string (caller frees), or NULL when no server matches or
 * the server returns nothing. The caller applies the edits. */
char *jc_lsp_format(struct jc_lsp_manager *m, const char *path);

/* Split a comma/space-separated list of CodeActionKind values
 * ("quickfix, refactor.extract") into a malloc'd JSON array string
 * (`["quickfix","refactor.extract"]`) for a codeAction request's
 * `context.only` filter. Returns NULL if `csv` is NULL or has no tokens (i.e.
 * no filter). Pure + unit-tested (M58). Caller frees. */
char *jc_lsp_only_array(const char *csv);

/* textDocument/codeAction at `line` (1-based) of `path`: return the raw
 * (CodeAction|Command)[] `result` as a malloc'd JSON string (caller frees), or
 * NULL when no server matches / the request fails. The whole line is the query
 * range; the line's structured diagnostics are sent in the context (M57). When
 * `only` is non-NULL/non-empty it is a comma/space-separated list of
 * CodeActionKinds sent as `context.only` to restrict the kinds returned (M58;
 * NULL => all kinds). */
char *jc_lsp_code_actions(struct jc_lsp_manager *m, const char *path,
                          long line, const char *only);

/* codeAction/resolve: re-send the action object `action_json` so a server can
 * fill a lazily-computed `edit`. Returns the resolved CodeAction JSON (caller
 * frees), or NULL when the server doesn't resolve it. */
char *jc_lsp_code_action_resolve(struct jc_lsp_manager *m, const char *path,
                                 const char *action_json);

/* workspace/executeCommand: run `command` (+ `args_json`, a JSON-array string or
 * NULL) on the server owning `path`. While the command runs, any
 * workspace/applyEdit request the server sends is acked ({applied:true}) and its
 * WorkspaceEdit collected: *edits_out (when non-NULL) receives a malloc'd
 * JSON-array string of the collected WorkspaceEdits (NULL if none), which the
 * caller applies through the path fence. Returns JC_OK once the command's
 * response arrives, else a transport error (M50). */
jc_status jc_lsp_execute_command(struct jc_lsp_manager *m, const char *path,
                                 const char *command, const char *args_json,
                                 char **edits_out);

#ifdef __cplusplus
}
#endif
#endif /* JC_LSP_H */
