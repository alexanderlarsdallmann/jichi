#!/bin/sh
# smoke lint: ENFORCED IMPLIES STATED -- every gate that fires on a SUBAGENT must be
# named in the subagent's prompt (M434). A pure lint; it reads the source.
#
# THE DEFECT THIS EXISTS FOR. jc_sysmsg_build_sub was PROMPT_SUB + append_env +
# system_prompt_extra and nothing else, while three gates hold at any depth:
# env_scope_fence (M133), the constraint gate (no depth check at all) and, since M431,
# the budget. So a delegate was FENCED BY RULES IT HAD NEVER SEEN -- and ANECDOTES #27
# records what a model does then: blocked on every write by a constraint inferred from
# the brief's prose, it never surfaced the block and spent 64 tool calls and a whole
# 1.5M budget trying to comply with something it could not read.
#
# The most serious omission was not efficiency: without the untrusted-content rule a
# subagent that fetches a URL has no statement that fetched content is data rather
# than instructions.
#
# WHAT THIS CHECKS: that the sub-prompt builder invokes the renderer for each rule
# enforced at depth. It checks the WIRING, not the wording -- a lint over prose would
# be guessing about English, and the M295 rule is to pin facts about C instead.
#
# WHAT IT DELIBERATELY DOES NOT CHECK: that the subagent gets the repo map, the skills
# catalogue, project rules or memory. Those are CONTEXT, not enforcement, and copying
# them would defeat the isolation that is the reason to delegate. The line is drawn at
# enforcement, and this lint is where that line is written down.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)
root=$(cd "$(dirname "$0")/../.." && pwd)
SM="$root/src/chat/jc_sysmsg.c"
AG="$root/src/chat/jc_agent.c"

# The sub builder's body, from its signature to the closing brace at column 1.
# M596: the body moved into jc_sysmsg_build_sub_as (persona-aware); the plain
# builder is a one-line wrapper. Extract the body, and check 4 pins the wrapper.
awk '/^char \*jc_sysmsg_build_sub_as/ { f=1 } f { print } f && /^}/ { exit }' \
    "$SM" > "$tmp/sub"
nl=$(grep -c . "$tmp/sub")

# --- 0: the extraction floor -------------------------------------------------
# A renamed function or a changed brace style must fail LOUDLY rather than leave the
# checks below scanning an empty file and passing (docs/TEST_INTEGRITY.md).
if [ "$nl" -ge 15 ]; then
    t_ok "extracted jc_sysmsg_build_sub_as ($nl lines)"
else
    t_fail "extraction floor tripped ($nl lines) -- the builder moved or was renamed; fix the extraction, never the floor"
fi

# --- 1: the untrusted-content rule, UNCONDITIONALLY --------------------------
# Unconditional because M300's rule holds here too: turning off prose guidance must
# not turn off a safety convention. So the call must not sit behind an `if`.
if grep -q "jc_untrusted_prompt_rule" "$tmp/sub"; then
    if ! grep -B 2 "jc_untrusted_prompt_rule" "$tmp/sub" | grep -q "^.*if ("; then
        t_ok "the untrusted-content rule is stated, and unconditionally"
    else
        t_fail "the untrusted-content rule is behind a condition -- a safety convention must not be optional"
    fi
else
    t_fail "the subagent prompt does not state the untrusted-content rule, yet a subagent can fetch a URL"
fi

# --- 2: the constraints, which the gate refuses at ANY depth ------------------
# Proven from the gate itself: if jc_constraint_blocks has no agent_depth guard, the
# rule binds a subagent, so the subagent must be told. The check is derived rather
# than hardcoded, so removing the depth-independence would relax the lint honestly.
if grep -B 8 "jc_constraint_blocks(app->constraints" "$AG" | grep -q "agent_depth"; then
    t_ok "the constraint gate is depth-guarded, so the sub prompt need not state it"
else
    if grep -q "jc_constraint_render" "$tmp/sub"; then
        t_ok "the constraint gate binds a subagent, and the sub prompt states it"
    else
        t_fail "jc_constraint_blocks has no depth guard, so it binds a subagent -- but the sub prompt never names the constraints"
    fi
fi

# --- 3: the edit scope, which M133 fences at ANY depth -----------------------
# Same derivation: env_scope_fence's own comment says it re-arms the fence for
# delegated writers, so the globs must reach the delegate's prompt.
if grep -A 4 "^static int env_scope_fence" "$AG" | grep -q "agent_depth"; then
    t_ok "the scope fence is depth-guarded, so the sub prompt need not state it"
else
    if grep -q "jc_sysmsg_append_scope_reach" "$tmp/sub"; then
        t_ok "the scope fence binds a subagent, and the sub prompt names its globs"
    else
        t_fail "env_scope_fence holds at any depth but the sub prompt never names the edit scope -- one run guessed 177 times"
    fi
fi

# --- 4: one body (M596) -------------------------------------------------------
# jc_sysmsg_build_sub must be a wrapper that delegates to jc_sysmsg_build_sub_as, so
# the enforced sections have exactly one home and checks 1-3 read the real one. A
# second full body here would be the pre-M596 shape: two builders, one of which
# drops the rules.
awk '/^char \*jc_sysmsg_build_sub\(/ { f=1 } f { print } f && /^}/ { exit }' \
    "$SM" > "$tmp/wrap"
if grep -q "return jc_sysmsg_build_sub_as(app, NULL, NULL);" "$tmp/wrap" && \
   ! grep -q "jc_sb_append" "$tmp/wrap"; then
    t_ok "jc_sysmsg_build_sub delegates to jc_sysmsg_build_sub_as (one body)"
else
    t_fail "jc_sysmsg_build_sub is not a plain wrapper over jc_sysmsg_build_sub_as -- two builders, and the rules can drift between them"
fi

t_done
