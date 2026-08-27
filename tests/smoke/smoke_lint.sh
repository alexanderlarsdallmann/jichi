#!/bin/sh
# smoke: the rig lint for this tier -- the sh analog of tests/e2e/
# rig_lint.py ("prefer a lint to an audit", docs/TEST_INTEGRITY.md).
# Mechanically enforces the invariants every smoke driver must hold:
#
#   1. sources _smoke.sh (no ad-hoc rigs beside the shared lib)
#   2. has a reachable t_fail path (a driver that cannot fail is not a test)
#   3. no python3 / nc / curl / /dev/tcp (the tier's whole point, and no
#      ad-hoc network mocks beside mockmodel)
#   4. no bashisms: `local`, `[[` (strict POSIX sh, like tests/e2e/run.sh).
#      `[[` is matched only when NOT followed by ':' -- `[[:space:]]` is a
#      POSIX character class, not bash's test keyword, and forbidding it
#      rejected valid POSIX sh (this file's own line below uses one).
#   5. runs "$BIN", never a bare `jichi` (harness parity: test the binary
#      under test, not whatever is on PATH) -- pure lints (*_lint.sh)
#      exempt, they scan files instead of running the binary
#   6. one driver, one tier (M210, R3): no tests/e2e/<name>.py may exist
#      for a tests/smoke/<name>.sh -- duplicated coverage drifts, and
#      drift makes one copy a lie
#   7. machine-profile determinism (M272): auto-lite on a low-RAM machine
#      resolves toolProfile auto -> core, silently unadvertising every
#      non-core tool -- a scripted mock still calls it, the fence refuses
#      it, and the driver fails on THAT machine only. So a driver whose
#      mock scripts a non-core tool must pin the profile: use write_config
#      (pinned in _smoke.sh) or carry an explicit toolProfile in its own
#      config. Found the expensive way on the V2e 256 MB guest, one
#      driver per guest-run (ask, then websearch).
#   8. machine-profile determinism, the root (M272): the tool profile is
#      only one of lite's effects (subagent depth 0 broke subagent_itercap
#      the same way) -- so every inline driver config must pin
#      "lowResource": false outright; write_config carries the pin for
#      everyone else. Requires the M272 tri-state (explicit config false
#      vetoes auto-lite), which this tier forced into the product.
#   9. a config-less PTY driver must pass --no-lite: with no config to
#      carry the pin, auto-lite still fires on a small machine -- its
#      startup notice contains "] " and satisfies a prompt expect early
#      (sessions_footprint on the V2e guest), and the lean profile
#      reshapes the TUI under test.
#  10. every tests/tools/*.c with a --deadline scales it by the shared
#      tt_timeout_mult (M273). An unscaled deadline layer fails a HEALTHY
#      run on slow silicon and lies about the cause: mockmodel's own
#      watchdog exited mid-run on the V2f guest, the driver hung on a reply
#      that could never come, and raising the expect budget -- a different
#      layer -- changed nothing across three attempts.
#  10b. `with_deadline` -- the SHELL deadline layer -- scales too (M465).
#      It was the fourth layer and the largest: 210 call sites across 124
#      drivers, each a fixed bound, guarding exactly the slowest calls in
#      the tier. Checked FUNCTIONALLY (does the deadline actually get
#      longer?) rather than by grepping for the variable, and two-sided:
#      the same command must die at mult 1 and survive at mult 9, so the
#      check cannot pass by doing nothing.
#
# EXEMPT: _smoke.sh (the lib itself), run.sh (the runner), this file.
. "$(dirname "$0")/_smoke.sh"

t_plan 17

