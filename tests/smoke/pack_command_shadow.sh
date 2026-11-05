#!/bin/sh
# smoke: a scaffold pack never ships a command the TUI's built-ins would hide, and
# doctor says so when a project has one (M751).
#
# THE DEFECT. The TUI dispatches its built-in slash commands BEFORE it looks in
# .jichi/commands/, and headless `-p` has no built-ins at all. The sdlc pack
# scaffolded commands/design.md ("draft docs/DESIGN.md"), and M462 had added a
# built-in /design (set or show the design doc). So one name did two things:
# `jichi -p "/design"` drafted a design, while typing /design in the TUI loaded
# nothing and said "no design doc loaded". UML_TUTORIAL.md told a learner to type
# it. doctor has warned about a shadowed command since M262, but its registry
# lacked `design`, and nothing had ever run that warning. This driver runs it.
#
# The pack's command is now /design-doc, named after the skill it follows.
# builtin_cmds_lint check 5 holds every pack to this statically; this driver holds
# the behaviour a user meets: what init writes, what headless expands, and what
# doctor says about a project scaffolded before the rename.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# --- 1: init sdlc writes /design-doc, and no design.md for /design to hide ------
(cd "$ws" && "$BIN" init sdlc < /dev/null > "$tmp/init" 2>&1); rc=$?
if [ $rc -eq 0 ] && [ -f "$ws/.jichi/commands/design-doc.md" ] \
   && [ ! -f "$ws/.jichi/commands/design.md" ]; then
    t_ok "init sdlc scaffolds commands/design-doc.md and no design.md"
else
    t_fail "init sdlc rc=$rc; design-doc.md $( [ -f "$ws/.jichi/commands/design-doc.md" ] && echo present || echo MISSING ), design.md $( [ -f "$ws/.jichi/commands/design.md" ] && echo PRESENT || echo absent )"
fi

# --- 2: headless -p "/design-doc" expands the pack's prompt -- it reaches the model
cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  text DRAFTED
EOF
mm_start_unbounded "$tmp/r.mm" "$tmp/cap"
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --no-session \
    -p "/design-doc" < /dev/null > "$tmp/out" 2> "$tmp/err"); rc=$?
mm_stop
if grep -q 'per the design-doc skill' "$tmp/cap"/req.* 2>/dev/null; then
    t_ok "headless /design-doc sends the pack's prompt to the model"
else
    t_fail "the model never saw the /design-doc prompt (rc=$rc): $(head_bytes 160 "$tmp/err")"
fi

# --- 3: doctor names the shadow for a design.md scaffolded before the rename -----
cp "$ws/.jichi/commands/design-doc.md" "$ws/.jichi/commands/design.md"
(cd "$ws" && "$BIN" --config "$tmp/c.json" doctor < /dev/null > "$tmp/doc" 2>&1)
if grep -q 'shadowed by the built-in /design' "$tmp/doc"; then
    t_ok "doctor warns that the built-in /design shadows commands/design.md"
else
    t_fail "doctor is silent about commands/design.md: $(grep -i 'design' "$tmp/doc" | head_bytes 200)"
fi

# --- 4: and does not warn about the renamed command (no false positive) ----------
if grep -q 'shadowed by the built-in /design-doc' "$tmp/doc"; then
    t_fail "doctor calls /design-doc shadowed, which no built-in is"
else
    t_ok "doctor does not call /design-doc shadowed"
fi

t_done
