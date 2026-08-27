/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_kinetic.h - detect a kinetic (physical-actuation) action in the agent's
 * tool stream (M163a).
 *
 * A tool is "kinetic" when invoking it moves mass or energy in the physical
 * world -- a motor, an arm, a valve, a siren. The operator marks such a tool
 * `kinetic: true`; the agent loop then applies a posture (ask / deny / allow)
 * and audits it, exactly as the M153 privileged-command gate does for
 * sudo/doas -- see docs/proposals/2026-07-robotics.md.
 *
 * The flag lives on the TOOL, but a device script is a plain executable the
 * shell tool could invoke directly (`run_terminal_command "./motor.sh 1 1"`),
 * sidestepping the flag. So the gate also SHADOW-MATCHES a shell command
 * against the kinetic tools' own configured commands (plus an operator prefix
 * list). This header holds that pure matcher, mirroring jc_priv's
 * segment-walking discipline (quote-aware; `;`/`&&`/`|`/`$(` segment openers;
 * skips `VAR=` assignments, an `env` prefix, transparent wrappers) with two
 * additions: it also skips INTERPRETERS (`sh motor.sh`, `python3 arm.py`
 * resolve to the script) and matches the leading token BASENAME-tolerantly
 * (`./motor.sh`, `/abs/motor.sh`, `motor.sh` all hit a prefix of `./motor.sh`).
 *
 * Like jc_priv it is a HEURISTIC on the visible command, not a sandbox: it
 * catches a drifting honest model, not an adversary (copied scripts, `cat x|sh`,
 * self-written code, cron persistence are out of scope -- the real last line is
 * OS device permissions + a hardware E-stop). Pure; unit-tested.
 */
#ifndef JC_KINETIC_H
#define JC_KINETIC_H


#ifdef __cplusplus
extern "C" {
#endif
/* True iff any command segment of `command` leads with one of the `n` kinetic
 * `prefixes` (each a NUL-terminated "command [args...]" string). On a hit, when
 * `out_hit` is non-NULL, `*out_hit` is set to the matched prefix entry (not
 * copied). n<=0 / NULL => 0. Pure. */
int jc_kinetic_shell_match(const char *command, const char *const *prefixes,
                           int n, const char **out_hit);

/* True iff `name` exactly equals one of the `n` trimmed `entries` -- the
 * tool-name arm of kineticCommandsAllow (the shell arm reuses
 * jc_priv_allowlisted for prefix + no-chaining semantics). Pure. */
int jc_kinetic_name_allowlisted(const char *name, const char *const *entries,
                                int n);

#ifdef __cplusplus
}
#endif
#endif /* JC_KINETIC_H */
