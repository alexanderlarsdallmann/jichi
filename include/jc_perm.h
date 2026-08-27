/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_perm.h - agent operating modes and the per-tool permission resolver.
 *
 * A mode (plan / chat / auto) is a posture that sets the low-level switches on
 * jc_app. Every tool call is then resolved to one verdict (ASK/ALLOW/DENY) by
 * the pure function jc_perm_for_tool, which composes the mode baseline, the
 * top-level config allow/deny lists, and the per-server MCP policy. See
 * docs/AGENT_MODES.md for the model and the truth table.
 *
 * jc_perm_for_tool takes already-resolved inputs (not live managers) so it is
 * pure and unit-tested offline.
 */
#ifndef JC_PERM_H
#define JC_PERM_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_config.h" /* struct jc_permissions */

/* A tool-call verdict. Value-compatible with enum jc_mcp_approval (jc_mcp.h);
 * a compile-time check in jc_perm.c guards the coupling. */
enum jc_approval {
    JC_APPROVAL_ASK = 0,
    JC_APPROVAL_ALLOW,
    JC_APPROVAL_DENY
};

/* Agent operating mode. Stored on jc_app/jc_config as a plain int to avoid an
 * include cycle (those headers do not include this one). */
enum jc_agent_mode {
    JC_MODE_CHAT = 0, /* safe tools run, mutating tools ask (the default) */
    JC_MODE_PLAN,     /* read-only fence; the model proposes a plan       */
    JC_MODE_AUTO      /* auto-approve, bounded by the iteration budget    */
};

/* Resolve a single tool call's verdict. `tool_readonly` is the tool's readonly
 * flag; `in_allow`/`in_deny` are membership in the config allow/deny lists;
 * `mcp_policy` is the per-server MCP verdict (JC_APPROVAL_ASK for non-MCP
 * tools). Deny dominates; see the truth table in docs/AGENT_MODES.md. */
enum jc_approval jc_perm_for_tool(enum jc_agent_mode mode, int tool_readonly,
                                  int in_allow, int in_deny,
                                  enum jc_approval mcp_policy);

/* --- narrowing the posture mid-run (M304) ----------------------------------
 *
 * How much the agent may do WITHOUT ASKING, as a total order. Note this is NOT the
 * enum's order, and the difference is the whole point of having a function:
 *
 *   auto (0)  auto-approves every permitted tool          -- widest
 *   chat (1)  asks before a mutating tool
 *   plan (2)  read-only fence; nothing mutates at all     -- narrowest
 *
 * `enum jc_agent_mode` is declared CHAT, PLAN, AUTO for historical reasons, so
 * comparing the enum values directly would let auto->chat look like a WIDENING and
 * chat->plan look like a narrowing by accident. Anyone tempted to write
 * `to > from` should read this twice. */
int jc_perm_mode_rank(enum jc_agent_mode mode);

/* Would switching from `from` to `to` strictly REDUCE what the agent may do
 * unattended? 1 yes, 0 no (equal, or wider).
 *
 * The mid-run mode change is deliberately one-way. The control channel's founding
 * rule is that it never widens -- no approve verb, no budget or scope changes -- and
 * a run an operator can loosen from outside is a privilege-escalation surface
 * wearing a convenience hat. A run you can only tighten is safe to expose. Pure;
 * unit-tested. */
int jc_perm_mode_narrows(enum jc_agent_mode from, enum jc_agent_mode to);

/* Parse "chat"/"plan"/"auto" into *out. Returns 1 on success, 0 if unknown. */
int jc_agent_mode_parse(const char *name, enum jc_agent_mode *out);

/* Stable lowercase name for a mode ("chat"/"plan"/"auto"). Never NULL. */
const char *jc_agent_mode_name(enum jc_agent_mode mode);

/* Membership tests against a permissions set (an "*"/true list matches any
 * name). NULL `p` => 0. */
int jc_perm_name_in_allow(const struct jc_permissions *p, const char *name);
int jc_perm_name_in_deny(const struct jc_permissions *p, const char *name);

#ifdef __cplusplus
}
#endif
#endif /* JC_PERM_H */
