/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateref.h - the `gate run --stage redfirst` body (M1157, batch H9b,
 * wave 3, task 9): the C89 port of the 2026-10-27 hardening-host drive's
 * redfirst.sh. The stage replays the red-first commits of <BASE>..<HEAD> from
 * the push repo (UNSTUBBED selection, shimmed replay via --redfirst-run /
 * --redfirst-unit-run, per-check verdict words, and the summary line). The
 * stage is INFORMATIONAL: a GREEN is a question for the reviewer, never a
 * gate verdict. rc 0 = the stage COMPLETED (the record carries the words),
 * 1 = an INVALID marker, 2 = usage. The record's last line is `records:
 * <records-dir>` (the sweep/ci idiom).
 *
 * The PURE seam (selection, verdicts, line assemblers, summary) is exported
 * below and unit-tested by tests/test_gateref.c. */
#ifndef JC_HP_GATE_REF_H
#define JC_HP_GATE_REF_H

#include <stddef.h>

#include "jc_mem.h" /* struct jc_arena (the stage entry) */

#ifdef __cplusplus
extern "C" {
#endif

/* --- the PURE seam (exported for the unit table) --------------------------- */

/* The red-first SELECTION (redfirst.sh:28-33, the reference's rules byte for
 * byte). Given the file-list text (the `git diff-tree --name-only -r` output,
 * one path per line): a commit is RED-FIRST iff (a) EVERY changed file is
 * under `tests/` or `docs/internal/` (the `grep -v -E '^(tests/|docs/internal/)'
 * | head -1` form -- a first non-matching line excludes it) AND (b) at least
 * one changed file is a driver (`tests/smoke/[^/]+\.sh`, excluding
 * `/(lib|common)[^/]*\.sh`, `/run\.sh`, `/_[^/]*\.sh`) or a unit test
 * (`tests/test_[^/]+\.c`).
 *
 * `drivers_out`: the space-joined list of qualifying driver basenames (without
 * the .sh), in the reference's order (the file-list order filtered); "" for
 * none. `first_unit_out`: the first qualifying unit-test path (the reference
 * takes the FIRST for the build trigger); "" for none. `all_units_out`: the
 * space-joined list of ALL qualifying unit-test paths (the reference's
 * `grep -E '^tests/test_'` for the changed: list); "" for none. `redfirst`
 * is set to 1 when the commit qualifies, 0 otherwise.
 *
 * out buffers are NUL-terminated. An over-cap list is CLAMPED (cut to
 * cap-1), not an error: the call returns 0 on success (clamped or not);
 * only a NULL argument or an OOM returns -1. */
int jc_gateref_select(const char *file_list,
                      char *drivers_out, size_t drv_cap,
                      char *first_unit_out, size_t fu_cap,
                      char *all_units_out, size_t au_cap,
                      int *redfirst);

/* The DRIVER verdict (redfirst.sh:43-46). Given the captured output text and
 * the child's rc: `word_out` receives "RED", "GREEN" or "SKIP"; `ok_out` the
 * count of `^ok` lines; `no_out` the count of `^not ok` lines. SKIP: the text
 * matches `^1\.\.0 *# *skip` or `# SKIP` (case-insensitive) AND no == 0 AND
 * ok == 0. RED: rc != 0 OR no > 0. GREEN: otherwise. Returns 0 on success. */
int jc_gateref_driver_verdict(const char *text, int rc,
                              char *word_out, size_t word_cap,
                              int *ok_out, int *no_out);

/* The UNIT verdict (redfirst.sh:51-55). Given the captured output text and
 * the child's rc: `word_out` receives "RED", "GREEN" or the NO SUMMARY
 * sentence; `sm_out` receives the full last line matching
 * `[0-9]+ checks, [0-9]+ failures` ("" when none). RED: the failure count > 0.
 * GREEN: failure count == 0. NO SUMMARY (counted RED): sm is empty. The
 * NO SUMMARY word_out is `NO SUMMARY (rc=<r>: the suite did not finish -- a
 * crash or a build error)`. Returns 0 on success. */
int jc_gateref_unit_verdict(const char *text, int rc,
                            char *word_out, size_t word_cap,
                            char *sm_out, size_t sm_cap);

