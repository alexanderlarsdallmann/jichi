#!/bin/sh
# smoke: a fence decides on the tool that will RUN, not the name that arrived
# (M532).
#
# THE DEFECT. `jc_tool_execute` resolves aliases before running anything --
# `jc_tool_find` matches the canonical name -- so a model calling `create_file`
# runs `write_file`. Every gate in the agent loop compared the RAW wire name and
# missed it. Reproduced before the fix: with `permissions.deny: ["write_file"]` a
# `create_file` call WROTE THE FILE and reported ok. The same bypass reached
# `--edit-scope`, `--strict-scope`, an enforced deny-cmd constraint, and
# `privilegedCommands: deny` -- where the gate never fired at all, so no audit row
# was written and the trail showed nothing.
#
# This is not adversarial input. The alias table exists BECAUSE models emit these
# names unprompted (`jc_tool.c`: "what a model guessing these names sends"), and
# in the deny case the canonical tool is un-advertised, which is exactly the
# condition that makes a model guess.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- three fences against an ALIASED call, each with two controls:
#                   the canonical name must still be refused (the fence works at
#                   all) and the alias must still WORK when nothing fences it (the
#                   fix did not break aliasing, which would be a worse defect than
#                   the one it fixes).
#   checked      -- the argument-shape half of the same root cause: a blob the
#                   fence's parser rejects but the executor REPAIRS (a trailing
#                   comma) must still be fenced, with a control proving the same
#                   path is allowed when it is inside the scope.
#   NOT checked  -- MCP tool policy, which takes the same canonical name but has no
#                   alias table of its own.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

# One call, then a plain reply so the turn ends.
mk_script() {   # mk_script <toolname> <argsjson>
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool $1 $2
rule
  text done
EOF
}

# run_case <toolname> <args> <extra-config-json> -> prints the tool result line
run_case() {
    ws=$(smoke_tmp)
    mk_script "$1" "$2"
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT" "$3"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --auto -p "do it" < /dev/null) > "$tmp/out" 2>&1
    mm_stop
    CASE_WS="$ws"
}

# --- 1-2: permissions.deny, and its two controls ----------------------------
run_case create_file '{"path":"pwned.txt","content":"x"}' '"permissions":{"deny":["write_file"]}'
if [ -f "$CASE_WS/pwned.txt" ]; then
    t_fail "permissions.deny ['write_file'] did NOT stop an aliased create_file --
 the file was written. A fence that decides on the wire name does not fence the
 tool that runs: $(grep -c . "$tmp/out") lines of output"
else
    t_ok "deny['write_file'] refuses an aliased create_file"
fi

run_case write_file '{"path":"pwned.txt","content":"x"}' '"permissions":{"deny":["write_file"]}'
if [ -f "$CASE_WS/pwned.txt" ]; then
    t_fail "the CONTROL failed: deny['write_file'] did not stop write_file either,
 so check 1 proves nothing about aliases"
else
    t_ok "control: the same fence refuses the canonical name too"
fi

# The control that matters most: the fix must not have broken aliasing.
run_case create_file '{"path":"fine.txt","content":"x"}' '"permissions":{"deny":["read_file"]}'
if [ -f "$CASE_WS/fine.txt" ]; then
    t_ok "control: an alias still WORKS when nothing fences it"
else
    t_fail "the alias no longer resolves at all -- the fix broke aliasing, which is
 a worse defect than the bypass it closed: $(tail -c 200 "$tmp/out")"
fi

# --- 3-4: the edit-scope fence, aliased ------------------------------------
run_case create_file '{"path":"outside.txt","content":"x"}' '"editScope":["src/**"]'
if [ -f "$CASE_WS/outside.txt" ]; then
    t_fail "--editScope src/** did not fence an aliased create_file writing
 outside it: the file exists"
else
    t_ok "editScope fences an aliased create_file writing outside it"
fi
run_case write_file '{"path":"outside.txt","content":"x"}' '"editScope":["src/**"]'
if [ -f "$CASE_WS/outside.txt" ]; then
    t_fail "the CONTROL failed: editScope did not fence the canonical name either"
else
    t_ok "control: editScope fences the canonical name too"
fi

# --- 5: privileged commands, aliased -- and the audit row -------------------
# `privilegedCommands: deny` must refuse a sudo-shaped command arriving under an
# alias. Before the fix the gate never fired, so jc_audit_privileged wrote nothing
# either -- a refusal that leaves no trace is indistinguishable from no attempt.
run_case run_shell_command '{"command":"sudo id"}' '"privilegedCommands":"deny"'
if grep -qE 'privileged|refus|denied|not allowed' "$tmp/out"; then
    t_ok "privilegedCommands:deny recognises a sudo command under an alias"
else
    t_fail "an aliased run_shell_command carrying sudo was not recognised by the
 privileged gate: $(tail -c 250 "$tmp/out")"
fi

# --- 7-8: the argument half -- a repairable blob must not slip the fence -----
# jc_tool_execute repairs nearly-JSON arguments (M148), so a blob the fence's own
# parser rejects can still become a write. Before the fix the fence returned "no
# violation" on a parse failure and the repaired write went through unfenced.
run_case write_file '{"path":"outside.txt","content":"x",}' '"editScope":["src/**"]'
if [ -f "$CASE_WS/outside.txt" ]; then
    t_fail "a trailing comma slipped the edit-scope fence: the executor repaired
 the arguments and wrote outside the scope, while the fence saw an unparseable
 blob and returned no violation"
else
    t_ok "a repairable argument blob is still fenced (trailing comma)"
fi
# Control: the same repairable blob INSIDE the scope must be written, or check 7
# would pass merely because repair is broken. The path needs no directory: a fresh
# workspace has no src/ and write_file does not invent parents, which made the
# first version of this control fail for a reason that had nothing to do with
# either the fence or the repair.
run_case write_file '{"path":"inside.txt","content":"x",}' '"editScope":["inside*.txt"]'
if [ -f "$CASE_WS/inside.txt" ]; then
    t_ok "control: the same repairable blob inside the scope is written"
else
    t_fail "control failed: a repairable blob inside the scope was NOT written, so
 check 7 may pass because repair is broken rather than because the fence works:
 $(tail -c 200 "$tmp/out")"
fi

t_done
