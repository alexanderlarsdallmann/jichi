#!/bin/sh
# smoke: the synthesis loop (M109) -- `improve <specs-dir>` grades a spec
# suite for a pass-rate, tracks it under $HOME/.jichi.d/improve/, and
# writes a propose-only report. Offline: verify commands are builtins.
# (Port of tests/e2e/improve.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9
mkdir "$ws/specs"

cat > "$ws/specs/a.md" <<'EOF'
---
title: A
audience: agent
verify: "true"
---
ok
EOF
cat > "$ws/specs/b.md" <<'EOF'
---
title: B
audience: agent
verify: "false"
---
no
EOF

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    improve specs < /dev/null > /dev/null 2>"$tmp/err1"); rc=$?
if [ $rc -eq 0 ] && grep -q "50%" "$tmp/err1" \
   && grep -q "baseline" "$tmp/err1"; then
    t_ok "first improve reports the 50% baseline"
else
    t_fail "first improve rc=$rc: $(head_bytes 200 "$tmp/err1")"
fi

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    improve specs < /dev/null > /dev/null 2>"$tmp/err2")
if grep -q "unchanged" "$tmp/err2"; then
    t_ok "second improve reads 'unchanged' vs the prior run"
else
    t_fail "no 'unchanged': $(head_bytes 200 "$tmp/err2")"
fi

hist="$HOME/.jichi.d/improve/history.jsonl"
if [ -f "$hist" ] && [ "$(grep -c . "$hist")" -ge 2 ]; then
    t_ok "a pass-rate line is appended per run"
else
    t_fail "history.jsonl missing or short"
fi

report=""
for f in "$HOME/.jichi.d/improve/report-"*.md; do
    [ -f "$f" ] && report="$f"
done
if [ -n "$report" ]; then
    t_ok "a propose-only report was written"
else
    t_fail "no report-*.md under ~/.jichi.d/improve"
fi

if [ -n "$report" ] && grep -q "Propose-only" "$report" \
   && grep -q "Pass-rate" "$report"; then
    t_ok "report carries its framing"
else
    t_fail "report missing Propose-only/Pass-rate framing"
fi

t_done
