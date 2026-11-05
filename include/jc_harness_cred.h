/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_cred.h - `jichi-harness credential-helper` (M1056, H3c): the C89
 * port of the drive's git-credential-from-file, the credential helper the
 * `mirror push` child registers with git (the two-word spec
 * `<ABS-JICHI-HARNESS> credential-helper`, the note's behaviour 10/11).
 *
 * git spawns the helper as `jichi-harness credential-helper <action>` with
 * <action> = get|store|erase on the command line; a `--config` flag is NOT on
 * the helper's argv (git does not pass it), so the helper reads only its env:
 * JICHI_HARNESS_GIT_TOKEN_FILE (the token FILE PATH the push child carries,
 * never the token), JICHI_HARNESS_CRED_RULE (the user-name rule) and
 * JICHI_HARNESS_CRED_URL (the configured remote URL the push child carries,
 * parsed from the stored config copy). On `get` it parses git's credential
 * identity from STDIN (key=value lines to a blank line: protocol, host,
 * path) and refuses (exit 1, NO password= line, the refusal on stderr naming
 * the remote it will not serve) when the identity does not match the
 * configured URL -- the race-free layer against a check-then-use race in the
 * URL guard (the note's 01:37 item 2). The token is read at the moment git
 * asks, whitespace-trimmed (the reference's tr -d ' \t\r\n'), and printed as
 * `username=<rule>\npassword=<token>\n` -- the credential is the helper's
 * output channel to git, by design. On `store` and `erase` it prints nothing
 * and exits 0, so git can never persist the token. The token is never on a
 * command line, never in a log, never in a child's environment.
 */
#ifndef JC_HARNESS_CRED_H
#define JC_HARNESS_CRED_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

/* The credential-helper subcommand. `verb` is argv[2] as git passes it:
 * "get", "store" or "erase". The env vars JICHI_HARNESS_GIT_TOKEN_FILE,
 * JICHI_HARNESS_CRED_RULE and JICHI_HARNESS_CRED_URL carry the configuration
 * (set by `mirror push` on the git child; the driver's own cells set them
 * directly). `a` owns the helper's scratch (stdin and the token read); the
 * caller frees it. Returns the process exit code: 0 (a get that answered, or
 * a store/erase), 1 (a refusal: an identity that does not match the
 * configured URL, or a token that could not be loaded), 2 (a usage error: an
 * unknown verb). */
int jc_harness_cred_helper_run(const char *verb, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_CRED_H */
