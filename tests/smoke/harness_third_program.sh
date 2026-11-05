#!/bin/sh
# smoke: `jichi-harness`, jichi's third C89 program, exists in the build and
# does ONLY what the H0 design note (notes/M1049.md) allows: it prints
# --version / -V / --help, and it refuses every not-yet-built command.
#
# THE RED THIS IS HELD BY. On the tree before the change the file
# `jichi-harness` does not exist in the tree: `make` builds two programs, the
# target is not wired, nothing is installed. Check 1 below fails at its first
# line -- the binary is absent -- and the red says "the third program is not
# built", which is the whole point of the driver.
#
# WHAT IS CHECKED:
#
#   1. `--version` exits 0, prints `jichi-harness ` plus the version read from
#      include/jc_version.h (not typed here), and -- in a git checkout -- a
#      `build: ` stamp line (M495: which build, not just which version).
#   2. `-V` says exactly what `--version` says (one print, two flags -- the
#      converter's shape, jc_convert_main.c).
#   3. `--help` exits 0, names the program, and lists the five commands the
#      plan names (init, doctor, mirror, run, drive): all five are marked
#      BUILT with their milestones (doctor M1052, init M1053, mirror
#      M1055/M1056, run M1057/M1058, drive M1059/H5). The run and drive
#      marks are PINNED on the command's own line: the `run` entry carries
#      "BUILT, M1057/M1058" and the `drive` entry "BUILT, M1059", and
#      neither entry's line reads "not built yet" (M1080: without these
#      greps, M1059-b's help marks could be reverted -- run/drive back to
#      "(not built yet)") with no check going red). The string "not built
#      yet" must still appear in --help, because the BARE forms of
#      init/mirror/run/drive keep their H0 not-built refusal -- held by
#      checks 4-7 -- and the usage's generic sentence names it.
#   4-7. each of the FOUR commands run BARE (init, mirror, run, drive)
#        refuses: exit 2, `not built yet` on stderr, the command NAMED, and
#        NOTHING on stdout. init is BUILT (M1053) but a BARE `init` (no mode)
#        still keeps the H0 not-built refusal; mirror, run, drive are not
#        built yet (behaviour 5: a command that does nothing and exits 0 is
#        the class this drive exists to stop).
#   8. an UNKNOWN command is refused as a usage error -- exit 2, `unknown
#        command`, and NOT `not built yet` (the check's ok line quotes the
#        input it ran).
#   9. an UNKNOWN OPTION is refused as a usage error: exit 2, `unknown option
#        '--bogus'` on stderr, and NOT `not built yet` (behaviour 6; usage
#        still goes to stdout, measured -- so stdout is not asserted empty).
#  10. `make install DESTDIR=...` lands all THREE programs (jichi,
#        jichi-convert, jichi-harness) under DESTDIR -- the count the note
#        asks for: two before, three after.
#
# It runs the tree's binary by path (the $HBIN below), never a PATH-resolved
# `jichi-harness`, and -- for smoke_lint check 5 -- also runs the tree's
# `"$BIN"` once: both programs are built from the same tree by this tier, so
# their `--version` first lines (version + build stamp) must agree. A tree
# whose third program came from another build fails here.
. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"
# No driver-wide skip on a missing jichi-harness: the checks that need it (1-9)
# are the red this driver is held by. On the tree before the change the file
# `jichi-harness` does not exist and check 1 fails -- the whole point. The
# tier builds it now (`smoke: $(BIN) $(HARNESS_BIN)`, behaviour 13), so a green
# tier reaches check 10 with it present. Only check 10 (the INSTALL check) needs
# a precondition, the way install_no_build.sh:60 needs jichi-convert: install
# copies all three programs, so it is untestable where jichi-convert was never
# built (a bare `make smoke` builds only jichi and jichi-harness, not the
# converter). That one check skips rather than fails for the missing file.
t_plan 10
tmp=$(smoke_tmp)

# The version, read from the header that is the single source of truth --
# never typed, so a driver that drifts from the header cannot pass.
JH_VER=$(sed -n 's/^#define JC_VERSION "\(.*\)"$/\1/p' "$ROOT/include/jc_version.h")

