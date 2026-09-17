#!/bin/sh
# smoke: `jichi assignments` is an orientation, not a flat list (M626; the
# DEFERRED row's own design: stage grouping, per-stage point totals, a filter).
#
# THE GAP. All 77 shipped specs printed as one name-sorted list, so a day-one
# learner saw language tracks they cannot run, and the stage-gate arithmetic
# ("14 of 17 points") was hand-arithmetic against INDEX.md. The unlock is a
# `stage:` frontmatter key; grouping activates ONLY when at least one spec
# carries it, so any non-curriculum workspace (and every existing fixture)
# renders exactly as before -- that boundary is check 7, and its tooth is
# perturbing the activation condition.
#
# Deliberately NOT printed: gate thresholds. A gate is more than points (two
# record entries, task 09 required, all-four-floors), so a binary saying
# "gate met" on points alone would lie about Sets B and C (DECISIONS, M626).
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws/docs/assignments"

cat > "$ws/docs/assignments/10-a.md" <<'EOF'
---
title: first shu task
stage: shu
phase: implementation
verify: "true"
points: 2
---
Always passes.
EOF
cat > "$ws/docs/assignments/11-b.md" <<'EOF'
---
title: second shu task
stage: shu
phase: implementation
verify: "false"
points: 3
---
Always fails.
EOF
cat > "$ws/docs/assignments/20-c.md" <<'EOF'
---
title: a ha task
stage: ha
phase: testing
verify: "false"
points: 4
---
Later stage.
EOF
cat > "$ws/docs/assignments/05-z.md" <<'EOF'
---
title: stageless straggler
phase: testing
verify: "false"
points: 3
---
Carries no stage key.
EOF

# one recorded pass, so the earned side of the totals is non-zero
(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/10-a.md --record \
    < /dev/null > /dev/null 2>&1)

out=$(cd "$ws" && with_deadline 20 "$BIN" assignments < /dev/null 2>&1); rc=$?

# --- 1: the shu group header carries the exact totals -----------------------------
case "$out" in
    *"-- shu  (2/5 pts, 1/2 passed)"*) t_ok "shu header: 2/5 pts, 1/2 passed" ;;
    *) t_fail "no shu totals header (rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)" ;;
esac

# --- 2: groups appear in first-seen order under the name sort ---------------------
shu_at=$(printf '%s\n' "$out" | grep -n -- "-- shu" | cut -d: -f1 | head -1)
ha_at=$(printf '%s\n' "$out" | grep -n -- "-- ha" | cut -d: -f1 | head -1)
if [ -n "$shu_at" ] && [ -n "$ha_at" ] && [ "$shu_at" -lt "$ha_at" ]; then
    t_ok "shu (first member 10-) groups before ha (first member 20-)"
else
    t_fail "group order wrong (shu@$shu_at ha@$ha_at)"
fi

# --- 3: a stage-less spec groups last under (no stage) ----------------------------
nostage_at=$(printf '%s\n' "$out" | grep -n -- "-- (no stage)" | cut -d: -f1 | head -1)
if [ -n "$nostage_at" ] && [ "$nostage_at" -gt "$ha_at" ]; then
    t_ok "the stageless spec groups last, labeled (no stage)"
else
    t_fail "(no stage) group missing or misplaced: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 4: one overall line, exact arithmetic ----------------------------------------
case "$out" in
    *"total: 2/12 pts, 1/4 passed"*) t_ok "overall line: 2/12 pts, 1/4 passed" ;;
    *) t_fail "overall line wrong or absent: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)" ;;
esac

# --- 5: --stage filters to one group ----------------------------------------------
fout=$(cd "$ws" && with_deadline 20 "$BIN" assignments --stage shu < /dev/null 2>&1)
if printf '%s' "$fout" | grep -q "10-a.md" && \
   printf '%s' "$fout" | grep -q "11-b.md" && \
   ! printf '%s' "$fout" | grep -q "20-c.md"; then
    t_ok "--stage shu shows the shu rows and nothing else"
else
    t_fail "--stage filter wrong: $(printf '%s' "$fout" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 6: an unknown slug is refused and the known values are named -----------------
fout=$(cd "$ws" && with_deadline 20 "$BIN" assignments --stage nosuch < /dev/null 2>&1); frc=$?
if [ "$frc" -eq 1 ] && printf '%s' "$fout" | grep -q "shu"; then
    t_ok "an unmatched --stage exits 1 and names the stages that exist"
else
    t_fail "unmatched slug: rc=$frc, $(printf '%s' "$fout" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 7: BOUNDARY -- a workspace with no stage: keys renders flat, as today --------
ws2=$(smoke_tmp)
mkdir -p "$ws2/docs/assignments"
sed '/^stage:/d' "$ws/docs/assignments/10-a.md" > "$ws2/docs/assignments/10-a.md"
fout=$(cd "$ws2" && with_deadline 20 "$BIN" assignments < /dev/null 2>&1)
if ! printf '%s' "$fout" | grep -q -- "^  --" && \
   ! printf '%s' "$fout" | grep -q "total:" && \
   printf '%s' "$fout" | grep -q "10-a.md"; then
    t_ok "no stage keys anywhere: no group headers, no total line -- flat as before"
else
    t_fail "flat mode broken: $(printf '%s' "$fout" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 8: JSON rows carry the stage -------------------------------------------------
jout=$(cd "$ws" && with_deadline 20 "$BIN" assignments --output json < /dev/null 2>&1)
if printf '%s' "$jout" | grep -q '"stage": *"shu"' && \
   printf '%s' "$jout" | grep -q '"stage": *"ha"'; then
    t_ok "--output json rows carry the stage string"
else
    t_fail "no stage in JSON: $(printf '%s' "$jout" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 9-10: the TUI /assignments shows the same grouping (one mechanic) ------------
cat > "$tmp/t.pd" <<'EOF'
expect "] " 15
delay 400
send "/assignments\r"
expect "-- shu  (2/5 pts, 1/2 passed)" 15
expect "total: 2/12 pts, 1/4 passed" 15
delay 400
send "/exit\r"
waitexit 12
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 120 \
    --log "$tmp/t.log" "$tmp/t.pd" -- "$BIN" --no-route --no-lite); rc=$?
if [ "$rc" -eq 0 ]; then
    t_ok "TUI /assignments prints the same shu header"
else
    t_fail "TUI grouping absent (rc=$rc): $(tail -c 200 "$tmp/t.log" | tr '\n' ' ')"
fi
if grep -q "hints 0" "$tmp/t.log"; then
    t_fail "TUI printed a hints suffix for rows with no pulls"
else
    t_ok "no spurious hints suffix in the TUI listing"
fi
t_done
