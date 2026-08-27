/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_selfheal.c - runtime self-healing guards (see jc_selfheal.h). Pure. */

#include "jc_selfheal.h"

#include <string.h>

void jc_editwatch_init(struct jc_editwatch *w)
{
    memset(w, 0, sizeof(*w));
}

int jc_editwatch_bump(struct jc_editwatch *w, const char *path)
{
    int i;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    for (i = 0; i < w->n; i++) {
        if (strcmp(w->paths[i], path) == 0) {
            return ++w->counts[i];
        }
    }
    if (w->n >= JC_SELFHEAL_MAX_PATHS) {
        return 0; /* table full: stop tracking new paths */
    }
    strncpy(w->paths[w->n], path, JC_SELFHEAL_PATH_MAX - 1);
    w->paths[w->n][JC_SELFHEAL_PATH_MAX - 1] = '\0';
    w->counts[w->n] = 1;
    w->n++;
    return 1;
}