/* The BUILD FAILED line (redfirst.sh:37): `<s8> BUILD FAILED at the red-first
 * commit (<subj>): see <out>/<s8>-build.log\n`. */
int jc_gateref_buildfail_line(const char *s8, const char *subj,
                              const char *out,
                              char *line, size_t cap);

/* The CHECKOUT FAILED line (redfirst.sh:35): `<s8> CHECKOUT FAILED\n`. */
int jc_gateref_checkoutfail_line(const char *s8, char *line, size_t cap);

/* The DRIVER line (redfirst.sh:47): `<s8> <WORD> driver <b> (rc=<r>, <ok> ok,
 * <no> not ok) -- <subj>\n`. */
int jc_gateref_driver_line(const char *s8, const char *word,
                           const char *b, int rc, int ok, int no,
                           const char *subj, char *line, size_t cap);

/* The UNIT line (redfirst.sh:56): `<s8> <WORD> unit suite (<sm>; changed:
 * <all matching tests/test_ files, space-joined> ) -- <subj>\n`. The space
 * before the closing paren is the reference's `tr '\n' ' '` on the changed
 * list (its final newline becomes a space); it is always present, because
 * the line is only emitted when a unit test was selected (a non-empty list).
 */
int jc_gateref_unit_line(const char *s8, const char *word,
                         const char *sm, const char *changed,
                         const char *subj, char *line, size_t cap);

/* The SUMMARY line (redfirst.sh:59): `redfirst: <n> red-first commit(s), <nr>
 * red, <ng> green, <ns> skip\n`. n = red-first COMMITS (not checks); nr/ng/ns
 * count CHECKS (a commit with 1 driver + 1 unit can add to two counters). */
int jc_gateref_summary_line(int n, int nr, int ng, int ns,
                            char *line, size_t cap);

/* The records line (the sweep/ci idiom): `records: <dir>\n`. */
int jc_gateref_records_line(const char *dir, char *line, size_t cap);

/* The WORKDIR + CMD joined command string (for unit testing).
 * `out` receives `cd '<workdir>' && <cmd>` when workdir is non-empty,
 * or `<cmd>` when workdir is NULL/empty. Returns 0 on success, -1 on overflow.
 * CHANGED in M1221: workdir is single-quoted to prevent word-splitting. */
int jc_gateref_build_cmd(const char *workdir, const char *cmd,
                         char *out, size_t cap);

/* --- the stage entry (the dispatch caller is jc_harness_main.c's gate arm) -
 * `base` and `head` are each 7-40 lowercase hex (the stage ECHOES them into
 * its lines; the range is resolved to the commits by real git, the
 * UNSTUBBED selection). `work` is the --work flag, else an mkdtemp
 * /tmp/jhp-ref-XXXXXX; `records` the --records flag, else <work>/records;
 * `out_dir` the --out flag, else <work>/out. `redfirst_run` is the
 * --redfirst-run command (else config host.redfirstRun, else the reference's
 * real forms: the BUILD child `make -j8 WERROR=1 jichi smoke-tools`, the
 * DRIVER child `TMPDIR=<short> sh <d>`); `unit_run` the --redfirst-unit-run
 * command (else config host.redfirstUnitRun, else `make -j8 WERROR=1 test`).
 * `push_repo` is the bare push.git the clone comes from (REQUIRED: flag, else
 * config host.pushRepo, else exit 2 naming --push-repo). The child env
 * carries the 16 keyless names set EMPTY (the sweep/ci idiom). rc: 0 when
 * the replay finished (the stage COMPLETED), 1 on an INVALID marker (the
 * setup/clone/rev-list faults, or a work/out/records home that could not be
 * made), 2 on usage (no config, missing --push-repo, a wrong positional, or a
 * non-hex BASE/HEAD). The record's last line is `records: <records-dir>`. */
int jc_hp_gateref_run(const char *config_path,
                      const char *base,
                      const char *head,
                      const char *work,
                      const char *records,
                      const char *out_dir,
                      const char *redfirst_run,
                      const char *unit_run,
                      const char *push_repo,
                      struct jc_arena *a);

#ifdef __cplusplus
}
#endif

#endif /* JC_HP_GATE_REF_H */
