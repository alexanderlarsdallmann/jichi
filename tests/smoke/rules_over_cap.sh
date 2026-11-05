#!/bin/sh
# smoke: M1102 -- `learn apply` must REFUSE to overwrite a PRESENT-but-unreadable
# rules file (the learn side of the D-16/M1100 finding), and the refusal line must
# NAME THE REAL FILE.
#
# The M1100 jc_read_existing tri-state in learn_apply_rules (the over-cap AGENTS.md
# refusal) was reached by NO driver: memory_over_cap.sh only goes through
# remember -> jc_memory_add. Its target is not .jichi/rules.md either --
# learn_rules_path writes the workspace's AGENTS.md (or CLAUDE.md, M533: write
# where the reader reads). This driver pins both legs, offline (no mockmodel):
# `learn apply` is a pure filesystem command.
#
# The supervisor's measurement (SV:1637): at the pre-M1100 head, an over-cap
# AGENTS.md (67109888 B) was REPLACED by an 85-byte fresh rules file; with M1100's
# tri-state it is REFUSED (67109888 -> 67109888). And the refusal's stdout detail
# line said ".jichi/rules.md" -- the WRONG file (point 3). The fix makes it name
# the real path.
#
#   LEG A (SV point 2 + 3, RED-FIRST): over-cap AGENTS.md -> refused, byte-exact
#         survival (check 1 green on the current head: the tri-state already
#         works), the run exits 0 (check 4 green), and the detail line NAMES THE
#         REAL FILE and NOT .jichi/rules.md (checks 2 and 3 RED on the unfixed
#         tree: today it says "rules skipped: .jichi/rules.md denied, symlink,
#         or unreadable"); the M1100 stderr warn is present (check 5 green).
#   LEG B (the append still works, SV's 36 -> 105): small AGENTS.md -> the bullet
#         is appended, the file grows, the detail says the rule was written.
#   LEG C (CLAUDE.md fallback, cheap): no AGENTS.md, a small CLAUDE.md -> the
#         bullet lands in CLAUDE.md and the detail names CLAUDE.md (pins
#         learn_rules_path's fallback in the apply context too).
#
# t_plan 7 (Leg A is 5 checks, A1-A5). RED-FIRST on the unfixed head: checks 2
# and 3 go red (the wording); checks 1, 4, 5 (the arm the SV measured at
# f7d02c40) and 6, 7 (Legs B and C) stay green.

. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

# 64 MiB + 1 KiB: 65537 x 1024 zeros -- exactly the SV's run-0106 repro size
# (the memory_over_cap.sh OVER_BYTES idiom; the trigger is over JC_READ_FILE_MAX).
OVER_BYTES=67109888

# --- LEG A: the finding (over-cap AGENTS.md refused, byte-exact, real name) ----
wsA=$(smoke_tmp)
mkdir -p "$wsA/.jichi"
dd if=/dev/zero of="$wsA/AGENTS.md" bs=1024 count=65537 2>/dev/null
# sanity: the fixture is really over the cap
szA0=$(wc -c < "$wsA/AGENTS.md" | tr -d ' ')

cat > "$wsA/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- prefer small commits
EOF

