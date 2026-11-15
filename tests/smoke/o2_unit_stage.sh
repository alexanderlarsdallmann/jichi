#!/bin/sh
# smoke: the -O2 unit-stage driver -- serially null-compile EVERY TU of the host
# gate's `make WERROR=1 CC=gcc OPT=-O2 test` universe, so a fortified -O
# compile error can never reach the gate as a NO SUMMARY again (M1160-a,
# batch H9b gate-RED).
#
# THE DEFECT (run 0039, H9b). The gate's pre-gate sweep (sweep.sh:47) runs
# `make -j8 WERROR=1 CC=gcc OPT=-O2 test` in a FRESH clone -- a clean build at
# -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3. At e12d9876 it died on
#
#   tests/test_gateci.c:175:13: error: '__builtin___strncat_chk' output may be
#   truncated copying between 0 and 511 bytes from a string of length 16383
#   [-Werror=stringop-truncation]
#
# and the gate reported it as `unit o2 rc=2 NO SUMMARY (build failed or
# aborted)` -- a RED sweep on a READY batch. Four batches later. That strncat is
# fixed on main now (01f4d901, the bounded-append idiom), so the gate stage is
# green at this head. But NO container stage compiles at -O2: the default build
# passes no -O, the Makefile does not recompile on a flag change (a stale-.o run
# reads as green), and sprintf_lint's pattern (strcpy|strcat|gets) never named
# strncat. The fortified -O error reached the host and nowhere else saw it.
#
# WHAT THIS DRIVER DOES. Extract the stage's compile set with
# `make -n WERROR=1 CC=gcc OPT=-O2 test`, keep the lines matching
# ` -c <f>.c -o <o>.o`, and null-compile each one SERIALLY under the gate's
# EXACT expanded flags (re-pointed at `-o /dev/null`, `-MMD -MP` stripped). A
# fortified -O compile error -- stringop-truncation, -Werror=unused-result, the
# class the Makefile :1808-1815 comment names -- then fails HERE, in the
# container, as a named line, before it can become a NO SUMMARY on the host.
#
# SV:0638. (1) Where the class cannot fire (no gcc -O2 fortified; musl/BSD/
# old-gcc rows), check 2 is a SKIP with its reason -- not red, not ok. (2) The
# route is a SERIAL POSIX loop (strict POSIX sh: no bashisms, no xargs -P --
# smoke_lint enforces both); 353 TUs at ~38 s on this bench is why it sits in
# the 120 s wider tier, not the 60 s lint tier. (3) `include/jc_buildrev_stamp.h`
# is gitignored and must exist for the null-compile of src/util/jc_buildrev.c,
# so the driver pre-makes it (a no-op if already built).
#
# MISSING COMPILER -> PRESENCE, NOT REPORT TEXT (SV:0937; the M1160-a-c rework).
# Where the gate's pinned compiler is absent, checks 2, 3 and 4 each SKIP with
# their reason. The absence is detected ONCE, up front, by `command -v` on the
# pinned name (the first token of an extracted flag set): a POSIX EFFECT that
# is the same answer on every shell. NOT by the report's text -- a real sh
# with the compiler absent writes `<prog>: not found` and exits 127, while a
# silent stub writes nothing; the two reports differ by shell, so a skip keyed
# on an empty report (the M1160-a-b form) missed the real no-gcc world and
# checks 3/4 read not-ok. Presence is what the real sh and the silent stub
# agree on. The universe (check 1) stays ok (make plans the stage regardless
# of whether the compiler is present), and the serial null-compile is NOT run
# when there is no compiler. A PRESENT compiler that fails a real TU stays
# RED (the strncat-revert teeth): presence is about the compiler existing, not
# about a TU compiling.
. "$(dirname "$0")/_smoke.sh"

# Run from the repository root: the extracted lines carry RELATIVE paths
# (-Iinclude, include/jc_buildrev_stamp.h, src/..., tests/...) and the stamp
# target, so `sh -c` needs the root as its CWD. run.sh may invoke a driver from
# anywhere.
cd "$SMOKE_ROOT" || exit 1

_mk=$(smoke_make)

