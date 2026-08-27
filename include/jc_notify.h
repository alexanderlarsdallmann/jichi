/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_notify.h - completion notification (M34f / F6).
 *
 * When a turn (TUI) or an unattended run (--auto / -p) finishes, optionally ring
 * the terminal bell and/or run a user-configured command, so you can step away
 * from a long task and be pinged when the agent is done or wants input. Both are
 * opt-in (config `notifyBell` / `notify`); the front-ends (TUI loop, headless)
 * call this -- the agent core and ACP never do.
 */
#ifndef JC_NOTIFY_H
#define JC_NOTIFY_H


#ifdef __cplusplus
extern "C" {
#endif
/* Fire a completion notification: if `bell`, write a BEL to stderr (the
 * terminal); if `command` is non-NULL/non-empty, run it via /bin/sh -c with a
 * short timeout, output discarded, exposing `summary` as $JICHI_NOTIFY and `cwd`
 * as $JICHI_CWD. `summary`/`cwd` may be NULL. A no-op when neither is enabled. */
void jc_notify_fire(const char *command, int bell, const char *cwd,
                    const char *summary);

#ifdef __cplusplus
}
#endif
#endif /* JC_NOTIFY_H */
