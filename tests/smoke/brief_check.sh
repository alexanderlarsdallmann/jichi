#!/bin/sh
# smoke: `brief-check` -- the pre-flight that spends no tokens (M433).
#
# THE MEASURED FAILURE. Across 7 driven runs, 4 SILENTLY ADOPTED a constraint from
# DESCRIPTIVE prose. The phrases responsible are descriptions, not instructions --
# "force never-compiled core code to compile", "47 of 88 files never compiled", "its
# body has never been compiled" -- so they do not look like constraints when read.
# Consequences ranged from harmless to total: one run was banned from the sweeps it
# depended on and died with no deliverable. Knowing about the footgun did not prevent
# it: the warning was written into a plan and tripped in the next brief authored.
#
# Two further runs spent ~3M tokens against gates that could not pass.
#
# Every part already existed -- jc_constraint_scan and M343's baseline probe -- and
# the only thing missing was a way to run them WITHOUT spending a run. So this is the
# "prefer a lint to an audit" rule applied to the brief itself.
#
# The two properties that make it usable are checked here: it names the LINE (the
# canonical constraint text is not the operator's sentence, so a list alone leaves
# them hunting), and it makes NO NETWORK CALL (checked by pointing the config at a
# closed port and requiring success anyway).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A config whose model is unreachable: any model call fails fast on a refused
# connection, so a clean exit is proof no call was attempted.
cat > "$tmp/dead.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

# Line 7 is the measured killer: a description of the GOAL that infers a ban on the
# very thing the task requires.
cat > "$tmp/brief.md" <<'EOF'
# Task: repair the animation evaluator

Background: 47 of 88 files were never compiled in this project.
The AnimationEvaluator's body has never been compiled, so its
HashMap field predates the 4-arg API.

Goal: force never-compiled core code to compile, then add tests.
Do not push anything.
EOF

out=$(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/dead.json" \
        brief-check "$tmp/brief.md" < /dev/null 2>/dev/null); rc=$?

# --- 1: it answers, with an unreachable model -----------------------------------
if [ "$rc" -eq 0 ] && [ -n "$out" ]; then
    t_ok "brief-check answers with no reachable model (exit 0, no network call)"
else
    t_fail "rc=$rc out=$(printf '%s' "$out" | head_bytes 120)"
fi

# --- 2: it names the inferred constraint --------------------------------------
if printf '%s' "$out" | grep -q "do not run build commands"; then
    t_ok "the build ban is reported before a token is spent"
else
    t_fail "build ban not reported: $(printf '%s' "$out" | head_bytes 200)"
fi

# --- 3: and the LINE it came from ---------------------------------------------
# The whole difficulty is that the trigger is a description. Naming the constraint
# without the line leaves the author hunting for words that do not look like a
# constraint -- line 7 here, the goal statement itself.
if printf '%s' "$out" | grep -q "from line 7"; then
    t_ok "it names line 7 -- the goal statement that bans the build"
else
    t_fail "no line attribution: $(printf '%s' "$out" | grep -A1 'do not run build' | head -2)"
fi

# --- 4: the intended constraint is reported too, not filtered out --------------
# "Do not push anything" IS meant. A pre-flight that only showed surprises would
# make the author trust the list less, not more.
if printf '%s' "$out" | grep -q "do not push"; then
    t_ok "the intended constraint is listed as well"
else
    t_fail "the deliberate push ban is missing from the report"
fi

# --- 5: it says writes are unfenced when no edit scope is given ----------------
if printf '%s' "$out" | grep -q "writes are NOT fenced"; then
    t_ok "an absent edit scope is stated, not left blank"
else
    t_fail "no unfenced-writes note"
fi

# --- 6: a GOAL gate that already passes is refused, with exit 1 ----------------
# M343's trap: a gate that passes without the work forces nothing, and two runs cost
# ~3M tokens against gates that could not pass. Exit 1 so a wrapper can gate on it.
out6=$(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/dead.json" \
        brief-check "$tmp/brief.md" --verify "true" --verify-kind goal \
        < /dev/null 2>/dev/null); rc6=$?
if [ "$rc6" -eq 1 ] && printf '%s' "$out6" | grep -q "FORCES NOTHING"; then
    t_ok "a declared goal gate that already passes exits 1 and says why"
else
    t_fail "rc6=$rc6, verdict: $(printf '%s' "$out6" | grep -i verdict | head -1)"
fi

# --- 7: a goal gate that is red is the NORMAL state, exit 0 -------------------
out7=$(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/dead.json" \
        brief-check "$tmp/brief.md" --verify "false" --verify-kind goal \
        < /dev/null 2>/dev/null); rc7=$?
if [ "$rc7" -eq 0 ] && printf '%s' "$out7" | grep -q "correct for a \`goal\`"; then
    t_ok "a red goal gate is accepted as the normal start (exit 0)"
else
    t_fail "rc7=$rc7, verdict: $(printf '%s' "$out7" | grep -i verdict | head -1)"
fi

t_done
