/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_workflow.c - pure workflow-spec parse + prompt expansion (jc_workflow.h). */

#include "jc_workflow.h"
#include "jc_json.h"
#include "jc_jsonc.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

const char *jc_wf_type_name(int type)
{
    switch (type) {
    case JC_WF_MAP:        return "map";
    case JC_WF_SYNTHESIZE: return "synthesize";
    case JC_WF_VERIFY:     return "verify";
    default:               return "unknown";
    }
}

static int type_from_name(const char *s)
{
    if (s == NULL) {
        return JC_WF_UNKNOWN;
    }
    if (strcmp(s, "map") == 0) {
        return JC_WF_MAP;
    }
    if (strcmp(s, "synthesize") == 0 || strcmp(s, "synth") == 0) {
        return JC_WF_SYNTHESIZE;
    }
    if (strcmp(s, "verify") == 0) {
        return JC_WF_VERIFY;
    }
    return JC_WF_UNKNOWN;
}

jc_status jc_workflow_parse(const char *json, struct jc_workflow *out,
                            struct jc_arena *a)
{
    char *clean;
    cJSON *root;
    cJSON *stages;
    cJSON *st;

    memset(out, 0, sizeof(*out));
    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    clean = jc_jsonc_strip(json, a);
    root = (clean != NULL) ? jc_json_parse(clean) : NULL;
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return JC_ERR_PARSE;
    }
    out->name = jc_json_dup_str(root, "name", a);
    stages = cJSON_GetObjectItem(root, "stages");
    if (!cJSON_IsArray(stages)) {
        cJSON_Delete(root);
        return JC_ERR_INVALID;
    }
    cJSON_ArrayForEach(st, stages) {
        struct jc_wf_stage *s;
        cJSON *items;
        if (!cJSON_IsObject(st) || out->nstages >= JC_WF_MAX_STAGES) {
            out->stages_dropped++; /* M610: not silent -- the caller warns */
            continue;
        }
        s = &out->stages[out->nstages];
        memset(s, 0, sizeof(*s));
        s->type = type_from_name(jc_json_get_str(st, "type", NULL));
        if (s->type == JC_WF_UNKNOWN) {
            out->stages_dropped++; /* a typo'd type is a stage that vanished */
            continue;
        }
        s->prompt = jc_json_dup_str(st, "prompt", a);
        s->command = jc_json_dup_str(st, "command", a);
        s->model = jc_json_dup_str(st, "model", a);
        /* M530: lenient -- a workflow map is a file a person writes, and the
         * default here is 1, so `"readonly": 0` (an operator asking for a
         * writable stage) previously fell back to read-only and ignored them. */
        s->readonly = jc_json_get_bool_lenient(st, "readonly", 1);
        items = cJSON_GetObjectItem(st, "items");
        if (cJSON_IsArray(items)) {
            cJSON *it;
            cJSON_ArrayForEach(it, items) {
                if (cJSON_IsString(it) && it->valuestring != NULL) {
                    if (s->nitems < JC_WF_MAX_ITEMS) {
                        s->items[s->nitems++] =
                            jc_arena_strdup(a, it->valuestring);
                    } else {
                        out->items_dropped++; /* M610: over the per-stage cap */
                    }
                }
            }
        }
        out->nstages++;
    }
    cJSON_Delete(root);
    if (out->nstages == 0) {
        return JC_ERR_INVALID;
    }
    return JC_OK;
}

char *jc_workflow_expand(const char *tmpl, const char *item, struct jc_arena *a)
{
    struct jc_sb sb;
    const char *p;
    char *out;

    if (tmpl == NULL) {
        return jc_arena_strdup(a, "");
    }
    if (item == NULL) {
        return jc_arena_strdup(a, tmpl);
    }
    jc_sb_init(&sb);
    for (p = tmpl; *p != '\0'; ) {
        if (strncmp(p, "$ITEM", 5) == 0) {
            jc_sb_append(&sb, item);
            p += 5;
        } else {
            jc_sb_append_char(&sb, *p);
            p++;
        }
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}
