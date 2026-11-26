/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sup_stop.c - def-377 W4 (M1194, A4b): the SOFT-STOP SENTINEL recognizer.
 *
 * Lives in src/chat (not src/util) deliberately: the agent loop (jc_agent.c)
 * is its only caller, and src/chat is wildcarded into the build, so a new
 * file needs no Makefile edit -- which matters because this wave runs in
 * parallel with A4c, which owns the same Makefile and edits no other file.
 * The recognizer itself is pure string logic; tests/test_sup_stop.c links it
 * and drives it directly.
 */
#include "jc_sup_stop.h"

#include <stddef.h>   /* NULL */

/* Byte-compare the first `n` characters of `s` against `prefix`. Returns 1
 * on a full match, 0 otherwise (including a short `s`). */
static int jc_sup_stop_prefix_match(const char *s, const char *prefix,
                                    unsigned long n)
{
    unsigned long i;
    if (s == NULL || prefix == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (s[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

int jc_sup_stop_line(const char *line)
{
    static const char prefix[] = JC_SUP_STOP_PREFIX;
    unsigned long plen;
    char seq;

    if (line == NULL) {
        return 0;
    }
    plen = 0;
    while (prefix[plen] != '\0') {
        plen++;
    }
    if (!jc_sup_stop_prefix_match(line, prefix, plen)) {
        return 0;
    }
    /* Exactly one character remains, and it is the seq digit. Nothing after. */
    seq = line[plen];
    if (seq != '1' && seq != '2') {
        return 0;
    }
    return line[plen + 1] == '\0';
}