# --- 1: --version exits 0, names the program and the version, stamps the build
# (smoke_lint check 5: this driver also runs the tree's "$BIN" once -- the
# meaningful assertion is that BOTH programs this tier built from the same tree
# carry the SAME build stamp; a third program from a different build fails here.)
"$HBIN" --version > "$tmp/v" 2> "$tmp/verr"; rc=$?
"$BIN" --version > "$tmp/binv" 2>/dev/null
_head1=$(sed -n '1p' "$tmp/v")
if [ "$rc" -ne 0 ] || [ "$_head1" != "jichi-harness $JH_VER" ]; then
    t_fail "--version: rc=$rc, first line '$_head1' -- expected 'jichi-harness \
$JH_VER' (the version read from include/jc_version.h)"
else
    _jstamp=$(grep '^build: ' "$tmp/binv" 2>/dev/null | sed -n '1p')
    _hstamp=$(grep '^build: ' "$tmp/v" 2>/dev/null | sed -n '1p')
    if [ "$(git rev-parse --short HEAD 2>/dev/null || true)" ] && [ -z "$_hstamp" ]; then
        t_fail "--version prints '$_head1' but no 'build: ' stamp, in a git \
checkout -- a 50-milestone-stale install cannot then be told from a fresh one (M495)"
    elif [ "$_jstamp" != "$_hstamp" ]; then
        t_fail "--version's build stamp differs from the tree's jichi \
(harness='${_hstamp:-none}' vs jichi='${_jstamp:-none}') -- both are built from \
this tree, so the stamps must agree"
    else
        t_ok "--version exits 0: $_head1 (build stamp agrees with the tree's jichi: ${_hstamp:-tarball, no stamp})"
    fi
fi

# --- 2: -V is the same print ------------------------------------------------
# Require -V itself to have run (rc=0): on a missing binary both captures are
# empty files and cmp would agree for the wrong reason.
"$HBIN" -V > "$tmp/vV" 2>/dev/null; rcv=$?
if [ "$rcv" -eq 0 ] && cmp -s "$tmp/v" "$tmp/vV"; then
    t_ok "-V prints exactly what --version prints (one print, two flags)"
else
    t_fail "-V differs from --version -- two flags, two answers (rc=$rcv):
$(diff "$tmp/v" "$tmp/vV" 2>/dev/null | head_bytes 200)"
fi

# --- 3: --help exits 0, names the program and the five commands --------------
"$HBIN" --help > "$tmp/h" 2> "$tmp/herr"; rc=$?
miss=""
for want in jichi-harness init doctor mirror run drive "not built yet"; do
    grep -qF -- "$want" "$tmp/h" || miss="$miss '$want'"
done
# The BUILT marks, pinned on the entry's own line (M1080): the run entry's
# mark line carries "(BUILT, M1057/M1058)" and the drive entry's "(BUILT,
# M1059)", and the OLD paren form "(not built yet)" -- the run/drive entry
# text before M1059-b -- is gone from the help. The quoted bare-form refusal
# text ("keeps the 'not built yet' refusal") stays, and the generic usage
# sentence still names "not built yet" (held above and in checks 4-7); a
# tree that reverts the marks (the entries read "(not built yet)" again)
# passes all of that and fails HERE -- the gap this check closes.
mark=""
grep -qF "(BUILT, M1057/M1058): one lead run in Mode A" "$tmp/h" \
    || mark="$mark run-mark"
grep -qF "push (BUILT, M1059): the C89 port of the drive's" "$tmp/h" \
    || mark="$mark drive-mark"
grep -qF "(not built yet)" "$tmp/h" && mark="$mark old-paren-form"
if [ "$rc" -ne 0 ] || [ -n "$miss" ] || [ -n "$mark" ]; then
    t_fail "--help: rc=$rc, missing:$miss, unpinned:$mark -- help must name \
the program and every command the plan lists, with the built ones marked \
BUILT on their entry's mark line (run: M1057/M1058, drive: M1059) and the \
old '(not built yet)' entry form gone; 'not built yet' remains only in the \
generic sentence and the bare-form refusal text (held by checks 4-7):
$(head_bytes 300 "$tmp/h")"
else
    t_ok "--help exits 0, names the five commands (all built, marked with their milestones), pins the run/drive BUILT marks (M1057/M1058, M1059) on their entry lines and still carries the 'not built yet' refusal text (the bare forms' refusal, held by checks 4-7)"
fi

# --- 4-7: the four not-built commands refuse, exit 2, name themselves --------
# doctor was BUILT by M1052 (H1b) -- its built behaviour (exit 0 / 1 with a
# report, never the exit-2 not-built class) is pinned by
# tests/smoke/harness_doctor.sh, so the loop covers the other four.
for cmd in init mirror run drive; do
    "$HBIN" "$cmd" > "$tmp/out.$cmd" 2> "$tmp/err.$cmd"; rc=$?
    if [ "$rc" -ne 2 ] || [ -s "$tmp/out.$cmd" ] || \
       ! grep -qF -- "$cmd" "$tmp/err.$cmd" || \
       ! grep -qF "not built yet" "$tmp/err.$cmd"; then
        t_fail "\`jichi-harness $cmd\`: rc=$rc (expected 2), stdout \
$([ -s "$tmp/out.$cmd" ] && echo NONEMPTY || echo empty), stderr
$(head_bytes 160 "$tmp/err.$cmd")
a refusal must exit 2, name the command and say 'not built yet' on stderr, and
print nothing on stdout -- a command that does nothing and exits 0 is the
class this drive exists to stop (never a silent success)"
    else
        t_ok "\`jichi-harness $cmd\` refuses: exit 2, '$(cat "$tmp/err.$cmd")'"
    fi
