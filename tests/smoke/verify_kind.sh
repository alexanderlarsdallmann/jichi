#!/bin/sh
# smoke: the declared gate kind is CHECKED at run start (M343).
#
# Finding 10 of the 2026-08-07 driving session: an invariant verifier ("is the
# tree healthy?") and a goal verifier ("has the work happened?" -- red before
# the work BY CONSTRUCTION) were treated as one, so a goal gate made
# --verify-baseline warn "not known-good" on every correct run. The recorded
# rejections were a silent flag split and inference; --verify-kind is neither:
# one optional declaration, and declaring it ARMS the baseline probe, which
# checks the declaration against the tree. The row that pays: a declared GOAL
# gate that is green on the untouched tree forces nothing (the M330 trap that
# cost ~3M tokens across two runs, ANECDOTES #38) -- said out loud before the
# run spends anything.
#
# Checks 4/5/6 follow the M310 pairing rule: the absence of the old false
# alarm (5) means nothing unless the probe provably ran (4) and the alarm
# still fires where it is true (6).
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mm_script() {
    cat > "$1" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF
}

# --- run 1: declared goal, gate GREEN on the untouched tree -------------------
# No --verify-baseline: the declaration alone must arm the probe.
mm_script "$tmp/a.mm"
mm_start "$tmp/a.mm" "$tmp/cap1" 2
write_config "$tmp/c1.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c1.json" --auto --no-lite \
    --no-session --verify true --verify-kind goal --verify-retries 0 \
    --journal "$tmp/j1.jsonl" -p "hi" < /dev/null) \
    >/dev/null 2>"$tmp/err1"
rc1=$?
mm_stop

if grep -Eq '"forces_nothing": ?1' "$tmp/j1.jsonl" 2>/dev/null && \
   grep -Eq '"kind": ?"goal"' "$tmp/j1.jsonl" 2>/dev/null; then
    t_ok "declaring a kind arms the probe, and the journal carries the verdict"
else
    t_fail "no forces_nothing/kind on the baseline event: $(cat "$tmp/j1.jsonl" 2>/dev/null | head -3)"
fi

if grep -q "forces nothing" "$tmp/err1" 2>/dev/null; then
    t_ok "a green goal gate is called out: it forces nothing"
else
    t_fail "no 'forces nothing' warning: $(cat "$tmp/err1")"
fi

if [ "$rc1" -eq 0 ]; then
    t_ok "the callout is advisory -- the run's verdict is unchanged (exit 0)"
else
    t_fail "run 1 exited $rc1; the baseline check must not change the outcome"
fi

# --- run 2: declared goal, gate RED at start (the normal state) ---------------
# Also passes --verify-every to get the banks-nothing advisory.
mm_script "$tmp/b.mm"
mm_start "$tmp/b.mm" "$tmp/cap2" 2
write_config "$tmp/c2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c2.json" --auto --no-lite \
    --no-session --verify false --verify-kind goal --verify-retries 0 \
    --verify-every 5 --journal "$tmp/j2.jsonl" -p "hi" < /dev/null) \
    >/dev/null 2>"$tmp/err2"
mm_stop

# The probe must provably have run before its silence below means anything.
if grep -Eq '"event": ?"baseline"' "$tmp/j2.jsonl" 2>/dev/null && \
   grep -Eq '"kind": ?"goal"' "$tmp/j2.jsonl" 2>/dev/null; then
    t_ok "the probe ran on the red goal gate (baseline event journaled)"
else
    t_fail "no baseline event in run 2 -- the absence check below proves nothing"
fi

if grep -q "not known-good" "$tmp/err2" 2>/dev/null; then
    t_fail "a red goal gate still raises the standing false alarm"
else
    t_ok "a red goal gate no longer warns 'not known-good' (expected state)"
fi

if grep -q "banks nothing until the gate first passes" "$tmp/err2" 2>/dev/null; then
    t_ok "--verify-every under a goal gate gets the banks-nothing advisory"
else
    t_fail "no verify-every advisory for the goal gate: $(cat "$tmp/err2")"
fi

# --- run 3: declared invariant, gate RED -- the alarm must still fire ---------
mm_script "$tmp/c.mm"
mm_start "$tmp/c.mm" "$tmp/cap3" 2
write_config "$tmp/c3.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c3.json" --auto --no-lite \
    --no-session --verify false --verify-kind invariant --verify-retries 0 \
    --journal "$tmp/j3.jsonl" -p "hi" < /dev/null) \
    >/dev/null 2>"$tmp/err3"
mm_stop

if grep -q "not known-good" "$tmp/err3" 2>/dev/null; then
    t_ok "a red declared invariant still warns 'not known-good' (pairing)"
else
    t_fail "the true alarm was silenced along with the false one: $(cat "$tmp/err3")"
fi

# --- 4: a bad kind is a hard usage error, never a silent drop -----------------
if with_deadline 20 "$BIN" --verify-kind sometimes -p "hi" < /dev/null \
        >/dev/null 2>"$tmp/err4"; then
    t_fail "an unknown kind was accepted"
else
    if grep -q "unknown kind" "$tmp/err4" 2>/dev/null; then
        t_ok "an unknown kind is refused with the two valid values named"
    else
        t_fail "refused, but without naming the problem: $(cat "$tmp/err4")"
    fi
fi

t_done