drivers=""
for f in "$SMOKE_DIR"/*.sh; do
    case "$(basename "$f")" in
        _smoke.sh|run.sh|smoke_lint.sh) continue ;;
    esac
    drivers="$drivers $f"
done

if [ -z "$drivers" ]; then
    t_fail "no drivers found to lint"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_done
fi

bad=""
for f in $drivers; do
    grep -q '_smoke\.sh"' "$f" || bad="$bad $(basename "$f")"
done
if [ -z "$bad" ]; then
    t_ok "every driver sources _smoke.sh"
else
    t_fail "not sourcing _smoke.sh:$bad"
fi

bad=""
for f in $drivers; do
    grep -q 't_fail' "$f" || bad="$bad $(basename "$f")"
done
if [ -z "$bad" ]; then
    t_ok "every driver can actually fail (has a t_fail path)"
else
    t_fail "no t_fail path:$bad"
fi

bad=""
for f in $drivers; do
    if grep -E -q 'python3|/dev/tcp' "$f" \
       || grep -E -q '(^|[^A-Za-z_#"])(nc|curl) ' "$f"; then
        bad="$bad $(basename "$f")"
    fi
done
if [ -z "$bad" ]; then
    t_ok "no python3/nc/curl//dev/tcp anywhere in the tier"
else
    t_fail "forbidden tool:$bad"
fi

# `local` is matched in command position only (line start / after ; or
# &&), so prose in a comment cannot trip it -- but a commented-out
# `# local x=1` still slips by; keep comments free of code.
bad=""
for f in $drivers; do
    if grep -E -q '(^|;|&&)[[:space:]]*local [A-Za-z_]' "$f" \
       || grep -E -q '\[\[([^:]|$)' "$f"; then
        bad="$bad $(basename "$f")"
    fi
done
if [ -z "$bad" ]; then
    t_ok "no bashisms (local, [[)"
else
    t_fail "bashism:$bad"
fi

bad=""
for f in $drivers; do
    case "$(basename "$f")" in
        # pure lints scan files, not the binary (docs_flags keeps its
        # e2e-era name rather than the *_lint.sh convention)
        *_lint.sh|docs_flags.sh) continue ;;
    esac
    grep -q '"\$BIN"' "$f" || bad="$bad $(basename "$f")"
    # M270: the ABSENCE side, which this check's own message has always
    # claimed. Presence of "$BIN" does not prove a driver never ALSO invokes a
    # PATH-resolved jichi -- one stray call would silently validate whatever
    # binary happens to be installed instead of the one just built (failure
    # mode 7 in docs/TEST_INTEGRITY.md, as an invariant rather than a habit).
    # Command position only, and comments stripped first: every current hit was
    # prose naming the client ("the shipped `jichi control` client"), never a
    # call. Backtick substitution is deliberately NOT a command-position marker
    # here -- this tier uses $( ) throughout, and accepting it made three
    # comments look like invocations. A longer word (.jichi, jichi-supervisor)
    # never matches.
    if sed 's/#.*//' "$f" \
        | grep -qE '(^|;|&&|\|\|?|\$\()[[:space:]]*(\./)?jichi(-convert)?([[:space:]]|$)'
    then
        bad="$bad $(basename "$f")(bare)"
    fi
done
if [ -z "$bad" ]; then
    t_ok 'every non-lint driver runs "$BIN" (never a PATH jichi)'
else
    t_fail 'not running "$BIN" / calls a bare jichi:'"$bad"
fi

# A `tool $var` directive (kinetic.sh, sound.sh) is conservatively treated
# as non-core: the variable may name one.
core=" read_file write_file edit_file apply_patch list_files search_code \
run_terminal_command load_skill "
bad=""
for f in $drivers; do
    _tools=$(grep '^[[:space:]]*tool ' "$f" | awk '{print $2}' | sort -u)
    [ -z "$_tools" ] && continue
    _need=0
    for _t in $_tools; do
        case "$core" in *" $_t "*) ;; *) _need=1 ;; esac
    done
    [ "$_need" -eq 0 ] && continue
    if ! grep -q 'toolProfile' "$f" && ! grep -q 'write_config' "$f"; then
        bad="$bad $(basename "$f")"
    fi
done
if [ -z "$bad" ]; then
    t_ok "every non-core scripted tool runs under a pinned tool profile"
else
    t_fail "non-core tool, unpinned profile (auto-lite will fence it):$bad"
fi

