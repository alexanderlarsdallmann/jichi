#!/bin/sh
# smoke: an inferred rule's note goes to a command that RUNS what the rule names,
# not to one that mentions it (M873: the demos drive's N40).
#
# THE CASE. Every run of the demos drive began with "[constraint] inferred from
# your request, ADVISORY ...: do not install packages", read from its standing
# prompt's "Never install anything." Then a grep for "install" in build.zig
# was told, in the result the model reads next, that it "goes against" the rule
# (the drive's LESSONS 2.13): an accusation about a search. An inferred rule's
# note now goes only to a command that runs the rule's subject. A rule the
# operator wrote still matches the word anywhere, and refuses, as a fence should.
#
# THE CHECKS, live under --auto with "Never install anything." inferred: the grep
# for the word gets no note (1); `./pip install demo`, a stand-in pip in the
# workspace, gets it (2); the journal counts the one call against the rule (3).
# CONTROL: with `deny-cmd install` written by the operator, the same grep is
# refused (4).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

tool_results() {  # REQUEST -> the tool-role message contents, one per line
    grep -o '"role":"tool","tool_call_id":"[^"]*","content":"[^"]*' "$1" 2>/dev/null
}

cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"grep -n -E \"install|zigodot\" build.zig"}
rule
  count 2
  tool run_terminal_command {"command":"./pip install demo"}
rule
  text R_DONE
EOF
ws=$(smoke_tmp)
printf '%s\n' 'const install = b.addInstallArtifact(exe, .{});' > "$ws/build.zig"
printf '%s\n' '#!/bin/sh' 'echo "PIP_STANDIN $*"' > "$ws/pip"
chmod +x "$ws/pip"
mkdir -p "$tmp/jr"
mm_start "$tmp/r.mm" "$tmp/capR" 6
write_config "$tmp/cr.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cr.json" --no-session \
    --auto --journal "$tmp/jr/j.jsonl" \
    -p "Look at the build. Never install anything. Then say R_DONE." \
    < /dev/null > "$tmp/outR" 2> "$tmp/errR")
mm_stop
last=$(ls "$tmp"/capR/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)
res=$(tool_results "$last")
grep_res=$(printf '%s\n' "$res" | sed -n 1p)
pip_res=$(printf '%s\n' "$res" | sed -n 2p)

# 1: the search for the word ran, and was not told it went against the rule.
if printf '%s' "$grep_res" | grep -q 'addInstallArtifact' &&
   ! printf '%s' "$grep_res" | grep -q 'goes against a rule'; then
    t_ok "a grep for the word \"install\" is not told it goes against \"do not install packages\""
else
    t_fail "grep result: $(printf '%s' "$grep_res" | head_bytes 300); err: $(grep constraint "$tmp/errR" | head_bytes 300)"
fi

# 2: the install that ran is.
if printf '%s' "$pip_res" | grep -q 'PIP_STANDIN install demo' &&
   printf '%s' "$pip_res" | grep -q 'goes against a rule'; then
    t_ok "\`./pip install demo\` is told it goes against the inferred rule"
else
    t_fail "pip result: $(printf '%s' "$pip_res" | head_bytes 300)"
fi

# 3: the journal counts that one call.
na=$(grep -c '"event":"constraint_advisory"' "$tmp/jr/j.jsonl" 2>/dev/null)
if [ "$na" = "1" ] &&
   grep '"event":"constraint_advisory"' "$tmp/jr/j.jsonl" | grep -q 'do not install packages'; then
    t_ok "the journal records one call against the rule"
else
    t_fail "constraint_advisory events: $na: $(grep '"event":"constraint' "$tmp/jr/j.jsonl" 2>/dev/null | head_bytes 300)"
fi

# --- 4: CONTROL -- a rule the operator wrote still matches the word -------------
cat > "$tmp/a.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"grep -n -E \"install|zigodot\" build.zig"}
rule
  text A_DONE
EOF
ws2=$(smoke_tmp)
cp "$ws/build.zig" "$ws2/build.zig"
mkdir -p "$ws2/.jichi"
printf '%s\n' "deny-cmd install" > "$ws2/.jichi/constraints.md"
mm_start "$tmp/a.mm" "$tmp/capA" 4
write_config "$tmp/ca.json" "$MM_PORT"
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/ca.json" --no-session \
    --auto -p "Look at the build. Then say A_DONE." < /dev/null > /dev/null 2>&1)
mm_stop
res=$(tool_results "$(ls "$tmp"/capA/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)")
if printf '%s' "$res" | grep -q 'blocked by an active constraint' &&
   ! printf '%s' "$res" | grep -q 'addInstallArtifact'; then
    t_ok "CONTROL: an authored deny-cmd install still refuses a command naming the word"
else
    t_fail "an authored deny-cmd did not refuse: $(printf '%s' "$res" | head_bytes 300)"
fi

t_done
