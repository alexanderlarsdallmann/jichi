#!/bin/sh
# smoke: machine-checkable assignment specs (M103) -- `assign` renders a
# spec and `grade` runs its verify command, scoring pass/fail with a
# matching exit code. Offline: the verify command is a shell builtin.
# (Port of tests/e2e/grade.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9

cat > "$ws/pass.md" <<'EOF'
---
title: OK
audience: agent
verify: "true"
---
Nothing to do.
EOF
cat > "$ws/fail.md" <<'EOF'
---
title: Nope
audience: junior
verify: "false"
---
Always fails.
EOF

(cd "$ws" && "$BIN" --config "$tmp/config.json" assign pass.md \
    < /dev/null > "$tmp/out" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "machine-checkable" "$tmp/out" \
   && grep -q "true" "$tmp/out"; then
    t_ok "assign renders the agent framing"
else
    t_fail "assign rc=$rc: $(head_bytes 150 "$tmp/out")"
fi

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    grade pass.md < /dev/null > "$tmp/out" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "PASS" "$tmp/out"; then
    t_ok "grade of a passing spec: exit 0 + PASS"
else
    t_fail "grade pass.md rc=$rc: $(head_bytes 150 "$tmp/out")"
fi

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    grade fail.md < /dev/null > "$tmp/out" 2>&1); rc=$?
if [ $rc -eq 1 ] && grep -q "FAIL" "$tmp/out"; then
    t_ok "grade of a failing spec: exit 1 + FAIL"
else
    t_fail "grade fail.md rc=$rc: $(head_bytes 150 "$tmp/out")"
fi

t_done
