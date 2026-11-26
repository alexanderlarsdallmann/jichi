/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gatesweep.h - the `gate run --stage sweep` body (M1154, batch H9b,
 * wave 2, task 5): the C89 port of the 2026-10-27 hardening-host drive's
 * sweep.sh. The stage runs in a scratch clone of push.git at exactly SHA8:
 * build, the keyless driver loop, the smoke-mutant, the two unit suites, the
 * two-userns container drivers, and the records line -- emitting the SAME
 * bytes the reference emits for the same inputs, with the note's behaviour 3
 * CHANGED lines (the `  build: <sha8>` line, the keys unset in the CHILD env
 * rather than the shell, the unit/container commands shimmable).
 *
 * The PURE line-assembly (the build line, the count line's red-join, the
 * mutant line's first-`^mutant:` extract, the unit line's last-`N checks, M
 * failures` extract, the container line's ok/not-ok count + SKIPPED arm, the
 * records line) is exported below and unit-tested by tests/test_gatesweep.c;
 * the git clone + the child spawns (the driver loop, the build/mutant/unit/
 * container commands) live in jc_hp_gatesweep.c and run through a
 * jc_hp_git_run-shaped git helper + a generic keyless child-runner. */
#ifndef JC_HP_GATESWEEP_H
#define JC_HP_GATESWEEP_H

#include <stddef.h>

#include "jc_mem.h"       /* struct jc_arena (the stage entry) */
#include "jc_platform.h" /* JC_OK */
#include "jc_str.h"     /* struct jc_sb */
#include "jc_vec.h"     /* struct jc_vec */

