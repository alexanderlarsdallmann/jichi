/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gate.h - the `gate run <SHA8> [BATCH]` orchestrator (M1159, batch H9b
 * wave 4): the C89 port of the 2026-10-27 hardening-host drive's gate-run.sh,
 * CORE PATH. The full run captures the ledger, runs the sweep, says the
 * verdict, writes the state file and delivers the note for one READY commit
 * (the design: docs/internal/drive/2026-11-05-harness-port/notes/M1159.md
 * D1-D6, D8-D11, D14; the driver: tests/smoke/harness_gate_run.sh's G-cells).
 *
 * The five stage children (ledger / sweep / verdict / redfirst / ci) are the
 * ALREADY-MERGED stage bodies, re-run as their own dispatch forms through a
 * `sh -c` self-exec (D1, the reference's `sh <stage>.sh ... > <record> 2>&1`
 * shape; <self> = readlink /proc/self/exe, the jc_hp_watchunits.c:94 idiom)
 * -- NOT in-process, so their flag-else-config defaults, their usage
 * refusals and their records all stay exactly as the driver pins them alone.
 *
 * WHAT THIS WAVE (Task B) DELIVERS: the usage refusals, the keyless setup
 * (D8), the `gated` word (D9), the say/state/note machinery (D6), the stage
 * children (D1/D2), the RED arms (red-setup / red-not-ff / red-ledger /
 * red-sweep / red-ci), the INVALID promotion (D10: invalid-sweep /
 * invalid-redfirst / invalid-ci / the stage-child-rc-2 rule) and the
 * terminal green-land-refused arm (the only green arm).
 *
 * WHAT THIS WAVE DELIBERATELY DOES NOT DO (M1159 D14, and Task C's): the
 * HOLD arm (a pre-existing <gate-home>/HOLD is a NAMED refusal, exit 1, so
 * the land stubs a cell may have named are never run), and the land / repin
 * steps (host.landRun / host.repinRun are READ but, when present, refuse
 * naming that H10 / H11's switch-over lands in wave 4 Task C) -- so every
 * fully-green run TERMINATES in green-land-refused, exit 1.
 */
#ifndef JC_HP_GATE_H
#define JC_HP_GATE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>   /* size_t (jc_capture_cmd) */
#include "jc_mem.h"  /* struct jc_arena */

/* Run the full `gate run <SHA8> [BATCH]` (the orchestrator's CORE PATH,
 * M1159 wave 4).
 *
 * `config_path` is REQUIRED (the dispatch's arm checks it first). `sha8` is
 * the READY commit (7-40 lowercase hex; the stage children ECHO it into
 * their lines). `batch` is the BATCH name or NULL (the notes and the
 * `gate:` say name `<batch>`, `?` when absent -- the reference's
 * ${2:-?}).
 *
 * The resolved homes: G = <gate-home>/<sha8> (the per-sha record home, D3);
 * the ci log dir is FLAT, <gate-home>/ci (the reference's $LOGDIR).
 * gate-home: the `gate_home` flag, else the config's host.gateHome, else
 * <dir>/gate (the notify's existing default). The state file: the `state`
 * flag, else <gate-home>/state (D6: verdict appends <sha8> <word> <ts>,
 * NEVER truncating). The push repo: `push_repo`, else host.pushRepo,
 * REQUIRED (the setup's own keyless git children read it, D8).
 *
 * `ledger_address` (--ledger-address) / `ledger_accept` (--ledger-accept)
 * are REQUIRED / optional: the address must be present in flag OR config
 * (host.ledgerAddress) -- the b1 CHANGED REQUIRED refusal, exit 2, before
 * any git read.
 *
 * The `*_run` / `*_dir` / `gate_home` / `state` flags are the forwarded /
 * resolved shimmable surfaces (NULL = absent: the stage child then uses its
 * OWN flag-else-config default, D2). `land_run` / `repin_run` are read
 * (flag else config host.landRun / host.repinRun) but this wave refuses
 * them as named (Task C's arms are not built).
 *
 * Exit classes (M1159 D4, the driver's G-cells):
 *   2  a usage refusal (no ledger address, a positional-count problem --
 *      the dispatch's arms, before this call; and the body's own no-push-
 *      repo refusal) -- always before any state word / note.
 *   1  a RED verdict (red-setup / red-not-ff / red-ledger / red-sweep /
 *      red-ci: the note is delivered, the state word written) or the
 *      terminal green-land-refused (no land step named, this wave's
 *      refusal) or a named HOLD / land / repin refusal.
 *   0  an INVALID verdict (invalid-sweep / invalid-redfirst /
 *      invalid-ci / the stage-child-rc-2 rule, D10): the gate's own tool
 *      faulted, the batch is not judged -- the note says so, exit 0.
 *   1  an OOM / record-write fault (named on stderr).
 *
 * The order is ALWAYS state word -> say -> note (b14): a crash between
 * steps leaves a state word the watcher can stand on. The say lines go to
 * <G>/run.log AND stdout (b12 CHANGED: the reference's say is run.log
 * only). The notes are delivered IN-PROCESS through jc_harness_notify
 * (b13); a non-zero notify rc is named, never fatal.
 *
 * `a` owns every string read; the caller creates and frees it (the
 * dispatch's fresh-arena-per-arm idiom). */
int jc_hp_gate_full_run(const char *config_path, const char *sha8,
                        const char *batch,
                        const char *gate_home, const char *state,
                        const char *push_repo,
                        const char *ledger_address,
                        const char *ledger_accept,
                        const char *bin,
                        const char *build_run,
                        const char *mutant_run,
                        const char *unit_run_san,
                        const char *unit_run_o2,
                        const char *container_run,
                        const char *redfirst_run,
                        const char *redfirst_unit_run,
                        const char *records,
                        const char *ci_run, const char *ci_log_dir,
                        const char *ci_smoke_keep_going,
                        const char *land_run, const char *repin_run,
                        struct jc_arena *a);

/* The gate's land/repin step capture, exported (M1202 D3, the approve verb):
 * run `line` as `sh -c '<line>'` (stdin /dev/null), capturing the child's
 * COMBINED stdout+stderr (up to cap bytes, trailing newline trimmed) and its
 * rc -- the exact spawn the orchestrator's land step uses. The approve verb
 * runs the land step it resolves through this, so the capture is byte-for-
 * byte the gate's (the env scrub, the combined output, the trim). `line` is
 * the FULL command (the caller has already appended the sha8 and substituted
 * `<s>`); it is NOT substituted again here. 1 on success (*out_text the
 * combined output, *out_rc the child's rc), 0 on a spawn fault (out_rc stays
 * 0; the caller names it). */
int jc_capture_cmd(const char *line, char *out_text, size_t cap, int *out_rc);

#ifdef __cplusplus
}
#endif
#endif /* JC_HP_GATE_H */
