/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_reread.c - see jc_reread.h. */

#include "jc_reread.h"
#include <stddef.h> /* NULL */

unsigned long jc_reread_hash(const char *data, unsigned long len)
{
    unsigned long h = 5381u; /* djb2 */
    unsigned long i;
    if (data == NULL) {
        return h;
    }
    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (unsigned long)(unsigned char)data[i]; /* h*33 + c */
    }
    return h;
}
