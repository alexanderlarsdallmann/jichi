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

enum jc_approval jc_perm_for_tool_why(enum jc_agent_mode mode, int tool_readonly,
                                      int in_allow, int in_deny,
                                      enum jc_approval mcp_policy,
                                      const char **why)
{
    const char *w;
    enum jc_approval v;

    /* 1. deny dominates (config deny list or an MCP deny). */
    if (in_deny) {
        w = "your deny list (permissions.deny) names it";
        v = JC_APPROVAL_DENY;
    } else if (mcp_policy == JC_APPROVAL_DENY) {
        w = "its MCP server's policy denies it";
        v = JC_APPROVAL_DENY;
    /* 2. plan mode forbids mutation, even if allow-listed. */
    } else if (mode == JC_MODE_PLAN && !tool_readonly) {
        w = "plan mode runs no tool that changes anything";
        v = JC_APPROVAL_DENY;
    /* 3. explicit allow-list overrides the mode baseline. */
    } else if (in_allow) {
        w = "your allow list (permissions.allow) names it";
        v = JC_APPROVAL_ALLOW;
    /* 4. an MCP autoApprove. */
    } else if (mcp_policy == JC_APPROVAL_ALLOW) {
        w = "its MCP server is set to autoApprove";
        v = JC_APPROVAL_ALLOW;
    /* 5. mode baseline. */
    } else if (mode == JC_MODE_AUTO) {
        w = "auto mode runs every tool without asking";
        v = JC_APPROVAL_ALLOW;
    } else if (tool_readonly) {
        w = "it only reads, and chat mode runs a tool that only reads "
            "without asking";
        v = JC_APPROVAL_ALLOW;
    } else {
        w = "chat mode asks before a tool that changes something";
        v = JC_APPROVAL_ASK;
    }
    if (why != NULL) {
        *why = w;
    }
    return v;
}

enum jc_approval jc_perm_for_tool(enum jc_agent_mode mode, int tool_readonly,
                                  int in_allow, int in_deny,
                                  enum jc_approval mcp_policy)
{
    return jc_perm_for_tool_why(mode, tool_readonly, in_allow, in_deny,
                                mcp_policy, NULL);
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

enum jc_agent_mode jc_perm_saved_mode(enum jc_agent_mode current, int pinned,
                                      enum jc_agent_mode saved,
                                      enum jc_saved_mode_outcome *outcome)
{
    enum jc_saved_mode_outcome o;
    enum jc_agent_mode to = current;

    if (saved == current) {
        o = JC_SAVED_MODE_SAME;
    } else if (!jc_perm_mode_narrows(current, saved)) {
        o = JC_SAVED_MODE_KEPT_WIDER;
    } else if (pinned) {
        o = JC_SAVED_MODE_KEPT_PINNED;
    } else {
        o = JC_SAVED_MODE_NARROWED;
        to = saved;
    }
    if (outcome != NULL) {
        *outcome = o;
    }
    return to;
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
