#!/bin/sh
# smoke: the depth-0 hook rule's consequence for delegated writes is pinned
# (B-10, M1117).
#
# THE DEFECT, from the 2026-10-17 process review (finding B-10): hooks fire
# only for the top-level agent -- the depth gate returns early wherever the
# call happens below it. docs/HOOKS.md says the top-level half ("never inside
# a subagent") but nowhere names the spawn_parallel fork-pool child, and
# nowhere states the consequence: an operator who believes "every write is
# gated" by a PreToolUse hook is wrong for delegated writes; the gate that
# covers those is the per-tool fence (path fence, edit scope, mode), and the
# hook is the operator's top-level veto. This driver pins that HOOKS.md says
# so.
#
# The finding's OTHER half -- HOOKS.md promised env vars that the hook child
# does not get -- closed before this drive at 267189de (docs/HOOKS.md:60 now
# says the fields are delivered on stdin as JSON, not as environment
# variables). Nothing of that half may be re-changed; the driver does not
# touch it.
#
# CHECK 1 is the mechanism's own line -- a src regression that deleted the
# depth gate (or renamed the top-level wording) turns this red. CHECK 2 is
# the documentation: red on the tree that carries the behaviour without
# saying it. CHECK 3 pins the M130 env sentence ("minus the secret
# variables", case-insensitive) so a future scrub redesign or re-wording of
# that sentence cannot pass every gate (M1117-b, hunk-audit F2 on 846cc123).
#
# Reads src/ and docs/ only, so it runs without a built binary -- that is why
# it is exempt from smoke_lint check 5 by name (the case at smoke_lint.sh's
# check 5; the docs_flags.sh shape -- a driver whose name keeps the docs_*
# convention rather than *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# --- 1: the mechanism still exists ---------------------------------------------
if "$G" -q 'agent_depth != 0' "$SMOKE_ROOT/src/chat/jc_hooks.c" \
    && "$G" -qi 'top level' "$SMOKE_ROOT/docs/HOOKS.md"; then
    t_ok "the hooks depth gate still exists in src, and HOOKS.md still states the top-level rule"
else
    t_fail "the hooks depth gate (agent_depth != 0) or the top-level wording is missing -- the documented behaviour is gone (or renamed); re-check the docs against it"
fi

# --- 2: the consequence for delegated writes is documented ----------------------
# The RED-ON-HEAD half: before M1117 HOOKS.md contained neither the
# spawn_parallel child nor the "every write is gated" consequence, so a tree
# that carries the depth gate without stating what it excludes is caught here.
m_par=$("$G" -qi 'spawn_parallel' "$SMOKE_ROOT/docs/HOOKS.md" && echo found || echo missing)
m_gate=$("$G" -q 'every write is gated' "$SMOKE_ROOT/docs/HOOKS.md" && echo found || echo missing)
if [ "$m_par" = found ] && [ "$m_gate" = found ]; then
    t_ok "HOOKS.md names the spawn_parallel child and the 'every write is gated' consequence for delegated writes"
else
    t_fail "the depth-0 rule's consequence for delegated writes is not documented: HOOKS.md spawn_parallel=$m_par 'every write is gated'=$m_gate (both must be stated; the finding is B-10, M1117)"
fi

# --- 3: the M130 env sentence is pinned -----------------------------------------
# HOOKS.md carries the one sentence that states what a hook child's
# environment is: jichi's environment minus the secret variables (the M130
# scrub). "minus the secret variables" is that sentence's stable fragment
# (case-insensitive). A scrub redesign or a re-wording of the sentence
# passes every gate without this pin (hunk-audit finding F2 on 846cc123);
# it goes red here.
m_env=$("$G" -qi 'minus the secret variables' "$SMOKE_ROOT/docs/HOOKS.md" && echo found || echo missing)
if [ "$m_env" = found ]; then
    t_ok "HOOKS.md carries the M130 scrub sentence: the hook child runs with jichi's environment minus the secret variables"
else
    t_fail "the M130-scrub sentence is gone from docs/HOOKS.md: 'minus the secret variables' (case-insensitive)=$m_env -- a hook child's environment is now unstated (the finding is B-10, M1117-b)"
fi

t_done
