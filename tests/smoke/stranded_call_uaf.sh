#!/bin/sh
# smoke: the M919 stranded-call loop must not use-after-free its own appends
# (R01-N2, M1127).
#
# THE GAP THIS CLOSES. `budget_stop_answers_stranded.sh` (M919) strands exactly
# ONE call, so the M919 loop body runs once and never re-reads `am` after the
# append. The defect (R01-N2) needs at least TWO stranded calls: the loop reads
# the assistant message through `am`, fetched ONCE before the loop, then appends
# a tool result for every stranded call. `jc_history_add` -> `jc_vec_push_slot`
# -> `jc_vec_reserve` REALLOCS the message vector, so from the second append on
# `am` points into freed memory and `jc_msg_tool_call_at(am, r)` is a heap
# use-after-free. On a default build the stale glibc bytes happen to be intact,
# so the defect is invisible there -- only AddressSanitizer sees it. That is why
# this driver is SAN-only (the leak_turn.sh shape): it runs in the Makefile's
# sanitizer stage, where the binary is built with the instrumentation.
#
# THE RIG (the lead's n2 shape, confirmed on head f4efc046): two single-call
# rounds (a, b), then a four-read_file round (c1..c4) under `--max-tool-calls 3`.
# jichi runs a (call 1), b (call 2), c1 (call 3) -- the budget is exhausted --
# and before c2 the mid-round budget check fires, stranding c2, c3 and c4:
# three appends into the history vector, the second of which reallocs it.
#
# Check 1 establishes the rig is real (a budget stop, at least three requests);
# checks 2-3 read the ASan verdict by effect: RED pre-fix (the report is
# present), GREEN post-fix (it is not).
. "$(dirname "$0")/_smoke.sh"

# ---- preconditions (before t_plan: t_skip is a whole-driver verb) ------------
strings "$BIN" 2>/dev/null | grep -qE "LeakSanitizer|__asan_" \
    || t_skip "needs a SAN=1 build (make clean && make SAN=1 CC=gcc jichi)"

smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

t_plan 3

# ---- the n2 rig ---------------------------------------------------------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"a.txt"}
rule
  count 2
  tool read_file {"path":"b.txt"}
rule
  count 3
  tool read_file {"path":"c1.txt"}
  tool read_file {"path":"c2.txt"}
  tool read_file {"path":"c3.txt"}
  tool read_file {"path":"c4.txt"}
rule
  count 4
  text DONE
EOF
for f in a b c1 c2 c3 c4; do
    printf 'content of %s\n' "$f" > "$ws/$f.txt"
done

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

# halt_on_error=1: the first ASan error kills the run, so the report in $tmp/err
# is THE one, and the driver cannot mistake a later report for it. `env` (not an
# assignment prefix): a prefix on the with_deadline shell function is not exported
# by FreeBSD's /bin/sh and would be silently dropped (smoke_lint check 13).
(cd "$ws" && with_deadline 120 env ASAN_OPTIONS=halt_on_error=1 "$BIN" \
    --config "$tmp/config.json" --auto --max-tool-calls 3 --no-session \
    -p "read the files" < /dev/null > "$tmp/out" 2> "$tmp/err") || true

# ---- 1: the rig is real -------------------------------------------------------
_nreq=$(ls "$tmp/cap"/req.* 2>/dev/null | wc -l | tr -d '[:space:]')
[ -n "$_nreq" ] || _nreq=0
if grep -q 'budget' "$tmp/err" && [ "$_nreq" -ge 3 ]; then
    t_ok "the run stopped on the tool-call budget mid-round ($_nreq requests, the four-call round stranded)"
else
    t_fail "no mid-round budget stop (or fewer than 3 requests) -- the rig is not
the n2 shape, so a clean verdict below would cover a different run:
budget-on-stderr=$(grep -c 'budget' "$tmp/err" 2>/dev/null) requests=$_nreq
err: $(head_bytes 300 < "$tmp/err")"
fi

# ---- 2: THE FIX, by effect ----------------------------------------------------
if grep -q 'heap-use-after-free' "$tmp/err"; then
    t_fail "AddressSanitizer heap-use-after-free in the M919 stranded-call loop
(R01-N2):
$(grep 'ERROR: AddressSanitizer' "$tmp/err" | head -1 | head_bytes 200)
freed-by frame: $(grep 'jc_history_add_tool_result' "$tmp/err" | head -1 | head_bytes 240)"
else
    t_ok "no ASan heap-use-after-free across the stranded-call loop (n2 shape: 3 appends into a reallocing history vector)"
fi

# ---- 3: no other ASan error either --------------------------------------------
if grep -q 'ERROR: AddressSanitizer' "$tmp/err"; then
    t_fail "AddressSanitizer reported another error in the same n2 shape:
$(grep -A8 'ERROR: AddressSanitizer' "$tmp/err" | head_bytes 600)"
else
    t_ok "no AddressSanitizer error of any kind on the stranded-call run"
fi

mm_stop
t_done
