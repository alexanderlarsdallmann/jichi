/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateverdict.h - the `gate run --stage verdict` body (M1154, batch
 * H9b, wave 1, task 3): the C89 port of the 2026-10-27 hardening-host
 * drive's sweep-verdict.sh (read line by line: :15-16 the argv + the repo
 * default, :18 the g() helper, :19-20 the build + sweep lines, :21-50 the
 * smoke-mutant arm with the 07:50 second count and its 14:47 uses_bin
 * selector, :51-54 the unit + container lines, :55-56 the flagged pipe,
 * :57-58 the green / red output).
 *
 * `jc_hp_gv_run` is the dispatch entry: it loads the config, resolves the
 * second-count repo (--push-repo wins; the config's host.pushRepo is the
 * absent-flag default; both absent = the reference's own missing-repo
 * behaviour, a git failure in the count arm, never a skip), reads the
 * sweep file, classifies its lines, runs the second count (git children
 * through jc_hp_git_run, T2's gl_git shape), prints `green<note?>` (rc 0)
 * or `red: <reasons>` (rc 1) -- the SAME bytes as the reference for the
 * SAME input. Usage refusals (no config, a bad config) are rc 2.
 *
 * The classification core (the per-stage line patterns, the flagged pipe,
 * the note assembly) is PURE (no git, no I/O) and exported for the unit
 * table (tests/test_gateverdict.c); the driver's V1-V12 cells hold the
 * end-to-end arms over a real git fixture. */

#ifndef JC_HP_GATEVERDICT_H
#define JC_HP_GATEVERDICT_H

#include <stddef.h>       /* size_t (the note / reason caps) */

#include "jc_mem.h"       /* struct jc_arena (the stage entry, the union) */
#include "jc_vec.h"       /* struct jc_vec (the smoke-driver union) */

/* The three arms the smoke-mutant line can take (sweep-verdict.sh:21-50):
 * GV_MUT_OK -- a `  smoke-mutant rc=0 .*mutant: OK` line is present
 * (clean, no second count); GV_MUT_ARM -- the no-changed-drivers line
 * `  smoke-mutant rc=0 (MUTANT_BASE=<hex 7-40>): mutant: no changed
 * drivers to sweep (0 excluded)` is present (the second count decides:
 * agree -> green with its note, disagree / git fault -> red);
 * GV_MUT_MISSING -- neither line (the plain "smoke-mutant" reason). */
enum { GV_MUT_OK = 0, GV_MUT_ARM, GV_MUT_MISSING };

/* What one pass over the sweep file found (sweep-verdict.sh's per-stage
 * g() calls, :19-54, + the flagged grep set, :55). `mut_mb` carries the
 * MUTANT_BASE hex of the FIRST no-changed-drivers line (the reference's
 * sed extraction on its matched line); `mut_nex` carries that line's
 * `(<n> excluded)` count (the reference's :31 sed extraction, M1187-b:
 * ANY digit run, not the literal zero); `flagged` carries the FIRST
 * THREE lines matching `INVALID|FAILED|SKIPPED|^  RED ` (cut to 120
 * bytes each, the head -3 / cut -c1-120 halves of the :55 pipe). */
struct jc_hp_gv_class {
    int build;                  /* a `  build: <s>` line */
    int sweep;                  /* a `sweep: ran N>0 drivers on <s>; red: none` line */
    int mut_ok;                 /* a `  smoke-mutant rc=0 .*mutant: OK` line */
    int mut_none;               /* the no-changed-drivers line */
    char mut_mb[64];            /* its MUTANT_BASE hex, "" when absent */
    int mut_nex;                /* its (<n> excluded) count, 0 when absent */
    int san, o2;                /* the two `unit <v> rc=0 N checks, 0 failures` lines */
    int cf, cr;                 /* the two `container <d> rc=0 N>0 ok, 0 not ok` lines */
    char flagged[3][121];       /* the first three flagged lines (120 + NUL) */
    int n_flagged;
};

/* One pass over the sweep file's text (newline-separated, a final
 * newline optional) for SHA8 `s`: set the struct's flags, exactly the
 * reference's per-line grep -E patterns (the anchored ones byte-for-byte,
 * the unanchored ones' first-occurrence / first-three behaviour). Pure. */
void jc_hp_gv_classify(const char *text, const char *s,
                       struct jc_hp_gv_class *c);

/* The reason + note assembly, in the reference's own order
 * (:19-56): build, sweep, the smoke-mutant arm, san, o2, the two
 * containers, the flagged join. `arm` is the smoke-mutant outcome; when
 * it is GV_MUT_ARM the caller supplies the second count: `cnt_fail` (a
 * git diff / diff-tree / show failure, or the repo absent -- the
 * reference's :65-67 arm, M1187-b), `gerr` ("" or " <driver>..." --
 * drivers git could not read, the :52-53 arm), `table_empty` (1 when the
 * EXCLUDE table read as empty, the :54-55 arm), and the counts nall
 * (the files changed from MUTANT_BASE to s, the diff ALONE -- :42) /
 * nsm (the union's smoke scripts, :43-44) / nd (of those the ones that
 * invoke the binary outside comments AND are not excluded, :48-50) /
 * nx (of those the ones that invoke the binary AND are in the EXCLUDE
 * table, :49). Fills `why` in the reference's leading-`;` form ("" when
 * green) and `note` ("" except the :57 green note); returns 1 iff green
 * (why empty). Pure. */
int jc_hp_gv_verdict(const struct jc_hp_gv_class *c, const char *s,
                     int arm, int cnt_fail, const char *gerr,
                     int table_empty,
                     int nall, int nsm, int nd, int nx,
                     char *why, size_t why_cap,
                     char *note, size_t note_cap);

/* The 14:47 selector (sweep-verdict.sh:31-37's inner loop, porting
 * scripts/mutant-sweep.sh's uses_bin): does the driver body INVOKE the
 * binary, not merely mention it? Every line is stripped from its first
 * `#` to end-of-line (the reference's sed -e 's|#.*$||'), and the
 * literal `"$BIN"` is required in what remains -- a `"$BIN"` inside a
 * comment does not count, one in a live line does. Pure. */
int jc_hp_gv_uses_bin(const char *body);

/* The reference's :31 selection over the two name-only captures: the
 * lines matching `^tests/smoke/.*\.sh$`, unioned, deduped and
 * byte-sorted (the sort -u). The out vector (the caller's, sized for
 * `const char *`) receives arena-owned copies in sorted order; returns
 * 0, -1 on OOM. Pure apart from the arena copies. */
int jc_hp_gv_smoke_union(const char *all, const char *own,
                         struct jc_arena *a, struct jc_vec *out);

/* The EXCLUDE-table read (M1187-b, the reference's :35-36 awk over the
 * commit's own scripts/mutant-sweep.sh text, byte-for-byte): at the
 * FIRST line beginning `EXCLUDE='` start (dropping that prefix), emit
 * the rest of that line; then keep whole following lines until a line
 * containing a `'`; the line with the first `'` contributes its prefix
 * before the `'`; stop. Newlines become single spaces (the reference's
 * `tr '\n' ' '`). A second line may carry the closing quote; a text
 * with no `EXCLUDE='` line leaves out empty (the caller's
 * table_empty arm then says red, never a silent green). Returns 0, -1
 * on OOM. Pure apart from the arena copy. */
int jc_hp_gv_excl_parse(const char *text, struct jc_arena *a, char **out);

/* The loop's table membership (the reference's :49 `case " $excl " in
 * *" $(basename "$d" .sh) "*)`): `name` matches the joined names as a
 * WHOLE word -- an occurrence bounded by start-of-string / a space on
 * the left and a space / end-of-string on the right (the reference's
 * added leading space becomes the start-of-string boundary; its added
 * trailing space the end-of-string one). A name that is a PREFIX of
 * another entry (`smoke_lint` vs `smoke_lint_extra`) does not match.
 * An empty table matches nothing. Pure. */
int jc_hp_gv_excl_contains(const char *table, const char *name);

/* The stage entry (the dispatch caller is jc_harness_main.c's gate
 * arm). `push_repo` is the FLAG value (NULL = absent; the config's
 * host.pushRepo is the absent-flag default). Prints the verdict line to
 * stdout; returns 0 green, 1 red, 2 usage (no config / a bad config),
 * 1 on OOM. */
int jc_hp_gv_run(const char *config_path, const char *file, const char *s,
                 const char *push_repo, struct jc_arena *a);

#endif /* JC_HP_GATEVERDICT_H */
