/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_refs.h - @-references: pull file/diff/url context into a chat message.
 *
 * A non-command user message may mention `@<path>` (inline a bounded file),
 * `@diff` (the working-tree git diff), `@url:<url>` (a fetched page),
 * `@rss:<url>` (an RSS/Atom feed reduced to text, W4),
 * `@sym:<name>` (a symbol's definition via the language server, or a code search
 * fallback), `@audio:<path>` (an audio file transcribed to text, M33), or
 * `@docs:<name>` (the most relevant passages of a configured external
 * documentation source, M34a), `@problems` (current LSP diagnostics for the
 * files touched this session), `@folder:<dir>` (a bounded tree + top-level
 * symbol outlines for a directory, M34/F5), or `@mcp:<uri>` (an MCP server
 * resource's text via read_mcp_resource, M47). At
 * submit time those are resolved and appended to the message as a
 * bounded "referenced context" block, so the model sees them. See
 * docs/REFERENCES.md.
 */
#ifndef JC_REFS_H
#define JC_REFS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_app;
struct jc_message; /* jc_message.h */

enum jc_ref_kind {
    JC_REF_FILE, JC_REF_DIFF, JC_REF_URL, JC_REF_SYM,
    JC_REF_IMAGE, /* an image file: attached to the message, not inlined as text */
    JC_REF_AUDIO, /* @audio:<path>: transcribed via transcribe_audio, inlined (M33) */
    JC_REF_DOCS,  /* @docs:<name>: external doc passages via search_docs (M34a) */
    JC_REF_PROBLEMS, /* @problems: LSP diagnostics for session-touched files (F5) */
    JC_REF_FOLDER, /* @folder:<dir>: a bounded tree + symbol outlines (F5) */
    JC_REF_MCP,   /* @mcp:<uri>: an MCP resource's text via read_mcp_resource (M47) */
    JC_REF_ALIAS, /* @ref:<name>: a config-defined alias (file/dir/url/ssh/key) (#6) */
    JC_REF_RSS    /* @rss:<url>: an RSS/Atom feed fetched + reduced to text (W4) */
};

struct jc_ref {
    int   kind; /* enum jc_ref_kind                                   */
    char *arg;  /* file path / url / symbol name / "" for diff (malloc'd) */
};

/* Pure: find @-references in `text` (an '@' at the start or right after
 * whitespace). Pushes struct jc_ref into `out` (caller-init'd vec of
 * struct jc_ref). Returns the count. */
int jc_refs_scan(const char *text, struct jc_vec *out);

/* Free the args pushed by jc_refs_scan and the vector. */
void jc_refs_free(struct jc_vec *refs);

/* Resolve any references in `raw` and return (in *out, arena-owned) the original
 * text followed by a bounded "referenced context" block for each ref that
 * resolved. When nothing resolves, *out is `raw` duplicated unchanged. Always
 * JC_OK. */
jc_status jc_refs_expand(struct jc_app *app, const char *raw,
                         struct jc_arena *a, char **out);

/* Scan `raw` for image references (`@photo.png` with an image extension, or an
 * explicit `@img:<path>`) and attach each to message `m` via jc_app_load_image
 * (honoring the path fence). Returns the number attached; load failures are
 * skipped. A no-op when the active model isn't vision-capable is the caller's
 * responsibility (this just resolves + attaches). See docs/VISION.md. */
int jc_refs_attach_images(struct jc_app *app, const char *raw,
                          struct jc_message *m);

#ifdef __cplusplus
}
#endif
#endif /* JC_REFS_H */
