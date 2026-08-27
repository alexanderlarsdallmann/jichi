/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_docs.h - external documentation retrieval (M34a).
 *
 * A named, persistent retrieval index over *external* documentation (a library's
 * docs, a house style guide, a spec, an API reference) so the agent can ground
 * answers in sources outside the workspace. Each source is a config
 * `docs: [{name, path}]` entry naming a local directory; jc_docs_run builds (or
 * incrementally reloads) an embeddings index over that directory using the
 * embed-role model -- reusing the whole jc_index/jc_cosine/jc_rerank stack that
 * codebase search already uses -- and returns the chunks most relevant to a
 * query.
 *
 * Backs the `search_docs` tool, the `@docs:<name>` reference, and the `docs`
 * subcommand. Registered/usable only when at least one `docs` source and an
 * embed-role model are configured.
 */
#ifndef JC_DOCS_H
#define JC_DOCS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

struct jc_app;
struct jc_docs_cfg;

/* Render `html` to plain text into `out`: drop tags (skipping <script>/<style>
 * bodies), turn block-level tags into newlines and others into spaces, decode a
 * few common entities, and collapse whitespace. Best-effort, not a full HTML
 * parser — enough to make a fetched doc page embed reasonably (M51). Pure. */
void jc_docs_html_to_text(const char *html, struct jc_sb *out);

/* Resolve a doc source to a local directory ready for jc_index_build: a `path`
 * source is realpath-resolved + validated as a directory; a `url` source is
 * fetched and cached as a local text file (see jc_docs_html_to_text), re-fetched
 * when the cache is stale. Writes the directory into `root`. On error returns a
 * status and sets *err to a heap message (caller frees) when possible. (M51) */
jc_status jc_docs_source_root(struct jc_app *app, const struct jc_docs_cfg *src,
                              char *root, jc_size cap, char **err);

/* Resolve a doc source by name. When `name` is NULL/empty and exactly one source
 * is configured, that sole source is returned; otherwise a NULL/ambiguous name
 * returns NULL. Returns NULL when no match. */
const struct jc_docs_cfg *jc_docs_find(struct jc_app *app, const char *name);

/* Retrieve the `top_k` (<=0 => default) chunks of doc source `src` most relevant
 * to `query`. On success returns JC_OK with *out_text a heap string (caller
 * frees) holding the formatted hits (or a "no match" note). On failure returns a
 * status and sets *out_text to a heap error string when possible. The index is
 * built/reloaded locally and freed before returning. */
jc_status jc_docs_run(struct jc_app *app, const struct jc_docs_cfg *src,
                      const char *query, int top_k, char **out_text);

#ifdef __cplusplus
}
#endif
#endif /* JC_DOCS_H */
