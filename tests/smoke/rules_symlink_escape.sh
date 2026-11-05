#!/bin/sh
# smoke: D-34 rules/glossary symlink escape prevention (M992).
# Verify that rules/glossary files that are symlinks to outside the workspace
# are refused. This is the end-to-end driver for the rules/glossary symlink
# protection.
# LEG A (rules escape): CLAUDE.md -> outside file. The marker must NOT appear.
# LEG B (rules control): CLAUDE.md is real file. The marker must appear.
# LEG C (glossary escape): .jichi/glossary.md -> outside file. The marker must NOT appear.
# LEG D (glossary control): .jichi/glossary.md is real file. The marker must appear.
# Every jichi call is under with_deadline so a hang is a t_fail, not a stall.

. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
# BIN comes from _smoke.sh's ${JC_SMOKE_BIN:-...}: the mutant sweep swaps the
# binary through JC_SMOKE_BIN, so a driver must never reassign it (smoke_lint 22).
ws_a="$tmp/ws_a"
ws_b="$tmp/ws_b"
ws_c="$tmp/ws_c"
ws_d="$tmp/ws_d"
outside_a="$tmp/outside_rules.md"
outside_c="$tmp/outside_gloss.md"

# Create mock reply files
cat > "$tmp/replies_a.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

cat > "$tmp/replies_b.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

cat > "$tmp/replies_c.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

cat > "$tmp/replies_d.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

# LEG A: rules escape via symlink
mkdir -p "$ws_a/.git"
printf '%s\n' "OUTSIDE_RULES_M992E2E" > "$outside_a"
ln -s "$outside_a" "$ws_a/CLAUDE.md"
mkdir -p "$tmp/cap_a"
mm_start "$tmp/replies_a.mm" "$tmp/cap_a"
write_config "$tmp/config_a.json" "$MM_PORT"
(cd "$ws_a" && with_deadline 60 "$BIN" --config "$tmp/config_a.json" --no-session --auto -p "hi" < /dev/null >/dev/null 2>&1)
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG A: command timed out (rc=$rc)"
elif [ $rc -ne 0 ] || [ ! -s "$tmp/cap_a/req.1" ]; then
    t_fail "LEG A: did not run (rc=$rc, cap $tmp/cap_a/req.1: $(test -s "$tmp/cap_a/req.1" && echo exists || echo missing))"
elif grep -q "OUTSIDE_RULES_M992E2E" "$tmp/cap_a/req.1" 2>/dev/null; then
    t_fail "LEG A: outside marker found in request"
else
    t_ok "LEG A: outside marker NOT in request (refused)"
fi

# LEG B: rules control (real file)
mkdir -p "$ws_b/.git"
printf '%s\n' "INSIDE_RULES_M992E2E" > "$ws_b/CLAUDE.md"
mkdir -p "$tmp/cap_b"
mm_start "$tmp/replies_b.mm" "$tmp/cap_b"
write_config "$tmp/config_b.json" "$MM_PORT"
(cd "$ws_b" && with_deadline 60 "$BIN" --config "$tmp/config_b.json" --no-session --auto -p "hi" < /dev/null >/dev/null 2>&1)
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG B: command timed out"
elif [ $rc -ne 0 ] || [ ! -s "$tmp/cap_b/req.1" ]; then
    t_fail "LEG B: did not run (rc=$rc, cap $tmp/cap_b/req.1: $(test -s "$tmp/cap_b/req.1" && echo exists || echo missing))"
elif grep -q "INSIDE_RULES_M992E2E" "$tmp/cap_b/req.1" 2>/dev/null; then
    t_ok "LEG B: inside marker found in request (loaded)"
else
    t_fail "LEG B: inside marker NOT in request (over-blocked?)"
fi

# LEG C: glossary escape via symlink
mkdir -p "$ws_c/.git"
mkdir -p "$ws_c/.jichi"
printf '%s\n' "OUTSIDE_GLOSS_M992E2E" > "$outside_c"
ln -s "$outside_c" "$ws_c/.jichi/glossary.md"
mkdir -p "$tmp/cap_c"
mm_start "$tmp/replies_c.mm" "$tmp/cap_c"
write_config "$tmp/config_c.json" "$MM_PORT"
(cd "$ws_c" && with_deadline 60 "$BIN" --config "$tmp/config_c.json" --no-session --auto -p "hi" < /dev/null >/dev/null 2>&1)
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG C: command timed out"
elif [ $rc -ne 0 ] || [ ! -s "$tmp/cap_c/req.1" ]; then
    t_fail "LEG C: did not run (rc=$rc, cap $tmp/cap_c/req.1: $(test -s "$tmp/cap_c/req.1" && echo exists || echo missing))"
elif grep -q "OUTSIDE_GLOSS_M992E2E" "$tmp/cap_c/req.1" 2>/dev/null; then
    t_fail "LEG C: outside marker found in request"
else
    t_ok "LEG C: outside marker NOT in request (refused)"
fi

# LEG D: glossary control (real file)
mkdir -p "$ws_d/.git"
mkdir -p "$ws_d/.jichi"
printf '%s\n' "INSIDE_GLOSS_M992E2E" > "$ws_d/.jichi/glossary.md"
mkdir -p "$tmp/cap_d"
mm_start "$tmp/replies_d.mm" "$tmp/cap_d"
write_config "$tmp/config_d.json" "$MM_PORT"
(cd "$ws_d" && with_deadline 60 "$BIN" --config "$tmp/config_d.json" --no-session --auto -p "hi" < /dev/null >/dev/null 2>&1)
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG D: command timed out"
elif [ $rc -ne 0 ] || [ ! -s "$tmp/cap_d/req.1" ]; then
    t_fail "LEG D: did not run (rc=$rc, cap $tmp/cap_d/req.1: $(test -s "$tmp/cap_d/req.1" && echo exists || echo missing))"
elif grep -q "INSIDE_GLOSS_M992E2E" "$tmp/cap_d/req.1" 2>/dev/null; then
    t_ok "LEG D: inside marker found in request (loaded)"
else
    t_fail "LEG D: inside marker NOT in request (over-blocked?)"
fi

# Teardown
rm -rf "$tmp"

t_done