# ---- CHECK 1: the universe ---------------------------------------------------
# Extract the stage's compile set and require a FLOOR of 353 distinct lines
# (the count at this head). A drop means the universe is silently shrinking --
# the exact defect this driver exists to catch. If `make` cannot plan the stage
# (no GNU make, bmake) there is nothing to null-compile, so the driver SKIPS
# the remaining checks with a reason rather than failing on a toolchain gap.
#
# THE `-B` IS LOAD-BEARING (M1160-a). The host gate plans its stage in a FRESH
# clone, where every TU is missing. A plain `make -n` here instead plans against
# the working tree: any stale `.o` (from a `make test` / default build, or a
# half-cleaned tree) is UP-TO-DATE and its compile line is DROPPED, so the
# universe silently shrinks -- measured: 706 stale objects dropped it to 1. That
# is the inverse of the stale-.o hole this driver closes: TUs vanish from the
# extraction, the null-compile skips them, and the verdict reads green with the
# coverage missing. `-B` (always-make) forces the plan to list the full universe
# regardless of object freshness, exactly as the fresh-clone gate does.
#
# THE SKIP SHAPE (SV:0638.1, reasoned). Where the class cannot fire -- no GNU
# make to plan the stage (bmake cannot parse the Makefile), or an empty plan --
# there is nothing to null-compile, so the driver skips its remaining checks
# WITH THE REASON rather than red, and the plan stays 4 (the checks are named,
# the instrument says why it is inert here). smoke_lint check 20 forbids a
# t_skip that follows t_plan (it emits a SECOND TAP plan line the runner reads
# as malformed), so t_plan 4 comes first and the skip path then emits exactly
# four t_skip_one lines, which count toward the plan and keep t_done honest.
_universe=$( (cd "$SMOKE_ROOT" && "$_mk" -n -B WERROR=1 CC=gcc OPT=-O2 test 2>/dev/null) \
    | grep -E ' -c [^ ]+\.c -o [^ ]+\.o' | sort -u)
_n_universe=$(printf '%s\n' "$_universe" | grep -c .)
[ -n "$_n_universe" ] || _n_universe=0

if [ "$_n_universe" -eq 0 ]; then
    t_plan 4
    t_skip_one "no universe: make -n -B WERROR=1 CC=gcc OPT=-O2 test ($(_mk))
planned no compile lines here (no GNU make / bmake cannot parse the Makefile) -- the
-O2 unit-stage driver cannot extract its set on this platform; the host gate's
make WERROR=1 CC=gcc OPT=-O2 test (sweep.sh:47) is not witnessable here"
    t_skip_one "teeth (no universe line to extract the flag set from)"
    t_skip_one "the universe null-compile (nothing to compile)"
    t_skip_one "clean verdict (no compile to judge)"
    t_done
fi

FLOOR=353
if [ "$_n_universe" -lt "$FLOOR" ]; then
    t_plan 4
    t_fail "universe has only $_n_universe distinct -O2 compile lines, FLOOR $FLOOR --
extracted with make -n -B WERROR=1 CC=gcc OPT=-O2 test, the universe is
silently shrinking (the defect this driver exists for): the host gate's
make WERROR=1 CC=gcc OPT=-O2 test (sweep.sh:47) would build fewer TUs than at
this head. Re-measure the floor; do not lower it."
    t_skip_one "teeth (the universe did not extract; nothing to judge against)"
    t_skip_one "the universe null-compile (the universe did not extract)"
    t_skip_one "clean verdict (the universe did not extract)"
    t_done
fi

t_plan 4
# Count only, never the 353 lines: a t_ok that dumps them would flood the TAP
# stream (the runner's transcript) on every green run.
t_ok "universe: $_n_universe distinct TUs of the -O2 stage (>= FLOOR $FLOOR;
make -n -B WERROR=1 CC=gcc OPT=-O2 test)"

# ---- PRESENCE (SV:0937, the route the three compile checks key off) ----------
# Detect the gate's PINNED compiler ONCE, by PRESENCE, before any of the
# compile checks runs: take the first token of an extracted flag set (the
# compiler name -- `gcc` from make -n -B WERROR=1 CC=gcc OPT=-O2 test) and ask
# `command -v` for it. `command -v` is a POSIX EFFECT (exit status 0 iff a
# command by that name is on PATH), not a report: it does not read a compile's
# stderr, and it is the same answer on a real sh and on a silent stub. That is
# the fix for the defect this rework closes -- the M1160-a-b driver routed its
# absent-compiler skip by the REPORT's text (an empty report, or the shell's
# "not found" line), but a real sh with the compiler absent does NOT stay
# silent: it writes `<prog>: not found` to the report and exits 127, so the
# empty-report skip arm never fired and checks 3/4 read not-ok (SV:0937: "the
# skip was routed by the report's contents, but the report's contents are the
# shell's, and they differ by shell"). Presence is what a real sh and a silent
# stub agree on. The flag keys checks 2/3/4; the universe (check 1) stays ok
# (make plans the stage regardless of whether the compiler is present -- a
# missing compiler is a platform row, not a universe change).
_cc=$(printf '%s' "$(printf '%s\n' "$_universe" | head -n 1)" | sed 's/ -c .*//' | awk '{print $1}')
_cc_present=1
if [ -n "$_cc" ] && ! command -v "$_cc" >/dev/null 2>&1; then
    _cc_present=0