#ifdef __cplusplus
extern "C" {
#endif

/* --- the PURE line-assembly (exported for the unit table) -----------------
 * Each takes the stage's input (the driver/mutant/unit/container command's
 * captured stdout, the rc, the SHA8, the origin/master sha) and assembles the
 * exact line(s) the stage prints. No git, no I/O. A returned line is written
 * into the caller's `out` buffer (NUL-terminated); the function returns the
 * number of bytes that WOULD have been written (snprintf semantics, including
 * the NUL) or -1 on truncation. The caller checks the return. */

/* The build line (the note's CHANGED: the reference's
 * `  <./jichi --version | grep build>` becomes `  build: <sha8>`; sweep.sh:17).
 * out receives `  build: <sha8>\n`. */
int jc_gatesweep_build_line(const char *sha8, char *out, size_t cap);

/* The count line (sweep.sh:26): `sweep: ran N drivers on <sha8>;
 * red:<space-joined basenames | none>`. `bad` is the space-joined set of red
 * driver basenames ("" for none). N is the driver count. out receives the
 * full line with trailing newline. */
int jc_gatesweep_count_line(int n, const char *sha8, const char *bad,
                            char *out, size_t cap);

/* A single red driver's `  RED <b> rc=<rc>` line (sweep.sh:23, the first of
 * its two print statements). out receives `  RED <b> rc=<rc>\n`. */
int jc_gatesweep_red_line(const char *b, int rc, char *out, size_t cap);

/* The driver's `not ok` excerpt: the first two `^not ok` lines of `text`,
 * each cut to 200 bytes, joined by '\n' with a trailing '\n' (sweep.sh:23,
 * the grep | head -2 | cut -c1-200 pipe). `out` is written; returns the byte
 * count (snprintf semantics) or -1 on truncation. Empty (0) when there is no
 * `^not ok` line. */
int jc_gatesweep_notok_excerp(const char *text, char *out, size_t cap);

/* The smoke-mutant line (sweep.sh:35): `  smoke-mutant rc=<r>
 * (MUTANT_BASE=<%.8s of mb>): <the FIRST ^mutant: line of text, cut 150>`.
 * `mb` is the full origin/master sha (it is cut to 8 here). out receives the
 * full line with trailing newline. When mb is absent the caller prints the
 * INVALID line instead (jc_gatesweep_mutant_nomb_line). */
int jc_gatesweep_mutant_line(int r, const char *mb, const char *text,
                             char *out, size_t cap);

/* The no-origin/master INVALID line (sweep.sh:38): `  smoke-mutant: no
 * origin/master in push.git: INVALID\n`. */
int jc_gatesweep_mutant_nomb_line(char *out, size_t cap);

/* The mutant's `not ok` excerpt: the first four `^not ok` lines of `text`,
 * each cut 150, each indented four spaces (sweep.sh:36, the
 * grep | head -4 | cut -c1-150 | sed 's/^/    /' pipe). Returns the byte
 * count or -1. */
int jc_gatesweep_mutant_notok(const char *text, char *out, size_t cap);

/* A unit line (sweep.sh:51): `  unit <v> rc=<r> <the LAST line of `text`
 * matching `[0-9]+ checks, [0-9]+ failures`, else `NO SUMMARY (build failed
 * or aborted)`>`. `v` is "san" or "o2". out receives the full line with a
 * trailing newline. */
int jc_gatesweep_unit_line(const char *v, int r, const char *text,
                           char *out, size_t cap);

/* The unit's FAIL/ASan/runtime-error excerpt (sweep.sh:52): the first three
 * lines of `text` matching `^ *FAIL |ERROR: AddressSanitizer|runtime error`,
 * each cut 200, each indented four spaces. Returns the byte count or -1. */
int jc_gatesweep_unit_excerp(const char *text, char *out, size_t cap);

/* A container line (sweep.sh:66): `  container <d> rc=<r> <ok> ok, <not ok>
 * not ok< -- SKIPPED: INVALID (<skip line>)?>`. `ok_count` / `not_count` are
 * the grep -c results for `^ok` / `^not ok` in the driver's output; `skip`
 * (may be "") is the first line matching `^1\.\.0|# skip`. out receives the
 * full line with a trailing newline. */
int jc_gatesweep_container_line(const char *d, int r, int ok_count,
                                int not_count, const char *skip,
                                char *out, size_t cap);

/* The podman-absent container INVALID (the note's behaviour 3 CHANGED: no
 * --container-run and no podman on PATH -> the lines are
 * `  container <d> rc=1 0 ok, 0 not ok -- SKIPPED: INVALID (podman not
 * available)`). out receives the full line with a trailing newline. */
int jc_gatesweep_container_podman_line(const char *d, char *out, size_t cap);

/* The container's `not ok` excerpt (sweep.sh:67): the first two `^not ok`
 * lines of `text`, each indented four spaces (grep | head -2 | sed). */
int jc_gatesweep_container_notok(const char *text, char *out, size_t cap);

/* The records line (sweep.sh:70): `records: <out>\n`. */
int jc_gatesweep_records_line(const char *out, char *outbuf, size_t cap);

/* The WORKDIR + CMD joined command string (for unit testing).
 * `out` receives `cd '<workdir>' && <cmd>` when workdir is non-empty,
 * or `<cmd>` when workdir is NULL/empty. Returns 0 on success, -1 on overflow.
 * CHANGED in M1221: workdir is single-quoted to prevent word-splitting. */
int jc_gatesweep_build_cmd(const char *workdir, const char *cmd,
                           char *out, size_t cap);

/* The build-failure INVALID (sweep.sh:16's || branch): `build failed:
 * INVALID\n`. */
int jc_gatesweep_buildfail_line(char *out, size_t cap);

/* --- F2 (M1154 T6): the keyless-env pin (the git READ children's env) -----
 * The review found the stage's rev-parse child (gsw_git -> jc_hp_git_run,
 * no credential env) leaking ZD_TOKEN_FILE -- the one name jc_proc's
 * built-in scrub lacks. The fix routes it through the keyless child runner;
 * these two exports pin that env for the unit table. */

/* The 16 keyless names (gsw_keys): `out` receives the name strings
 * (pointers into the static table); returns the count (16), -1 when `cap`
 * is short. */
int jc_gatesweep_key_names(const char **out, int cap);

/* The env the git READ children receive, as "KEY=VALUE" entries pushed
 * into the caller-initialized `out` (the entries persist in `a`). 0 on
 * success, -1 on a bad argument / OOM. */
int jc_gatesweep_gitread_env(struct jc_arena *a, struct jc_vec *out);

/* --- the stage entry (the dispatch caller is jc_harness_main.c's gate arm) -
 * `s` is the SHA8 the sweep runs on (7-40 lowercase hex; the stage ECHOES it
 * into its lines and does not resolve it to the full sha -- the orchestrator,
 * wave 4, resolves and passes the full sha). The clone/driver-loop/mutant/
 * unit/container homes come from `work` (the --work flag, else
 * /tmp/jhp-sweep-XXXXXX) and `records` (the --records flag, else <work>/
 * records). `bin`/`build_run`/`mutant_run`/`unit_run_san`/`unit_run_o2`/
 * `container_run` are the shimmable commands (NULL = the note's default: a
 * stub binary, a stub build, the system make/podman). `push_repo` is the bare
 * push.git the clone comes from (REQUIRED; NULL/empty -> exit 2 naming
 * --push-repo). All spawn children run with the API-key envs scrubbed (the
 * note's KEYS UNSET, behaviour 3). rc: 0 (the sweep completed -- every line
 * printed, the records line last), 1 (a build failure OR no origin/master OR a
 * clone failure -- the INVALID marker was printed), 2 (usage: no config /
 * missing --push-repo / a wrong positional), 1 on OOM. */
int jc_hp_gatesweep_run(const char *config_path,
                        const char *s,
                        const char *work,
                        const char *records,
                        const char *bin,
                        const char *build_run,
                        const char *mutant_run,
                        const char *unit_run_san,
                        const char *unit_run_o2,
                        const char *container_run,
                        const char *push_repo,
                        struct jc_arena *a);

#ifdef __cplusplus
}
#endif

#endif /* JC_HP_GATESWEEP_H */
