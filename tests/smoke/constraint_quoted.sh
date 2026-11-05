#!/bin/sh
# smoke: what a request QUOTES or ASKS is not an order, and every inferred rule
# names the sentence it was read from (M872: the demos drive's N45 (a) and (b)).
#
# THE INCIDENT (2026-10-02, the zigodot drive's run 0091). The lead prompt carried
# the supervisor's question `should a task text say "write REPORT.md, do not
# commit it", or name a per-task file in /tmp?`, and jichi told the lead agent --
# whose job is to commit merges -- for the whole session: "do not use the tool
# `git_commit`, do not commit". The notice named the canonical rules, not the
# words they were read from, so nothing on the screen showed the misreading.
#
# THE CHECKS: offline, the drive's sentence infers nothing, and an order that is
# said still infers its rule and quotes its sentence; live under --auto, the
# note's red-first prompt adopts nothing, while for a said order the adoption
# notice, the system prompt, the note on a call against it and the journal each
# quote the sentence; brief-check names it beside its line. M878 (N45, continued): a
# read-only phrase stated of someone else infers nothing (9), the same words as an
# order to the reader still do (10), and a supervisor's markdown note about its
# children -- a heading, the refusal it quotes, an order FOR them -- infers nothing (11).
. "$(dirname "$0")/_smoke.sh"

t_plan 11
smoke_home
tmp=$(smoke_tmp)

# --- 1-2: the scanner, offline ---------------------------------------------------
printf '%s\n' 'should a task text say "write REPORT.md, do not commit it", or name a per-task file in /tmp?' \
    > "$tmp/q.md"
out=$("$BIN" constraints scan "$tmp/q.md" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'nothing would be adopted'; then
    t_ok "the drive's quoted question infers nothing (N45's sentence)"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -4)"
fi
printf '%s\n' 'Fix the parser. Do not commit. Then report.' > "$tmp/said.md"
out=$("$BIN" constraints scan "$tmp/said.md" 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q 'read from: "Do not commit\."'; then
    t_ok "an order that is said still infers its rule, and the scan quotes its sentence"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -6)"
fi

# --- 3: the note's red-first prompt, live ------------------------------------------
cat > "$tmp/q.mm" <<'EOF'
wire openai
rule
  text Q_DONE
EOF
ws=$(smoke_tmp)
mm_start "$tmp/q.mm" "$tmp/capQ" 4
write_config "$tmp/cq.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cq.json" --no-session \
    --auto -p 'Should a task say "do not commit it"? Then say Q_DONE.' \
    < /dev/null > "$tmp/outQ" 2> "$tmp/errQ")
mm_stop
if grep -q 'Q_DONE' "$tmp/outQ" && ! grep -q '\[constraint\] inferred' "$tmp/errQ"; then
    t_ok "an --auto run asked about a quoted \"do not commit it\" adopts nothing"
else
    t_fail "out: $(head_bytes 100 < "$tmp/outQ"); err: $(grep constraint "$tmp/errQ" | head_bytes 300)"
fi

# --- 4-6, 8: an order that is said, live, and a call against it ---------------------
cat > "$tmp/t.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_tests {}
rule
  text T_DONE
EOF
ws2=$(smoke_tmp)
mkdir -p "$tmp/jt"
mm_start "$tmp/t.mm" "$tmp/capT" 4
write_config "$tmp/ct.json" "$MM_PORT" '"testCommand":"echo TESTS_RAN_HERE"'
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/ct.json" --no-session \
    --auto --journal "$tmp/jt/j.jsonl" \
    -p "Fix the parser. Do not run tests. Then say T_DONE." \
    < /dev/null > "$tmp/outT" 2> "$tmp/errT")
mm_stop
first=$(ls "$tmp"/capT/req.* 2>/dev/null | sort -t. -k2 -n | head -1)
last=$(ls "$tmp"/capT/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)

# 4: the operator's notice quotes the sentence.
if grep '\[constraint\] inferred' "$tmp/errT" | grep -q '(from "Do not run tests\.")'; then
    t_ok "the adoption notice quotes the sentence its rules were read from"
else
    t_fail "notice: $(grep constraint "$tmp/errT" | head_bytes 400)"
fi

# 5: the system prompt names it beside the rule (JSON-escaped in the body).
if [ -n "$first" ] &&
   grep -q 'do not run tests (read from: \\"Do not run tests\.\\")' "$first"; then
    t_ok "the system prompt quotes the sentence beside the advisory rule"
else
    t_fail "prompt: $(grep -o 'Inferred from the request[^#]*' "$first" 2>/dev/null | head_bytes 400)"
fi

# 6: the call that went against it is told the sentence too.
if [ -n "$last" ] &&
   grep -q 'read from: \\"Do not run tests\.\\"\. Inferred rules' "$last"; then
    t_ok "a call against the rule is told the sentence it was read from"
else
    t_fail "note: $(grep -o 'inferred from your request[^]]*' "$last" 2>/dev/null | head_bytes 400)"
fi

# --- 7: brief-check names the sentence beside its line -----------------------------
printf '%s\n' '# Task' '' 'Fix the parser. Do not commit.' > "$tmp/brief.md"
out=$(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/cq.json" \
        brief-check "$tmp/brief.md" < /dev/null 2>/dev/null)
if printf '%s' "$out" | grep -q 'from line 3: "Do not commit\."'; then
    t_ok "brief-check quotes the sentence beside its line"
else
    t_fail "brief-check: $(printf '%s' "$out" | grep -A2 'constraints inferred' | head -4)"
fi

# --- 8: the journal records the sentences, at the adoption and at the call ----------
if grep '"event":"constraint"' "$tmp/jt/j.jsonl" 2>/dev/null |
       grep -qF '"from":["Do not run tests."]' &&
   grep '"event":"constraint_advisory"' "$tmp/jt/j.jsonl" 2>/dev/null |
       grep -qF '"from":"Do not run tests."'; then
    t_ok "the journal records the sentence at the adoption and at the call"
else
    t_fail "journal: $(grep '"event":"constraint' "$tmp/jt/j.jsonl" 2>/dev/null | head_bytes 400)"
fi

# --- 9-11: N45 continued -- a statement about someone else is not an order (M878) ---
printf '%s\n' 'Children run in read-only mode, so they cannot commit.' > "$tmp/other.md"
out=$("$BIN" constraints scan "$tmp/other.md" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'nothing would be adopted'; then
    t_ok "a read-only phrase stated of the children infers nothing (N45, continued)"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -4)"
fi
printf '%s\n' 'Run everything in read-only mode, so you cannot commit.' > "$tmp/own.md"
out=$("$BIN" constraints scan "$tmp/own.md" 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q 'read-only: do not edit files'; then
    t_ok "the same words as an order to the reader still infer read-only"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -4)"
fi
printf '%s\n' '## The children' '> tool disabled in read-only mode' '' \
    'Run the children in read-only mode; you merge their work.' > "$tmp/note.md"
out=$("$BIN" constraints scan "$tmp/note.md" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'nothing would be adopted'; then
    t_ok "a supervisor's markdown note about its children infers nothing"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -4)"
fi

t_done
