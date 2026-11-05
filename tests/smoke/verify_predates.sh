#!/bin/sh
# smoke: an end-of-run failure on a line that already failed before the run's first
# change is said to predate the run's changes, and the round spends no retry (M893,
# the demos drive's N52 (b)).
#
# THE DEFECT. In run 0105 a toolchain update broke every verify before the lead had
# changed anything: both of batch 1's worktree verifies failed at 07:57 on
# `build.zig:181:11: error: no field named 'args' in struct 'Build'`. At 08:17 the
# end-of-run check failed on the same line, and jichi asked for a fix as if the run had
# caused it, spending a retry, while its own journal held the earlier failure.
#
# THE FIX (the operator's decision, 2026-10-03: "(a) and (b)"; this is (b)). jichi
# keeps the first failing line -- the whole line, location included -- that the check
# printed before the run's first change: in the run-start verify (`--verify-baseline`,
# or a declared `verifyKind`), or in a batch's worktree verify of the same command
# whose base is still the starting tree. When the end-of-run check fails on that very
# line, the message says the failure predates the run's changes and names where it was
# seen, and that round spends no retry. Once per run.
#
#   1  the message says the failure predates the changes, and quotes the line
#   2  it names the run-start verify as the evidence
#   3  that round spends no retry: the next check still has 3 left
#   4  once per run: the second failure on the same line gets the plain message
#   5  the baseline's journal event records the line
#   6  CONTROL: a failure on another line gets the plain message and spends a retry
#   7  a batch's worktree verify on the starting tree is evidence too, named by task
#   8  CONTROL: a batch on a base the lead had already changed is no evidence
#
# The evidence is what jichi sent the model and what its journal holds, never a
# command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
L1="src/a.c:1:1: error: the toolchain broke"
L2="src/b.c:2:2: error: something else broke"

newws() {
    _w=$(smoke_tmp)
    (cd "$_w" && git init -q && printf 'seed\n' > seed.txt && git add seed.txt &&
     git -c user.email=t@example.com -c user.name=t commit -qm seed) >/dev/null 2>&1
    printf '%s' "$_w"
}

# gate NAME FIRSTLINE LATERLINE -- prints FIRSTLINE on its first run, LATERLINE after
gate() {
    cat > "$tmp/$1.gate" <<EOF
#!/bin/sh
echo run >> "$tmp/$1.n"
if [ "\$(grep -c run "$tmp/$1.n")" -eq 1 ]; then echo "$2"; else echo "$3"; fi
exit 1
EOF
}

# run_case NAME WS MOCK EXTRA_JSON FLAGS...
run_case() {
    _n="$1"; _ws="$2"; _mm="$3"; _x="$4"; shift 4
    mm_start_unbounded "$_mm" "$tmp/$_n.cap"
    cat > "$tmp/$_n.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "toolProfile":"full"$_x}
EOF
    (cd "$_ws" && with_deadline 120 "$BIN" --config "$tmp/$_n.json" --auto --no-lite \
        --no-session --verify "sh $tmp/$_n.gate" --verify-retries 3 \
        --journal "$tmp/$_n.jsonl" "$@" -p "LEAD$_n change the tree" < /dev/null) \
        >/dev/null 2>"$tmp/$_n.err"
    mm_stop
}

# --- case A: the run-start verify fails on L1, and so does every check after ---------
gate a "$L1" "$L1"
cat > "$tmp/a.mm" <<'EOF'
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
  text NOT_MINE
EOF
run_case a "$(newws)" "$tmp/a.mm" "" --verify-baseline

if grep -q "This failure predates your changes" "$tmp/a.cap/req.3" 2>/dev/null &&
   grep -qF "$L1" "$tmp/a.cap/req.3"; then
    t_ok "the message says the failure predates the changes, and quotes the line"
else
    t_fail "no predates note in request 3: $(grep -o "end-of-run check[^\"]*" "$tmp/a.cap/req.3" 2>/dev/null | head_bytes 240)"
fi
if grep -q "the run-start verify, before any change, failed on the same line" "$tmp/a.cap/req.3" 2>/dev/null; then
    t_ok "it names the run-start verify as the evidence"
else
    t_fail "the evidence is not named in request 3"
fi
v1=$(grep '"event":"verify"' "$tmp/a.jsonl" 2>/dev/null | sed -n 1p)
v2=$(grep '"event":"verify"' "$tmp/a.jsonl" 2>/dev/null | sed -n 2p)
case "$v1" in *'"retries_left":3'*'"predates":true'*) ok1=1 ;; *) ok1=0 ;; esac
case "$v2" in *'"retries_left":3'*) ok2=1 ;; *) ok2=0 ;; esac
if [ "$ok1" = 1 ] && [ "$ok2" = 1 ]; then
    t_ok "that round spends no retry: the next check still has 3 left"