( cd "$wsA" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/outA" 2> "$tmp/errA" ); rcA=$?
szA=$(wc -c < "$wsA/AGENTS.md" 2>/dev/null | tr -d ' ')

# CHECK 1 (A1): AGENTS.md is STILL over the cap after the run, and its leading
# bytes are still zeros (the pre-M1100 replacement shape was 85 bytes of text).
first16A=$(head_bytes 16 < "$wsA/AGENTS.md" | od -An -tx1 | tr -d ' \n')
if [ "$szA" = "$OVER_BYTES" ] && [ "$first16A" = "00000000000000000000000000000000" ]; then
    t_ok "Leg A: over-cap AGENTS.md still $OVER_BYTES bytes, leading bytes untouched (refused, not replaced)"
else
    t_fail "Leg A: over-cap AGENTS.md was REPLACED (now $szA bytes, first16 0x$first16A; expected $OVER_BYTES zeros)"
fi

# CHECK 2 (A2): the run's stdout refusal detail line NAMES THE REAL FILE
# (AGENTS.md). Matched on the basename + the distinctive refusal text, so a
# /tmp symlink (macOS) cannot wedge the absolute-path form. RED on the unfixed
# head: today the line is "rules skipped: .jichi/rules.md denied, ...".
if grep -q 'rules skipped: .*AGENTS\.md denied, symlink, or unreadable' "$tmp/outA"; then
    t_ok "Leg A: the refusal detail line names the real file (AGENTS.md)"
else
    t_fail "Leg A: refusal detail line does not name AGENTS.md: $(head_bytes 200 "$tmp/outA")"
fi

# CHECK 3 (A3): the detail line does NOT contain the wrong name .jichi/rules.md.
# RED on the unfixed head (same root cause as check 2).
if ! grep -q '\.jichi/rules\.md' "$tmp/outA"; then
    t_ok "Leg A: the refusal detail line does not name .jichi/rules.md"
else
    t_fail "Leg A: refusal detail line still says .jichi/rules.md: $(grep 'rules skipped' "$tmp/outA")"
fi

# CHECK 4 (A4): the run exits 0 -- a refusal is not fatal (run_learn_apply returns
# 0 for JC_OK with the detail line; only the missing-draft NOTFOUND arm exits 1).
if [ "$rcA" -eq 0 ]; then
    t_ok "Leg A: a refusal is not fatal (rc=0)"
else
    t_fail "Leg A: the refusal run exited nonzero (rc=$rcA; out: $(head_bytes 200 "$tmp/outA"))"
fi

# CHECK 5 (A5): the M1100 stderr warn line (which already names the real path) is
# present. The over-cap file trips jc_read_existing's TOOBIG arm, not D-15, so
# "is present but could not be read" isolates the M1100 warn from the detail line.
if grep -q 'AGENTS\.md is present but could not be read' "$tmp/errA"; then
    t_ok "Leg A: the M1100 warn line names the real path on stderr"
else
    t_fail "Leg A: M1100 warn line absent: $(head_bytes 200 "$tmp/errA")"
fi

# --- LEG B: the append still works (SV's 36 -> 105 measurement) ---------------
# A small AGENTS.md gets the convention appended under "## Learned conventions".
wsB=$(smoke_tmp)
mkdir -p "$wsB/.jichi"
printf 'line1\nline2\n' > "$wsB/AGENTS.md"
szB0=$(wc -c < "$wsB/AGENTS.md" | tr -d ' ')
cat > "$wsB/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- always run make WERROR=1 test before pushing
EOF
( cd "$wsB" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/outB" 2> "$tmp/errB" ); rcB=$?
szB=$(wc -c < "$wsB/AGENTS.md" 2>/dev/null | tr -d ' ')
if [ "$rcB" -eq 0 ] && [ "$szB" -gt "$szB0" ] &&
   grep -q "always run make WERROR=1 test before pushing" "$wsB/AGENTS.md" &&
   grep -q "wrote 1 project rule(s) to the rules file" "$tmp/outB"; then
    t_ok "Leg B: a small AGENTS.md still gets the rule appended ($szB0 -> $szB bytes)"
else
    t_fail "Leg B: append regressed (rc=$rcB, size $szB0->$szB): $(head_bytes 200 "$tmp/outB")"
fi

# --- LEG C: the CLAUDE.md fallback (learn_rules_path, no AGENTS.md) -----------
# With no AGENTS.md and a small CLAUDE.md, the bullet lands in CLAUDE.md and the
# success detail (which names the file, M533) references CLAUDE.md.
wsC=$(smoke_tmp)
mkdir -p "$wsC/.jichi"
printf 'claude line1\n' > "$wsC/CLAUDE.md"
cat > "$wsC/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- prefer small commits
EOF
( cd "$wsC" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/outC" 2> "$tmp/errC" ); rcC=$?
if [ "$rcC" -eq 0 ] &&
   grep -q "prefer small commits" "$wsC/CLAUDE.md" &&
   ! grep -q "prefer small commits" "$wsC/AGENTS.md" 2>/dev/null &&
   grep -q "wrote 1 project rule(s) to the rules file" "$tmp/outC" &&
   ! grep -q "rules skipped" "$tmp/outC"; then
    t_ok "Leg C: the CLAUDE.md fallback appends there and the run is not a refusal"
else
    t_fail "Leg C: CLAUDE.md fallback regressed (rc=$rcC): $(head_bytes 200 "$tmp/outC")"
fi

t_done
