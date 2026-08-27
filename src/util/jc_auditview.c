/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_auditview.c - offline summarizer for the privileged-command audit log
 * (M158). See jc_auditview.h. Pure: text in, summary + rendered report out. */

#include "jc_auditview.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

void jc_auditview_init(struct jc_audit_summary *s)
{
    memset(s, 0, sizeof(*s));
    jc_vec_init(&s->by_decision, sizeof(struct jc_audit_count));
    jc_vec_init(&s->by_launcher, sizeof(struct jc_audit_count));
}

void jc_auditview_free(struct jc_audit_summary *s)
{
    if (s == NULL) {
        return;
    }
    jc_vec_free(&s->by_decision);
    jc_vec_free(&s->by_launcher);
}

int jc_auditview_is_refusal(const char *decision)
{
    if (decision == NULL) {
        return 0;
    }
    return strcmp(decision, "deny") == 0 ||
           strcmp(decision, "ask_denied") == 0 ||
           strcmp(decision, "unattended_refused") == 0;
}

static void count_bump(struct jc_vec *v, const char *name)
{
    struct jc_audit_count c;
    jc_size i;

    if (name == NULL || name[0] == '\0') {
        name = "?";
    }
    for (i = 0; i < v->len; i++) {
        struct jc_audit_count *e = (struct jc_audit_count *)jc_vec_at(v, i);
        if (strcmp(e->name, name) == 0) {
            e->n++;
            return;
        }
    }
    memset(&c, 0, sizeof(c));
    jc_snprintf(c.name, sizeof(c.name), "%s", name);
    c.n = 1;
    jc_vec_push(v, &c);
}

static void copy_field(char *dst, jc_size cap, const cJSON *o, const char *key)
{
    const char *v = jc_json_get_str(o, key, "");
    jc_snprintf(dst, cap, "%s", v);
}

static void feed_line(struct jc_audit_summary *s, const char *line,
                      jc_size len, double since_ts)
{
    char *buf;
    cJSON *o;
    double ts;
    struct jc_audit_row *r;
    const char *cmd;

    if (len == 0) {
        return;
    }
    buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    o = cJSON_Parse(buf);
    free(buf);
    if (o == NULL) {
        s->malformed++;
        return;
    }
    ts = jc_json_get_num(o, "ts", 0.0);
    if (since_ts > 0.0 && ts < since_ts) {
        s->skipped++;
        cJSON_Delete(o);
        return;
    }

    s->total++;
    {
        const char *dec = jc_json_get_str(o, "decision", "?");
        if (jc_auditview_is_refusal(dec)) {
            s->refused++;
        } else {
            s->ran++;
        }
        count_bump(&s->by_decision, dec);
    }
    count_bump(&s->by_launcher, jc_json_get_str(o, "launcher", "?"));

    /* Ring of newest entries: overwrite oldest once full. */
    r = &s->recent[s->rpos];
    s->rpos = (s->rpos + 1) % JC_AUDITVIEW_RECENT;
    if (s->nrecent < JC_AUDITVIEW_RECENT) {
        s->nrecent++;
    }
    memset(r, 0, sizeof(*r));
    r->ts = ts;
    copy_field(r->launcher, sizeof(r->launcher), o, "launcher");
    copy_field(r->decision, sizeof(r->decision), o, "decision");
    copy_field(r->mode, sizeof(r->mode), o, "mode");
    cmd = jc_json_get_str(o, "command", "");
    if (strlen(cmd) >= sizeof(r->cmd)) {
        memcpy(r->cmd, cmd, sizeof(r->cmd) - 4);
        strcpy(r->cmd + sizeof(r->cmd) - 4, "...");
    } else {
        strcpy(r->cmd, cmd);
    }

    cJSON_Delete(o);
}

void jc_auditview_feed(struct jc_audit_summary *s, const char *text,
                       double since_ts)
{
    const char *p;

    if (s == NULL || text == NULL) {
        return;
    }
    p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - p) : strlen(p);
        feed_line(s, p, len, since_ts);
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
}

static void render_counts(const struct jc_vec *v, const char *label,
                          struct jc_sb *out)
{
    jc_size i;

    if (v->len == 0) {
        return;
    }
    jc_sb_append_fmt(out, "  by %s:", label);
    for (i = 0; i < v->len; i++) {
        const struct jc_audit_count *e =
            (const struct jc_audit_count *)jc_vec_at((struct jc_vec *)v, i);
        jc_sb_append_fmt(out, "%s %s %ld", i == 0 ? "" : ",", e->name, e->n);
    }
    jc_sb_append(out, "\n");
}

