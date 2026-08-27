/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jq_core.c - pure core of jsonq: dot-path parse + cJSON tree lookup.
 * Unit-tested in tests/test_ttools.c. */

#include "jq_core.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

static int jq_err(char *err, size_t errcap, const char *msg)
{
    if (err != NULL && errcap > 0)
        jc_snprintf(err, errcap, "%s", msg);
    return -1;
}

int jq_path_parse(const char *text, struct jq_path *out,
                  char *err, size_t errcap)
{
    const char *p = text;

    out->nsteps = 0;
    if (err != NULL && errcap > 0)
        err[0] = '\0';

    if (p == NULL || *p != '.')
        return jq_err(err, errcap, "path must start with '.'");
    if (strcmp(p, ".") == 0)
        return 0;               /* the whole document */

    while (*p != '\0') {
        if (out->nsteps >= JQ_MAX_STEPS) {
            jq_path_free(out);
            return jq_err(err, errcap, "path too deep");
        }
        if (*p == '.') {
            const char *start;
            size_t klen;
            p++;
            if (*p == '[')      /* ".[0]" -- fall through to the index arm */
                continue;
            start = p;
            while (*p != '\0' && *p != '.' && *p != '[')
                p++;
            klen = (size_t)(p - start);
            if (klen == 0) {
                jq_path_free(out);
                return jq_err(err, errcap, "empty key");
            }
            out->steps[out->nsteps].kind = JQ_STEP_KEY;
            out->steps[out->nsteps].key = (char *)malloc(klen + 1);
            if (out->steps[out->nsteps].key == NULL) {
                jq_path_free(out);
                return jq_err(err, errcap, "out of memory");
            }
            memcpy(out->steps[out->nsteps].key, start, klen);
            out->steps[out->nsteps].key[klen] = '\0';
            out->nsteps++;
        } else if (*p == '[') {
            char *end = NULL;
            long v;
            p++;
            v = strtol(p, &end, 10);
            if (end == p || end == NULL || *end != ']' || v < 0) {
                jq_path_free(out);
                return jq_err(err, errcap, "bad [index]");
            }
            out->steps[out->nsteps].kind = JQ_STEP_INDEX;
            out->steps[out->nsteps].key = NULL;
            out->steps[out->nsteps].index = (int)v;
            out->nsteps++;
            p = end + 1;
        } else {
            jq_path_free(out);
            return jq_err(err, errcap, "expected '.' or '['");
        }
    }
    return 0;
}

void jq_path_free(struct jq_path *p)
{
    int i;
    if (p == NULL)
        return;
    for (i = 0; i < p->nsteps; i++) {
        free(p->steps[i].key);
        p->steps[i].key = NULL;
    }
    p->nsteps = 0;
}

cJSON *jq_lookup(cJSON *doc, const struct jq_path *p)
{
    cJSON *cur = doc;
    int i;
    for (i = 0; i < p->nsteps && cur != NULL; i++) {
        if (p->steps[i].kind == JQ_STEP_KEY) {
            if (!cJSON_IsObject(cur))
                return NULL;
            cur = cJSON_GetObjectItemCaseSensitive(cur, p->steps[i].key);
        } else {
            if (!cJSON_IsArray(cur))
                return NULL;
            cur = cJSON_GetArrayItem(cur, p->steps[i].index);
        }
    }
    return cur;
}

int jq_type_matches(const cJSON *item, const char *tname)
{
    if (strcmp(tname, "string") == 0)
        return cJSON_IsString(item) ? 1 : 0;
    if (strcmp(tname, "number") == 0)
        return cJSON_IsNumber(item) ? 1 : 0;
    if (strcmp(tname, "bool") == 0)
        return cJSON_IsBool(item) ? 1 : 0;
    if (strcmp(tname, "object") == 0)
        return cJSON_IsObject(item) ? 1 : 0;
    if (strcmp(tname, "array") == 0)
        return cJSON_IsArray(item) ? 1 : 0;
    if (strcmp(tname, "null") == 0)
        return cJSON_IsNull(item) ? 1 : 0;
    return -1;
}
