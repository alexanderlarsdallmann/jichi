#!/bin/sh
# smoke: a read-only delegate is read-only at the execution gate, not only on
# its advertised tool menu (M750). Regression test for a permission-enforcement
# fix in jc_agent_run_subagent.
#
# CONTRACT. When a caller runs a delegate with the mutating tools withheld
# (include_mutating 0), that delegate must be read-only in effect: if the model
# names a write tool it was not advertised, the call is refused and the tree is
# unchanged. Three callers relied on the withheld menu alone before M750 --
# spawn_subagent {"readonly": true}, a spawn_parallel task without "write", and
# ask_for_help's helper -- and jc_tool_execute's gate reads app->readonly, which
# none of them set. M634 had set app->readonly around the workflow's map and
# refute stages for the same reason; M750 sets it once, in the function all
# delegates run through. This driver holds the two delegate tools to the contract.
#
# ROUTING IS BY REQUEST INDEX (mockmodel `count`), not by body substring: the
# parent's own delegation carries the child's task text, so a substring rule
# cannot tell a later parent turn from the child's. The parent and child hit the
# one sequential mock in a fixed order (spawn_subagent is synchronous; the one
# parallel child runs before the parent resumes), so the count is deterministic.
# The mock's capture directory (one sequential writer) is the faithful record;
# --dump-requests collides by name across the forked parallel child.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home

REFUSAL="tool disabled in read-only mode"

# --- Scenario A: spawn_subagent {"readonly": true} -------------------------------
# req1 parent -> delegate read-only; req2 child -> name write_file (unadvertised);
# req3 child -> answer; req4 parent -> make its OWN write (must still land -- the
# child's read-only run must not leak onto the parent); req5 parent -> answer.
wsA=$(smoke_tmp)
tmpA=$(smoke_tmp)
printf 'original\n' > "$wsA/guarded.txt"
cp "$wsA/guarded.txt" "$tmpA/guarded.orig"
cat > "$tmpA/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"Review guarded.txt and report back.","readonly":true}
rule
  count 2
  tool write_file {"path":"guarded.txt","content":"update\n"}
rule
  count 3
  text reviewed, nothing to change
rule
  count 4
  tool write_file {"path":"parent_out.txt","content":"parent wrote this\n"}
rule
  text PARENT_DONE
EOF
mm_start_unbounded "$tmpA/r.mm" "$tmpA/cap"
write_config "$tmpA/config.json" "$MM_PORT"
(cd "$wsA" && with_deadline 120 "$BIN" --config "$tmpA/config.json" --no-session \
    --auto -p "Delegate a read-only review of guarded.txt, then note it done." \
    < /dev/null > "$tmpA/out" 2> "$tmpA/err"); rcA=$?
mm_stop
nA=$(ls "$tmpA/cap"/req.* 2>/dev/null | wc -l)

# 1: the child ran two turns (else the effect checks are vacuous): >= 4 requests
if [ "$nA" -ge 4 ]; then
    t_ok "A subagent: the delegate ran ($nA requests captured)"
else
    t_fail "A subagent: only $nA requests (rc=$rcA) -- the child never ran: $(head_bytes 120 "$tmpA/err")"
fi

# 2: the guarded file is byte-identical -- the effect, not the attempt
if cmp -s "$wsA/guarded.txt" "$tmpA/guarded.orig"; then
    t_ok "A subagent: guarded.txt is unchanged after the read-only child"
else
    t_fail "A subagent: guarded.txt was modified by a read-only delegate: $(head_bytes 60 "$wsA/guarded.txt")"
fi

# 3: the refusal reached the model (a value it can act on, not a silent no-op)
if grep -q "$REFUSAL" "$tmpA/cap"/req.* 2>/dev/null; then
    t_ok "A subagent: the read-only refusal is on the wire"
else
    t_fail "A subagent: no read-only refusal returned to the model"
fi

# 4: NO LEAK -- the parent's own write succeeds after the read-only child. Not a
# red-before-fix check: it passes on the unfixed tree too, and guards the fix
# against over-reaching (leaving the parent read-only).
if [ -f "$wsA/parent_out.txt" ] && grep -q 'parent wrote this' "$wsA/parent_out.txt"; then
    t_ok "A subagent: the writable parent can still write after a read-only child"
else
    t_fail "A subagent: the parent's own write did not land -- read-only leaked from the child"
fi

# --- Scenario B: spawn_parallel with a read-only task (no "write") ----------------
# A read-only parallel task runs against the LIVE workspace (no worktree), so a
# write by it lands directly on the tree unless the gate holds.
# req1 parent -> spawn_parallel; req2 child -> write_file; req3 child -> answer;
# req4 parent -> answer.
wsB=$(smoke_tmp)
tmpB=$(smoke_tmp)
printf 'original\n' > "$wsB/guarded.txt"
cp "$wsB/guarded.txt" "$tmpB/guarded.orig"
cat > "$tmpB/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"Review guarded.txt and report back."}]}
rule
  count 2
  tool write_file {"path":"guarded.txt","content":"update\n"}
rule
  count 3
  text reviewed, nothing to change
rule
  text PARENT_DONE
EOF
mm_start_unbounded "$tmpB/r.mm" "$tmpB/cap"
write_config "$tmpB/config.json" "$MM_PORT"
(cd "$wsB" && with_deadline 120 "$BIN" --config "$tmpB/config.json" --no-session \
    --auto -p "Run one parallel review task over guarded.txt, then note it done." \
    < /dev/null > "$tmpB/out" 2> "$tmpB/err"); rcB=$?
mm_stop

# 5: the guarded file is byte-identical after the read-only parallel task
if cmp -s "$wsB/guarded.txt" "$tmpB/guarded.orig"; then
    t_ok "B parallel: guarded.txt is unchanged after the read-only task"
else
    t_fail "B parallel: guarded.txt was modified by a read-only parallel task: $(head_bytes 60 "$wsB/guarded.txt")"
fi

# 6: the refusal reached the model
if grep -q "$REFUSAL" "$tmpB/cap"/req.* 2>/dev/null; then
    t_ok "B parallel: the read-only refusal is on the wire"
else
    t_fail "B parallel: no read-only refusal returned to the model (rc=$rcB): $(head_bytes 120 "$tmpB/err")"
fi

t_done
