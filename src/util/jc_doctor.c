/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_doctor.c - health-check report aggregation (see jc_doctor.h). */

#include "jc_doctor.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

#define DOC_GRN "\x1b[1;32m"
#define DOC_YEL "\x1b[33m"
#define DOC_RED "\x1b[31m"
#define DOC_DIM "\x1b[2m"
#define DOC_RST "\x1b[0m"

struct jc_doctor_item {
    int   status;
    char *label;
    char *detail;
};

void jc_doctor_init(struct jc_doctor *d)
{
    jc_vec_init(&d->items, sizeof(struct jc_doctor_item));
}

void jc_doctor_free(struct jc_doctor *d)
{
    jc_size i;
    for (i = 0; i < d->items.len; i++) {
        struct jc_doctor_item *it =
            (struct jc_doctor_item *)jc_vec_at(&d->items, i);
        free(it->label);
        free(it->detail);
    }
    jc_vec_free(&d->items);
}

void jc_doctor_add(struct jc_doctor *d, int status, const char *label,
                   const char *detail)
{
    struct jc_doctor_item it;
    it.status = status;
    it.label = jc_strdup(label != NULL ? label : "");
    it.detail = (detail != NULL && detail[0] != '\0') ? jc_strdup(detail) : NULL;
    jc_vec_push(&d->items, &it);
}

int jc_doctor_count(const struct jc_doctor *d, int status)
{
    jc_size i;
    int n = 0;
    for (i = 0; i < d->items.len; i++) {
        if (((struct jc_doctor_item *)jc_vec_at((struct jc_vec *)&d->items,
                                                i))->status == status) {
            n++;
        }
    }
    return n;
}

int jc_doctor_exit_code(const struct jc_doctor *d)
{
    return jc_doctor_count(d, JC_DOC_FAIL) > 0 ? 1 : 0;
}

static const char *glyph(int status, int unicode)
{
    if (unicode) {
        return status == JC_DOC_OK ? "\xe2\x9c\x93" :        /* check */
               status == JC_DOC_WARN ? "!" : "\xe2\x9c\x97"; /* cross */
    }
    return status == JC_DOC_OK ? "ok" : status == JC_DOC_WARN ? "!" : "x";
}

static const char *color_for(int status)
{
    return status == JC_DOC_OK ? DOC_GRN :
           status == JC_DOC_WARN ? DOC_YEL : DOC_RED;
}

void jc_doctor_render(const struct jc_doctor *d, int color, int unicode,
                      struct jc_sb *out)
{
    jc_size i;
    for (i = 0; i < d->items.len; i++) {
        struct jc_doctor_item *it =
            (struct jc_doctor_item *)jc_vec_at((struct jc_vec *)&d->items, i);
        if (color) {
            jc_sb_append(out, color_for(it->status));
        }
        jc_sb_append(out, glyph(it->status, unicode));
        if (color) {
            jc_sb_append(out, DOC_RST);
        }
        jc_sb_append(out, " ");
        jc_sb_append(out, it->label);
        jc_sb_append(out, "\n");
        if (it->detail != NULL) {
            if (color) {
                jc_sb_append(out, DOC_DIM);
            }
            jc_sb_append(out, "    ");
            jc_sb_append(out, it->detail);
            if (color) {
                jc_sb_append(out, DOC_RST);
            }
            jc_sb_append(out, "\n");
        }
    }
    jc_sb_append_fmt(out, "\n%d ok, %d warning%s, %d problem%s\n",
                     jc_doctor_count(d, JC_DOC_OK),
                     jc_doctor_count(d, JC_DOC_WARN),
                     jc_doctor_count(d, JC_DOC_WARN) == 1 ? "" : "s",
                     jc_doctor_count(d, JC_DOC_FAIL),
                     jc_doctor_count(d, JC_DOC_FAIL) == 1 ? "" : "s");
}

/* Append `s` as a JSON string body (no surrounding quotes), escaped. */
static void json_str(struct jc_sb *out, const char *s)
{
    const char *p;
    for (p = (s != NULL) ? s : ""; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            jc_sb_append_char(out, '\\');
            jc_sb_append_char(out, (char)c);
        } else if (c == '\n') {
            jc_sb_append(out, "\\n");
        } else if (c == '\t') {
            jc_sb_append(out, "\\t");
        } else if (c == '\r') {
            jc_sb_append(out, "\\r");
        } else if (c < 0x20) {
            jc_sb_append_fmt(out, "\\u%04x", (unsigned int)c);
        } else {
            jc_sb_append_char(out, (char)c);
        }
    }
}

void jc_doctor_render_json(const struct jc_doctor *d, struct jc_sb *out)
{
    static const char *const NAMES[] = { "ok", "warn", "fail" };
    jc_size i;

    jc_sb_append_fmt(out, "{\"ok\":%d,\"warn\":%d,\"fail\":%d,\"exit\":%d,"
                          "\"checks\":[",
                     jc_doctor_count(d, JC_DOC_OK),
                     jc_doctor_count(d, JC_DOC_WARN),
                     jc_doctor_count(d, JC_DOC_FAIL),
                     jc_doctor_exit_code(d));
    for (i = 0; i < d->items.len; i++) {
        struct jc_doctor_item *it =
            (struct jc_doctor_item *)jc_vec_at((struct jc_vec *)&d->items, i);
        int s = it->status;
        if (s < 0 || s > 2) {
            s = 2;
        }
        if (i > 0) {
            jc_sb_append_char(out, ',');
        }
        jc_sb_append_fmt(out, "{\"status\":\"%s\",\"label\":\"", NAMES[s]);
        json_str(out, it->label);
        jc_sb_append(out, "\",\"detail\":\"");
        json_str(out, it->detail);
        jc_sb_append(out, "\"}");
    }
    jc_sb_append(out, "]}\n");
}
