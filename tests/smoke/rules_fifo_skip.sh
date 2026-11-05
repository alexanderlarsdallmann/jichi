#!/bin/sh
# smoke: M1020 rules/glossary FIFO skip (review-13 #1 + #3).
# Verify that FIFOs (named pipes) in rules/glossary positions are skipped with a
# single WARN line per file, and that the run continues without blocking.
# POSIX sh only (no python3). Mirror rules_symlink_escape.sh's shape.

. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)

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

# LEG A (FIFO rules): CLAUDE.md is a FIFO. The run must SUCCEED, and a WARN line must appear.
mkdir -p "$tmp/ws_a/.git"
mkfifo "$tmp/ws_a/CLAUDE.md"
mkdir -p "$tmp/cap_a"
mm_start "$tmp/replies_a.mm" "$tmp/cap_a"
write_config "$tmp/config_a.json" "$MM_PORT"
(cd "$tmp/ws_a" && with_deadline 60 "$BIN" --config "$tmp/config_a.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/a_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG A: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG A: did not run (rc=$rc)"
elif ! grep -q "rules: skipping.*CLAUDE.md: not a regular file or symlink" "$tmp/a_err"; then
    t_fail "LEG A: no WARN line for FIFO skip (stderr: $(cat "$tmp/a_err"))"
else
    t_ok "LEG A: FIFO skipped with WARN line (run succeeded)"
fi

# LEG A2 (M1108-b, DIRECTORY rules): CLAUDE.md is a DIRECTORY. The dir-walk
# reaches add_dir_rules' M1020 lstat branch, which skips every non-regular,
# non-symlink shape (a directory included) WITHOUT reading it. Measured on the
# unfixed binary: it prints M1020's own message, not add_file's -- the path
# never reaches the M1108 guard. Assert the full measured text so the leg
# cannot drift to a prefix.
mkdir -p "$tmp/ws_a2/.git"
mkdir -p "$tmp/ws_a2/CLAUDE.md"
mkdir -p "$tmp/cap_a2"
mm_start "$tmp/replies_a.mm" "$tmp/cap_a2"
write_config "$tmp/config_a2.json" "$MM_PORT"
(cd "$tmp/ws_a2" && with_deadline 60 "$BIN" --config "$tmp/config_a2.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/a2_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG A2: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG A2: did not run (rc=$rc)"
elif ! grep -q "rules: skipping.*CLAUDE.md: not a regular file or symlink" "$tmp/a2_err"; then
    t_fail "LEG A2: no full-skip WARN for directory (stderr: $(cat "$tmp/a2_err"))"
else
    t_ok "LEG A2: directory skipped with WARN line, not read (run succeeded)"
fi

# LEG B (control - real file): CLAUDE.md is a regular file. No WARN, file loads.
mkdir -p "$tmp/ws_b/.git"
printf '%s\n' "INSIDE_RULES_M1020" > "$tmp/ws_b/CLAUDE.md"
mkdir -p "$tmp/cap_b"
mm_start "$tmp/replies_b.mm" "$tmp/cap_b"
write_config "$tmp/config_b.json" "$MM_PORT"
(cd "$tmp/ws_b" && with_deadline 60 "$BIN" --config "$tmp/config_b.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/b_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG B: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG B: did not run (rc=$rc)"
elif grep -q "rules: skipping.*CLAUDE.md" "$tmp/b_err"; then
    t_fail "LEG B: WARN line appeared for regular file: $(grep 'skipping' "$tmp/b_err")"
elif ! grep -q "INSIDE_RULES_M1020" "$tmp/cap_b/req.1" 2>/dev/null; then
    t_fail "LEG B: inside marker NOT in request (over-blocked?)"
else
    t_ok "LEG B: regular file loaded (no WARN)"
fi

# --- M1108 (D-33): the OTHER choke routes. add_dir_rules (LEG A/B) was the only
# guarded route; add_file (global AGENTS.md, explicit instructions) and
# append_file (project + global glossary) each reach a FIFO and block start-up
# in jc_read_file's fopen. Same legs: FIFO must be skipped with a WARN and the
# run must NOT hit the deadline (pre-fix it does: rc 124).

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

cat > "$tmp/replies_e.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

cat > "$tmp/replies_f.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

cat > "$tmp/replies_g.mm" <<'EOF'
wire openai
rule
  count 1
  text DONE
EOF

# LEG C (project glossary FIFO): .jichi/glossary.md is a FIFO. append_file must
# skip it (WARN) BEFORE jc_read_file's fopen can block.
mkdir -p "$tmp/ws_c/.git"
mkdir -p "$tmp/ws_c/.jichi"
mkfifo "$tmp/ws_c/.jichi/glossary.md"
mkdir -p "$tmp/cap_c"
mm_start "$tmp/replies_c.mm" "$tmp/cap_c"
write_config "$tmp/config_c.json" "$MM_PORT"
(cd "$tmp/ws_c" && with_deadline 60 "$BIN" --config "$tmp/config_c.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/c_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG C: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG C: did not run (rc=$rc, stderr: $(cat "$tmp/c_err"))"
elif ! grep -q "glossary: skipping" "$tmp/c_err" || ! grep -q "glossary.md" "$tmp/c_err"; then
    t_fail "LEG C: no WARN line for project glossary FIFO (stderr: $(cat "$tmp/c_err"))"
else
    t_ok "LEG C: project glossary FIFO skipped with WARN line (run succeeded)"
fi

# LEG C2 (M1108-b, DIRECTORY glossary): .jichi/glossary.md is a DIRECTORY.
# append_file's M1108 guard takes every non-regular shape, directory included,
# and must say so with a message TRUE for a directory: the M1108-b wording
# "not a regular file -- skipped without reading" (the old
# "(FIFO/device/socket)" parenthetical was false for a directory).
# Red-first: on the unfixed binary this leg's full-text grep fails while the
# run still succeeds (old parenthetical printed).
mkdir -p "$tmp/ws_c2/.git"
mkdir -p "$tmp/ws_c2/.jichi/glossary.md"
mkdir -p "$tmp/cap_c2"
mm_start "$tmp/replies_c.mm" "$tmp/cap_c2"
write_config "$tmp/config_c2.json" "$MM_PORT"
(cd "$tmp/ws_c2" && with_deadline 60 "$BIN" --config "$tmp/config_c2.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/c2_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG C2: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG C2: did not run (rc=$rc, stderr: $(cat "$tmp/c2_err"))"
elif ! grep -q "glossary: skipping.*glossary.md: not a regular file -- skipped without reading" "$tmp/c2_err"; then
    t_fail "LEG C2: no M1108-b WARN for directory (stderr: $(cat "$tmp/c2_err"))"
else
    t_ok "LEG C2: directory glossary skipped with M1108-b WARN (run succeeded)"
fi

# LEG D (control - regular project glossary): loads, no WARN (not over-blocked).
mkdir -p "$tmp/ws_d/.git"
mkdir -p "$tmp/ws_d/.jichi"
printf '%s\n' "INSIDE_GLOSSARY_M1108" > "$tmp/ws_d/.jichi/glossary.md"
mkdir -p "$tmp/cap_d"
mm_start "$tmp/replies_d.mm" "$tmp/cap_d"
write_config "$tmp/config_d.json" "$MM_PORT"
(cd "$tmp/ws_d" && with_deadline 60 "$BIN" --config "$tmp/config_d.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/d_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG D: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG D: did not run (rc=$rc, stderr: $(cat "$tmp/d_err"))"
elif grep -q "glossary: skipping" "$tmp/d_err"; then
    t_fail "LEG D: WARN line appeared for regular file: $(grep 'skipping' "$tmp/d_err")"
elif ! grep -q "INSIDE_GLOSSARY_M1108" "$tmp/cap_d/req.1" 2>/dev/null; then
    t_fail "LEG D: inside marker NOT in request (over-blocked?)"
else
    t_ok "LEG D: regular project glossary loaded (no WARN)"
fi

# LEG E (global rules FIFO): $HOME/.config/jichi/AGENTS.md is a FIFO. The global
# route reaches add_file directly (no dir walk), so M1020's lstat guard never
# saw it. $HOME is the isolated smoke home; clean the fixture up after the leg
# so it cannot leak into LEG F's home scan.
mkdir -p "$HOME/.config/jichi"
mkfifo "$HOME/.config/jichi/AGENTS.md"
mkdir -p "$tmp/ws_e/.git"
mkdir -p "$tmp/cap_e"
mm_start "$tmp/replies_e.mm" "$tmp/cap_e"
write_config "$tmp/config_e.json" "$MM_PORT"
(cd "$tmp/ws_e" && with_deadline 60 "$BIN" --config "$tmp/config_e.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/e_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG E: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG E: did not run (rc=$rc, stderr: $(cat "$tmp/e_err"))"
elif ! grep -q "rules: skipping" "$tmp/e_err" || ! grep -q "AGENTS.md" "$tmp/e_err"; then
    t_fail "LEG E: no WARN line for global AGENTS.md FIFO (stderr: $(cat "$tmp/e_err"))"
else
    t_ok "LEG E: global rules FIFO skipped with WARN line (run succeeded)"
fi
rm -rf "$HOME/.config/jichi"

# LEG F (explicit-instruction rules FIFO): the config `instructions` array
# names a FIFO by absolute path. It reaches add_file directly as well.
mkfifo "$tmp/instr_m1108.md"
mkdir -p "$tmp/ws_f/.git"
mkdir -p "$tmp/cap_f"
mm_start "$tmp/replies_f.mm" "$tmp/cap_f"
cat > "$tmp/config_f.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,
"maxRetries":0,
"instructions":["$tmp/instr_m1108.md"]}
EOF
(cd "$tmp/ws_f" && with_deadline 60 "$BIN" --config "$tmp/config_f.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/f_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG F: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG F: did not run (rc=$rc, stderr: $(cat "$tmp/f_err"))"
elif ! grep -q "rules: skipping" "$tmp/f_err"; then
    t_fail "LEG F: no WARN line for explicit-instruction FIFO (stderr: $(cat "$tmp/f_err"))"
else
    t_ok "LEG F: explicit-instruction FIFO skipped with WARN line (run succeeded)"
fi

# LEG G (M1108-b-b, DIRECTORY explicit-instruction rules): the config
# `instructions` array names a DIRECTORY by absolute path. It reaches
# add_file directly (the explicit-instructions route), which pins the
# RULES-side M1108-b message (the qwen27b M1108-b review finding) at
# full-text driver altitude. Distinct from LEG A2 (which pins M1020's
# add_dir_rules message for a project-rules directory) and from LEG C2
# (which pins the GLOSSARY-side add_file message). A directory cannot
# hang, so rc must be 0.
mkdir -p "$tmp/instr_m1108b_dir"
mkdir -p "$tmp/ws_g/.git"
mkdir -p "$tmp/cap_g"
mm_start "$tmp/replies_g.mm" "$tmp/cap_g"
cat > "$tmp/config_g.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,
"maxRetries":0,
"instructions":["$tmp/instr_m1108b_dir"]}
EOF
(cd "$tmp/ws_g" && with_deadline 60 "$BIN" --config "$tmp/config_g.json" --no-session --auto -p "hi" </dev/null >/dev/null 2>"$tmp/g_err")
rc=$?
if [ $rc -ge 124 ]; then
    t_fail "LEG G: command timed out"
elif [ $rc -ne 0 ]; then
    t_fail "LEG G: did not run (rc=$rc, stderr: $(cat "$tmp/g_err"))"
elif ! grep -q "rules: skipping.*instr_m1108b_dir: not a regular file -- skipped without reading" "$tmp/g_err"; then
    t_fail "LEG G: no M1108-b full-text WARN for directory (stderr: $(cat "$tmp/g_err"))"
else
    t_ok "LEG G: directory explicit-instruction skipped with M1108-b full-text WARN (run succeeded)"
fi

# Teardown
rm -rf "$tmp"

t_done