bad=""
for f in $drivers; do
    grep -q '"models"' "$f" || continue
    grep -q 'lowResource' "$f" || bad="$bad $(basename "$f")"
done
if [ -z "$bad" ]; then
    t_ok "every inline driver config pins lowResource false"
else
    t_fail "inline config without a lowResource pin (auto-lite will reshape it):$bad"
fi

bad=""
for f in $drivers; do
    grep -q 'ptydrive' "$f" || continue
    if ! grep -q -- '--config' "$f" && ! grep -q -- '--no-lite' "$f"; then
        bad="$bad $(basename "$f")"
    fi
done
if [ -z "$bad" ]; then
    t_ok "every config-less PTY driver pins --no-lite"
else
    t_fail "config-less PTY driver without --no-lite (the auto-lite notice will break its expects):$bad"
fi

bad=""
_ntools=0
for f in "$SMOKE_ROOT"/tests/tools/*.c; do
    grep -q -- '--deadline' "$f" || continue
    _ntools=$((_ntools + 1))
    grep -q 'tt_timeout_mult' "$f" || bad="$bad $(basename "$f")"
done
if [ "$_ntools" -eq 0 ]; then
    t_fail "no tool with a --deadline found -- the scan is broken, not clean"
elif [ -z "$bad" ]; then
    t_ok "all $_ntools tools with a --deadline scale it by tt_timeout_mult"
else
    t_fail "unscaled deadline (slow silicon will fail a healthy run):$bad"
fi

# --- 10b (M465): the FOURTH deadline layer -- the shell one -- also scales ----
# tt_mult.c states the invariant: "Every deadline in this tier must scale by the
# same knob, or the knob is a lie", and records three layers found one at a time
# (M220 run.sh's outer limit; M272 ptydrive's expect/waitexit; M273 mockmodel's
# self-watchdog, found "the hard way" because the layer that died was not the one
# being raised). Check 10 above enforces it for tests/tools/*.c. `with_deadline`
# in _smoke.sh was the fourth and was NOT covered: 210 call sites across 124
# drivers, every one a fixed wall-clock bound, guarding exactly the slowest calls
# in the tier (its own comment: "this guards direct jichi runs").
#
# Tested FUNCTIONALLY rather than by grepping for the variable name, because what
# matters is that the deadline actually gets longer. Two-sided: the same command
# must fail at mult 1 and pass at a mult that covers it. ~3s.
# `export` in a subshell rather than an assignment prefix: that prefix form is
# what check 12 below forbids, and a lint that exempts itself is a lint nobody
# believes. (It would in fact work here -- with_deadline reads the value as a shell
# variable, not from a child's environment -- but "it happens to be safe" is exactly
# the reasoning check 12 exists to remove.)
_wd_short=$( export JC_SMOKE_TIMEOUT_MULT=1; with_deadline 1 sleep 3 >/dev/null 2>&1; echo $? )
_wd_long=$(  export JC_SMOKE_TIMEOUT_MULT=9; with_deadline 1 sleep 3 >/dev/null 2>&1; echo $? )
if [ "$_wd_short" -eq 0 ]; then
    t_fail "with_deadline 1 did not stop a 3s command at mult 1 -- this check cannot fail, so it proves nothing"
elif [ "$_wd_long" -eq 0 ]; then
    t_ok "with_deadline scales by JC_SMOKE_TIMEOUT_MULT (rc $_wd_short at mult 1, 0 at mult 9)"
else
    t_fail "with_deadline does NOT scale: a 3s command still dies under 'with_deadline 1' at mult 9 (rc $_wd_long). 210 fixed deadlines across 124 drivers will fail healthy runs on slow targets -- the knob is a lie for the shell layer"
fi

bad=""
for f in $drivers "$SMOKE_DIR/smoke_lint.sh"; do
    _n=$(basename "$f" .sh)
    [ -f "$SMOKE_ROOT/tests/e2e/$_n.py" ] && bad="$bad $_n"
done
if [ -z "$bad" ]; then
    t_ok "one driver, one tier: no name exists in both smoke and e2e"
else
    t_fail "in BOTH tiers (delete the Python original):$bad"
fi

# NO ORPHANS: every driver on disk must be named in run.sh. rig_lint.py has had
# this check for the e2e tier since M213, when a port dropped a driver from the
# list without deleting the file and silently ended its coverage. The smoke tier
# never got the equivalent -- and had one: faults_net_midstream.sh, added in the
# M269/M272 wave beside faults_net.sh, was never listed, so it had never run
# (found in M297 while MEASURING the driver count for PROJECT_TIMELINE: `ls` said
# 104, `make smoke` said 103). A file that looks like a test is the worst kind of
# missing coverage, because it reads as present.
bad=""
_norph=0
for f in $drivers; do
    _n=$(basename "$f" .sh)
    _norph=$((_norph + 1))
    # Whitespace before, a non-word character (or end of line) after: the name
    # may be followed by ' ', '\' or ';' in the for-list. The trailing
    # non-word-char requirement is what keeps `faults_net` from matching inside
    # `faults_net_midstream` -- which is the very pair this check was added for.
    grep -qE "[[:space:]]$_n([^A-Za-z0-9_]|\$)" "$SMOKE_DIR/run.sh" \
        || bad="$bad $_n"
done
if [ "$_norph" -eq 0 ]; then
    t_fail "no drivers enumerated -- the orphan scan is broken, not clean"
elif [ -z "$bad" ]; then
    t_ok "no orphans: all $_norph drivers are named in run.sh"
else
    t_fail "on disk but never run (add to run.sh, or delete the file):$bad"
fi

# --- 12 (M465): no assignment PREFIXED to with_deadline -- it misses the child --
# THE DEFECT THIS EXISTS FOR, and it is invisible on every shell this project is
# developed on. A variable assignment prefixing a SHELL FUNCTION is visible inside
# that function as a shell variable everywhere, but whether it is EXPORTED to the
# processes that function runs is shell-dependent. Measured 2026-08-17:
#
#   dash (Linux /bin/sh)  FOO=bar f  ->  in the child's env: YES
#   bash                  FOO=bar f  ->  in the child's env: YES
#   FreeBSD /bin/sh       FOO=bar f  ->  in the child's env: NO
#
# with_deadline ALWAYS runs a child process, so the prefix form silently loses the
# variable on FreeBSD. That is the whole of the `setup_keyfile` check 6 mystery
# which stood open since M460: prefixing JICHI="$BIN" to the with_deadline call left
# JICHI unset in run.sh, whose `${JICHI:-jichi}` fell back to a bare `jichi` that is
# not on PATH -- so $out was the 22 bytes "exec: jichi: not found", contained no
# "api key" line, and the driver reported "the key did not reach jichi" with an
# empty tail. Every element passed by hand because a prefix on an EXTERNAL command
# does export correctly.
#
# Five more sites had the same shape and were worse than a failure: two lost HOME
# isolation (so they would run against the real $HOME), one lost a fault-injection
# variable (so the fault never fired and the driver asserted nothing), and two lost
# TERM/LC_ALL. All would have PASSED on FreeBSD while testing something else.
#
# The fix is `with_deadline N env NAME=value cmd ...`: env is POSIX and puts the
# variable in the child's environment unambiguously. Reading a value that
# with_deadline itself consumes (the timeout multiplier) is legitimate, but use an
# explicit `export` in a subshell -- this file does -- so the rule stays total and
# needs no exception list.
#
# The value class excludes ; & | deliberately: `NAME=1; with_deadline ...` is a
# SEQUENCE, not a prefix, and the two mean different things. Without that exclusion
# this check's own two calls matched it -- a false positive found by running it.
bad=""
for f in $drivers "$SMOKE_DIR/_smoke.sh" "$SMOKE_DIR/smoke_lint.sh"; do
    [ -f "$f" ] || continue
    if sed 's/#.*//' "$f" \
        | grep -qE '(^|[[:space:]&(])[A-Za-z_][A-Za-z0-9_]*=[^[:space:];&|]*[[:space:]]+with_deadline([[:space:]]|$)'
    then
        bad="$bad $(basename "$f")"
    fi
done
if [ -z "$bad" ]; then
    t_ok "no assignment prefixed to with_deadline (FreeBSD's sh would not export it)"
else
    t_fail "an assignment prefixed to with_deadline -- FreeBSD's /bin/sh does NOT export it to the child, so the variable is silently lost (write it as: with_deadline N env NAME=value cmd):$bad"
fi

# ---- 14: every driver dispatch honours keep-going ---------------------------
# run.sh has TWO driver loops plus a named-subset path, and JC_SMOKE_KEEP_GOING
# (M466) only works if all of them route failure through driver_failed. A third
# loop written as `run_driver "$t" 60 || exit 1` would silently opt its drivers
# out of keep-going, and the symptom would be a remote platform row that stops
# early again -- the exact thing this replaced, and invisible from a green tier.
#
# Floor first: if the dispatch shape moves, this must fail loudly rather than
# count zero calls and pass. (The slash_commands_lint lesson, M295.)
_rs="$SMOKE_DIR/run.sh"
_calls=$(grep -c 'run_driver "\$t"' "$_rs" 2>/dev/null || echo 0)
_routed=$(grep -c 'run_driver "\$t" [0-9]* || driver_failed "\$t"' "$_rs" 2>/dev/null || echo 0)
if [ "$_calls" -lt 3 ]; then
    t_fail "run.sh has $_calls run_driver dispatch(es), expected at least 3 -- the dispatch shape moved and this check is measuring nothing"
elif [ "$_calls" -eq "$_routed" ]; then
    t_ok "all $_calls run_driver dispatches route failure through driver_failed (keep-going works)"
else
    t_fail "$((_calls - _routed)) of $_calls run_driver dispatch(es) bypass driver_failed -- those drivers silently ignore JC_SMOKE_KEEP_GOING:
$(grep -n 'run_driver "\$t"' "$_rs" | grep -v 'driver_failed' | head -n 4)"
fi

# ---- 15: a backgrounded subshell whose $! is captured must exec ------------
# MEASURED, both shells, same script (M467):
#
#   ( cd /tmp && sleep 20 ) &  p=$!   ->  dash: $! is `sleep`   (implicit exec)
#                                        bash: $! is `sleep`   (implicit exec)
#                                        OpenBSD ksh: $! is `sh` -- THE SUBSHELL
#
# So on ksh the captured pid is a shell, and the command under test is its
# GRANDCHILD. Every `kill`, `kill -INT` and `wait` on that pid then hits the
# wrong process. parallel_abort SIGINTs what it believes is jichi and reports
# "parent did not exit within 15s of SIGINT -- abort/reaping deadlocked", which
# accuses the agent of a defect the harness caused; stop_reason_capped leaves a
# daemon running, and a timeout(1) that waits for the process group then fails a
# driver whose every check passed.
#
# `exec` makes the subshell BECOME the command, so $! is the command on every
# shell. signals.sh has done this since it was written -- its whole subject is
# signal delivery, so it could not have worked otherwise -- and the idiom was
# never generalised to the five other sites. This check generalises it.
#
# Scope: only backgrounded subshells (`) &`) -- a synchronous `(cd X && cmd)`
# needs nothing, and there are a dozen of those. A `$!` capture is not required
# for the finding: if a subshell is backgrounded at all, something later waits
# for or signals it.
_bg_bad=$(for f in $drivers; do
    [ -f "$f" ] || continue
    awk -v F="$f" '
      /^\(/ { inb = 1; blk = ""; }
      inb   { blk = blk $0 " "; }
      inb && /\) *&[[:space:]]*$/ {
          if (blk !~ /exec /) print F ":" NR;
          inb = 0; blk = "";
      }
      inb && /\)[[:space:]]*$/ { inb = 0; blk = ""; }
    ' "$f"
done)
# Emptiness is tested directly, NOT via `grep -c . || echo 0`: on an empty input
# that emits "0" AND exits 1, so the `||` appends a second "0" and the numeric
# test then sees "0 0". That exact construct picked the wrong branch twice in one
# day (docs/analysis/2026-08-17-instruments-not-systems.md).
if [ -z "$_bg_bad" ]; then
    t_ok "every backgrounded subshell execs, so \$! is the command and not a shell"
