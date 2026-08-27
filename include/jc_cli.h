/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_cli.h - small pure helpers for CLI argument parsing + display. */
#ifndef JC_CLI_H
#define JC_CLI_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Parse an --output format value into a code: "text" => 0, "json" => 1 (one
 * object at end), "jsonl" => 2 (streaming one JSON object per event). Returns 0
 * on success, -1 for any other (or NULL) value. */
int jc_output_format_parse(const char *s, int *out_json);

/* Human one-line summary of a tool call's arguments for display: the first
 * meaningful field of the JSON object (path/command/query/pattern/symbol/url/
 * name/file/task), else the raw JSON. Always NUL-terminates `buf` (truncated to
 * `cap`); empty when args_json is NULL/empty. `name` is currently unused but
 * kept for future per-tool tuning. */
void jc_tool_arg_summary(const char *name, const char *args_json,
                         char *buf, jc_size cap);

/* The display-friendly short model name: the text after the last '/'
 * ("jlu/qwen3-coder-next" -> "qwen3-coder-next"); the whole string when there is
 * no '/'. Returns a pointer into `name` (or "" when NULL).
 *
 * Use this ONLY for the config `name` half of a display. Applying it to a wire
 * `model` id silently drops the vendor prefix, so `jlu/coder` and `other/coder`
 * render identically -- see jc_model_display (M296). */
const char *jc_model_short_name(const char *name);

/* Render a model for a human as "<short name> (<full wire id>)" -- M296.
 *
 * A config `name` is an INTENT label (`fast`, `strong`) chosen so an agent profile
 * can pin a tier without naming a vendor id. It says which tier is active and
 * nothing about which model is answering, so both halves are shown. The id half is
 * the FULL id, deliberately: shortening it is the precision loss above.
 *
 * The parenthetical is suppressed where it would carry no information -- when the
 * config declares no `name` (the common case; `name` is then NULL, NOT mirrored
 * from the id), when `name` and the id are the same string, or when the short name
 * already equals the id. In those cases the full id alone is rendered, so no config
 * gains a redundant duplicate and none loses its vendor prefix.
 *
 * Writes at most `cap` bytes incl. the NUL (truncating), returns the length
 * written. "?" when both are NULL/empty -- a display function must not render
 * nothing, and must never pass NULL to "%s" (undefined behaviour in C89, and the
 * `(null)` that leaked into `/status` before M296). Pure. */
jc_size jc_model_display(const char *name, const char *id, char *out,
                         jc_size cap);

/* Render the setup wizard's "I looked at this machine" OS line into `out`, kept
 * within `max_cols` columns INCLUDING the two-space indent it is printed with.
 *
 * uname's `release` is unbounded, and vendor kernels use it for build
 * provenance rather than a version: Android ships
 * "5.15.185-android13-8-00044-g051a97cf151a-ab14024729" -- 50 characters, 42
 * of which are a git hash and a build number. Printed inline after the core
 * and RAM figures that line measured 94 columns on a real tablet, against the
 * wizard's 76-column accessibility contract (a long line wraps mid-word, which
 * is worst under screen magnification). Found by the Termux row, M459.
 *
 * The release is truncated ONLY if it still would not fit, and then with a
 * trailing "..." so the reader can see something was dropped -- this line
 * exists to report what was probed, so a silent shortening would make it lie.
 * Plain "..." rather than an ellipsis character: the wizard runs on terminals
 * this project does not assume are UTF-8.
 *
 * Writes at most `cap` bytes incl. the NUL and returns the length written.
 * A NULL/empty field is skipped rather than passed to "%s". Pure. */
jc_size jc_os_line(const char *sysname, const char *release, const char *machine,
                   jc_size max_cols, char *out, jc_size cap);

/* Resolve whether to emit ANSI color: mode <0 => follow is_tty; 0 => off;
 * >0 => on. Returns 0/1. */
int jc_color_enabled(int mode, int is_tty);

/* The ANSI color escape for an operating mode (an `enum jc_agent_mode` value):
 * chat => green, plan => blue, auto => yellow (flags the unattended posture),
 * anything else => cyan. Returns a static escape string (never NULL). Pure;
 * unit-tested. Used to emphasize the mode in the TUI header and prompt. */
const char *jc_mode_color(int mode);

/* M346: format a counting-up elapsed time for a live display. Under a minute
 * it is byte-identical to the old raw form ("12.3s" -- the decimal is the
 * liveness signal on a line that redraws in place); from there "2m 07s", and
 * past the hour "1h 04m", the trailing unit zero-padded so the line's width
 * does not shimmer as it ticks. Negative (a clock hiccup) clamps to "0.0s".
 * Pure; unit-tested. */
void jc_fmt_elapsed(double secs, char *buf, jc_size cap);

/* Format a coarse "<n> ago" relative time for `delta_secs` (seconds in the
 * past, clamped at 0) into `buf`: "just now" / "Nm ago" / "Nh ago" / "Nd ago"
 * / "Nw ago". Always NUL-terminates. */
void jc_reltime(long delta_secs, char *buf, jc_size cap);

/* Index of the single id in `ids` (n entries) that has `prefix` as a prefix;
 * an exact full match wins outright. Returns -1 when none match, -2 when the
 * prefix is ambiguous (matches two or more without an exact hit). */
int jc_id_prefix_unique(const char *const *ids, int n, const char *prefix);

/* True when `s` looks like a model server's context-window overflow message
 * (e.g. llama.cpp "n_keep ... >= n_ctx", "context length", "Context size has
 * been exceeded"). Some servers return this as completion content (HTTP 200)
 * rather than an error, so jichi can't catch it on the wire — this lets the CLI
 * recognize it in the answer and point the user at the fix. 0 for NULL. Pure;
 * unit-tested (M73). */
int jc_text_is_context_overflow(const char *s);

/* Compose a compact one-line TUI status indicator of noteworthy session state
 * into `buf` (cap): the non-zero items among active constraints, running
 * background jobs, and open todos (e.g. "2 constraints · 1 job · 3 todos").
 * `unicode` selects a leading glyph vs an ASCII prefix. Returns the composed
 * length (0 = nothing noteworthy, buf left empty). Pure; unit-tested (M122). */
jc_size jc_tui_indicator(int n_constraints, int n_bg, int n_todos,
                         int unicode, char *buf, jc_size cap);

/* Decide whether an interactive prompt-level interrupt (Ctrl-C on an empty line)
 * should QUIT jichi. A single Ctrl-C at the prompt cancels and stays (returns 0,
 * standard REPL behavior — "stop, don't exit", M107); only a SECOND consecutive
 * one exits (returns 1). `consecutive` is the number of back-to-back prompt
 * interrupts including this one (reset to 0 by any real input). Pure; unit-tested. */
int jc_interrupt_should_exit(int consecutive);

#ifdef __cplusplus
}
#endif
#endif /* JC_CLI_H */
