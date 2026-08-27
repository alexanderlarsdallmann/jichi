/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tui.h - interactive terminal UI (REPL) for jichi.
 *
 * Runs an interactive chat loop: read a user line (with editing/history),
 * stream the assistant's reply, render tool activity, and gate tool calls
 * behind a permission prompt. Falls back to a line-at-a-time loop when stdin
 * is not a TTY.
 */
#ifndef JC_TUI_H
#define JC_TUI_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_app.h"

int jc_tui_run(struct jc_app *app);

#ifdef __cplusplus
}
#endif
#endif /* JC_TUI_H */
