/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_perm.c - agent modes and the pure per-tool permission resolver.
 *
 * No I/O and no provider/network dependencies, so the resolver is exercised
 * offline by the test suite. See docs/AGENT_MODES.md for the model.
 */

#include "jc_perm.h"
#include "jc_mcp.h" /* enum jc_mcp_approval (value-coupling check below) */

#include <string.h>

/* The agent loop casts between jc_mcp_approval and jc_approval; this fails to
 * compile if their values ever diverge. */
typedef char jc_perm_enum_check[
    (JC_APPROVAL_ASK == (int)JC_MCP_APPROVAL_ASK &&
     JC_APPROVAL_ALLOW == (int)JC_MCP_APPROVAL_ALLOW &&
     JC_APPROVAL_DENY == (int)JC_MCP_APPROVAL_DENY) ? 1 : -1];

enum jc_approval jc_perm_for_tool(enum jc_agent_mode mode, int tool_readonly,
                                  int in_allow, int in_deny,
                                  enum jc_approval mcp_policy)
{
    /* 1. deny dominates (config deny list or an MCP deny). */
    if (in_deny || mcp_policy == JC_APPROVAL_DENY) {
        return JC_APPROVAL_DENY;
    }
    /* 2. plan mode forbids mutation, even if allow-listed. */
    if (mode == JC_MODE_PLAN && !tool_readonly) {
        return JC_APPROVAL_DENY;
    }
    /* 3. explicit allow-list overrides the mode baseline. */
    if (in_allow) {
        return JC_APPROVAL_ALLOW;
    }
    /* 4. an MCP autoApprove. */
    if (mcp_policy == JC_APPROVAL_ALLOW) {
        return JC_APPROVAL_ALLOW;
    }
    /* 5. mode baseline. */
    if (mode == JC_MODE_AUTO) {
        return JC_APPROVAL_ALLOW;
    }
    return tool_readonly ? JC_APPROVAL_ALLOW : JC_APPROVAL_ASK;
}

int jc_perm_mode_rank(enum jc_agent_mode mode)
{
    /* An explicit table, not arithmetic on the enum: the enum is declared CHAT,
     * PLAN, AUTO, so any expression using its values directly would get the safety
     * ordering wrong in a way that reads as correct. */
    switch (mode) {
    case JC_MODE_AUTO: return 0;   /* widest: no approval prompts        */
    case JC_MODE_CHAT: return 1;   /* asks before mutating               */
    case JC_MODE_PLAN: return 2;   /* narrowest: read-only               */
    default:           return 1;   /* unknown: treat as the middle       */
    }
}

int jc_perm_mode_narrows(enum jc_agent_mode from, enum jc_agent_mode to)
{
    return jc_perm_mode_rank(to) > jc_perm_mode_rank(from) ? 1 : 0;
}

int jc_agent_mode_parse(const char *name, enum jc_agent_mode *out)
{
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "chat") == 0) {
        *out = JC_MODE_CHAT;
        return 1;
    }
    if (strcmp(name, "plan") == 0) {
        *out = JC_MODE_PLAN;
        return 1;
    }
    if (strcmp(name, "auto") == 0) {
        *out = JC_MODE_AUTO;
        return 1;
    }
    return 0;
}

const char *jc_agent_mode_name(enum jc_agent_mode mode)
{
    switch (mode) {
    case JC_MODE_PLAN: return "plan";
    case JC_MODE_AUTO: return "auto";
    case JC_MODE_CHAT: return "chat";
    }
    return "chat";
}

static int name_in(const struct jc_vec *list, int all, const char *name)
{
    jc_size i;
    if (all) {
        return 1;
    }
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < list->len; i++) {
        const char *s = *(char **)jc_vec_at((struct jc_vec *)list, i);
        if (s != NULL && strcmp(s, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int jc_perm_name_in_allow(const struct jc_permissions *p, const char *name)
{
    if (p == NULL) {
        return 0;
    }
    return name_in(&p->allow, p->allow_all, name);
}

int jc_perm_name_in_deny(const struct jc_permissions *p, const char *name)
{
    if (p == NULL) {
        return 0;
    }
    return name_in(&p->deny, p->deny_all, name);
}
