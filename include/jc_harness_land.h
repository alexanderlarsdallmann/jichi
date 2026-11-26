/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_land.h - `jichi-harness land` (M1202, H10, wave 1): the C port of
 * the 2026-10-27 hardening-host drive's land.sh (42 lines). Moves the remote's
 * master to a SHA8 by a credential-wired fast-forward push, under the
 * operator's standing rule (a make ci record with rc=0, a 'ci: OK' line, a
 * green sweep verdict, the sha on the team branch, master a proper ancestor
 * re-read by ls-remote NOW), then refreshes both clones and re-reads.
 *
 * The reference's every refusal string is kept BYTE-FOR-BYTE (the note's
 * b1-b16): the usage refusal is rc 2 (`usage: jichi-harness land <SHA8>`),
 * every policy refusal is rc 1 (`land: refused: ...`), and the exit contract
 * (b16) is rc 0 only after the main checkout's origin/master reads the full
 * sha. The git children run through jc_hp_git_run (the M1056 idiom); the
 * credential-wired ones (b7 ls-remote, b9 fetch fallback, b11 push, b14
 * push-repo fetch) carry the port's OWN credential helper wired against the
 * config's gitTokenFile (D4: never a hand-exported token) -- and every
 * captured git output passes the in-memory redactor (b13, the same two
 * land.sh:18 sed rules) before it is printed or appended to the run log.
 *
 * The verb runs host-side, like the reference (its two pre-b2 refusals --
 * the harness-path and the stored-URL readbacks -- are fail-closed
 * additions the reference does not carry).
 *
 * Pure in the sense that the paths are ALL named by the caller (the five
 * flags --push-repo / --ci-log-dir / --gate-home / --main-checkout /
 * --land-log) and the config names only the gitTokenFile reference + the
 * team-branch prefix: no new config fields in this wave (wave 2 adds the
 * orchestrator half, D1-D3). Returns the verb's rc; never exits.
 */
#ifndef JC_HARNESS_LAND_H
#define JC_HARNESS_LAND_H

#include <stddef.h>           /* size_t */

#include "jc_mem.h"           /* struct jc_arena */

#ifdef __cplusplus
extern "C" {
#endif

/* The b13 redactor (land.sh:18's two sed -E rules, in memory): (i) a
 * credential in a URL, `://user@` (user an `[^@/ ]*` run) -> `://<r>@`;
 * (ii) any 40+ char [A-Za-z0-9_-] run -> `<r>` (a full sha / a leaked
 * token). Applied to every captured git output line before print/append;
 * clean output is byte-unchanged. `src` is read-only (NULL = empty); `dst`
 * is the caller's buffer (NUL-terminated, truncated to cap). Pure. */
void jc_harness_land_redact(const char *src, char *dst, size_t cap);

/* The `land` verb (M1202, H10, wave 1). All eight path / value parameters
 * except sha8 are the caller's named flags (NULL = absent); sha8 is the
 * single positional (b1: absent -> the usage refusal, rc 2). `config_path`
 * is the --config value (NULL = absent -> the load refusal, rc 2). Returns:
 *   0  -- success (b16: after b15, the main checkout's origin/master read)
 *   1  -- every policy refusal (b2-b9, b11, b12, b14, b15)
 *   2  -- the usage refusal (b1) and the load refusals
 */
int jc_harness_land_run(const char *config_path,
                        const char *push_repo, const char *ci_log_dir,
                        const char *gate_home, const char *main_checkout,
                        const char *land_log,
                        const char *sha8, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_LAND_H */
