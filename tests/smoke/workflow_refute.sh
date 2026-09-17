#!/bin/sh
# smoke: the `refute` workflow stage -- a read-only second seat whose frame the
# spec author cannot weaken, fed the previous stage's output as the claim
# under review (M634).
#
# THE GAP. M602's R2 recommended "ask the second seat to adversarially refute
# the first" and noted the workflow `verify` stage could host it. `verify`
# runs a shell command. Counter-argument was not a move the loop could make.
# Now a fourth stage type beside map / synthesize / verify takes the pipeline
# context as "another agent's claim and evidence" and runs a READ-ONLY
# subagent under a fixed frame: Rebutting / Undercutting / Stands, and "say
# nothing found rather than invent a defeater".
#
# What this driver can prove: the frame reaches the wire (the mock records
# the request), the stage's output carries the three headings, the claim
# under review is the previous stage's text, and the stage cannot write.
# What it cannot prove: that a refuter finds anything real -- that is the
# pre-registered A/B (docs/proposals/2026-09-refute-ab.md), reported as run
# or as not run, never as expected.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
echo "int add(int a, int b) { return a - b; }" > "$ws/add.c"

# Stage 1 (map, read-only): the first seat "reviews" add.c and claims it is
# correct. Stage 2 (refute): the second seat gets that claim. The mock answers
# the refute request -- recognisable by its frame -- with the three headings and
# a write attempt, which the stage must refuse (read-only).
cat > "$tmp/replies.mm" <<'EOF2'
wire openai
rule
  match "You are the second seat"
  match "\"role\":\"tool\""
  text ## Rebutting\n- add.c:1 returns a - b; `add(2,3)` gives -1, the claim of correctness is false.\n## Undercutting\n- nothing found\n## Stands\n- the file exists (checked by reading it)
  usage 30 20
rule
  match "You are the second seat"
  tool write_file {"path":"add.c","content":"int add(int a, int b) { return a + b; }"}
rule
  match "\"role\":\"tool\""
  text FIRST_SEAT_CLAIM: add.c is correct; add returns the sum of its arguments.
  usage 20 10
rule
  tool write_file {"path":"add.c","content":"int add(int a, int b) { return a + b; }"}
EOF2
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
cat > "$ws/spec.json" <<'EOF2'
{ "name": "second-seat",
  "stages": [
    { "type": "map", "readonly": true, "prompt": "Review $ITEM for bugs.", "items": ["add.c"] },
    { "type": "refute" }
  ] }
EOF2
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" workflow spec.json \
    < /dev/null > "$tmp/out.txt" 2> "$tmp/err.txt"); rc=$?
mm_stop

# --- 1: the stage ran and announced itself ---------------------------------------
if [ $rc -eq 0 ] && grep -q "workflow: \[stage 2\] refute" "$tmp/err.txt"; then
    t_ok "the refute stage is a stage type the runner knows and announces"
else
    t_fail "rc=$rc: $(head_bytes 300 "$tmp/err.txt")"
fi
# --- 2: the frame reached the wire, in the SYSTEM message, verbatim ---------------
if grep -q "You are the second seat" "$tmp/cap"/* 2>/dev/null && \
   grep -q "Do not agree with it" "$tmp/cap"/* 2>/dev/null && \
   grep -q "nothing found" "$tmp/cap"/* 2>/dev/null; then
    t_ok "the fixed frame reached the model: second seat, do not agree, say nothing found"
else
    t_fail "frame not on the wire: $(grep -l "second seat" "$tmp/cap"/* 2>/dev/null | wc -l) capture(s)"
fi
# --- 3: the claim under review is the previous stage's output ----------------------
if grep -q "FIRST_SEAT_CLAIM" "$tmp/cap"/* 2>/dev/null; then
    t_ok "the first seat's claim was handed to the second seat as the text under review"
else
    t_fail "previous stage output not in the refute request"
fi
# --- 4: the stage's output carries the three headings --------------------------------
if grep -q "## Rebutting" "$tmp/out.txt" && grep -q "## Undercutting" "$tmp/out.txt" && \
   grep -q "## Stands" "$tmp/out.txt"; then
    t_ok "the workflow's output is the refutation under its three headings"
else
    t_fail "headings missing: $(head_bytes 300 "$tmp/out.txt")"
fi
# --- 5: the refuter could not write -----------------------------------------------------
if grep -q "return a - b" "$ws/add.c"; then
    t_ok "the second seat's write_file was refused: it is a reader, not an author"
else
    t_fail "the refute stage edited add.c"
fi
# --- 6: the first seat's claim is KEPT beside the refutation, not replaced ----------------
if grep -q "FIRST_SEAT_CLAIM" "$tmp/out.txt"; then
    t_ok "the output keeps the claim under review above the refutation (a human reads both)"
else
    t_fail "claim dropped from the output: $(head_bytes 200 "$tmp/out.txt")"
fi
# --- 7: BOTH seats' writes were refused AT THE GATE, not merely unadvertised -----------
# Each refused call comes back to the model as a tool result naming the fence, so
# the next request carries the sentence; two seats, two refusals. The first build
# left the mutating tools off the menu and let the unadvertised call through.
n=$(cat "$tmp/cap"/* 2>/dev/null | grep -o "tool disabled in read-only mode" | wc -l | tr -d ' ')
if [ "$n" -ge 2 ]; then
    t_ok "the read-only map AND the refuter had their write refused at the gate ($n refusals on the wire)"
else
    t_fail "expected 2 gate refusals on the wire, saw $n -- read-only by advertisement only"
fi
t_done
