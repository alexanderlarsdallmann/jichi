/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_str.h - growable string builder (jc_sb) and small string helpers.
 *
 * jc_sb owns a heap buffer it grows with realloc; it is the right tool for
 * data of unbounded size (JSON bodies, streamed content, SSE accumulation).
 * For fixed-lifetime nodes prefer the arena (jc_mem.h).
 */
#ifndef JC_STR_H
#define JC_STR_H


#ifdef __cplusplus
extern "C" {

#endif
#include "jc_platform.h"
#include <stdarg.h>

struct jc_sb {
    char  *data;  /* heap buffer, NUL-terminated after any append; may be NULL */
    jc_size len;  /* bytes used, excluding the terminator                      */
    jc_size cap;  /* bytes allocated                                           */
};

void      jc_sb_init(struct jc_sb *b);
void      jc_sb_free(struct jc_sb *b);
void      jc_sb_clear(struct jc_sb *b);          /* len = 0, keep capacity */

/* Clear; additionally, if capacity exceeds `max_cap`, free the backing store
 * entirely (re-init) so a one-off huge value does not pin its high-water for
 * the buffer's lifetime (M218: the provider stream scratch lives as long as
 * the provider). Under the bound it behaves exactly like jc_sb_clear. */
void      jc_sb_clear_shrink(struct jc_sb *b, jc_size max_cap);
jc_status jc_sb_reserve(struct jc_sb *b, jc_size extra);
jc_status jc_sb_append(struct jc_sb *b, const char *s);
jc_status jc_sb_append_n(struct jc_sb *b, const char *s, jc_size n);
jc_status jc_sb_append_char(struct jc_sb *b, char c);

/* printf-style append using jc_vsnprintf (see jc_snprintf.h). */
jc_status jc_sb_append_fmt(struct jc_sb *b, const char *fmt, ...);

/* Detach the buffer: returns the NUL-terminated data and resets the builder
 * to empty. The caller owns the returned pointer (free with free()). */
char *jc_sb_finish(struct jc_sb *b);

/* malloc-backed strdup (C89 has no strdup). NULL in -> NULL out. */
char *jc_strdup(const char *s);

/* Levenshtein edit distance between two NUL-terminated strings (insert/delete/
 * substitute, cost 1 each). Returns -1 if either is NULL. Names longer than 63
 * chars fall back to the length difference (this is used for short identifiers
 * like tool names). Pure; unit-tested. */
int jc_str_edit_distance(const char *a, const char *b);

/* M345: the one rule for "is this name close enough to suggest?" -- shared by
 * the tool-name suggester (the model's typos, M91) and the slash-command one
 * (the human's), so the two kindnesses cannot drift apart. About half the
 * typed length, clamped to [2,4]: a wild guess must yield silence, never a
 * misleading nudge. A negative distance (the error value) is never close.
 * Pure; unit-tested. */
int jc_str_close_enough(jc_size unknown_len, int distance);

/* M345: the nearest candidate from a NULL-terminated array, or NULL when
 * nothing passes jc_str_close_enough. A candidate's leading '/' is skipped for
 * both the comparison and the RETURN (callers print their own slash), so a
 * slash-command table can be passed as-is beside bare custom-command names.
 * First-listed wins a tie. Pure; unit-tested. */
const char *jc_str_closest(const char *unknown, const char *const *cands);

/* Write the non-negative integer value of `v` (rounded) into `buf` with `sep`
 * inserted every 3 digits from the right (e.g. sep='.' => "1.234.567"). sep==0
 * disables grouping. Always NUL-terminates within `cap`. Pure; unit-tested. */
void jc_group_num(double v, char sep, char *buf, jc_size cap);

/* The grouping separator an AUDIENCE should get: the configured one normally,
 * and none (0, which jc_group_num reads as "do not group") when accessible mode
 * is on. M555, and it is a defect report rather than a preference -- the
 * operator's German screen reader spoke `4.946` as digits with the dot read
 * aloud, so a listener got four digits and the name of a punctuation mark
 * instead of a number.
 *
 * NOT A GERMAN PROBLEM: jc_config sets `.` as the fallback separator when the
 * locale has none, which is what LC_ALL=C gives, so an English user with no
 * locale configured hears the same. The comma form is the same shape.
 *
 * WHICH LAYER, corrected at M559 -- the first version of this comment named
 * the wrong one. `espeak-ng -v de -q -x 4.946` says "vier tausend neunhundert
 * sechsundvierzig": the SYNTHESIZER reads a grouped number correctly, in German
 * and in English. The layer that speaks the separator is **Orca's punctuation
 * verbalisation** (`verbalizePunctuationStyle`, measured at SOME on the
 * operator's machine, with `speakNumbersAsDigits` False). At SOME it voices
 * embedded symbols, so the reader splits `4.946` at the dot, says the dot, and
 * hands the synthesizer two fragments. The fix is unaffected -- no separator
 * means no punctuation to verbalise -- but the mechanism was asserted without
 * measurement and that is what M559 corrects.
 *
 * The SIGHTED rendering keeps its separator -- a bare six-digit integer is
 * harder to scan -- so the two audiences want opposite things and this function
 * is where that is decided, once. Pure, so it is unit-testable; it lives here
 * rather than in the TUI because main.c's headless token line needs it too, and
 * a rule applied in one front-end and not the neighbouring one is the mistake
 * this project has recorded five times. */
char jc_group_sep_audience(char configured, int accessible);

/* Is `name` a name a POSIX shell can actually export -- [A-Za-z_][A-Za-z0-9_]*,
 * non-empty? (M326e.)
 *
 * The predicate behind three checks that must agree: the config's `apiKeyEnv`
 * lint in doctor, the `setup` wizard's validation of the same answer, and
 * jc_proc's refusal to build an unset-prefix from a config-supplied name. It
 * exists because a user holding an `sk-...` key pastes it at a prompt labelled
 * "API key env var"; the result is unexportable, so getenv can never find it,
 * and the run 401s with nothing naming the cause.
 *
 * NOTE this is deliberately stricter than the C standard, which places no
 * constraint on an environment name beyond excluding '='. putenv() could
 * install "sk-abc=1" and getenv would find it -- but no POSIX shell can
 * `export` it, so a config naming one cannot be satisfied by a user. Pure;
 * unit-tested. NULL => 0. */
int jc_envvar_name_valid(const char *name);

/* M534: THE boolean dialect, in one place.
 *
 * jichi reads booleans written by humans, models, editors and supervisor
 * scripts, and it had FOUR different opinions about what "true" spells:
 * jc_json_get_bool_lenient (true/yes/1, case-insensitive), three hand-rolled
 * `strcmp(str, "true")` readers in the YAML frontmatter path, and `config set`'s
 * own validator, which blesses `on`/`off` -- a spelling no reader accepted. A
 * boolean's meaning must not depend on which file it lives in.
 *
 * Accepts, case-insensitively: true/false, yes/no, on/off, 1/0. `on`/`off` are in
 * because jichi's own `config set` writes them; a spelling the tool blesses must
 * be a spelling the tool reads. Anything else -- prose, an empty string -- is NOT
 * a boolean and leaves *out untouched, so a caller's default survives and a typo
 * can never flip a fence in either direction.
 *
 * Returns 1 if `s` was a boolean word (and writes it to *out), else 0. Pure. */
int jc_bool_from_word(const char *s, int *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_STR_H */
