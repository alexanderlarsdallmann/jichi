#!/bin/sh
# smoke: a tool the depth gate will refuse is not advertised at depth (M436).
#
# THE MEASURED WASTE. `todowrite`, `todoread` and `board` are `readonly: 1` -- they touch
# no filesystem and run no command, only agent state -- so they passed every filter in
# jc_tool_build_neutral_ex and were advertised to every subagent. Each was then refused
# at runtime by an `agent_depth > 0` check inside its own body. At the measured 25-42k
# input tokens per call on a cacheless backend, each of those round trips is real money,
# and the model learns nothing from the refusal that omission would not have told it
# sooner and for free.
#
# The GATE is right and stays: a delegate must not stomp the user's task list, which is
# shared with the human and outlives the subtask. Only the advertisement was wrong.
#
# WHY A SMOKE DRIVER AND NOT ONLY THE UNIT TEST. test_subagent's
# test_main_agent_only_advertising calls the builder directly with a depth argument. That
# proves the filter, not the WIRING -- whether run_agent_loop actually passes
# app->agent_depth rather than a literal 0. The ground truth for "what the subagent was
# told" is the captured request body, which is what this reads. The same distinction
# M429 turned on.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The subagent calls `todowrite` ANYWAY -- which is what a model does with a tool it
# remembers from training or from an earlier turn -- and then answers. That exercises
# both halves in one run: the ARRAY it was offered (checks 2-5) and the BACKSTOP that
# refuses the call (check 7). The backstop matters more than it looks: M436 deleted three
# hand-written `agent_depth > 0` checks from the tool bodies and replaced them with one
# check in jc_tool_execute, so if that chokepoint were wrong a delegate could now
# overwrite the user's task list.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "TOP run"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP run"
  tool spawn_subagent {"task":"SUBTASK investigate the readme"}
rule
  match "SUBTASK"
  match "\"role\":\"tool\""
  text SUBDONE
rule
  match "SUBTASK"
  tool todowrite {"todos":[{"content":"stomp the parent's list","status":"pending"}]}
EOF

echo "hello" > "$ws/README"

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT" '"board":true,"maxSubagentIters":6'

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 300k \
    -p "TOP run a subagent" < /dev/null > "$tmp/out.txt" 2>&1); rc=$?
mm_stop

# Partition the captured requests. Both kinds exist, so a check over "some request"
# would prove nothing -- but the DISCRIMINATOR has to be chosen carefully, and my first
# one was wrong in a way worth recording. Keying on the subagent's task text
# ("SUBTASK investigate") misfiles the top-level request made AFTER the spawn: that
# history contains the spawn_subagent tool call, whose arguments quote the task. The
# result was two false failures blaming the product for a fixture bug -- the same class
# as the M293/M296 PTY fixtures that matched a string printed by a different surface in
# the same transcript.
#
# The sound key is the TOP-LEVEL USER PROMPT, which a subagent's history never carries:
# a subagent is seeded with its own task, not with the parent's conversation.
sub=""; top=""
for f in "$tmp"/cap/req.*; do
    [ -f "$f" ] || continue
    if grep -q "TOP run a subagent" "$f"; then top="$top $f"; else sub="$sub $f"; fi
done

# --- 1: the fixture actually produced both kinds of request --------------------
# The extraction floor. Without it every check below could pass over an empty set --
# the failure mode docs/TEST_INTEGRITY.md names as a lint checking nothing.
if [ -n "$sub" ] && [ -n "$top" ]; then
    t_ok "captured both kinds ($(echo $top | wc -w) top, $(echo $sub | wc -w) sub)"
else
    t_fail "partition empty: sub='$sub' top='$top' of $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l)"
fi

# The ADVERTISED names, from the request's own `tools` array via jsonq -- not a grep
# over the whole body. Grepping `"name":"todowrite"` also matches the subagent's tool
# CALL and the tool RESULT in the same request, which is why check 2 first reported the
# tool as still advertised when it was not: the name appeared because the subagent had
# just called it. Reading the array is the only form that answers the question asked.
#
# The body is the last line of a capture file (HTTP headers, blank line, then JSON).
ads() {
    tail -1 "$1" > "$tmp/body.json" || return 1
    i=0
    while :; do
        n=$("$SMOKE_TOOLS/jsonq" ".tools[$i].function.name" "$tmp/body.json" \
              2>/dev/null) || break
        printf '%s\n' "$n"
        i=$((i + 1))
    done
}

# --- 2: the subagent is NOT offered the three main-agent tools -----------------
bad=""
for f in $sub; do
    for t in todowrite todoread board; do
        ads "$f" | grep -qx "$t" && bad="$bad $t"
    done
done
if [ -z "$bad" ]; then
    t_ok "the subagent's tool array omits todowrite, todoread and board"
else
    t_fail "still advertised to the subagent:$bad"
fi

# --- 3: the TOP-LEVEL agent still gets all three ------------------------------
# The check that makes check 2 mean something. A filter that dropped them everywhere
# would satisfy check 2 and break the feature for the agent it belongs to.
miss=""
for t in todowrite todoread board; do
    found=0
    for f in $top; do
        ads "$f" | grep -qx "$t" && found=1
    done
    [ "$found" = "1" ] || miss="$miss $t"
done
if [ -z "$miss" ]; then
    t_ok "the top-level agent is still offered all three"
else
    t_fail "the filter reached depth 0 too, losing:$miss"
fi

# --- 4: the subagent's toolset is otherwise intact ----------------------------
# An extraction floor as much as a property: a tools array read as empty would satisfy
# checks 2 and 5 and mean nothing.
nsub=0
for f in $sub; do
    n=$(ads "$f" | grep -c .)
    [ "$n" -gt "$nsub" ] && nsub=$n
done
if [ "$nsub" -ge 10 ] && for f in $sub; do ads "$f"; done | grep -qx "read_file"; then
    t_ok "the subagent still gets its ordinary tools ($nsub advertised)"
else
    t_fail "the subagent's array is $nsub entries -- the filter is too broad, or the extraction is reading nothing"
fi

# --- 5: spawn_parallel is still hidden from the subagent (pre-existing) --------
# The exclude_name mechanism runs in the same loop as the new filter; this proves the
# new `continue` did not land above it and swallow the older exclusions.
if ! for f in $sub; do ads "$f"; done | grep -qx "spawn_parallel"; then
    t_ok "spawn_parallel is still excluded from the subagent"
else
    t_fail "the pre-existing exclude_name filter stopped working"
fi

# --- 6: the run still completes -----------------------------------------------
if [ "$rc" -eq 0 ] && grep -q "TOPDONE" "$tmp/out.txt"; then
    t_ok "the nested run completes unchanged (rc=0)"
else
    t_fail "rc=$rc, out: $(head_bytes 160 "$tmp/out.txt")"
fi

# --- 7: the BACKSTOP refuses the call, and says what to do instead ------------
# The refusal travels back as a tool result inside the subagent's next request. Its
# wording follows M342/M360: a refusal that states only a cause is the message class
# that amplifies retry loops, so it must name the alternative.
if grep -l "belongs to the main agent" $sub >/dev/null 2>&1 &&
   grep -l "final answer" $sub >/dev/null 2>&1; then
    t_ok "the call is refused at depth, naming what to do instead"
else
    t_fail "backstop missing: $(grep -ho "error: '[^\"]\{0,120\}" $sub 2>/dev/null | head -1)"
fi

t_done