static void render_ts(double ts, char *buf, jc_size cap)
{
    time_t t = (time_t)ts;
    struct tm *tm;

    if (ts <= 0.0) {
        jc_snprintf(buf, cap, "%s", "?");
        return;
    }
    tm = localtime(&t);
    if (tm == NULL || strftime(buf, cap, "%Y-%m-%d %H:%M", tm) == 0) {
        jc_snprintf(buf, cap, "%.0f", ts);
    }
}

static cJSON *counts_json(const struct jc_vec *v)
{
    cJSON *o = cJSON_CreateObject();
    jc_size i;

    if (o == NULL) {
        return NULL;
    }
    for (i = 0; i < v->len; i++) {
        const struct jc_audit_count *e =
            (const struct jc_audit_count *)jc_vec_at((struct jc_vec *)v, i);
        cJSON_AddNumberToObject(o, e->name, (double)e->n);
    }
    return o;
}

cJSON *jc_auditview_json(const struct jc_audit_summary *s)
{
    cJSON *o;
    cJSON *arr;
    int i;

    if (s == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", 1);
    cJSON_AddNumberToObject(o, "total", (double)s->total);
    cJSON_AddNumberToObject(o, "refused", (double)s->refused);
    cJSON_AddNumberToObject(o, "ran", (double)s->ran);
    cJSON_AddNumberToObject(o, "skipped", (double)s->skipped);
    cJSON_AddNumberToObject(o, "malformed", (double)s->malformed);
    cJSON_AddItemToObject(o, "by_decision", counts_json(&s->by_decision));
    cJSON_AddItemToObject(o, "by_launcher", counts_json(&s->by_launcher));

    arr = cJSON_CreateArray();
    if (arr != NULL) {
        /* Oldest-first, same order as the text renderer. */
        for (i = 0; i < s->nrecent; i++) {
            int idx = (s->nrecent == JC_AUDITVIEW_RECENT)
                          ? (s->rpos + i) % JC_AUDITVIEW_RECENT
                          : i;
            const struct jc_audit_row *r = &s->recent[idx];
            cJSON *e = cJSON_CreateObject();
            if (e == NULL) {
                continue;
            }
            cJSON_AddNumberToObject(e, "ts", r->ts);
            cJSON_AddStringToObject(e, "launcher", r->launcher);
            cJSON_AddStringToObject(e, "decision", r->decision);
            cJSON_AddStringToObject(e, "mode", r->mode);
            cJSON_AddStringToObject(e, "command", r->cmd);
            cJSON_AddItemToArray(arr, e);
        }
    }
    cJSON_AddItemToObject(o, "recent", arr);
    return o;
}

void jc_auditview_render(const struct jc_audit_summary *s, struct jc_sb *out)
{
    int i;

    if (s->total == 0) {
        jc_sb_append(out, "no privileged-command attempts recorded");
        if (s->skipped > 0) {
            jc_sb_append_fmt(out, " in the window (%ld older entries)",
                             s->skipped);
        }
        jc_sb_append(out, "\n");
        return;
    }

    jc_sb_append_fmt(out, "%ld privileged-command attempt%s: "
                     "%ld refused, %ld ran\n",
                     s->total, s->total == 1 ? "" : "s",
                     s->refused, s->ran);
    render_counts(&s->by_decision, "decision", out);
    render_counts(&s->by_launcher, "launcher", out);
    if (s->skipped > 0) {
        jc_sb_append_fmt(out, "  (%ld older entr%s outside --since window)\n",
                         s->skipped, s->skipped == 1 ? "y" : "ies");
    }
    if (s->malformed > 0) {
        jc_sb_append_fmt(out, "  (%ld malformed line%s skipped)\n",
                         s->malformed, s->malformed == 1 ? "" : "s");
    }

    if (s->nrecent > 0) {
        jc_sb_append_fmt(out, "\nmost recent (up to %d):\n",
                         JC_AUDITVIEW_RECENT);
        /* Oldest-first: when the ring is full the oldest sits at rpos. */
        for (i = 0; i < s->nrecent; i++) {
            int idx = (s->nrecent == JC_AUDITVIEW_RECENT)
                          ? (s->rpos + i) % JC_AUDITVIEW_RECENT
                          : i;
            const struct jc_audit_row *r = &s->recent[idx];
            char when[24];
            render_ts(r->ts, when, sizeof(when));
            jc_sb_append_fmt(out, "  %-16s %-8s %-18s %s\n",
                             when, r->launcher, r->decision, r->cmd);
        }
    }
}
