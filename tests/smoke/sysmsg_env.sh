#!/bin/sh
# smoke: `sysmsg` and `context` show the envelope-gated prompt sections (M444).
#
# THE DEFECT. Both subcommands dispatch from main() long BEFORE the autonomy envelope is
# armed, so every envelope-gated section was simply absent from their output: the M355
# flight plan (the armed budgets), the M387 STATE-THE-REACH paragraph with its glob list,
# and the M332 gate contract. Meanwhile DESIGN_INPUT.md offers `jichi --design <f> sysmsg`
# as the way to "print the full system prompt", and `context` exists precisely to size the
# prompt a run will send -- so it was under-reporting by the largest env-gated block.
#
# WHY IT WAS PARKED, and what changed. Moving the dispatch was not an option: MCP servers
# get connected between the dispatch point and the arming block, so a read-only prompt
# dump would have started spawning subprocesses. The fix instead SPLITS the arming (pure
# computation) from the journal (the only side effect), so an introspection command can
# arm an envelope with `journal_path = NULL`. Check 5 is what makes that a fact rather
# than an intention: it runs under a throwaway HOME and requires the runs directory to
# stay empty.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x"}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

# No mock server is needed: both subcommands are offline. An unreachable apiBase also
# proves that -- a network call would fail on a refused connection.
plain=$(cd "$ws" && "$BIN" --config "$tmp/config.json" sysmsg \
          < /dev/null 2>/dev/null)
# --auto too, because the M87 verify-gate note ("do not re-run the gate yourself",
# which NAMES the command) is AUTO-only -- and an --auto run is the case an operator
# previews. The first cut of check 4 omitted it and failed on the command name while
# the contract paragraph was in fact present: the assertion was testing two things and
# only one of them belonged to this milestone.
armed=$(cd "$ws" && "$BIN" --config "$tmp/config.json" --auto \
          --edit-scope 'src/**' --verify 'make test' --budget-tokens 50k \
          --strict-green sysmsg < /dev/null 2>/dev/null)
# M543: the SAME run with a periodic gate armed. The verify-gate paragraph is the
# only thing that differs, and checks 7-8 pin both halves -- because check 4 above
# asserts only that the command is NAMED, which was true of the wrong text too.
periodic=$(cd "$ws" && "$BIN" --config "$tmp/config.json" --auto \
          --edit-scope 'src/**' --verify 'make test' --verify-every 5 \
          --budget-tokens 50k --strict-green sysmsg < /dev/null 2>/dev/null)

# --- 1: the baseline -- no envelope flags, no envelope sections -----------------
# The extraction floor AND a property: arming must be driven by the flags, not always on.
if [ -n "$plain" ] && ! printf '%s' "$plain" | grep -q "edit-scope"; then
    t_ok "with no envelope flags the sections are absent, as they should be"
else
    t_fail "baseline wrong: $(printf '%s' "$plain" | grep -c . ) lines"
fi

# --- 2: the STATE-THE-REACH paragraph, with its globs -------------------------
# The glob list is the part that matters: M431 added it after a run guessed 177 times at
# which paths were writable, and it was reaching the model but not the operator's preview.
if printf '%s' "$armed" | grep -q "This run has an --edit-scope" &&
   printf '%s' "$armed" | grep -q "src/\*\*"; then
    t_ok "the scope-reach paragraph appears, naming its globs"
else
    t_fail "no scope section: $(printf '%s' "$armed" | grep -c 'edit-scope') matches"
fi

# --- 3: the flight plan, with the real budget ---------------------------------
# 50k was given on the command line, so the number proves the FLAGS reached the
# envelope -- not merely that some boilerplate paragraph was emitted.
if printf '%s' "$armed" | grep -q "This run is bounded" &&
   printf '%s' "$armed" | grep -q "token budget: 50000"; then
    t_ok "the flight plan appears with the budget actually given (50000)"
else
    t_fail "no flight plan: $(printf '%s' "$armed" | grep -o 'token budget: [0-9]*' | head -1)"
fi

# --- 4: the gate contract, naming the verifier -------------------------------
if printf '%s' "$armed" | grep -q "this run's CONTRACT" &&
   printf '%s' "$armed" | grep -q "make test"; then
    t_ok "the gate contract appears and the verify command is named"
