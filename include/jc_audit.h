/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audit.h - always-on audit of privileged commands (M154).
 *
 * When the agent's shell issues a privileged command (sudo/doas/pkexec/su/
 * run0), the decision -- allowed, denied, refused-unattended, approved -- is
 * appended as one JSON line to ~/.jichi.d/audit/privileged.jsonl,
 * 0600, secret-scrubbed, whether or not opt-in telemetry is on. It is
 * DELIBERATELY independent of jc_eventlog's on/off gating: a security audit
 * must not go dark exactly when someone disables telemetry.
 *
 * This is a self-audit convenience -- the authoritative, tamper-evident record
 * is the OS's (sudo already logs every invocation to the system log; the
 * agent's own Unix user could alter a file it owns). Best-effort: an open
 * failure is a stderr warning, never a crash. Disable only via config
 * `privilegedAudit: false` (doctor warns). See
 * docs/proposals/2026-07-privileged-commands.md.
 */
#ifndef JC_AUDIT_H
#define JC_AUDIT_H


#ifdef __cplusplus
extern "C" {
#endif
struct jc_app; /* jc_app.h */

/* Record one privileged-command attempt. `launcher` is the detected tool
 * ("sudo", ...); `command` the full shell string (scrubbed + written whole);
 * `decision` one of "allow"/"allowlist"/"deny"/"ask_approved"/"ask_denied"/
 * "unattended_refused". No-op when config privilegedAudit is off. */
void jc_audit_privileged(struct jc_app *app, const char *launcher,
                         const char *command, const char *decision);

/* Record one kinetic-action attempt (M163a) to audit/kinetic.jsonl, same
 * shape/fields as the privileged log (so `jichi audit` reads either):
 * `launcher` is "tool:<name>" or "shell"; `detail` the command/args; the same
 * decision vocabulary. No-op when config kineticAudit is off. */
void jc_audit_kinetic(struct jc_app *app, const char *launcher,
                      const char *detail, const char *decision);

#ifdef __cplusplus
}
#endif
#endif /* JC_AUDIT_H */