else
    t_fail "retries: first [$(printf '%s' "$v1" | head_bytes 160)] second [$(printf '%s' "$v2" | head_bytes 160)]"
fi
np=$(grep -o "This failure predates your changes" "$tmp/a.cap/req.5" 2>/dev/null | wc -l | tr -d ' ')
if [ "$np" = "1" ] && grep -q "jichi's end-of-run check failed" "$tmp/a.cap/req.5"; then
    t_ok "once per run: the second failure on the same line gets the plain message"
else
    t_fail "request 5 carries $np predates notes (expected 1, the first message's)"
fi
if grep '"event":"baseline"' "$tmp/a.jsonl" 2>/dev/null | grep -qF "\"line\":\"$L1\""; then
    t_ok "the baseline's journal event records the line"
else
    t_fail "baseline event: $(grep '"event":"baseline"' "$tmp/a.jsonl" 2>/dev/null | head_bytes 200)"
fi

# --- case B: CONTROL -- the baseline fails on L1, the end-of-run check on L2 ----------
gate b "$L1" "$L2"
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one\n"}
rule
  count 2
  text FIRST_ANSWER
rule
  text NOT_MINE
EOF
run_case b "$(newws)" "$tmp/b.mm" "" --verify-baseline
if [ -s "$tmp/b.cap/req.3" ] && ! grep -q "predates your changes" "$tmp/b.cap/req.3" &&
   grep '"event":"verify_declined"' "$tmp/b.jsonl" 2>/dev/null | grep -q '"retries_left":2'; then
    t_ok "CONTROL: a failure on another line gets the plain message and spends a retry"
else
    t_fail "CONTROL: $(grep -c "predates your changes" "$tmp/b.cap/req.3" 2>/dev/null) predates notes; $(grep '"event":"verify_declined"' "$tmp/b.jsonl" 2>/dev/null | head_bytes 160)"
fi

# --- case C: a batch's worktree verify on the starting tree fails on L1 ----------------
gate c "$L1" "$L1"
cat > "$tmp/c.mm" <<'EOF'
wire openai
rule
  match "LEADc"
  match "end-of-run check"
  text LEAD_NOT_MINE
rule
  match "LEADc"
  match "## Parallel agents"
  text LEAD_DONE
rule
  match "LEADc"
  tool spawn_parallel {"tasks":[{"task":"CHILDP add a file","write":true}]}
rule
  match "CHILDP"
  match "\"role\":\"tool\""
  text CHILD_DONE
rule
  match "CHILDP"
  tool write_file {"path":"c.txt","content":"c\n"}
EOF
run_case c "$(newws)" "$tmp/c.mm" ',"parallelVerify":true'
LR=$(grep -l "end-of-run check failed" "$tmp/c.cap"/req.* 2>/dev/null | head -n 1)
if [ -n "$LR" ] &&
   grep -q "This failure predates your changes: task 1's worktree verify" "$LR"; then
    t_ok "a batch's worktree verify on the starting tree is evidence too, named by task"
else
    t_fail "case C: $(grep -h -o "end-of-run check failed[^\"]*" "$tmp/c.cap"/req.* 2>/dev/null | head_bytes 300)"
fi

# --- case D: CONTROL -- the lead changes the tree, then runs the batch ------------------
gate d "$L1" "$L1"
cat > "$tmp/d.mm" <<'EOF'
wire openai
rule
  match "LEADd"
  match "end-of-run check"
  text LEAD_NOT_MINE
rule
  match "LEADd"
  match "## Parallel agents"
  text LEAD_DONE
rule
  match "LEADd"
  match "lead.txt"
  tool spawn_parallel {"tasks":[{"task":"CHILDQ add a file","write":true}]}
rule
  match "LEADd"
  tool write_file {"path":"lead.txt","content":"l\n"}
rule
  match "CHILDQ"
  match "\"role\":\"tool\""
  text CHILD_DONE
rule
  match "CHILDQ"
  tool write_file {"path":"c.txt","content":"c\n"}
EOF
run_case d "$(newws)" "$tmp/d.mm" ',"parallelVerify":true'
LD=$(grep -l "end-of-run check failed" "$tmp/d.cap"/req.* 2>/dev/null | head -n 1)
if [ -n "$LD" ] && ! grep -q "predates your changes" "$LD" &&
   grep -q '"event":"parallel_verify"' "$tmp/d.jsonl" 2>/dev/null; then
    t_ok "CONTROL: a batch on a base the lead had already changed is no evidence"
else
    t_fail "case D: request ${LD:-none}; $(grep -c "predates your changes" "${LD:-/dev/null}" 2>/dev/null) predates notes; parallel_verify $(grep -c '"event":"parallel_verify"' "$tmp/d.jsonl" 2>/dev/null)"
fi

t_done
