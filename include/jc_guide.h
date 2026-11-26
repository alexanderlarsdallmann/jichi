/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_guide.h - the compiled-in jichi guide: one text, two surfaces.
 *
 * `jichi guide [topic]` prints a topic's sections offline, and the same text
 * is served inside a run by the built-in `jichi-guide` skill (load_skill).
 * The TEXT lives in exactly one place -- the generated data in src/guide/
 * jc_guide.c, produced by tests/tools/guide_gen.sh from the marked page
 * sections (`<!-- jichi-guide -->` comments) in docs/. Do NOT hand-edit the
 * data file: edit the page, run the generator, commit its output. The smoke
 * driver tests/smoke/guide_lint.sh diffs the generator against the committed
 * file, so a page edit that is not regenerated fails the build.
 *
 * A topic's lines are the sections' lines concatenated in the note-table
 * order; each section contributes a heading line ("## <the page heading>,
 * exactly as it reads after `## `") followed by its body lines. This header
 * is hand-written (struct + accessors); only the DATA in jc_guide.c is
 * generated.
 */
#ifndef JC_GUIDE_H
#define JC_GUIDE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_sb;          /* include/jc_str.h */
struct jc_skill_set;   /* include/jc_skill.h */
struct jc_arena;       /* include/jc_mem.h */

struct jc_guide_topic {
    const char *name;          /* one of: setup model learner agents limits */
    const char *const *lines;  /* the topic's lines (heading/body), NULL-terminated */
};

/* The five topics in order, NULL-terminated. */
struct jc_guide_topic *jc_guide_topics(void);

/* The line array for a topic name, or NULL for an unknown name. */
const char *const *jc_guide_lines(const char *name);

/* Append the topic's sections as `jichi guide <topic>` renders them. */
void jc_guide_append_topic(struct jc_sb *out, const char *name);

/* Append the built-in skill's body (`# jichi guide` preamble line + the
 * five `## <topic>` sections, each rendered as the command renders it + the
 * project-skills line) to `out`. Rendered from the generated data. */
void jc_guide_skill_body(struct jc_sb *out);

/* Append the built-in `jichi-guide` skill to the set, AFTER the disk load
 * (jc_skill_load). A project skill named jichi-guide loaded afterwards wins
 * on find (add_or_replace); the built-in text this milestone ships is always
 * present unless a project skill takes its name. */
void jc_skill_add_builtin(struct jc_skill_set *s, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_GUIDE_H */