fi

# ---- CHECK 2: the flags fire (teeth) -----------------------------------------
# From ONE extracted line, take the tokens before ` -c ` (the gate's exact
# expanded O2 flags, compiler name included) and compile a C89 TU whose strncat
# is the shape the gate's -O2 fortified build rejects. If stringop-truncation
# fires, the instrument has teeth on this box; if it compiles clean, the class
# cannot fire here (SV:0638.1 -- skip, not red, not ok); if it fails on
# something else, the flags are wrong. WHERE THE COMPILER IS MISSING the check
# SKIPs with its reason rather than reading "flags are wrong" -- and (SV:0937)
# that is decided by the up-front PRESENCE flag `_cc_present`, not by the
# teeth's own report: a real sh with the compiler absent writes "not found"
# and exits 127, a silent stub writes nothing, and the two reports differ by
# shell -- presence is what they agree on. When the compiler is absent the
# teeth TU is never even compiled.
_one=$(printf '%s\n' "$_universe" | head -n 1)
# The flag set = the tokens before ` -c ` (includes the leading compiler). Strip
# the dependency flags, which with `-o /dev/null` would derive `dev/null.d` in
# the CWD and die (Permission denied) -- they are not part of the compile class.
_flags=$(printf '%s' "$_one" | sed 's/ -c .*//' | sed 's/ -MMD -MP//')

if [ "$_cc_present" -eq 0 ]; then
    # The pinned compiler is not present (SV:0937: presence, not report text).
    # The teeth are inert here by the up-front flag; the TU is not compiled.
    t_skip_one "teeth: the gate's pinned compiler ($_cc) is not present on this
