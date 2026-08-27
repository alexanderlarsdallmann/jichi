/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_utf8.h - minimal UTF-8 codepoint + display-width helpers (M127).
 *
 * jichi treats text as opaque UTF-8 bytes end to end (no codepage conversion). The
 * line editor, however, must move/delete by whole CODEPOINTS and measure display
 * WIDTH correctly (CJK + fullwidth forms occupy two terminal columns; combining
 * marks occupy zero) or the cursor and line-wrap drift on non-ASCII input.
 *
 * Design decision: a SELF-CONTAINED width table (an East-Asian-Wide / Fullwidth /
 * zero-width range check) rather than libc `wcwidth()`+`setlocale()`. It is pure,
 * deterministic across libc versions, unit-testable offline, C89-clean, and needs
 * no locale state or new dependency (jichi's "libcurl + cJSON only" rule).
 */
#ifndef JC_UTF8_H
#define JC_UTF8_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Decode the codepoint at byte `pos` in `s[0..len)`. Sets *adv (may be NULL) to
 * the byte length consumed (>=1). Returns U+FFFD on an invalid/truncated
 * sequence (with adv=1), 0 at/after the end. */
unsigned long jc_utf8_decode(const char *s, jc_size len, jc_size pos,
                             jc_size *adv);

/* Byte index of the start of the codepoint before `pos` (steps back over
 * continuation bytes). 0 when pos==0. */
jc_size jc_utf8_prev(const char *s, jc_size pos);

/* Byte index just past the codepoint at `pos` (clamped to `len`). */
jc_size jc_utf8_next(const char *s, jc_size len, jc_size pos);

/* Display columns for a codepoint: 0 (combining / zero-width / format), 2
 * (East-Asian Wide / Fullwidth / common emoji), else 1. */
int jc_utf8_width(unsigned long cp);

/* Total display columns of `s[0..n)` (sum of per-codepoint widths; no ANSI
 * handling -- callers that pass styled text strip escapes first). */
int jc_utf8_str_cols(const char *s, jc_size n);

/* ---- well-formedness + boundary-safe truncation (M191) ------------------- *
 * Text bound for a JSON request body must be well-formed UTF-8: a server that
 * decodes the body strictly rejects the WHOLE request over one split character,
 * and because the split byte lives on in the conversation history, every later
 * turn is rejected too. Truncating model-bound text at a raw byte offset is
 * therefore not a cosmetic bug but a way to wedge a run permanently.
 * See docs/ANECDOTES.md #22. */

/* 1 iff `s[0..len)` is well-formed UTF-8 (strict: rejects overlongs,
 * surrogates, > U+10FFFF and truncated sequences). NULL counts as valid. */
int jc_utf8_valid(const char *s, jc_size len);

/* Largest length <= n that does not split a UTF-8 sequence -- use for a kept
 * PREFIX. `s` must have at least n+1 readable bytes (NUL-terminated is enough;
 * s[n] is inspected, never consumed). */
jc_size jc_utf8_trunc_len(const char *s, jc_size n);

/* Smallest offset >= off (and <= len) that starts a UTF-8 sequence -- use for a
 * kept SUFFIX, whose first byte would otherwise be a stray continuation byte. */
jc_size jc_utf8_resync(const char *s, jc_size len, jc_size off);

/* Replace every ill-formed byte in `s[0..len)` with U+FFFD. Returns 1 and sets
 * *out (malloc'd, NUL-terminated; caller frees) and *out_len (may be NULL) when
 * a repair was needed; returns 0 -- allocating nothing -- when `s` is already
 * well-formed, which is the overwhelmingly common case. An allocation failure
 * also returns 0, so a caller using this as a backstop simply keeps the
 * original bytes rather than failing. */
int jc_utf8_sanitize(const char *s, jc_size len, char **out, jc_size *out_len);

/* --- C0 control characters and the terminal (M472) ---------------------------
 *
 * A terminal is an INTERPRETER, not a display: some bytes are printed and some
 * are commands. `ESC ] 52 ; c ; <base64> BEL` writes the user's system clipboard;
 * `ESC [ 2 K` erases the line jichi just printed, which in an agent means hiding
 * what it ran. So text from outside jichi -- model output, tool results -- must
 * not reach a terminal verbatim.
 *
 * M363 already decided this rule for the INPUT side (bracketed paste) and wrote
 * down why: "the editor re-emits the buffer per redraw, so a pasted ESC replays
 * escape sequences into the terminal on every keystroke -- output-side paste
 * injection". These two functions are that same rule, extracted so the input and
 * output sides cannot drift apart: jc_paste_splice now calls the predicate rather
 * than carrying its own copy of the condition. One rule, two consequences -- the
 * M326e shape.
 *
 * Measured before the output side existed: a mock provider's assistant text
 * reached jichi's stdout byte-for-byte, OSC 0 (window title) and OSC 52
 * (clipboard write) included.
 *
 * NOT applied to jichi's own output. jichi emits SGR colour deliberately, around
 * content; the strip belongs where untrusted content meets the terminal, which is
 * the front-end's raw write. Nor to the `--output json`/`jsonl` paths: cJSON
 * already escapes a control byte to \u001b, so it is inert there, and stripping
 * would lose fidelity for a machine consumer that wants to see what arrived.
 * See docs/analysis/2026-08-17-source-hardening-audit.md §H3. */

/* 1 iff `c` is safe to write to a terminal as content. False for the C0 controls
 * and DEL; TRUE for newline and tab, which are real content (a Makefile's tabs)
 * and which the column arithmetic already accounts for, and for every byte
 * >= 0x80, so UTF-8 passes untouched. */
int jc_ctrl_display_safe(unsigned char c);

/* Remove every byte jc_ctrl_display_safe rejects from `s[0..len)`. Returns 1 and
 * sets *out (malloc'd, NUL-terminated; caller frees) and *out_len (may be NULL)
 * when something was stripped; returns 0 -- allocating nothing -- when `s` is
 * already clean, which is the overwhelmingly common case. An allocation failure
 * also returns 0, matching jc_utf8_sanitize: a backstop must not fail hard.
 *
 * Safe to call per streamed delta: a control byte is one byte, so unlike UTF-8 it
 * cannot be split across two SSE chunks. */
int jc_ctrl_sanitize(const char *s, jc_size len, char **out, jc_size *out_len);

#ifdef __cplusplus
}
#endif
#endif /* JC_UTF8_H */