else
    _bg_n=$(printf '%s\n' "$_bg_bad" | grep -c .)
    t_fail "$_bg_n backgrounded subshell(s) without exec -- on OpenBSD ksh \$! is the SUBSHELL, so kill/wait hits the wrong process:
$(printf '%s\n' "$_bg_bad" | head -n 6)
write it as ( cd DIR && exec CMD ... ) &"
fi

# ---- 16: every FAULT-gated driver is named in the Makefile's smoke-faults ----
# WHY (M482). A driver that skips unless a special build exists is invisible
# coverage: `faults`, `faults_net` and `faults_net_midstream` each declined on
# EVERY platform and in every run of this tier, including the development box,
# because no stage of `make ci` ever built FAULT=1. They ran only when somebody
# typed it by hand. When they were finally run they found a real product defect
# and three of their own checks vacuous -- so the cost of that invisibility was
# not hypothetical.
#
# This is the orphan check (above) one level up: that one asserts every driver is
# named in run.sh, and these WERE. Being listed and being reachable are different
# properties when a driver gates itself on a build flag.
_fault_drivers=""
for f in $drivers; do
    if grep -q 'needs a FAULT=1 binary' "$f" 2>/dev/null; then
        _fault_drivers="$_fault_drivers $(basename "$f")"
    fi
