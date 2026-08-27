/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_priv.h - detect a privileged launcher in a shell command (M152).
 *
 * The agent's shell is an opaque string with no privilege awareness (an
 * incident had a model run `sudo apt-get update && upgrade` unbidden). This
 * pure detector recognizes when a command *segment* is launched under a
 * privilege-escalation tool (sudo/doas/pkexec/su/run0), so the agent loop can
 * apply a posture policy (ask / deny / allow) and audit it -- see
 * docs/proposals/2026-07-privileged-commands.md.
 *
 * It is a HEURISTIC on the visible launcher token, NOT a sandbox. It sees
 * `sudo apt-get`, `x && sudo y`, `foo | sudo tee`, `env X=1 sudo ...`,
 * `sudo -E ...`. It deliberately does NOT unpick quote/escape obfuscation
 * (`s""udo`, `\sudo`), variable indirection (`S=sudo; $S apt`), PATH tricks,
 * or an interpreter that sudos internally (`sh -c '... sudo ...'` -- the inner
 * script is one quoted arg it does not descend into). The value is catching an
 * *incidental* escalation, not defeating an adversary; the real guarantee is
 * running jichi as a non-root user without passwordless sudo. Pure; unit-tested.
 */
#ifndef JC_PRIV_H
#define JC_PRIV_H


#ifdef __cplusplus
extern "C" {
#endif
enum jc_priv_kind {
    JC_PRIV_NONE = 0,
    JC_PRIV_SUDO,
    JC_PRIV_DOAS,
    JC_PRIV_PKEXEC,
    JC_PRIV_SU,
    JC_PRIV_RUN0
};

/* The first privileged launcher that begins a command segment in `command`,
 * or JC_PRIV_NONE. When non-NONE and `out_tok` is non-NULL, `*out_tok` points
 * at the matched launcher token within `command` (not copied). */
enum jc_priv_kind jc_priv_detect(const char *command, const char **out_tok);

/* Stable lowercase name for a kind ("sudo", "doas", ..., "" for NONE). */
const char *jc_priv_kind_name(enum jc_priv_kind k);

/* True iff `command` is pre-approved by an operator allowlist (M153): it
 * exactly equals a trimmed entry, or begins with "<entry> " -- AND the whole
 * command contains no unquoted command-chaining operator (`;`/`&`/`|`/newline/
 * backtick/`$(`). The chaining guard is what stops a `sudo systemctl` entry
 * from admitting `sudo systemctl x ; sudo rm -rf /`. `entries` is `n`
 * NUL-terminated prefix strings; n<=0 or NULL => not allowlisted. Pure. */
int jc_priv_allowlisted(const char *command, const char *const *entries,
                        int n);

#ifdef __cplusplus
}
#endif
#endif /* JC_PRIV_H */
