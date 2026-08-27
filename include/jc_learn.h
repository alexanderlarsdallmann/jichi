/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_learn.h - parse a mentor "lessons draft" into committable lessons (M70).
 *
 * The mentor (the scaffolded `/learn` command) writes `.jichi/lessons.draft.md`
 * with these sections: "## Memory notes" (one-line `- ` bullets), "## Skills"
 * ("### name: description" + a body each), "## Corrections" (M78 — `- remove:
 * <substr>` / `- replace: <substr> => <new note>` directives that supersede a
 * now-stale memory note the mentor identified), "## Project rules" (M106 —
 * one-line `- ` bullets of durable conventions committed to AGENTS.md, deduped),
 * and "## Suggested (manual)" (ignored — config/agent changes the human applies
 * by hand). `learn apply` parses the (human-edited) draft and commits the memory
 * notes + skills + corrections + project rules. Corrections are how the loop *corrects* (not just teaches): a
 * lesson that has become false — e.g. a note about a bug a commit has since
 * fixed — is retracted or reworded instead of lingering forever.
 *
 * jc_learn_parse_draft is pure (no I/O); the file read + the memory/skill writes
 * are jc_learn_apply below (M293 -- they lived in main.c until then, which is
 * why the TUI could not commit a draft). See docs/LEARNING.md.
 */
#ifndef JC_LEARN_H
#define JC_LEARN_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_mem.h"

struct jc_sb;  /* jc_str.h */
struct jc_app; /* jc_app.h  */

struct jc_learn_skill {
    char *name;        /* slug for the .jichi/skills/<name>/ folder */
    char *description; /* one-liner                               */
    char *body;        /* the procedure (markdown)                */
};

/* A correction to an existing memory note (M78). `match` is a substring that
 * identifies the stale note; `replacement` is the corrected note, or NULL to
 * simply remove the matching note(s). */
struct jc_learn_correction {
    char *match;
    char *replacement; /* NULL => remove; else remove matches + add this */
};

struct jc_learn_draft {
    struct jc_vec memory;      /* of char*: one-line notes (arena-owned)      */
    struct jc_vec skills;      /* of struct jc_learn_skill (arena-owned)      */
    struct jc_vec corrections; /* of struct jc_learn_correction (arena-owned) */
    struct jc_vec rules;       /* of char*: AGENTS.md project rules (M106)     */
    struct jc_vec checks;      /* M602: of char*: "## Checks" bullets of the form
                                * `constraint: <phrase>` -- a lesson stated as
                                * something jichi REFUSES, committed by apply as
                                * an AUTHORED constraint (.jichi/constraints.md),
                                * the one bridge in this codebase from a sentence
                                * to a fence (jc_constraint_scan). Only the
                                * phrase after "constraint:" is kept.         */
    int checks_unsupported;    /* M602: "## Checks" bullets of another kind
                                * (`hook:` and the like) -- counted, not built:
                                * hooks live in config.json and need the human
                                * to enable them (DEFERRED.md)               */
    int corrections_malformed; /* M600: "## Corrections" bullets that were not
                                * `remove: <substr>` / `replace: <old> => <new>`
                                * -- prose where a directive was required. They
                                * retract nothing, and until M600 vanished
                                * without a count: the zigodot draft's whole
                                * Corrections section was three such bullets. */
};

/* Init the (empty) draft vectors. */
void jc_learn_draft_init(struct jc_learn_draft *d);
void jc_learn_draft_free(struct jc_learn_draft *d);

/* Parse `text` into `out` (must be init'd; strings arena-owned). Pure:
 * - under "## Memory notes": each "- <note>" line becomes a memory note. Its
 *   trailing "[evidence: …]" and "[pins: …]" annotations are KEPT (M600 -- until
 *   then evidence was stripped, so the one place a note's provenance could
 *   travel to memory.md was discarded, and a later `## Corrections` pass had
 *   nothing to check a note against);
 * - under "## Skills": each "### <name>: <desc>" starts a skill whose body runs
 *   to the next "### " or "## ";
 * - under "## Corrections": each "- remove: <substr>" or "- replace: <substr>
 *   => <new note>" bullet becomes a correction (M78);
 * - "## Suggested" (manual) and any other text is ignored. */
void jc_learn_parse_draft(const char *text, struct jc_arena *a,
                          struct jc_learn_draft *out);

/* Render the offline `learn analyze` report for one telemetry log: rank recurring
 * problems (jc_insights), add the redo-loop scan over recent sessions, and add a
 * staleness review of the workspace's memory notes. `ws_arg` (may be NULL)
 * filters to one workspace, canonicalised to match the stamped "ws". Appends to
 * `out`; no printing, so the CLI and the TUI render the same bytes (M292). */
