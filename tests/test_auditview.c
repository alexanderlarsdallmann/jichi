/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_auditview.c - the pure privileged-audit summarizer (M158). */

#include "jc_test.h"
#include "jc_auditview.h"

#include <stdlib.h>
#include <string.h>

static const char *LOG =
    "{\"v\":1,\"ts\":1000,\"launcher\":\"sudo\","
    "\"decision\":\"unattended_refused\",\"mode\":\"auto\","
    "\"command\":\"sudo apt-get update\"}\n"
    "{\"v\":1,\"ts\":2000,\"launcher\":\"sudo\","
    "\"decision\":\"allowlist\",\"mode\":\"auto\","
    "\"command\":\"sudo systemctl restart myapp\"}\n"
    "\n"
    "not json\n"
    "{\"v\":1,\"ts\":3000,\"launcher\":\"doas\","
    "\"decision\":\"ask_denied\",\"mode\":\"chat\","
    "\"command\":\"doas rm -rf /tmp/x\"}\n";

static void test_feed_counts(void)
{
    struct jc_audit_summary s;
    struct jc_sb out;

    jc_auditview_init(&s);
    jc_auditview_feed(&s, LOG, 0.0);
    JC_CHECK(s.total == 3);
    JC_CHECK(s.refused == 2);   /* unattended_refused + ask_denied */
    JC_CHECK(s.ran == 1);       /* allowlist */
    JC_CHECK(s.malformed == 1); /* "not json" */
    JC_CHECK(s.skipped == 0);
    JC_CHECK(s.by_decision.len == 3);
    JC_CHECK(s.by_launcher.len == 2);
    JC_CHECK(s.nrecent == 3);

    jc_sb_init(&out);
    jc_auditview_render(&s, &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "3 privileged-command attempts") != NULL);
        JC_CHECK(strstr(out.data, "2 refused, 1 ran") != NULL);
        JC_CHECK(strstr(out.data, "sudo 2") != NULL);
        JC_CHECK(strstr(out.data, "unattended_refused") != NULL);
        JC_CHECK(strstr(out.data, "sudo apt-get update") != NULL);
        JC_CHECK(strstr(out.data, "malformed") != NULL);
    }
    jc_sb_free(&out);
    jc_auditview_free(&s);
}

static void test_since_cutoff(void)
{
    struct jc_audit_summary s;

    jc_auditview_init(&s);
    jc_auditview_feed(&s, LOG, 1500.0); /* drops the ts=1000 entry */
    JC_CHECK(s.total == 2);
    JC_CHECK(s.skipped == 1);
    JC_CHECK(s.refused == 1);
    jc_auditview_free(&s);
}

static void test_ring_and_empty(void)
{
    struct jc_audit_summary s;
    struct jc_sb out;
    struct jc_sb log;
    int i;

    /* More entries than the ring: the newest survive, oldest-first render. */
    jc_sb_init(&log);
    for (i = 0; i < JC_AUDITVIEW_RECENT + 5; i++) {
        jc_sb_append_fmt(&log,
            "{\"ts\":%d,\"launcher\":\"sudo\",\"decision\":\"deny\","
            "\"command\":\"cmd-%d\"}\n", 1000 + i, i);
    }
    jc_auditview_init(&s);
    jc_auditview_feed(&s, log.data, 0.0);
    JC_CHECK(s.total == JC_AUDITVIEW_RECENT + 5);
    JC_CHECK(s.nrecent == JC_AUDITVIEW_RECENT);
    /* The oldest surviving entry is #5; #4 was evicted. */
    jc_sb_init(&out);
    jc_auditview_render(&s, &out);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "cmd-5") != NULL);
        JC_CHECK(strstr(out.data, "cmd-4") == NULL);
        JC_CHECK(strstr(out.data,
                        "cmd-14") != NULL); /* newest kept */
    }
    jc_sb_free(&out);
    jc_sb_free(&log);
    jc_auditview_free(&s);

    /* Empty log renders the no-attempts line. */
    jc_auditview_init(&s);
    jc_auditview_feed(&s, "", 0.0);
    jc_sb_init(&out);
    jc_auditview_render(&s, &out);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "no privileged-command attempts") != NULL);
    jc_sb_free(&out);
    jc_auditview_free(&s);

    /* Refusal classifier. */
    JC_CHECK(jc_auditview_is_refusal("deny") == 1);
    JC_CHECK(jc_auditview_is_refusal("ask_denied") == 1);
    JC_CHECK(jc_auditview_is_refusal("unattended_refused") == 1);
    JC_CHECK(jc_auditview_is_refusal("allowlist") == 0);
    JC_CHECK(jc_auditview_is_refusal("allow") == 0);
    JC_CHECK(jc_auditview_is_refusal("ask_approved") == 0);
    JC_CHECK(jc_auditview_is_refusal(NULL) == 0);
}

static void test_audit_json(void)
{
    struct jc_audit_summary s;
    cJSON *o;
    char *js;

    jc_auditview_init(&s);
    jc_auditview_feed(&s, LOG, 0.0);
    o = jc_auditview_json(&s);
    JC_CHECK(o != NULL);
    if (o != NULL) {
        const cJSON *bd = cJSON_GetObjectItem(o, "by_decision");
        const cJSON *rec = cJSON_GetObjectItem(o, "recent");
        JC_CHECK(jc_json_get_num(o, "v", 0) == 1.0);
        JC_CHECK(jc_json_get_num(o, "total", 0) == 3.0);
        JC_CHECK(jc_json_get_num(o, "refused", 0) == 2.0);
        JC_CHECK(jc_json_get_num(o, "ran", 0) == 1.0);
        JC_CHECK(jc_json_get_num(o, "malformed", 0) == 1.0);
        JC_CHECK(cJSON_IsObject(bd) &&
                 jc_json_get_num(bd, "ask_denied", 0) == 1.0);
        JC_CHECK(cJSON_IsArray(rec) && cJSON_GetArraySize(rec) == 3);
        js = cJSON_PrintUnformatted(o);
        JC_CHECK(js != NULL);
        if (js != NULL) {
            JC_CHECK(strstr(js, "sudo apt-get update") != NULL);
            free(js);
        }
        cJSON_Delete(o);
    }
    jc_auditview_free(&s);
    JC_CHECK(jc_auditview_json(NULL) == NULL);
}

void test_auditview(void)
{
    test_feed_counts();
    test_since_cutoff();
    test_ring_and_empty();
    test_audit_json();
}
