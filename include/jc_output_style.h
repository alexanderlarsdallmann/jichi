/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_output_style.h - custom output styles (Claude Code parity).
 *
 * An output style is a markdown file (YAML frontmatter `description`, plus a
 * body) that customises *how* the agent responds — its tone, format, verbosity —
 * by appending an authoritative instruction block to the system prompt. Unlike a
 * command's `agent:` profile (which replaces the whole persona for one turn), an
 * output style augments the persona and stays active for the whole session.
 *
 * Styles are discovered under the project's .jichi/output-styles and the global
 * ~/.config/jichi/output-styles directories (project overrides global on
 * a name collision). The active one is chosen by config `outputStyle`,
 * `--output-style`, or the TUI `/output-style` command. Parse / find / render
 * helpers are pure and unit-tested. See docs/OUTPUT_STYLES.md.
 */
#ifndef JC_OUTPUT_STYLE_H
#define JC_OUTPUT_STYLE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

struct jc_sb; /* jc_str.h */

struct jc_output_style {
    char *name;        /* file basename without .md, arena-owned */
    char *description; /* frontmatter, or NULL                   */
    char *body;        /* the style instructions, arena-owned    */
};

struct jc_output_style_set {
    struct jc_vec styles; /* of struct jc_output_style          */
    const char *active;   /* name of the active style, or NULL  */
};

void jc_output_style_set_init(struct jc_output_style_set *s);
void jc_output_style_set_free(struct jc_output_style_set *s);

/* Load styles from the global then project dirs. Always returns JC_OK (missing
 * dirs => empty set). Does not set the active style. */
jc_status jc_output_style_load(struct jc_output_style_set *s, const char *cwd,
                               struct jc_arena *a);

const struct jc_output_style *jc_output_style_find(
    const struct jc_output_style_set *s, const char *name);

/* The active style (set->active resolved), or NULL when none is active. */
const struct jc_output_style *jc_output_style_active(
    const struct jc_output_style_set *s);

/* Make `name` the active style. Returns 1 when found+activated, 0 when no such
 * style (active unchanged). A NULL/empty name clears the active style (returns
 * 1). The stored pointer is the style's own arena-owned name. */
int jc_output_style_set_active(struct jc_output_style_set *s, const char *name);

/* Parse one style file `text` into *out (pure; exposed for tests). `name` is the
 * basename. All fields are arena-owned. */
void jc_output_style_parse(const char *text, const char *name,
                           struct jc_arena *a, struct jc_output_style *out);

/* Append a human-readable listing into `out` (one line per style, the active one
 * marked). Pure; used by the `output-styles` subcommand and `/output-style`. */
void jc_output_style_render_list(const struct jc_output_style_set *s,
                                 struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_OUTPUT_STYLE_H */
