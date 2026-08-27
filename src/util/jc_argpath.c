/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_argpath.c - see jc_argpath.h (M501). */

#include "jc_argpath.h"

#include <stddef.h>

static int push(const char **out, int cap, int n, const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return n;                      /* nothing declared is not a path */
    }
    if (n >= cap) {
        return -1;                     /* fail-closed; see the header */
    }
    out[n] = s;
    return n + 1;
}

int jc_argpath_collect(const cJSON *args, const char **out, int cap)
{
    const cJSON *p;
    const cJSON *edits;
    int n = 0;

    if (args == NULL || out == NULL || cap <= 0) {
        return 0;
    }
    p = cJSON_GetObjectItem((cJSON *)args, "path");
    if (p != NULL && cJSON_IsString(p)) {
        n = push(out, cap, n, p->valuestring);
        if (n < 0) {
            return -1;
        }
    }
    edits = cJSON_GetObjectItem((cJSON *)args, "edits");
    if (edits != NULL && cJSON_IsArray(edits)) {
        const cJSON *e;
        for (e = edits->child; e != NULL; e = e->next) {
            const cJSON *ep;
            if (!cJSON_IsObject(e)) {
                continue;
            }
            ep = cJSON_GetObjectItem((cJSON *)e, "path");
            if (ep == NULL || !cJSON_IsString(ep)) {
                continue;
            }
            n = push(out, cap, n, ep->valuestring);
            if (n < 0) {
                return -1;
            }
        }
    }
    return n;
}