done
# $SMOKE_DIR is what this driver has (there is no $ROOT here); the Makefile is
# two levels up from it. Naming the wrong file made this check report every
# driver missing from a Makefile it had never opened -- a false RED, which is
# the safe direction, and it was obvious on the first run.
_mk="$SMOKE_DIR/../../Makefile"
_missing=""
_nfd=0
for d in $_fault_drivers; do
    _nfd=$((_nfd + 1))
    grep -q "tests/smoke/$d" "$_mk" 2>/dev/null || _missing="$_missing $d"
done
if [ "$_nfd" -eq 0 ]; then
    t_fail "no driver requires a FAULT=1 binary -- either the skip wording moved \
(so this check is scanning for something it cannot find) or the tier was deleted"
elif [ -n "$_missing" ]; then
    t_fail "$_nfd FAULT-gated driver(s), but the Makefile names none of:$_missing \
-- they will SKIP in every build and nothing will say so (M482). Add them to the \
smoke-faults target."
else
    t_ok "all $_nfd FAULT-gated drivers are named in the Makefile gate"
fi

# ---- 17: `make` with no target must build the BINARIES -----------------------
# WHY (M495). make takes the FIRST target in the makefile as its default goal, so
# adding a rule near the top silently redefined what `make` does: for one commit
# `make` built a generated header and nothing else -- no binary, exit 0, no error.
# `make clean && make` then left no binary at all, and the only reason it surfaced
# was the operator asking for a clean install.
#
# A TEXT check, not `make -p`: this driver runs on BSDs where /bin/make is BSD make,
# and resolving GNU make just to ask about a default goal is more machinery than the
# invariant needs. The invariant is that the makefile SAYS which goal is default,
# rather than leaving it to the order rules happen to be written in.
_mk="$SMOKE_DIR/../../Makefile"
if grep -q '^\.DEFAULT_GOAL[[:space:]]*:=[[:space:]]*all' "$_mk" 2>/dev/null; then
    t_ok "the Makefile pins .DEFAULT_GOAL, so rule order cannot redefine \`make\`"
else
    t_fail "no '.DEFAULT_GOAL := all' in the Makefile -- the default goal is then \
whichever target happens to come first, and adding a rule above \`all\` makes bare \
\`make\` build something else while exiting 0 (M495)"
fi

t_done
