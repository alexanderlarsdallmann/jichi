/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_command.h - user-defined slash commands loaded from markdown.
 *
 * Markdown files under the project's .jichi/commands directory and the global
 * ~/.config/jichi/commands directory define reusable prompt templates.
 * Optional frontmatter (description, model, agent, subtask) precedes a body
 * template that supports these substitutions on invocation:
 *   $ARGUMENTS   the full argument string
 *   $1, $2, ...  positional arguments (whitespace-split)
 *   !`cmd`       the stdout of running `cmd` (bounded)
 *   @path        the contents of a file (bounded)
 * Invoked in the TUI (and headless) as `/name args`.
 */
#ifndef JC_COMMAND_H
#define JC_COMMAND_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

struct jc_command {
    char *name;        /* file basename without .md, arena-owned */
    char *description; /* frontmatter, or NULL                   */
    char *model;       /* frontmatter, or NULL (v1: unused)      */
    char *agent;       /* frontmatter: run the command under this agent profile
                        * (its system prompt + readonly), or NULL */
    int   subtask;     /* frontmatter (v1: unused)               */
    char *language;    /* frontmatter: answer language for this command's run,
                        * overriding config `language` (M597) -- the
                        * English-canonical-lessons option is `language:
                        * English` on learn.md. NULL if unset. */
    char *output;      /* frontmatter: workspace-relative file the command's
                        * subtask is expected to produce; if the run leaves it
                        * unchanged (the model narrated instead of writing it),
                        * the subtask's answer is persisted there (M79). NULL if
                        * unset. */
    char *body;        /* the prompt template, arena-owned       */
};

struct jc_command_set {
    struct jc_vec commands; /* of struct jc_command */
};

void jc_command_set_init(struct jc_command_set *s);
void jc_command_set_free(struct jc_command_set *s);

/* Load commands from the global then project dirs (project overrides global on
 * name collision). Always returns JC_OK (missing dirs => empty set). */
jc_status jc_command_load(struct jc_command_set *s, const char *cwd,
                          struct jc_arena *a);

const struct jc_command *jc_command_find(const struct jc_command_set *s,
                                         const char *name);

struct jc_sb; /* jc_str.h */

/* Render the set as a human-readable listing into `out` (one line per command:
 * `/name - description`). Pure; unit-tested. Used by the `commands` subcommand. */
void jc_command_render_list(const struct jc_command_set *s, struct jc_sb *out);

/* Expand a command's template. `args_raw` is the text after the command name
 * (may be NULL/empty); `cwd` anchors @path and !`cmd`. *out is arena-owned. */
jc_status jc_command_expand(const struct jc_command *c, const char *args_raw,
                            const char *cwd, struct jc_arena *a, char **out);

#ifdef __cplusplus
}
#endif
#endif /* JC_COMMAND_H */
