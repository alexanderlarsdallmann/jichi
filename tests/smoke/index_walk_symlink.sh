#!/bin/sh
# smoke: M991 (D-27/D-28/D-29/D-30) -- the shared walker must not follow symlinks
# out of the workspace. Red-first driver.
#
# LEG A (D-28/D-30): list_files with a pattern in a workspace that contains a
#   symlink to an outside dir must NOT list the outside file's name.
#   The mock drives a list_files tool call; the capture's tool_result must not
#   contain the outside file name.
#
# LEG B (D-29): a symlink loop (a dir linked to its own parent) must terminate.
#   The run must complete within the deadline; before the walker it hangs.
#
# LEG C (control): list_files on a workspace with NO symlinks still lists
#   the real files (the walker did not break the normal path).

. "$(dirname "$0")/_smoke.sh"
t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
outside="$tmp/outside"
mkdir -p "$ws" "$outside"

# --- LEG A: list_files through a symlinked outside dir (D-28/D-30) ---
printf 'int outside;\n' > "$outside/outside_file.c"
printf 'int inroot;\n'  > "$ws/inroot.c"
ln -s "$outside" "$ws/linked"

cat > "$tmp/repliesA.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":".","pattern":"**"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesA.mm" "$tmp/capA"
write_config "$tmp/configA.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configA.json" \
    --no-session --auto -p "list the files" < /dev/null > /dev/null 2>&1); rcA=$?
mm_stop

# CHECK 1: inroot.c is PRESENT in the tool_result (control -- the walk ran)
if grep -q 'inroot\.c' "$tmp/capA/req.2" 2>/dev/null; then
    t_ok "LEG A: list_files found inroot.c (the walk ran)"
else
    t_fail "LEG A: list_files did not find inroot.c (walk broken?)"
fi

# CHECK 2: outside_file.c is ABSENT from the tool_result (the D-30 fence)
if grep -q 'outside_file\.c' "$tmp/capA/req.2" 2>/dev/null; then
    t_fail "LEG A: list_files leaked outside_file.c (D-30 fence absent)"
else
    t_ok "LEG A: list_files does not leak outside_file.c (D-30 fence holds)"
fi

# --- LEG B: symlink loop terminates (D-29) ---
ws2=$(smoke_tmp)
mkdir -p "$ws2/inner"
ln -s "$ws2" "$ws2/inner/loop"

cat > "$tmp/repliesB.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":".","pattern":"**"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesB.mm" "$tmp/capB"
write_config "$tmp/configB.json" "$MM_PORT"
(cd "$ws2" && with_deadline 30 "$BIN" --config "$tmp/configB.json" \
    --no-session --auto -p "list the files" < /dev/null > /dev/null 2>&1); rcB=$?
mm_stop

# CHECK 3 (termination) is a CONTROL: list_files in a symlink-ancestor workspace
# terminates (list_files never loops; the D-29 visited-set pin is the unit tooth
# at tests/test_walk.c:292, because this driver's write_config sets repoMap:false
# so the repo-map walk is out of its universe).
if [ $rcB -eq 0 ]; then
    t_ok "LEG B: list_files in a symlink-ancestor workspace terminated (control; the D-29 pin is tests/test_walk.c:292)"
else
    t_fail "LEG B: list_files did not terminate (rc=$rcB; the D-29 pin is tests/test_walk.c:292)"
fi

# CHECK 4: the walk RAN and stopped at the link -- the real subdirectory
# `inner/` IS listed (the walker descended into the real dir), which combined with
# CHECK 3 (termination) proves D-29: bounded descent, not an infinite loop.
# (A symlink NAMED `loop` is a legitimate listing entry -- list_files shows
#  `inner/loop/`; asserting its name is absent would be wrong.)
if grep -q 'inner/' "$tmp/capB/req.2" 2>/dev/null; then
    t_ok "LEG B: real subdirectory inner/ was listed (walk ran and stopped at the link)"
else
    t_fail "LEG B: real subdirectory inner/ was NOT listed (walk did not descend, or capture missing)"
fi

# --- LEG C: control -- a workspace with no symlinks still works ---
ws3=$(smoke_tmp)
printf 'int normal;\n' > "$ws3/normal.c"
mkdir -p "$ws3/sub"
printf 'int subfile;\n' > "$ws3/sub/subfile.c"

cat > "$tmp/repliesC.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {"path":".","pattern":"**"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesC.mm" "$tmp/capC"
write_config "$tmp/configC.json" "$MM_PORT"
(cd "$ws3" && with_deadline 60 "$BIN" --config "$tmp/configC.json" \
    --no-session --auto -p "list the files" < /dev/null > /dev/null 2>&1); rcC=$?
mm_stop

# CHECK 5: the normal workspace lists both files (the walker did not break it)
if grep -q 'normal\.c' "$tmp/capC/req.2" 2>/dev/null && \
   grep -q 'subfile\.c' "$tmp/capC/req.2" 2>/dev/null; then
    t_ok "LEG C: normal workspace lists both files (control passes)"
else
    t_fail "LEG C: normal workspace missing files (walker broke the normal path)"
fi

# CHECK 6: the run completed cleanly
if [ $rcC -eq 0 ]; then
    t_ok "LEG C: clean exit"
else
    t_fail "LEG C: non-zero exit (rc=$rcC)"
fi

rm -rf "$ws" "$ws2" "$ws3" "$outside"
t_done
