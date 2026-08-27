#!/bin/sh
# smoke: `constraints scan` predicts what a prompt would get ADOPTED, offline
# (M327), and `constraints` lists the persisted store.
#
# The check that carries the weight is the last one: what `scan` predicts must
# equal what a real --auto run actually adopts. A predictor that drifts from the
# thing it predicts is worse than none -- it is a green light that means nothing
# (docs/TEST_INTEGRITY.md). So the prediction is compared against the adoption
# notice from a real run over the identical prompt, rather than against a
# hand-written expectation.
#
# Everything except that last check needs no model at all: the scanner is pure.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# --- 1. a real prohibition is reported, and exits 1 -------------------------
printf 'Fix the parser. Do not run the build, nor the tests.\n' > "$tmp/bad.md"
out=$("$BIN" constraints scan "$tmp/bad.md" 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q 'would be ADOPTED'; then
    t_ok "prohibition reported, exit 1"
else
    t_fail "expected exit 1 + ADOPTED, got rc=$rc: $(printf '%s' "$out" | head -2)"
fi

# The constraints must be NAMED, not merely counted -- M167's whole lesson is
# that a count is what let a misparse stay a mystery.
if printf '%s' "$out" | grep -q 'deny-cmd' &&
   printf '%s' "$out" | grep -q 'build' &&
   printf '%s' "$out" | grep -q 'test'; then
    t_ok "each adopted constraint is named with its kind and subject"
else
    t_fail "not named: $out"
fi

# --- 2. ordinary prose adopts nothing, and exits 0 -------------------------
cat > "$tmp/clean.md" <<'EOF'
Wire src/animation into the test gate.

Write only under `src/animation/**` and `src/root.zig`. The Godot tree at
../godotengine/godot is an input you read. Keep every existing gate block and
add to them. Read the files directly; you do not need vm.zig, which is larger
than the whole context window.
EOF
out=$("$BIN" constraints scan "$tmp/clean.md" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'nothing would be adopted'; then
    t_ok "positively-phrased brief adopts nothing, exit 0"
else
    t_fail "expected exit 0 + nothing, got rc=$rc: $(printf '%s' "$out" | head -2)"
fi

# --- 3. stdin works, so a brief can be piped in ----------------------------
out=$(printf 'Do not run the tests.\n' | "$BIN" constraints scan - 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q '(stdin)'; then
    t_ok "reads stdin via -"
else
    t_fail "stdin form failed rc=$rc: $(printf '%s' "$out" | head -2)"
fi

# --- 4. the listing form reads the persisted store -------------------------
mkdir -p "$ws/.jichi"
printf 'deny-tool git_commit; leave committing to the operator\n' \
    > "$ws/.jichi/constraints.md"
out=$(cd "$ws" && "$BIN" constraints 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'deny-tool' &&
   printf '%s' "$out" | grep -q 'git_commit'; then
    t_ok "listing shows the persisted store"
else
    t_fail "listing failed rc=$rc: $(printf '%s' "$out" | head -3)"
fi

# --- 5. the prediction agrees with what a real run adopts ------------------
# This is the anti-drift check. Same prompt, two paths: the offline scanner and
# the live adoption notice. If they ever disagree, the predictor is lying.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text DONE
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"testCommand":"true"'

prompt='Fix the parser. Do not run the build, nor the tests.'
predicted=$(printf '%s\n' "$prompt" | "$BIN" constraints scan - 2>&1)
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "$prompt" \
    < /dev/null > /dev/null 2>"$tmp/live.err")

# Every subject the scanner predicted must appear in the live adoption notice.
#
# `npred` is load-bearing, and it is here because this check was HOLLOW without
# it: with the subcommand reverted, `constraints scan` fell through to being
# treated as a prompt, predicted nothing, the loop below iterated over an empty
# set, and the check passed green while measuring nothing. Requiring the
# predictor to have actually predicted something is what makes the agreement
# mean agreement (docs/TEST_INTEGRITY.md; the same shape as M86's hollow gate).
missing=
npred=0
for subj in build test run_tests; do
    if printf '%s' "$predicted" | grep -q "$subj"; then
        npred=$((npred + 1))
        grep -q "$subj" "$tmp/live.err" || missing="$missing $subj"
    fi
done
if [ "$npred" -ge 2 ] && [ -z "$missing" ] &&
   grep -i -q 'constraint' "$tmp/live.err"; then
    t_ok "prediction matches the live adoption notice ($npred subjects)"
else
    t_fail "predicted=$npred (need >=2), live notice lacks:$missing -- $(tail -c 300 "$tmp/live.err")"
fi

t_done