void jc_learn_analyze_render(struct jc_arena *arena, const char *text,
                             const char *ws_arg, struct jc_sb *out);

/* --- applying a draft (M293) ---------------------------------------------- */

/* Which of the draft's sections to commit. The mask exists from the start
 * because a CORRECTIONS-ONLY apply is a real operation, not a hypothetical: when
 * memory.md has outgrown the injection budget, what the user needs is to retract
 * stale notes, NOT to add more (M294). */
#define JC_LEARN_MEMORY      0x01u
#define JC_LEARN_SKILLS      0x02u
#define JC_LEARN_CORRECTIONS 0x04u
#define JC_LEARN_RULES       0x08u
#define JC_LEARN_CHECKS      0x10u  /* M602 */
#define JC_LEARN_ALL         0x1fu

struct jc_learn_apply_stats {
    unsigned sections;         /* the mask that was applied (echoed back)     */
    int memory_added;
    int skills_added;
    int skills_skipped;        /* existed already; needs `force`              */
    int corrections_applied;
    int corrections_unmatched; /* no memory note matched the directive        */
    int corrections_malformed; /* M600: bullets under "## Corrections" that were
                                * not a remove:/replace: directive; ignored, and
                                * SAID so -- they retract nothing               */
    int rules_added;           /* new bullets written to AGENTS.md            */
    int checks_added;          /* M602: AUTHORED constraints committed from
                                * "## Checks" (deduped by the constraint store) */
    int checks_unrecognised;   /* M602: `constraint:` phrases jc_constraint_scan
                                * could not read -- reported with the phrasings
                                * it does understand                          */
    int checks_unsupported;    /* M602: bullets of a kind apply cannot commit   */
    int rules_retracted;       /* M601: learned conventions a `## Corrections`
                                * directive removed or replaced in the rules
                                * file -- the loop can now take a RULE back, not
                                * only a memory note                          */
    int parsed_nothing;        /* draft had content but no parseable sections  */
    int pending_other;         /* items in sections the mask EXCLUDED (M294)   */
};

/* M601: pure core of retracting a learned convention. Copy `text` (a rules file)
 * to `out`, dropping every "- " bullet UNDER the "## Learned conventions" heading
 * whose body contains `match` (raw substring, as jc_memory_apply_correction);
 * bullets anywhere else in the file -- the hand-written rules above -- are never
 * touched, because `learn apply` only ever wrote under that heading and must not
 * reach past what it wrote. When `replacement` is non-empty and something was
 * removed, the replacement is appended as a bullet at the end of that section.
 * Returns removed + (1 if appended); 0 when the heading is absent. Pure;
 * unit-tested. Until M601 the store this writes to was append-only: the loop
 * could correct a lesson it had put in memory.md and not one it had put in
 * AGENTS.md (M533 is what one such write did). */
int jc_learn_rules_correct(const char *text, const char *match,
                           const char *replacement, struct jc_sb *out);

/* Write `<app->cwd>/.jichi/lessons.draft.md` into `out`. ONE path resolution, so
 * the CLI and the TUI cannot disagree about which draft is being applied. */
void jc_learn_draft_path(const struct jc_app *app, char *out, jc_size cap);

/* Commit the (human-edited) lessons draft: memory notes via jc_memory_add
 * (deduped), skills to .jichi/skills/<slug>/SKILL.md (skip-exists unless
 * `force`), corrections via jc_memory_correct, project rules appended to
 * AGENTS.md. The "Suggested (manual)" section is always left to the human.
 * Offline; no model call.
 *
 * `sections` masks what is applied (JC_LEARN_ALL for everything). Counts land in
 * `st`; per-item lines are APPENDED to `detail` (may be NULL) rather than
 * printed, so the CLI keeps its printf voice and the TUI uses put() while both
 * render one set of numbers.
 *
 * Refreshes app->memory when it changed, which is the reason this is a shared
 * function at all: a `learn apply` in a second process cannot do that, so a live
 * TUI session kept notes a `## Corrections` section had just superseded.
 *
 * Returns JC_ERR_NOTFOUND if there is no draft (the only failure a caller must
 * report differently); JC_OK otherwise, including when nothing applied. */
jc_status jc_learn_apply(struct jc_app *app, unsigned sections, int force,
                         struct jc_learn_apply_stats *st, struct jc_sb *detail);

/* Render the one-line outcome for `st` (or, when st->parsed_nothing, the
 * actionable "the draft has no parseable sections" advice naming `draft_path`).
 * Appends to `out`; the caller chooses stdout / stderr / put(). Shared so the
 * two front-ends cannot drift into describing the same result differently. */
void jc_learn_apply_summary(const struct jc_learn_apply_stats *st,
                            const char *draft_path, struct jc_sb *out);

#ifdef __cplusplus
}
#endif

#endif /* JC_LEARN_H */
