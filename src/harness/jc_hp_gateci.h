/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateci.h - the `gate run --stage ci` body (M1154, batch H9b,
 * wave 3, task 8): the C89 port of the 2026-10-27 hardening-host drive's
 * ci-gate.sh. The stage runs `make ci` -- or the shimmable --ci-run
 * command -- UNCAPPED on exactly SHA8, in a FRESH clone of push.git
 * (never the team's tree): the dead clone is MOVED ASIDE, the checkout
 * is verified byte-for-byte, MUTANT_BASE is the merge-base of
 * refs/remotes/origin/master and SHA8, the run's log + meta carry the
 * reference's bytes, and the stage's assembled stdout (the record) ends
 * with the gate-run.sh:77-79 lines (the rc line, the RED half with the
 * SV:1238 excerpt attach) + the records line.
 *
 * The CHANGED set (documented in the .c header, the port's rule: every
 * behaviour is kept unless the note names the change):
 *   :13-14/:18 -- a clone home left by a dead gate is MOVED ASIDE to
 *     <home>.dead-<UTC yyyymmdd-HHMMSS> (gate-run.sh:72; nothing removed),
 *     with the marker `  ci: moved a dead clone aside: <home> -> <new>` on
 *     the record, instead of the reference's exit-1 refusal;
 *   :20-25     -- the refusal / diagnostic bytes lose the `ci-gate.sh: `
 *     prefix EXCEPT the two KEPT lines (ci-gate.sh:24's GitLab-master
 *     warning and :25's MUTANT_BASE line -- the reference's own
 *     diagnostic sentences, verbatim); an exit-1 becomes the INVALID
 *     marker class (M1154.b11: a fault in the gate's own tools is never
 *     a stage RED), and the two >&2 lines go to the stage RECORD (the
 *     sweep idiom: the record is the orchestrator's input and journal);
 *   :26        -- the short TMPDIR is <work>/tmp (the reference's
 *     /tmp/jfx-ci-$C; the orchestrator passes the work home, the
 *     SHORT-DIR reason stands -- a long one breaks the Unix-socket
 *     drivers);
 *   :28        -- NO setsid (the port's child IS the unit's process; the
 *     reference's setsid -w kept the run alive past a log-out, which the
 *     unit's systemd-run already owns), NO timeout (the drive's rule: a
 *     cap that fires manufactures a plausible different answer); the
 *     16 key names are scrubbed in the CHILD env (the sweep's gsw_run
 *     idiom) PLUS TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING;
 *   :31        -- the reference's `cat meta; grep FAILED; tail -3` is the
 *     ORCHESTRATOR's diagnostic half (gate-run.sh:74-81 folds them); it
 *     is NOT ported into the stage. The record instead carries the
 *     note's b6 lines: the gate-run.sh:77 `ci: rc=` say (CHANGED into
 *     the record) and, on FAIL (rc != 0 OR no `ci: OK` line), the RED
 *     half with the SV:1238 ask-3 EXCERPT attach (<log-dir>/ci-<s>.
 *     excerpt, written only on FAIL -- the port's own addition).
 *
 * The PURE seam (the line assembly, the RED join, the excerpt block
 * selection, the KEPT-prefix diagnostics, the run env) is exported
 * below, unit-tested by tests/test_gateci.c, and byte-pins the
 * gate-run.sh:77-79 / ci-gate.sh:24-25 shapes. */
#ifndef JC_HP_GATECI_H
#define JC_HP_GATECI_H

#include <stddef.h>

#include "jc_mem.h"       /* struct jc_arena (the stage entry) */
#include "jc_vec.h"       /* struct jc_vec (the run env) */

#ifdef __cplusplus
extern "C" {
#endif

/* --- the PURE seam (exported for the unit table) ---------------------------
 * The line builders write NUL-terminated lines into the caller's buffer and
 * return the number of bytes that WOULD have been written (snprintf
 * semantics, including the NUL) or -1 on truncation (the sweep idiom). */

/* The gate-run.sh:77 rc line (CHANGED into the record): `  ci: rc=<r>
 * <the LAST ^ci: OK line of the log | no 'ci: OK' line>`. `okl` is "" when
 * the log has no `^ci: OK` line (the reference's `${okl:-no 'ci: OK' line}`
 * -- the inner single quotes are its own). out receives the full line with
 * a trailing newline. */
int jc_gateci_rc_line(int r, const char *okl, char *out, size_t cap);

/* The RED half's line join (gate-run.sh:79's EXACT bytes): the first four
 * lines of `log` matching `^smoke: FAILED --` OR `^not ok` OR `^\*\* \[` OR
 * `^ *FAIL `, each cut to 160 bytes, joined by ';' with no added space --
 * the reference's `| cut -c1-160 | tr '\n' ';'` (every matched line ends
 * in a newline, so the join carries a TRAILING ';' and is EMPTY when
 * nothing matches -- the cell's double space is the reference's own).
 * out receives the join (no trailing newline). */
int jc_gateci_red_join(const char *log, char *out, size_t cap);

/* The full RED line (the note's b6 CHANGED, SV:1238 ask 3: the reference's
 * `red ci` text byte-for-byte + the excerpt attach appended):
 * `  ci: RED make ci rc=<r>. <join> Log on the host: <log path>. (excerpt:
 * <excerpt path>)\n`. `<join>` is jc_gateci_red_join's result (the reference
 * substitutes it with its surrounding space; an empty join is the
 * reference's double space). */
int jc_gateci_red_line(int r, const char *log, const char *log_path,
                       const char *excerpt_path, char *out, size_t cap);

/* The excerpt block selection: every `^not ok` line of `log`. `starts` /
 * `counts` (both of length `cap`): for the i-th match, the byte offset in
 * `log` of that line's start and the number of lines in its block (the ONE
 * line before it + the line itself + up to the TWENTY lines after it,
 * clipped at the log's end). Returns the match count, -1 when a match
 * would exceed `cap`. The excerpt TEXT is then the blocks in log order,
 * each line cut to 300 bytes, separated by one blank line
 * (jc_gateci_excerpt_text). */
int jc_gateci_excerpt_blocks(const char *log, long *starts, int *counts,
                             int cap);

/* The excerpt file's content: jc_gateci_excerpt_blocks' blocks as text
 * (the one-before / twenty-after windows, each line cut to 300 bytes,
 * blocks separated by one blank line, trailing newline). The writer's
 * pure half -- the file itself is written by the stage on FAIL only.
 * Empty (0) when there is no `^not ok` line. */
int jc_gateci_excerpt_text(const char *log, char *out, size_t cap);

/* The excerpt file's STREAMING writer (F2: the pre-F2 64KB
 * `exc[GCI_EXC_CAP]` text buffer is gone -- the file is written one line
 * at a time and is unbounded in size): the SAME blocks as
 * jc_gateci_excerpt_text (for every `^not ok` line, the ONE line before
 * it + the line + up to the TWENTY lines after it, each cut to 300 bytes,
 * the blocks in log order separated by one blank line), written to `path`
 * (create/truncate). `log` is the log TEXT (the stage passes its read
 * .log). JC_OK on success (an empty excerpt still creates and truncates
 * the file), -1 on a write fault. The stage uses it on FAIL only. */
int jc_gateci_excerpt_file(const char *log, const char *path);

/* The ci-gate.sh:24 KEPT-prefix warning (verbatim, do not strip):
 * `ci-gate.sh: GitLab master <B8> is not an ancestor of <s>; the sweep
 * uses their merge-base\n`. `b8` is origin/master's 8-hex; `s` is the
 * SHA8 as given. */
int jc_gateci_mergebase_warn(const char *b8, const char *s, char *out,
                             size_t cap);

/* The ci-gate.sh:25 KEPT-prefix MUTANT_BASE line (verbatim, the
 * reference's own printf: the FULL 40-hex MB, the 8-hex B):
 * `ci-gate.sh: MUTANT_BASE=<mb> (GitLab master as push.sh fetched it:
 * <b8>)\n`. */
int jc_gateci_mutant_base_line(const char *mb, const char *b8, char *out,
                               size_t cap);

/* The 16 keyless names (the sweep's gsw_keys, the unit pin): `out`
 * receives the name strings (pointers into the static table); returns the
 * count (16), -1 when `cap` is short. */
int jc_gateci_key_names(const char **out, int cap);

/* The env the :28 run child receives, as "KEY=VALUE" entries pushed into
 * the caller-initialized `out` (the 16 names set EMPTY -- the keyless
 * scrub -- PLUS TMPDIR=<td>, MUTANT_BASE=<mb> and JC_SMOKE_KEEP_GOING=<n>,
 * in that order; the entries persist in `a`). 0 on success, -1 on a bad
 * argument / OOM. */
int jc_gateci_run_env(struct jc_arena *a, const char *td, const char *mb,
                      const char *n, struct jc_vec *out);

/* --- the stage entry (the dispatch caller is jc_harness_main.c's gate arm) -
 * `s` is the SHA8 (7-40 lowercase hex; echoed into the stage's paths and
 * lines -- the orchestrator, wave 4, passes the full sha). `work` is the
 * --work flag, else an mkdtemp /tmp/jhp-ci-XXXXXX; `records` the --records
 * flag, else <work>/records; `log_dir` the --ci-log-dir flag, else config
 * host.ciLogDir, else <work>/ci. `ci_run` is the --ci-run command (else
 * config host.ciRun, else `make ci`); `keep_going` the
 * --ci-smoke-keep-going value (else config host.ciSmokeKeepGoing, else
 * "0" -- a non-numeric value is a usage refusal exit 2 naming the flag:
 * the loader does not refuse, the command does). `push_repo` is the bare
 * push.git the clone comes from (REQUIRED: flag, else config
 * host.pushRepo, else exit 2 naming --push-repo). The child runs with the
 * 16 key names scrubbed + TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING,
 * workdir = the clone, stdin /dev/null, stdout+stderr -> the .log, NO
 * setsid, NO timeout. rc: 0 when the run finished (the stage COMPLETED --
 * the record carries the words, the ORCHESTRATOR decides RED/GREEN), 1 on
 * an INVALID marker (a clone/checkout fault, steps 4-5, or a work/log/
 * records home that could not be made), 2 on usage. The record's last
 * line is `records: <records-dir>` (the sweep idiom). */
int jc_hp_gateci_run(const char *config_path,
                     const char *s,
                     const char *work,
                     const char *records,
                     const char *log_dir,
                     const char *ci_run,
                     const char *keep_going,
                     const char *push_repo,
                     struct jc_arena *a);

#ifdef __cplusplus
}
#endif

#endif /* JC_HP_GATECI_H */