else
    t_fail "contract=$(printf '%s' "$armed" | grep -c CONTRACT) verify_named=$(printf '%s' "$armed" | grep -c 'make test')"
fi

# --- 5: showing the prompt creates NO run record -----------------------------
# The load-bearing check. The arming and the journal were one operation, and a naive
# fix -- arm the envelope wherever it is convenient -- would make `jichi sysmsg` litter
# ~/.jichi.d/runs with a journal per invocation. A read-only command must stay read-only.
# smoke_home already points HOME at a throwaway dir, so the count starts at zero.
before=$(ls "$HOME/.jichi.d/runs" 2>/dev/null | wc -l | tr -d ' ')
(cd "$ws" && "$BIN" --config "$tmp/config.json" --verify 'make test' \
    --budget-tokens 50k sysmsg < /dev/null > /dev/null 2>&1)
(cd "$ws" && "$BIN" --config "$tmp/config.json" --verify 'make test' \
    --budget-tokens 50k context < /dev/null > /dev/null 2>&1)
after=$(ls "$HOME/.jichi.d/runs" 2>/dev/null | wc -l | tr -d ' ')
if [ "$before" = "$after" ]; then
    t_ok "neither subcommand created a run journal ($after entries, unchanged)"
else
    t_fail "a journal was created: $before -> $after entries"
fi

# --- 6: `context` sizes the bigger prompt -------------------------------------
# The consequence for the subcommand whose entire job is the size. Compared as a
# DIFFERENCE, not against a fixed number, so the check survives prompt edits.
c_plain=$(cd "$ws" && "$BIN" --config "$tmp/config.json" context < /dev/null \
            2>/dev/null | sed -n 's/^  system prompt *~\([0-9]*\).*/\1/p')
c_armed=$(cd "$ws" && "$BIN" --config "$tmp/config.json" \
            --edit-scope 'src/**' --verify 'make test' --budget-tokens 50k \
            context < /dev/null 2>/dev/null \
            | sed -n 's/^  system prompt *~\([0-9]*\).*/\1/p')
if [ -n "$c_plain" ] && [ -n "$c_armed" ] && [ "$c_armed" -gt "$c_plain" ]; then
    t_ok "context now sizes the env sections too ($c_plain -> $c_armed tokens)"
else
    t_fail "context unchanged by the envelope: plain=$c_plain armed=$c_armed"
fi

# --- 7: with NO --verify-every, the prompt says the gate runs at turn END ------
# THE DEFECT M543 FIXED, pinned through the real system prompt rather than the pure
# helper. `--verify` alone arms no periodic gate: jc_env_should_verify_now returns 0
# for verify_every <= 0, so nothing runs the verifier between tool calls. The old
# text said it ran "after your tool calls" and told the model not to run any
# build/test command itself -- which is an instruction to edit blind for the whole
# turn. The prohibition must be ABSENT here, and the honest timing present.
if printf '%s' "$armed" | grep -q 'when your turn ENDS' &&
   printf '%s' "$armed" | grep -q 'does NOT run between your tool calls' &&
   ! printf '%s' "$armed" | grep -q 'Do NOT run'; then
    t_ok "default cadence: the prompt says the gate runs at turn end, and does \
not forbid the model from checking its own work"
else
    t_fail "the default-cadence text is wrong -- ends=$(printf '%s' "$armed" \
| grep -c 'when your turn ENDS') between=$(printf '%s' "$armed" \
| grep -c 'does NOT run between') forbid=$(printf '%s' "$armed" \
| grep -c 'Do NOT run') (want 1 1 0)"
fi

# --- 8: with --verify-every 5, the cadence is stated and M87's rule returns ----
# The other half. Where the gate really is periodic, re-running it by hand is the
# dominant no-prompt-cache cost and M87's prohibition is right -- so it must appear
# here and nowhere else.
if printf '%s' "$periodic" | grep -q 'every 5 tool calls' &&
   printf '%s' "$periodic" | grep -q 'Do NOT run'; then
    t_ok "periodic cadence: the prompt states 'every 5 tool calls' and restores \
M87's do-not-re-run rule"
else
    t_fail "the periodic text is wrong -- cadence=$(printf '%s' "$periodic" \
| grep -c 'every 5 tool calls') forbid=$(printf '%s' "$periodic" \
| grep -c 'Do NOT run') (want 1 1)"
fi

t_done
