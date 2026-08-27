/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sessmeta.h - read a session file's listing metadata WITHOUT a parse tree.
 *
 * WHY (M202). jc_session_list needs four scalars and a message count out of each
 * session file. It used to get them with cJSON_Parse, which builds a full tree:
 * ~64 bytes of node per JSON value plus a copy of every string. The cost
 * therefore tracks the number of VALUES, not the file size -- and glibc does not
 * return the freed nodes to the OS within a run, so the peak is cumulative.
 * Measured: `ls --all` over a real 243-file / 17 MB store peaked at 193 MB RSS,
 * and on synthetic stores of identical byte size the peak went 9.3 / 16.9 /
 * 237.8 MB at 2 / 200 / 2000 messages per session. docs/LOW_MEMORY.md's tiers
 * start at 32 MB total RAM, so one /sessions could OOM the machines that guide is
 * written for.
 *
 * This is a single forward pass with no allocation: it tracks string/escape state
 * and brace depth, captures the wanted top-level string values, and counts the
 * objects directly inside the "history" array. Tracking string state is what
 * makes the count trustworthy -- a message whose CONTENT contains the text
 * `"role":` must not inflate it, and a naive substring count would.
 *
 * It is deliberately NOT a JSON parser. It answers exactly the listing's
 * questions and reports failure otherwise, so the caller can fall back to
 * cJSON_Parse for anything unusual (a foreign writer, a shape we do not expect).
 * See docs/analysis/2026-07-29-tool-arena.md.
 */
#ifndef JC_SESSMETA_H
#define JC_SESSMETA_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Capacities are generous for what jc_session_save writes: the title is
 * truncated to 60 chars at save time, an alias is <= 64, a workspace is a path. */
#define JC_SESSMETA_ID    128
#define JC_SESSMETA_TITLE 256
#define JC_SESSMETA_ALIAS 80
#define JC_SESSMETA_WS    1024

struct jc_sessmeta {
    char id[JC_SESSMETA_ID];
    char title[JC_SESSMETA_TITLE];
    char alias[JC_SESSMETA_ALIAS];
    char workspace[JC_SESSMETA_WS];
    int  has_id;
    int  has_title;
    int  has_alias;
    int  has_workspace;
    int  nmsgs;      /* objects directly inside the top-level "history" array */
    int  has_history;
};

/* Scan `text` (NUL-terminated is fine; `len` bounds it) for the listing fields.
 *
 * Returns 1 when the scan is trustworthy: the text is a top-level object whose
 * braces and strings balance, and a "history" array was seen. Returns 0
 * otherwise -- unbalanced, truncated, not an object, or no history -- and the
 * caller should fall back to a full parse rather than trust partial output.
 *
 * Escapes in captured values are decoded for \\" \\\\ \\/ \\n \\r \\t \\b \\f;
 * a \\uXXXX sequence below 0x80 becomes that byte, and anything else is emitted
 * as UTF-8 when it fits, else '?'. Values longer than their capacity are
 * truncated (they are display-only), which never affects the return value.
 *
 * Pure: no allocation, no I/O. Unit-tested in tests/test_sessmeta.c. */
int jc_sessmeta_scan(const char *text, jc_size len, struct jc_sessmeta *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_SESSMETA_H */