platform/toolchain -- the stringop-truncation class cannot be measured here
(SV:0638.1; SV:0937: the absence is detected by command -v on the pinned name,
a POSIX effect, not by the report's text). The universe check (1) stays live;
the null-compile (3) and the clean verdict (4) skip on the same flag."
else
    tmp=$(smoke_tmp)
    printf '#include <string.h>\n' > "$tmp/teeth.c"
    printf 'char big[1024]; char small[128];\n' >> "$tmp/teeth.c"
    printf 'int main(void){ strncat(small, big, sizeof small - strlen(small) - 1); return small[0]; }\n' >> "$tmp/teeth.c"
    : > "$tmp/teeth.err"
    sh -c "$_flags -o /dev/null $tmp/teeth.c" > "$tmp/teeth.err" 2>&1
    _teeth_rc=$?
    if [ "$_teeth_rc" -eq 0 ]; then
        t_skip_one "teeth: stringop-truncation cannot fire on this platform/toolchain
(no gcc -O2 fortified; musl/BSD/old-gcc rows) -- the universe check (1) and the
null-compile (3) stay live, the teeth are inert here (SV:0638.1: skip, not
red, not ok)"
    elif grep -q 'stringop-truncation' "$tmp/teeth.err"; then
        t_ok "teeth: the gate's extracted -O2 flags fire stringop-truncation on a C89
strncat TU (the instrument has teeth on this box)"
    else
        _first_err=$(grep 'error' "$tmp/teeth.err" | head -n 1)
        t_fail "teeth: the extracted -O2 flags failed to compile the strncat TU on
something other than stringop-truncation -- the flags are wrong. First error:
$_first_err"
    fi
fi

# ---- CHECK 3: the serial null-compile RETURNED 0 over the whole universe ----
# Pre-make the stamp (gitignored; required by src/util/jc_buildrev.c), then
# re-point every extracted line at `-o /dev/null`, strip `-MMD -MP`, and null-
# compile SERIALLY (one `sh -c` per line, no `xargs -P`, no `jobs`). This check
# counts the RESULT, not the line: the number of compiles whose `sh -c` RETURNED
# 0 must EQUAL the universe (check 1). (SV:0837: the earlier form counted the
# re-pointed LINES -- which equal check 1's universe by construction -- so it
# was never a guard; a world where nothing compiled and nothing reported
# `error:` read GREEN. The result count is what actually proves the loop
# compiled the TUs.)
#
# WHERE THE COMPILER IS ABSENT (SV:0937) this check does not RUN AT ALL -- it
# SKIPs off the up-front PRESENCE flag `_cc_present`, not off the report. The
# M1160-a-b form keyed its skip on an EMPTY report: a silent stub gcc gives
# that, but a REAL sh with the compiler absent writes `<prog>: not found` to
# the report and exits 127, so the empty-report arm never fired and the
# result count (0 of 353) read not-ok. Running the loop when there is no
# compiler would only fill the report with 353 "not found" lines. A PRESENT
# compiler that fails a real TU (the stringop-truncation class, or any other
# error) still reads not-ok here -- that is the driver doing its job.
if [ "$_cc_present" -eq 0 ]; then
    t_skip_one "serial null-compile: the gate's pinned compiler ($_cc) is not
present on this platform/toolchain -- the stringop-truncation class cannot be
measured here (SV:0638.1; SV:0937: the absence is detected by command -v on
the pinned name, a POSIX effect, not by the report's text). The universe check
(1) stays live; nothing is null-compiled (there is no compiler)."
else
    ( cd "$SMOKE_ROOT" && "$_mk" include/jc_buildrev_stamp.h >/dev/null 2>&1 ) || true
    # Re-point: `-o <obj>.o` (the LAST flag on the line) -> `-o /dev/null`,
    # and drop the dependency flags (which with -o /dev/null would derive
    # dev/null.d in the CWD and die). `-c <f>.c` stays, so the TU is still
    # compiled from its source; the output is discarded.
    _cmds="$tmp/cmds"
    : > "$_cmds"
    printf '%s\n' "$_universe" \
        | sed -e 's/ -MMD -MP//' -e 's/ -o [^ ]*\.o$/ -o \/dev\/null/' > "$_cmds"
    : > "$tmp/o2err"
    # Count the compiles that RETURNED 0. The subshell's exit status is the
    # `sh -c` status; `&& _rc0=...` bumps only on 0. This check counts the
    # RESULT, not the line: a present compiler that fails a real TU leaves the
    # count below the universe and reads not-ok.
    _rc0=0
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        ( cd "$SMOKE_ROOT" && sh -c "$line" >> "$tmp/o2err" 2>&1 ) && _rc0=$((_rc0 + 1))
    done < "$_cmds"
    if [ "$_rc0" -eq "$_n_universe" ]; then
        t_ok "serial null-compile: $_rc0 of $_n_universe TUs returned 0 -- the result
count equals the universe (one sh -c each, no xargs -P)"
    else
        t_fail "serial null-compile: only $_rc0 of $_n_universe TUs returned 0 and the
compiles reported errors -- TUs the gate's make WERROR=1 CC=gcc OPT=-O2 test
(sweep.sh:47) would compile failed here. First 3 lines of the report verbatim:
$(head -n 3 "$tmp/o2err")"
    fi
fi

# ---- CHECK 4: clean verdict --------------------------------------------------
# Judge ONLY a full result: the verdict is clean only when EVERY line returned 0
# (check 3's result count equals the universe). Where some did not, the verdict
# reads the report -- first 5 error: lines (or the first 5 lines if a compile
# failed without one). WHERE THE COMPILER IS ABSENT (SV:0937) the verdict
# SKIPs off the same up-front PRESENCE flag `_cc_present`: a verdict over
# nothing is the NO SUMMARY this driver exists to remove, and the absence is
# decided by command -v on the pinned name, not by the (empty) report.
if [ "$_cc_present" -eq 0 ]; then
    t_skip_one "clean: verdict inert -- the null-compile (3) skipped, the gate's
pinned compiler ($_cc) is not present on this platform/toolchain
(SV:0638.1; SV:0937: presence, not report text)"
else
    if [ "$_rc0" -eq "$_n_universe" ]; then
        t_ok "clean: all $_n_universe TUs of the -O2 stage null-compile clean under the
gate's exact expanded flags (serial, -o /dev/null, -MMD -MP stripped)"
    elif [ -s "$tmp/o2err" ]; then
        _n_err=$(grep -c 'error:' "$tmp/o2err")
        [ -n "$_n_err" ] || _n_err=0
        _n_fail=$((_n_universe - _rc0))
        if [ "$_n_err" -gt 0 ]; then
            t_fail "clean: $_n_fail of $_n_universe TUs did not return 0 and the null-compile
reported errors -- the host gate's make WERROR=1 CC=gcc OPT=-O2 test (the
sweep's unit o2 line, sweep.sh:47) would die here. First 5 error: lines verbatim:
$(grep 'error:' "$tmp/o2err" | head -n 5)"
        else
            t_fail "clean: $_n_fail of $_n_universe TUs did not return 0 and the null-compile
reported a failure with no error: line -- the host gate's make WERROR=1
CC=gcc OPT=-O2 test (the sweep's unit o2 line, sweep.sh:47) would die here.
First 5 lines of the report verbatim:
$(head -n 5 "$tmp/o2err")"
        fi
    else
        # A present compiler that returned fewer than the universe but reported
        # nothing (e.g. every `sh -c` died before writing) is still RED, not a
        # skip: the result count is the guard, and it is short.
        t_fail "clean: $_n_universe TUs were to be judged and the null-compile
reported nothing -- the result count is short of the universe with an empty
report, which the verdict cannot read as clean."
    fi
fi

t_done
