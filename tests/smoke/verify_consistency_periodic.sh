#!/bin/sh
# smoke: a PERIODIC verify whose verdict contradicts its evidence tells the MODEL,
# not only the operator (M431). Sibling of verify_stuck_periodic.sh, and the same
# defect shape one check over.
#
# THE DEFECT THIS EXISTS FOR. M331 detects a gate that reports failure while no
# test failed and some passed -- a harness fault (a wrapper's exit code, a lint
# step beside the tests), not a code fault. The check's whole return is one
# sentence put in front of the model, and its own comment says so: "so the caller
# can put one sentence in front of the model, which is the whole return on this
# check". It has TWO call sites. The completion fix-forward path rendered it. The
# periodic (--verify-every) path called it as `(void)` and threw the verdict away,
# so a mid-turn red handed the model the raw failure and the exit code with nothing
# redirecting it at the harness -- and a model handed "exit 1" plus a list of zero
# failures reads the exit code and starts editing code.
#
# That inverts the point of the flag: --verify-every exists to catch a problem
# EARLIER, so it is the site that most needs the reframing, and it was the one site
# without it.
#
# Two-sided by construction. The gate below prints "3 passed, 0 failed" and exits 1,
# so jc_env_verify_consistency returns JC_VERIFY_HOLLOW_RED every round -- BOTH counts
# must parse, since the condition is failed == 0 and an unparsed -1 does not satisfy it
# (the first cut of this driver printed only "3 passed", never fired the finding, and
# so failed for the wrong reason). Before the
# fix the harness sentence appeared in the journal and on stderr and in NO captured
# request. MEASURED pre-fix: checks 1 and 2 PASS, 3 and 4 fail (4 depends on 3).
# Check 2 passing is the load-bearing half of the proof -- it shows the finding DID
# fire and was journaled, so check 3 is a genuine model-facing gap rather than a
# dead check that never classified anything.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The model edits a file each round, so tool calls accrue and --verify-every 1
# fires after every round.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"work.txt","content":"attempt one\n"}
rule
  count 2
  tool write_file {"path":"work.txt","content":"attempt two\n"}
rule
  text GAVE_UP
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# A HOLLOW RED gate: it reports passing tests, names no failure, and still exits
# non-zero -- the shape M331 classifies (exit != 0, failed == 0, passed > 0).
out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto \
      --verify "echo '3 passed, 0 failed'; exit 1" \
      --verify-every 1 --max-tool-calls 2 \
      --journal "$tmp/run.jsonl" \
      -p "edit the file" < /dev/null 2>&1); rc=$?
mm_stop

# --- 1: the periodic gate ran and stayed red ---------------------------------
nver=$(grep -c '"event":"verify"' "$tmp/run.jsonl" 2>/dev/null || true)
if [ "$nver" -ge 1 ]; then
    t_ok "the periodic verifier ran $nver time(s)"
else
    t_fail "rc=$rc no verify events -- the periodic gate did not fire; events=$(grep -o '"event":"[a-z_]*"' "$tmp/run.jsonl" 2>/dev/null | sort | uniq -c | tr '\n' ' ')"
fi

# --- 2: the OPERATOR half (unchanged by M431) --------------------------------
# The journal carried the classification before the fix and must still carry it;
# this is what proves check 3 is a genuine model-facing gap, not a dead check.
if grep -q '"consistency":"hollow_red"' "$tmp/run.jsonl" 2>/dev/null; then
    t_ok "the journal classifies the periodic red as hollow_red"
else
    t_fail "no consistency:hollow_red in the journal: $(grep -o '"phase":"periodic"[^}]*' "$tmp/run.jsonl" 2>/dev/null | head -1)"
fi

# --- 3: the MODEL half (the M431 fix) ----------------------------------------
# The note travels in a user message inside the NEXT request body, so the captured
# requests are the only ground truth for "the model was told" -- the M429 lesson,
# whose driver first asserted on the transcript and failed while the code was right.
if grep -l "likely in the verification harness itself" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the harness-fault reframing reached the model"
else
    t_fail "reframing absent from all $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l) captured requests -- the model got the exit code with no redirection"
fi

# --- 4: and it arrives BEFORE the evidence it reframes ------------------------
# Ordering is the point, not decoration: the completion path's own comment says the
# sentence "has to arrive before the evidence it reframes". Assert the NOTE precedes
# the output tail in the same request body.
req=$(grep -l "likely in the verification harness itself" "$tmp"/cap/req.* 2>/dev/null | head -1)
if [ -n "$req" ]; then
    n_note=$(grep -o "NOTE: 3 test" "$req" | wc -l)
    # tr the body to lines so the two markers can be ordered by line number
    note_at=$(tr ',' '\n' < "$req" | grep -n "likely in the verification harness" | head -1 | cut -d: -f1)
    tail_at=$(tr ',' '\n' < "$req" | grep -n -- "--- output" | head -1 | cut -d: -f1)
    if [ -n "$note_at" ] && [ -n "$tail_at" ] && [ "$note_at" -lt "$tail_at" ]; then
        t_ok "the reframing precedes the output tail (note@$note_at < tail@$tail_at)"
    else
        t_fail "ordering wrong or markers missing (note@${note_at:-none} tail@${tail_at:-none}, notes=$n_note)"
    fi
else
    t_fail "no request carried the reframing, so ordering cannot be checked"
fi

t_done
