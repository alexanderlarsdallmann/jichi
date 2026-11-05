#!/bin/sh
# smoke: the end-of-run check's message names its sender and a way out, and an
# answer after which nothing changed ends the run on the check's verdict (M892, the
# demos drive's N52 (a)).
#
# THE DEFECT. The fix-forward message began "The verification command `zig build
# test` failed (exit 1)" and ended "Fix the code so the verification passes." It
# arrives as a user-role message, and names no sender. In run 0105 a toolchain update
# broke every verify before the lead had changed anything; the lead wrote "The
# operator now asks me to make verification pass" and spent 33 calls porting the
# project to the new toolchain, which the operator had decided against. A model that
# saw the cause was not its own had no way to say so: an answer with no edits only
# re-ran the same check on the same tree, until the retries ran out.
#
# THE FIX (the operator's decision, 2026-10-03: "(a) and (b)"; this is (a)). The
# message says it comes from jichi, not from the operator, and ends with a way out:
# "If your changes did not cause this failure ... say so in your answer and make no
# further edits: the run then ends on this verdict." An answer after which nothing
# changed ends the run on that verdict, with no retry spent re-asking. "Changed" is the
# worktree's content where git can fingerprint it (a shell command that only reads
# changes nothing), and otherwise any executed call that is not a read.
#
#   1  the message names jichi's end-of-run check, and says it is not the operator
#   2  it offers the way out
#   3  in a git workspace, an answer after a shell command that changed nothing ends
#      the run: the check ran once
#   4  the journal records verify_declined, by content
#   5  without git, an answer after only a read ends the run, by calls -- a read is not
#      a call that could change a file
#   6  CONTROL: an answer after an edit re-runs the check
#   7  CONTROL: the verdict is the failed check's -- exit 1, as when the retries run out
#
# The counts come from the check itself (a counter file it appends to) and from the
# journal jichi writes, never from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

cat > "$tmp/c.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "toolProfile":"full"}
EOF

# run_case NAME WS MOCKFILE -- one run; the check counts itself in $tmp/NAME.n
run_case() {
    _n="$1"; _ws="$2"; _mm="$3"
    cat > "$tmp/$_n.gate" <<EOF
#!/bin/sh
echo run >> "$tmp/$_n.n"
echo "src/a.c:1:1: error: the toolchain broke"
exit 1
EOF
    mm_start "$_mm" "$tmp/$_n.cap" 12
    sed "s/PORT/$MM_PORT/" "$tmp/c.json" > "$tmp/$_n.json"
    (cd "$_ws" && with_deadline 120 "$BIN" --config "$tmp/$_n.json" --auto --no-lite \
        --no-session --verify "sh $tmp/$_n.gate" --verify-retries 3 \
        --journal "$tmp/$_n.jsonl" -p "change the tree" < /dev/null) \
        >/dev/null 2>"$tmp/$_n.err"
    eval "rc_$_n=\$?"
    mm_stop
}

# --- case 1: a git workspace; after the failure, a shell command that changes nothing
ws1=$(smoke_tmp)
(cd "$ws1" && git init -q && printf 'seed\n' > seed.txt && git add seed.txt &&
 git -c user.email=t@example.com -c user.name=t commit -qm seed) >/dev/null 2>&1
cat > "$tmp/one.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one\n"}
rule
  count 2
  text FIRST_ANSWER
rule
  count 3
  tool run_terminal_command {"command":"true"}
rule
  count 4
  text MY_CHANGES_DID_NOT_CAUSE_THIS
rule
  text MORE
EOF
run_case one "$ws1" "$tmp/one.mm"

if grep -q "jichi's end-of-run check failed" "$tmp/one.cap/req.3" 2>/dev/null &&
   grep -q "not from the operator" "$tmp/one.cap/req.3"; then
    t_ok "the message names jichi's end-of-run check, and says it is not the operator"
else
    t_fail "request 3 does not name the sender: $(grep -o 'verif[^"]*' "$tmp/one.cap/req.3" 2>/dev/null | head_bytes 200)"
fi
if grep -q "make no further edits: the run then ends on this verdict" "$tmp/one.cap/req.3" 2>/dev/null; then
    t_ok "it offers the way out"
else
    t_fail "no way out in request 3"
fi
n1=$(grep -c run "$tmp/one.n" 2>/dev/null)
if [ "$n1" = "1" ]; then
    t_ok "a shell command that changed nothing, then an answer, ends the run: the check ran once"
else
    t_fail "the check ran ${n1:-0} times (expected 1)"
fi
if grep '"event":"verify_declined"' "$tmp/one.jsonl" 2>/dev/null | grep -q '"by":"content"'; then
    t_ok "the journal records verify_declined, by content"
else
    t_fail "no verify_declined by content: $(grep -o '"event":"verify[a-z_]*"' "$tmp/one.jsonl" 2>/dev/null | tr '\n' ' ')"
fi

# --- case 2: no git; after the failure, a read, then an answer ------------------------
ws2=$(smoke_tmp)
cat > "$tmp/two.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one\n"}
rule
  count 2
  text FIRST_ANSWER
rule
  count 3
  tool read_file {"path":"a.txt"}
rule
  count 4
  text NOT_MINE
rule
  text MORE
EOF
run_case two "$ws2" "$tmp/two.mm"
n2=$(grep -c run "$tmp/two.n" 2>/dev/null)
if [ "$n2" = "1" ] &&
   grep '"event":"verify_declined"' "$tmp/two.jsonl" 2>/dev/null | grep -q '"by":"calls"'; then
    t_ok "without git, an answer after only a read ends the run, by calls"
else
    t_fail "no-git case: the check ran ${n2:-0} times; $(grep -o '"event":"verify_declined"[^}]*' "$tmp/two.jsonl" 2>/dev/null)"
fi

# --- case 3: CONTROL -- an edit after the failure re-runs the check --------------------
ws3=$(smoke_tmp)
(cd "$ws3" && git init -q && printf 'seed\n' > seed.txt && git add seed.txt &&
 git -c user.email=t@example.com -c user.name=t commit -qm seed) >/dev/null 2>&1
cat > "$tmp/three.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one\n"}
rule
  count 2
  text FIRST_ANSWER
rule
  count 3
  tool write_file {"path":"b.txt","content":"two\n"}
rule
  count 4
  text SECOND_ANSWER
rule
  text MORE
EOF
run_case three "$ws3" "$tmp/three.mm"
n3=$(grep -c run "$tmp/three.n" 2>/dev/null)
if [ "${n3:-0}" -ge 2 ]; then
    t_ok "CONTROL: an answer after an edit re-runs the check (${n3} runs)"
else
    t_fail "CONTROL: the check ran ${n3:-0} times after an edit"
fi

if [ "$rc_one" = "1" ] && [ "$rc_two" = "1" ] && [ "$rc_three" = "1" ]; then
    t_ok "CONTROL: the verdict is the failed check's: exit 1"
else
    t_fail "exit codes: one=$rc_one two=$rc_two three=$rc_three (expected 1 each)"
fi

t_done
