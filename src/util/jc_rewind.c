/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rewind.c - checkpoint<->turn mapping (see jc_rewind.h). */

#include "jc_rewind.h"

#include <string.h>

#define JC_REWIND_CLEAN_CAP 256

/* Collapse runs of whitespace in `s` to single spaces, trim a trailing space,
 * and truncate to `cap` bytes (NUL-terminated). Mirrors jc_snapshot_clean_label
 * so a cleaned user message compares equal to a freshly-stored checkpoint
 * label. */
static void clean(const char *s, char *buf, jc_size cap)
{
    jc_size o = 0;
    int prev_space = 0;

    if (cap == 0) {
        return;
    }
    if (s == NULL) {
        buf[0] = '\0';
        return;
    }
    while (*s != '\0' && o + 1 < cap) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (o > 0 && !prev_space) {
                buf[o++] = ' ';
                prev_space = 1;
            }
        } else {
            buf[o++] = (char)c;
            prev_space = 0;
        }
        s++;
    }
    if (o > 0 && buf[o - 1] == ' ') {
        o--;
    }
    buf[o] = '\0';
}

int jc_rewind_label_match(const char *user_content, const char *label)
{
    char buf[JC_REWIND_CLEAN_CAP];
    jc_size lu;
    jc_size ll;

    if (user_content == NULL || label == NULL || label[0] == '\0') {
        return 0;
    }
    clean(user_content, buf, sizeof(buf));
    if (buf[0] == '\0') {
        return 0;
    }
    lu = strlen(buf);
    ll = strlen(label);
    /* One a prefix of the other: compare over the shorter length. */
    return strncmp(buf, label, (lu <= ll) ? lu : ll) == 0;
}

void jc_rewind_match(const char *const *users, int nusers,
                     const char *const *labels, int nlabels, int *out_user)
{
    int ui = 0;
    int ci;

    for (ci = 0; ci < nlabels; ci++) {
        out_user[ci] = -1;
        while (ui < nusers) {
            int matched = jc_rewind_label_match(users[ui], labels[ci]);
            ui++;
            if (matched) {
                out_user[ci] = ui - 1;
                break;
            }
        }
    }
}