done

# --- 8: an UNKNOWN command is a usage error, not a not-built refusal ---------
# The refusal must name the input (behaviour 6), so the assertion is the quoted
# phrase, not the generic one: a program that stopped echoing the input would
# fail here.
"$HBIN" nosuchcommand > "$tmp/uout" 2> "$tmp/uerr"; rc=$?
if [ "$rc" -ne 2 ] || ! grep -q "unknown command 'nosuchcommand'" "$tmp/uerr" || \
   grep -q "not built yet" "$tmp/uerr"; then
    t_fail "\`jichi-harness nosuchcommand\`: rc=$rc, stderr
$(head_bytes 160 "$tmp/uerr")
an unknown command must be refused as a USAGE error ('unknown command'), not
swallowed as a not-built command ('not built yet') -- the two refusal classes
must stay tellable apart"
else
    t_ok "\`jichi-harness nosuchcommand\` is refused as a usage error, not 'not built yet'"
fi

# --- 9: an UNKNOWN OPTION is a usage error, not a not-built refusal ----------
# Behaviour 6 names both paths (an unknown command AND an unknown option are
# refused, exit 2, naming the input); check 8 above holds the command path.
# Note the stdout asymmetry, measured on this head: the usage-error prints the
# full usage on STDOUT (the not-built refusals of checks 4-7 print nothing
# there), so this check does NOT assert $tmp/oout is empty.
"$HBIN" --bogus > "$tmp/oout" 2> "$tmp/oerr"; rc=$?
if [ "$rc" -ne 2 ] || ! grep -q "unknown option '--bogus'" "$tmp/oerr" || \
   grep -q "not built yet" "$tmp/oerr"; then
    t_fail "\`jichi-harness --bogus\`: rc=$rc (expected 2), stderr
$(head_bytes 160 "$tmp/oerr")
an unknown option must be refused as a USAGE error naming it, not swallowed
as a not-built command -- the two refusal classes must stay tellable apart
(behaviour 6). The usage text still goes to stdout, measured; that is not
asserted here."
else
    t_ok "\`jichi-harness --bogus\` is refused as a usage error: exit 2, 'unknown option '--bogus'' on stderr, not 'not built yet'"
fi

# --- 10: make install lands all THREE programs under DESTDIR -----------------
# PRECONDITION (the note's executable-check skip): install copies all three
# programs, so it cannot succeed where jichi-convert was never built. A bare
# `make smoke` builds only $(BIN) and $(HARNESS_BIN), not the converter -- so
# on such a tree this one check skips rather than fails for a missing file,
# the way install_no_build.sh:60 does.
if [ ! -x "$ROOT/jichi-convert" ]; then
    t_skip_one "jichi-convert is not built, and \`make install\` copies all three \
programs -- this check 10 would fail for a missing file. Run \`make all\`."
else
_mk=$(smoke_make)
printf '#define JC_BUILD_REV "deadbee"\n' > "$tmp/clean_stamp.h"
iout=$(cd "$ROOT" && "$_mk" install DESTDIR="$tmp/dest" STAMP="$tmp/clean_stamp.h" 2>&1); irc=$?
if [ "$irc" -ne 0 ]; then
    t_fail "make install DESTDIR failed (rc=$irc):
$(head_bytes 240 "$iout")"
else
    nbin=0
    for want in jichi jichi-convert jichi-harness; do
        [ -x "$tmp/dest/usr/local/bin/$want" ] || nbin=$((nbin + 1))
    done
    if [ "$nbin" -ne 0 ]; then
        t_fail "install ran but $nbin of the three binaries are not under \
DESTDIR/usr/local/bin -- the install that succeeded and is wrong (M661). Tree:
$(find "$tmp/dest" -type f 2>/dev/null | sed -n 's|^.*bin/||p' | tr '\n' ' ' | head_bytes 160)"
    else
        t_ok "make install lands all three programs (jichi, jichi-convert, jichi-harness) under DESTDIR"
    fi
fi
fi

t_done
