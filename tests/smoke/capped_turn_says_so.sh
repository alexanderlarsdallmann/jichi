#!/bin/sh
# smoke: a turn that stops at the tool-call cap says so where a reader looks
# (M687).
#
# WHAT WENT WRONG, driving jichi on a real second project. The task was "list
# which git branches are merged into master". jichi found the answer in its
# first ~16 tool calls, then looped re-deriving it, hit the cap at 200, and
# exited. What the caller got:
#
#     stdout    : 0 bytes
#     exit code : 0
#     stderr    : [jichi warn] hit max tool iterations (200)
#                 [jichi] checked: verify green · 200 tool calls, 0 errors ...
#                 not checked: (nothing -- a verifier and an edit scope were armed)
#                 [envelope] verified ok (tokens 4,887,733, tool calls 200)
#
# Every channel a caller reads said success. 4.9 million tokens, no answer, and
# an envelope calling it verified. A script driving `jichi -p` cannot tell that
# from "finished and had nothing to say" -- which is the exact sentence M322
# wrote when it fixed this for `--output json` (stop_reason: "max_iters"). The
# TEXT path was left behind, and the envelope footer never knew at all.
#
# WHY NOT JUST PRINT IT ON STDOUT: M73. stdout is the raw answer so a script can
# pipe it; prose there would break every such caller. The honest channel is the
# one that already exists for "what was NOT checked" -- the reach footer -- and
# a truncated turn is the definition of not-checked.
#
# WHY THE EXIT CODE STAYS 0 HERE: M322 decided that deliberately -- the cap is a
# circuit breaker, the history is intact, and another prompt resumes from it.
# Flipping a currently-zero exit code is a stable-interface change and this
# project has already had one such flip withdrawn for being proposed ahead of
# its measurement (DEFERRED's --strict-green row). The argument that a
# `--no-session` one-shot has no "next prompt" is real and is recorded there,
# not decided here.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'hello\n' > "$ws/a.txt"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# A rule with no `count` matches every request, so the model asks for the same
# tool forever and the loop can only end at the cap. That is the fixture: a
# model that never says it is done.
cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  tool read_file {"path":"a.txt"}
EOF

# ---- run A: capped, NO envelope armed (maxToolIters is honoured as written) --
mm_start "$tmp/loop.mm" "$tmp/capA"
write_config "$tmp/a.json" "$MM_PORT" '"maxToolIters":3'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/a.json" --no-session \
    -p "read a.txt" < /dev/null > "$tmp/a.out" 2> "$tmp/a.err")
echo $? > "$tmp/a.rc"
mm_stop

# ---- 1: the denominator -- run A really did cap, with nothing to show -------
# Without this every assertion below holds trivially in the output of a run that
# finished normally, and a fixture whose model stopped asking for tools would
# look exactly like a passing check.
if $G -q 'hit max tool iterations' "$tmp/a.err" && [ ! -s "$tmp/a.out" ]; then
    t_ok "the fixture caps and answers nothing (rc=$(cat "$tmp/a.rc"), stdout 0 bytes)"
else
    t_fail "the fixture did not reproduce the case: capped=$($G -c 'hit max tool' \
"$tmp/a.err"), stdout=$(wc -c < "$tmp/a.out") bytes. Checks 2-5 would pass for \
the wrong reason. stderr: $(head_bytes 200 "$tmp/a.err")"
fi

# ---- 2: the reach footer names the cap --------------------------------------
if $G -qi 'stopped at the tool-call cap' "$tmp/a.err"; then
    t_ok "the reach footer names the cap"
else
    t_fail "a capped turn produced no answer and the footer does not say why -- \
this is the defect: every channel a caller reads said success. Footer: \
$($G -A2 'checked:' "$tmp/a.err" | head -3 | tr '\n' ' ' | head_bytes 240)"
fi

# ---- 3: it survives the NO-ENVELOPE branch ----------------------------------
# reach_halves has two shapes for the unchecked half and the no-envelope one
# returns EARLY with a single sentence. A cap notice added only to the other
# branch would be invisible on exactly the runs that arm the least.
if $G -q 'no envelope armed' "$tmp/a.err" &&
   $G -qi 'stopped at the tool-call cap' "$tmp/a.err"; then
    t_ok "the cap is named even with no envelope armed (the sparser branch)"
else
    t_fail "run A armed no envelope and the cap notice did not survive that \
branch: $($G 'not checked' "$tmp/a.err" | head_bytes 240)"
fi

# ---- run B: capped, WITH a verifier and an edit scope armed -----------------
# The case from the report. An envelope forces the cap to >= 200 (jc_agent.c),
# so this run is the slow one; it is worth it because this is the branch whose
# unchecked half collapses to "(nothing -- a verifier and an edit scope were
# armed)" -- a sentence that was being printed about a truncated turn.
mm_start "$tmp/loop.mm" "$tmp/capB"
write_config "$tmp/b.json" "$MM_PORT" '"verify":"true","testCommand":"true"'
(cd "$ws" && with_deadline 180 "$BIN" --config "$tmp/b.json" --no-session \
    --edit-scope '**' -p "read a.txt" < /dev/null > "$tmp/b.out" 2> "$tmp/b.err")
echo $? > "$tmp/b.rc"
mm_stop

# ---- 4: the "(nothing)" fallback does not swallow it ------------------------
if $G -qi 'stopped at the tool-call cap' "$tmp/b.err" &&
   ! $G -q 'not checked: (nothing' "$tmp/b.err"; then
    t_ok "with an envelope armed the cap is reported, not collapsed to (nothing)"
else
    t_fail "the fully-armed run reported '(nothing -- a verifier and an edit \
scope were armed)' about a turn that was cut off mid-task: \
$($G 'not checked' "$tmp/b.err" | head_bytes 240)"
fi

# ---- 5: the verdict line does not read as an unqualified pass ---------------
# The last line a reader sees. A footer that says "cut short" above an
# "[envelope] verified ok" below is worse than either alone.
if $G -q '\[envelope\]' "$tmp/b.err"; then
    if $G '\[envelope\]' "$tmp/b.err" | $G -q 'cap'; then
        t_ok "the envelope verdict line qualifies its ok with the cap"
    else
        t_fail "the verdict a reader ends on is unqualified: \
$($G '\[envelope\]' "$tmp/b.err" | head_bytes 200)"
    fi
else
    t_fail "run B printed no [envelope] verdict line at all, so check 5 is \
reading nothing: $(tail -3 "$tmp/b.err" | tr '\n' ' ' | head_bytes 220)"
fi

# ---- 6: a turn that does NOT cap says nothing about a cap -------------------
# The two-sided half. A notice printed unconditionally passes checks 2-5 while
# telling every reader that every run was truncated.
cat > "$tmp/done.mm" <<'EOF'
wire openai
rule
  text ALL_DONE
EOF
mm_start "$tmp/done.mm" "$tmp/capC"
write_config "$tmp/c.json" "$MM_PORT" '"maxToolIters":3'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --no-session \
    -p "say something" < /dev/null > "$tmp/c.out" 2> "$tmp/c.err")
mm_stop
if $G -q 'ALL_DONE' "$tmp/c.out" && ! $G -qi 'tool-call cap' "$tmp/c.err"; then
    t_ok "an uncapped turn answers and says nothing about a cap"
else
    t_fail "either the clean run did not answer (stdout: \
$(head_bytes 80 "$tmp/c.out")) or it claimed a cap it never hit: \
$($G 'not checked' "$tmp/c.err" | head_bytes 200)"
fi

t_done
