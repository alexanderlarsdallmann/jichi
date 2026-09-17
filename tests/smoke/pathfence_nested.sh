#!/bin/sh
# smoke: a write into a directory that does not exist yet, INSIDE the
# workspace, is not a fence violation (M638) -- and a refusal is counted as
# one in the reach footer, apart from the errors.
#
# WHY. Reading the footer in anger (docs/analysis/2026-09-17-reading-the-
# footer-in-anger.md): the model's first write_file aimed at
# `tests/bench/refute_ab/refute_ab.py` before that directory existed, and the
# path fence answered "refused by safety fence (path outside workspace)" --
# false on its face. jc_path_resolve canonicalized one missing component (the
# leaf) and failed closed on a missing PARENT, and jc_app_path_denied reads a
# resolver failure as "outside". The model then built the file by fifty shell
# appends, a third of which failed on quoting; the footer read "23 errors".
# Now the resolver walks up to the deepest existing ancestor and re-appends
# the missing tail (refusing a tail with `.`/`..`), so the write lands, and
# write_file's own mkdir -p creates the directories. The same footer says how
# many of its errors were a fence working: "(1 refused by a fence)".
#
# The mock orders two writes: one into a fresh nested directory inside the
# workspace, one into a fresh nested directory OUTSIDE it. The first must
# land, the second must be refused, the turn completes, and the footer counts
# 2 calls, 1 error, 1 refused.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
other=$(smoke_tmp)
outside="$other/nd/sub/escaped.txt"

cat > "$tmp/replies.mm" <<MM
wire openai
rule
  count 1
  tool write_file {"path":"newdir/sub/files.txt","content":"landed\\n"}
rule
  count 2
  tool write_file {"path":"$outside","content":"pwned"}
rule
  text NESTED_DONE
MM

mm_start "$tmp/replies.mm" "$tmp/cap" 3
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session --auto -p "write the files" < /dev/null \
      > "$tmp/out.txt" 2> "$tmp/err.txt"); rc=$?
mm_stop

if [ -f "$ws/newdir/sub/files.txt" ] && grep -q landed "$ws/newdir/sub/files.txt"; then
    t_ok "write_file into a not-yet-existing nested directory inside the workspace landed"
else
    t_fail "the in-workspace nested write did not land (rc=$rc): $(grep -h -e refused -e error "$tmp/cap/req.2" 2>/dev/null | head_bytes 200)"
fi
if [ ! -e "$outside" ]; then
    t_ok "the same shape OUTSIDE the workspace was still refused (nothing created)"
else
    t_fail "fence ON but the outside nested target was created: $outside"
fi
if grep -q 'refused by safety fence' "$tmp/cap/req.3" 2>/dev/null &&
   ! grep -q 'refused by safety fence' "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "the tool results: the inside write was not refused, the outside one was"
else
    t_fail "refusal placement wrong: req.2=$(grep -c 'refused by safety fence' "$tmp/cap/req.2" 2>/dev/null) req.3=$(grep -c 'refused by safety fence' "$tmp/cap/req.3" 2>/dev/null)"
fi
if grep -q NESTED_DONE "$tmp/out.txt"; then
    t_ok "the turn still completed (rc=$rc)"
else
    t_fail "turn incomplete (rc=$rc): $(head_bytes 160 "$tmp/out.txt")"
fi
if grep -q "2 tool calls, 1 error (1 refused by a fence)" "$tmp/err.txt"; then
    t_ok "the reach footer counts the refusal apart from the errors"
else
    t_fail "footer wrong: $(grep checked "$tmp/err.txt" | head_bytes 240)"
fi
t_done
