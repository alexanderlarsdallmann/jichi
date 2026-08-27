/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolcaps.h - the built-in per-tool output caps, in ONE place (M440).
 *
 * These five numbers were each a `#define` inside the tool that enforced it:
 * READ_MAX_BYTES in jc_tool_read.c, RUN_MAX_OUTPUT in jc_tool_run.c, and so on.
 * That was fine while the only reader was the enforcer. M440 tells the MODEL what
 * its output caps are -- so a second reader exists, and two copies of a number
 * that must agree is the drift M296 forbids. Hence one header, included by the
 * tool that enforces the cap and by the prompt that reports it.
 *
 * They are SAFETY bounds, not budgets: the thing that stops one runaway command
 * from exhausting memory. docs/TOOL_OUTPUT_COST.md §7 records why they are not
 * lowered to save tokens -- a bound that tries to be both serves neither, and only
 * the operator knows which backend they are on. The config keys
 * (`readMaxBytes`/`runMaxBytes`/`fetchMaxBytes`/`searchMaxBytes`/`gitMaxBytes`,
 * resolved through jc_config_cap) are the budget knob; these are the floor under
 * a missing one.
 */
#ifndef JC_TOOLCAPS_H
#define JC_TOOLCAPS_H

#define JC_CAP_READ_DEFAULT   (256 * 1024)  /* read_file                        */
#define JC_CAP_RUN_DEFAULT    (64 * 1024)   /* run_terminal_command / run_tests  */
#define JC_CAP_FETCH_DEFAULT  (128 * 1024)  /* fetch_url                        */
#define JC_CAP_SEARCH_DEFAULT (64 * 1024)   /* search_code                      */
#define JC_CAP_GIT_DEFAULT    (32 * 1024)   /* git_status/diff/log/blame        */

#endif /* JC_TOOLCAPS_H */
