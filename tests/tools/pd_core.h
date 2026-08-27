/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* pd_core.h - the pure core of ptydrive (tests/tools).
 *
 * Script parsing, C-style escape decoding, and the streaming pattern
 * matcher. The PTY/fork/select machinery lives in ptydrive.c; everything
 * here is libc-only and unit-tested in tests/test_ttools.c.
 *
 * Script format (one command per line; '#' comments; blank lines ignored):
 *
 *   expect "PAT" [SECS]    # wait until PAT is a substring of the whole
 *                          #   accumulated transcript (default 10s)
 *   send "TEXT"            # write TEXT to the pty (C escapes: \r \n \t
 *                          #   \e \\ \" \xNN)
 *   delay MS               # sleep (inter-keystroke timing)
 *   drain MS               # read and discard output for a duration
 *   winsize ROWS COLS      # TIOCSWINSZ + SIGWINCH to the child
 *   signal TERM|INT|HUP|KILL
 *   waitexit [SECS]        # child must exit within SECS (default 10)
 *   assertexit N           # last waitexit's status must equal N
 *
 * expect matches plain substrings -- no regex, no VT interpretation
 * (deliberate: see the M209 plan, decision D7). Matching runs over the
 * accumulated transcript, so a pattern split across reads is found.
 */
#ifndef PD_CORE_H
#define PD_CORE_H

#include <stddef.h>

enum pd_cmd_kind {
    PD_CMD_EXPECT = 1,
    PD_CMD_SEND,
    PD_CMD_DELAY,
    PD_CMD_DRAIN,
    PD_CMD_WINSIZE,
    PD_CMD_SIGNAL,
    PD_CMD_WAITEXIT,
    PD_CMD_ASSERTEXIT
};

struct pd_cmd {
    enum pd_cmd_kind kind;
    char *text;         /* expect pattern / send bytes (may embed NUL)   */
    size_t text_len;
    long a;             /* expect/waitexit: secs; delay/drain: ms;
                           winsize: rows; signal: signo; assertexit: N   */
    long b;             /* winsize: cols                                 */
    int line;           /* 1-based source line, for diagnostics          */
};

struct pd_script {
    struct pd_cmd *cmds;
    int ncmds;
};

/* Parse a script. Returns 0 on success, -1 with "line N: msg" in err. */
int pd_script_parse(const char *text, struct pd_script *out,
                    char *err, size_t errcap);
void pd_script_free(struct pd_script *s);

/* Decode a C-escaped string (no surrounding quotes) into out (cap bytes).
 * Returns 0 and sets *outlen, or -1 on a malformed escape / overflow.
 * The output is also NUL-terminated when it fits. */
int pd_unescape(const char *in, char *out, size_t cap, size_t *outlen);

/* Binary-safe substring search; NULL when absent. */
const char *pd_match(const char *buf, size_t len,
                     const char *pat, size_t patlen);

/* "TERM" -> SIGTERM etc. (TERM/INT/HUP/KILL); -1 for unknown names. */
int pd_signal_from_name(const char *name);

#endif /* PD_CORE_H */
