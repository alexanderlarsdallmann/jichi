/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_uuid.c - UUID-v4-shaped id generation (see jc_uuid.h). */

#include "jc_uuid.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static int g_seeded = 0;

/* Fill `buf` with `n` bytes from the OS entropy pool (M472). Returns 1 on
 * success, 0 if unavailable -- in which case the caller falls back to rand().
 *
 * Why this exists: none of jc_uuid's five consumers is an authentication token,
 * so time+pid-seeded rand() was not the vulnerability it would be in a web
 * application. Two consumers still deserve better. The MULTIPART BOUNDARY is a
 * delimiter, and jc_multipart_file appends payload bytes without checking they do
 * not contain it -- a predictable delimiter plus an unchecked payload is the shape
 * of a multipart injection, and the payload there is a model-chosen file. Session
 * ids name files.
 *
 * Uses stdio rather than open()/read(): this TU is compiled as strict C89 with no
 * POSIX, and fopen is all it needs. A short read counts as failure rather than
 * silently returning fewer random bytes than asked for. */
static int os_entropy(unsigned char *buf, int n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    int ok;
    if (f == NULL) {
        return 0;
    }
    ok = (fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    return ok;
}

static void ensure_seed(void)
{
    if (!g_seeded) {
        /* time() is the only clock guaranteed by C89; mix in the address of
         * a local to add a little per-process variation. */
        int local;
        unsigned long s = (unsigned long)time(NULL);
        s ^= (unsigned long)(size_t)&local;
        srand((unsigned int)s);
        g_seeded = 1;
    }
}

static int hex_nibble(void)
{
    static const char *h = "0123456789abcdef";
    return h[rand() & 0xF];
}

void jc_uuid_v4(char *out)
{
    /* Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     * where y is one of 8,9,a,b. */
    static const char *layout = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    static const char *hex = "0123456789abcdef";
    unsigned char rnd[36];
    int have_os;
    int i;

    /* 36 bytes covers the layout one-for-one, so a single read serves the whole
     * id and there is no per-nibble syscall. One byte per nibble is wasteful of
     * entropy and cheap to reason about, which is the right trade here. */
    have_os = os_entropy(rnd, 36);
    if (!have_os) {
        ensure_seed();   /* documented fallback: see jc_uuid.h */
    }
    for (i = 0; layout[i] != '\0'; i++) {
        char c = layout[i];
        if (c == '-' || c == '4') {
            out[i] = c;
        } else if (c == 'y') {
            static const char *v = "89ab";
            out[i] = have_os ? v[rnd[i] & 0x3] : v[rand() & 0x3];
        } else {
            out[i] = have_os ? hex[rnd[i] & 0xF] : (char)hex_nibble();
        }
    }
    out[i] = '\0';
}
