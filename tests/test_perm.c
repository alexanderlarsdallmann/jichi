/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_perm.c - offline tests for the agent-mode permission resolver. */

#include "jc_test.h"
#include "jc_perm.h"
#include "jc_vec.h"

#include <stdlib.h>
#include <string.h>

/* Shorthands for the verdict enum. */
#define ASK   JC_APPROVAL_ASK
#define ALLOW JC_APPROVAL_ALLOW
#define DENY  JC_APPROVAL_DENY
#define RO 1  /* read-only tool */
#define MU 0  /* mutating tool  */

static void test_resolver_truth_table(void)
{
    /* deny dominates regardless of everything else. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_AUTO, RO, 1, 1, ALLOW) == DENY);
    JC_CHECK(jc_perm_for_tool(JC_MODE_CHAT, MU, 0, 0, DENY) == DENY);

    /* plan mode forbids mutation even when allow-listed. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_PLAN, MU, 1, 0, ASK) == DENY);
    JC_CHECK(jc_perm_for_tool(JC_MODE_PLAN, MU, 0, 0, ASK) == DENY);
    /* plan mode still permits read-only tools. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_PLAN, RO, 0, 0, ASK) == ALLOW);

    /* auto allows anything not denied. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_AUTO, MU, 0, 0, ASK) == ALLOW);
    JC_CHECK(jc_perm_for_tool(JC_MODE_AUTO, RO, 0, 0, ASK) == ALLOW);

    /* chat: safe tools run, mutating tools ask. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_CHAT, RO, 0, 0, ASK) == ALLOW);
    JC_CHECK(jc_perm_for_tool(JC_MODE_CHAT, MU, 0, 0, ASK) == ASK);
    /* chat: explicit allow-list lifts a mutating tool to ALLOW. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_CHAT, MU, 1, 0, ASK) == ALLOW);
    /* chat: an MCP autoApprove also lifts it. */
    JC_CHECK(jc_perm_for_tool(JC_MODE_CHAT, MU, 0, 0, ALLOW) == ALLOW);
}

static void test_mode_parse_name(void)
{
    enum jc_agent_mode m = JC_MODE_AUTO;
    JC_CHECK(jc_agent_mode_parse("chat", &m) == 1 && m == JC_MODE_CHAT);
    JC_CHECK(jc_agent_mode_parse("plan", &m) == 1 && m == JC_MODE_PLAN);
    JC_CHECK(jc_agent_mode_parse("auto", &m) == 1 && m == JC_MODE_AUTO);
    JC_CHECK(jc_agent_mode_parse("bogus", &m) == 0);
    JC_CHECK(jc_agent_mode_parse(NULL, &m) == 0);

    JC_CHECK_STR(jc_agent_mode_name(JC_MODE_CHAT), "chat");
    JC_CHECK_STR(jc_agent_mode_name(JC_MODE_PLAN), "plan");
    JC_CHECK_STR(jc_agent_mode_name(JC_MODE_AUTO), "auto");
}

/* Push a malloc'd copy of `s` onto a char* vector. */
static void push_str(struct jc_vec *v, const char *s)
{
    char *c = (char *)malloc(strlen(s) + 1);
    strcpy(c, s);
    jc_vec_push(v, &c);
}

static void free_strs(struct jc_vec *v)
{
    jc_size i;
    for (i = 0; i < v->len; i++) {
        free(JC_VEC_STR(v, i));
    }
    jc_vec_free(v);
}

static void test_name_membership(void)
{
    struct jc_permissions p;
    memset(&p, 0, sizeof(p));
    jc_vec_init(&p.allow, sizeof(char *));
    jc_vec_init(&p.deny, sizeof(char *));

    push_str(&p.allow, "edit_file");
    JC_CHECK(jc_perm_name_in_allow(&p, "edit_file") == 1);
    JC_CHECK(jc_perm_name_in_allow(&p, "write_file") == 0);
    JC_CHECK(jc_perm_name_in_deny(&p, "edit_file") == 0);

    /* "*"/true sets the _all flag => matches anything. */
    p.deny_all = 1;
    JC_CHECK(jc_perm_name_in_deny(&p, "anything") == 1);

    /* NULL permissions are safe and match nothing. */
    JC_CHECK(jc_perm_name_in_allow(NULL, "x") == 0);
    JC_CHECK(jc_perm_name_in_deny(NULL, "x") == 0);

    free_strs(&p.allow);
    free_strs(&p.deny);
}

void test_perm(void)
{
    test_resolver_truth_table();
    test_mode_parse_name();
    test_name_membership();
}

/* M304: narrowing the posture mid-run. The ordering is the substance here, because
 * `enum jc_agent_mode` is declared CHAT, PLAN, AUTO -- so comparing the enum values
 * directly would make auto->chat look like a widening and get the safety property
 * exactly backwards. */
void test_perm_mode_narrow(void)
{
    /* The safety order, which is NOT the enum order. */
    JC_CHECK(jc_perm_mode_rank(JC_MODE_AUTO) < jc_perm_mode_rank(JC_MODE_CHAT));
    JC_CHECK(jc_perm_mode_rank(JC_MODE_CHAT) < jc_perm_mode_rank(JC_MODE_PLAN));
    /* Stated explicitly: the enum's numeric order disagrees, which is the trap. */
    JC_CHECK((int)JC_MODE_AUTO > (int)JC_MODE_CHAT);
    JC_CHECK(jc_perm_mode_rank(JC_MODE_AUTO) < jc_perm_mode_rank(JC_MODE_CHAT));

    /* Narrowing: allowed. */
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_AUTO, JC_MODE_CHAT) == 1);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_AUTO, JC_MODE_PLAN) == 1);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_CHAT, JC_MODE_PLAN) == 1);

    /* Widening: refused. A run an operator can loosen from outside is a
     * privilege-escalation surface wearing a convenience hat. */
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_PLAN, JC_MODE_CHAT) == 0);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_PLAN, JC_MODE_AUTO) == 0);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_CHAT, JC_MODE_AUTO) == 0);

    /* No move is not a narrowing (the caller reports "already in X"). */
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_AUTO, JC_MODE_AUTO) == 0);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_CHAT, JC_MODE_CHAT) == 0);
    JC_CHECK(jc_perm_mode_narrows(JC_MODE_PLAN, JC_MODE_PLAN) == 0);

    /* Every ordered pair is decided one way only -- narrowing must be a strict
     * order, so no pair may be narrowing in BOTH directions. */
    {
        enum jc_agent_mode all[3];
        int i, j;
        all[0] = JC_MODE_CHAT; all[1] = JC_MODE_PLAN; all[2] = JC_MODE_AUTO;
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                if (jc_perm_mode_narrows(all[i], all[j])) {
                    JC_CHECK(!jc_perm_mode_narrows(all[j], all[i]));
                }
            }
        }
    }

    /* An out-of-range value is treated as the middle rather than as widest, so a
     * corrupted mode cannot be used to claim a narrowing to AUTO. */
    JC_CHECK(jc_perm_mode_narrows((enum jc_agent_mode)99, JC_MODE_AUTO) == 0);
    JC_CHECK(jc_perm_mode_narrows((enum jc_agent_mode)99, JC_MODE_PLAN) == 1);
}
