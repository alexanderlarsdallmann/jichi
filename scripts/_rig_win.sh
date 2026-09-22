#!/bin/sh
# _rig_win.sh -- the steps shared by the two Windows-family emulation rows,
# Cygwin and MSYS2/MSYS (M698).
#
# WHY THIS SHAPE, AND WHY NOT THE tier-v-*.sh SHAPE. Every other tier-v rig boots
# a guest and drives it over ssh. These two rows are not guests: they are POSIX
# emulation layers installed on the machine running the rig, so there is nothing
# to boot and nothing to reach. What is left of a rig is the part that matters
# here anyway -- run the same steps, in the same order, recording the same
# evidence, so that a row somebody measured by hand can be measured again by
# anybody.
#
# WHY IT EXISTS ONLY NOW. docs/DEFERRED.md has carried the Guix rule since May:
# a rig written blind for a platform nobody can run is a never-executed artifact.
# Every step below was performed by hand first -- Cygwin at M696, MSYS2 at M697 --
# and this is the transcript of two rows that ran, not a guess at them.
#
# WHAT THESE ROWS UNIQUELY EXERCISE, and no Linux or BSD row can:
#   * POSIX emulated over Win32, twice, by two independently maintained DLLs --
#     so an agreement between them is evidence about emulation in general, and a
#     divergence isolates Cygwin-version behaviour from emulation behaviour;
#   * a filesystem that may accept chmod and do NOTHING (MSYS2's noacl mount),
#     which is the only place jichi's file-privacy guarantees are known to fail;
#   * a fork penalty two orders of magnitude wide WITHIN one tier -- 1.5x for a
#     wall-clock-bound driver, ~300x for one that spawns a process per file.
#
# Requires ok/bad/note/gl from the calling rig, and jc_rig_mult / jc_rig_ref_or_die
# from _rig_mult.sh.

# jcw_guard_other_layer LAYER OTHER_MARKER -- refuse while the other layer is live.
#
# Cygwin and MSYS2 must not run at the same time: their cygwin1.dll and
# msys-2.0.dll shared-memory regions collide (measured 60344 vs 59320), fork
# begins failing in BOTH, and a pacman lock can be left stranded. A rig that
# starts anyway produces a row full of fork failures that look like platform
# defects.
# THE PROBE MUST NOT CAUSE THE CONDITION IT TESTS FOR, AND MUST NAME THE RIGHT
# INSTALLATION. Both halves were measured on 2026-09-22, each after the previous
# version of this guard was written and tried.
#
#   1. The first version launched the OTHER layer's bash and counted what `ps`
#      reported there. It STARTED THE OTHER LAYER IN ORDER TO ASK WHETHER THE
#      OTHER LAYER WAS RUNNING -- the hazard is two emulation DLLs mapped at
#      once, and the guard mapped the second one to find out. A check that
#      creates the state it reports is worse than no check, because it then
#      reports it truly. (It was also miscalibrated: an idle layer answers 4,
#      not 0 -- the `ps -ef` header, the login shell, `ps`, and the `grep`.)
#
#   2. The second version asked Windows `tasklist /m msys-2.0.dll`, which does
#      not start anything and looked right -- and refused every run on this
#      machine. GIT FOR WINDOWS IS BUILT ON THE MSYS2 RUNTIME: every Git Bash
#      shell maps msys-2.0.dll from `C:\Program Files\Git`. That is a THIRD
#      cygwin-family installation, separate from C:\msys64, and the collision
#      this guard exists to prevent was measured between the two *installations*
#      -- their shared-memory regions are named per installation root. A guard
#      keyed on the DLL's name alone refuses whenever anyone has a Git Bash
#      window open, and a guard that always refuses is one that gets bypassed.
#
# So the question is "does a process from the other INSTALLATION exist", and
# `ps -W` answers it: it lists every Windows process with its full image path,
# it ships with both Cygwin and MSYS2 (both are cygwin-family), it needs no
# native helper, and it parses no localised column title.
#
# jcw_guard_other_layer ME OTHER_ROOT   (OTHER_ROOT e.g. C:\msys64)
jcw_guard_other_layer() {
    _jw_me=$1
    _jw_root=$2

    if ! command -v ps >/dev/null 2>&1; then
        note "!! $_jw_me: no ps, so this guard DID NOT CHECK whether the other"
        note "   emulation layer is running. That is not the same as idle. Close"
        note "   it by hand: the two installations' shared-memory regions collide,"
        note "   fork begins failing in BOTH, and a pacman lock can be stranded."
        return 0
    fi

    _jw_hits=$(ps -W 2>/dev/null | tr -d '\r' | grep -i -F "$_jw_root")
    if [ -n "$_jw_hits" ]; then
        _jw_n=$(printf '%s\n' "$_jw_hits" | grep -c .)
        bad "$_jw_me: $_jw_n process(es) are running from $_jw_root."
        printf '%s\n' "$_jw_hits" | sed 's/^/    | /'
        note "    Close them and re-run. Cygwin's and MSYS2's shared-memory regions"
        note "    collide, fork starts failing in both, and a pacman lock can be left"
        note "    stranded. A row measured through that collision is a row full of"
        note "    defects that belong to neither platform."
        return 1
    fi
    ok "$_jw_me: no process is running from $_jw_root"
    note "    (Git for Windows also maps msys-2.0.dll and is deliberately NOT"
    note "    counted: it is a separate installation from C:\\msys64.)"
    return 0
}

# WHERE SCRATCH GOES, AND WHY NOT THE TREE (fixed 2026-09-22, one minute into a
# run whose own provenance line exposed it). These helpers first wrote their
# build, tier and surface logs into $TREE -- the repository being measured. Two
# things were wrong with that. It pollutes a checkout the operator may be about
# to read `git status` on; and jc_rig_ship_stamp counts differing paths, so the
# row's provenance line read "7 path(s) differ" when four were the rig's own
# source and three were its scratch. A provenance stamp that inflates itself is
# worse than none: it is the line a reader trusts to tell them what was measured.
#
# The band plan already said it -- "rig output goes to $TIER_V_DIR, never the
# repo tree" -- so this is a rule that was written down and then not followed,
# which is the same shape as ANECDOTES #93's lesson one file over.
JCW_OUT="${JCW_OUT:-${TMPDIR:-/tmp}}"

# jcw_prereqs LABEL -- the tools these rows need, and the one whose ABSENCE is
# worse than a failure.
#
# pgrep is the load-bearing one. scripts/preflight.sh decides whether a tree is
# busy with `pgrep -x make`, sends its errors to /dev/null, and reports zero
# matches when pgrep is missing -- so on a host without procps it prints
# "tree is quiet" WITHOUT HAVING CHECKED. A gate that cannot fail is worse than
# one that fails, and both these rows are such a host by default. The rig says so
# loudly rather than inheriting the false green.
jcw_prereqs() {
    _jw_label=$1
    _jw_missing=''
    for _jw_t in diff cmp git make awk sed; do
        command -v "$_jw_t" >/dev/null 2>&1 || _jw_missing="$_jw_missing $_jw_t"
    done
    if [ -n "$_jw_missing" ]; then
        bad "$_jw_label: missing required tools:$_jw_missing"
        note "    MSYS2 ships without diffutils by default (no diff, no cmp)."
        return 1
    fi
    ok "$_jw_label: diff, cmp, git, make, awk, sed all present"

    if command -v pgrep >/dev/null 2>&1; then
        ok "$_jw_label: pgrep present, so scripts/preflight.sh can actually check"
    else
        note "!! $_jw_label: pgrep is ABSENT (procps-ng not installed)."
        note "   scripts/preflight.sh will print \"tree is quiet\" WITHOUT CHECKING,"
        note "   because busy_pids sends pgrep's error to /dev/null and reads the"
        note "   empty result as zero. Treat its verdict here as unmeasured, not as"
        note "   a green. Install procps-ng to get a preflight that can fail."
    fi
    return 0
}

# jcw_report_modes LABEL DIR -- does chmod mean anything on the filesystem the
# STATE will live on? Reported, never asserted: a row where it does not is a
# real row about a real installation, and the point is that it SAYS so.
#
# MSYS2 mounts noacl by default, so chmod returns success and changes nothing.
# Adding `acl` for /tmp makes the smoke tier pass, because the tier's isolated
# HOME lives there -- and leaves a real user's ~/.jichi.env under /home still
# world-readable. The line that covers a user is
#   C:/msys64/home /home ntfs binary,acl 0 0
# and mounting / does not work: the installation root is handled specially.
jcw_report_modes() {
    _jw_label=$1
    _jw_dir=$2
    _jw_f="$_jw_dir/.jcw_mode_probe.$$"
    : > "$_jw_f" 2>/dev/null || { note "    $_jw_label: cannot write in $_jw_dir"; return 0; }
    chmod 600 "$_jw_f" 2>/dev/null
    _jw_m=$(ls -l "$_jw_f" 2>/dev/null | cut -c1-10)
    rm -f "$_jw_f"
    JCW_MODES_OK=0
    case "$_jw_m" in
        -rw-------)
            JCW_MODES_OK=1
            ok "$_jw_label: chmod is honoured in $_jw_dir ($_jw_m)" ;;
        *)
            note "!! $_jw_label: chmod 0600 left $_jw_m in $_jw_dir -- this filesystem"
            note "   IGNORES POSIX MODES. jichi's file-privacy guarantees do not hold"
            note "   here: the API key file, the daemon socket and the audit log are"
            note "   readable by other local users, and the daemon will REFUSE TO"
            note "   START rather than expose a socket that runs shell commands."
            note "   \`jichi doctor\` says so too, and probes the real state root." ;;
    esac
    return 0
}

# jcw_count PATTERN FILE -- a count that is a number, always.
#
# THE IDIOM THIS REPLACES, and it shipped in the first run of this rig:
#     n=$(grep -c 'KILLED' "$log" || echo 0)
# `grep -c` PRINTS the count -- including `0` -- and separately EXITS 1 when it
# matched nothing. So on the no-match path the substitution captures grep's own
# "0" and then the fallback's "0", and the variable holds a two-line string.
# Observed 2026-09-22 as `build 1: 127s, 0\n0 warning(s)`, which is merely ugly;
# in jcw_tier the same expression feeds the KILLED and FAILED counters, which are
# the numbers a platform row is read from, and "0" is exactly the value they hold
# on the runs worth trusting.
#
# The `|| echo 0` is not just redundant, it is the bug: grep has already printed
# the count. What actually needs a default is the case where the FILE is missing
# and grep prints nothing at all.
jcw_count() {
    # `--` ends option parsing, so a pattern beginning with `-` is a pattern.
    if [ "$1" = "--" ]; then shift; fi
    _jw_c=$(grep -c -e "$1" "$2" 2>/dev/null)
    case "$_jw_c" in
        ''|*[!0-9]*) _jw_c=0 ;;
    esac
    printf '%s' "$_jw_c"
}

# jcw_build_median LABEL DIR -- three clean builds, median, and the multiplier.
#
# Three runs and the median, not one: the numbers this produces end up on a row
# as a denominator, and a single build is a sample of one. The multiplier is
# ceil(this median / --ref-secs), computed by jc_rig_mult with awk rather than bc
# -- bc is absent from a stock Raspberry Pi image and its absence was SILENT,
# which once put multiplier 1 on the slowest board in the fleet.
jcw_build_median() {
    _jw_label=$1
    _jw_dir=$2
    _jw_a=0; _jw_b=0; _jw_c=0
    _jw_i=0
    while [ "$_jw_i" -lt 3 ]; do
        _jw_i=$((_jw_i + 1))
        ( cd "$_jw_dir" && make clean ) > /dev/null 2>&1
        _jw_s=$(date +%s)
        ( cd "$_jw_dir" && make WERROR=1 ) > "$JCW_OUT/build.log" 2>&1
        _jw_rc=$?
        _jw_e=$(date +%s)
        _jw_d=$((_jw_e - _jw_s))
        if [ "$_jw_rc" -ne 0 ]; then
            bad "$_jw_label: build $_jw_i FAILED (rc=$_jw_rc)"
            tail -20 "$JCW_OUT/build.log" | sed 's/^/    | /'
            return 1
        fi
        _jw_w=$(jcw_count 'warning:' "$JCW_OUT/build.log")
        note "    build $_jw_i: ${_jw_d}s, $_jw_w warning(s)"
        case "$_jw_i" in
            1) _jw_a=$_jw_d ;; 2) _jw_b=$_jw_d ;; 3) _jw_c=$_jw_d ;;
        esac
    done
    JCW_BUILD_MEDIAN=$(printf '%s\n%s\n%s\n' "$_jw_a" "$_jw_b" "$_jw_c" | sort -n | sed -n '2p')
    ok "$_jw_label: build median ${JCW_BUILD_MEDIAN}s over three clean builds"
    return 0
}

# ===================== PER-DRIVER TIMING, AND WHY IT IS AWK =================
#
# WHAT WAS MISSING (measured 2026-09-22, three minutes into a 3.5-hour run).
# `tests/smoke/run.sh` announces each driver with `--- smoke: <name>` and prints
# no timing at all. A tier log is therefore a list of WHAT ran, never of what
# anything COST -- so a run made generous precisely so that slow drivers would
# finish would have recorded the one number it was bought for nowhere.
#
# That matters most exactly here. A driver killed at its deadline yields a BOUND;
# one that completes yields a DURATION; and the whole argument for raising the
# multiplier is turning the first into the second. Without a stamp the log cannot
# tell them apart afterwards, and the run is unrepeatable for a quarter of a day.
#
# WHY awk AND NOT A SHELL LOOP. The obvious `while read l; do echo "$(date +%s)
# $l"; done` spends a FORK PER LINE, on the two platforms in the matrix whose
# defining property is that forking is expensive -- an instrument built out of
# the thing it is measuring. ANECDOTES #92 records the operator's instruction
# after a `ps`-sampling loop was run beside a Cygwin tier for the same reason:
# *"do not interfere with the test runs"*. One long-lived awk adds one process to
# the whole run, and the timing comes out of the run rather than from beside it.
#
# `systime()` and `fflush()` are not in POSIX awk -- illumos `nawk` has neither --
# so the capability is PROBED and the tier still runs without it, saying so. A
# rig that refused to measure a platform because it could not timestamp would
# have its priorities inverted.

jcw_can_stamp() {
    awk 'BEGIN { if (systime() > 1000000000) exit 0; exit 1 }' 2>/dev/null
}

# jcw_stamp T0 -- prefix every line with whole seconds since T0.
jcw_stamp() {
    awk -v t0="$1" '{ printf "%6d %s\n", systime() - t0, $0; fflush(); }'
}

# jcw_durations LOG -- per-driver seconds, dearest first, from the stamps.
#
# The cost of driver i is the distance from its own announcement to the NEXT
# one, which is exact for a serial runner and needs nothing added to run.sh.
# The last driver is closed off with the final line of the log.
jcw_durations() {
    awk '
        { last = $1 + 0 }
        / --- smoke: / { n++; tm[n] = $1 + 0; nm[n] = $NF }
        END {
            for (i = 1; i <= n; i++) {
                e = (i < n) ? tm[i + 1] : last
                printf "%6d  %s\n", e - tm[i], nm[i]
            }
        }
    ' "$1" | sort -rn
}

# jcw_tier LABEL DIR MULT -- the offline gate, with the denominator reported
# whether it passes or fails.
#
# JC_SMOKE_KEEP_GOING=1 always: a row that stops at the first failure reports a
# POSITION, not a size, and enumerating a platform's defects one boot at a time
# is what M466 was written to end. And the driver COUNT is printed either way --
# a rig that captures it only on success left the Pi 400 at "n/a" for months.
jcw_tier() {
    _jw_label=$1
    _jw_dir=$2
    _jw_mult=$3
    ( cd "$_jw_dir" && make smoke-tools ) > "$JCW_OUT/smoke-tools.log" 2>&1 || {
        bad "$_jw_label: make smoke-tools FAILED -- the tier cannot run at all"
        tail -15 "$JCW_OUT/smoke-tools.log" | sed 's/^/    | /'
        note "    This is not a detail: ptydrive is what the tier drives a terminal"
        note "    with, so a tooling build failure takes every driver with it. That"
        note "    state went unnoticed on Cygwin from M683 until M696."
        return 1
    }
    ok "$_jw_label: smoke tooling builds"

    # The marker carries make's status out of the pipeline: `$?` after a pipe is
    # the LAST command's, and `pipefail` is not POSIX. Reading it back from the
    # log also means the status survives in the artifact rather than only in the
    # shell that is about to exit.
    _jw_lim=$((60 * _jw_mult))
    _jw_t0=$(date +%s)
    if jcw_can_stamp; then
        ( cd "$_jw_dir" && JC_SMOKE_TIMEOUT_MULT="$_jw_mult" JC_SMOKE_KEEP_GOING=1 \
            make smoke 2>&1; echo "JCW_TIER_RC=$?" ) \
            | jcw_stamp "$_jw_t0" > "$JCW_OUT/smoke.log"
        _jw_stamped=1
    else
        note "    awk here has no systime(), so the tier log carries no per-driver"
        note "    timing. Counts and verdicts below are unaffected; durations are"
        note "    simply not measured rather than estimated."
        ( cd "$_jw_dir" && JC_SMOKE_TIMEOUT_MULT="$_jw_mult" JC_SMOKE_KEEP_GOING=1 \
            make smoke 2>&1; echo "JCW_TIER_RC=$?" ) > "$JCW_OUT/smoke.log"
        _jw_stamped=0
    fi
    _jw_rc=$(sed -n 's/.*JCW_TIER_RC=\([0-9][0-9]*\).*/\1/p' "$JCW_OUT/smoke.log" | tail -1)
    case "$_jw_rc" in
        ''|*[!0-9]*) _jw_rc=1 ;;
    esac
    _jw_elapsed=$(( $(date +%s) - _jw_t0 ))
    note "    tier wall clock: ${_jw_elapsed}s at multiplier $_jw_mult (deadlines x$_jw_mult)"
    # UNANCHORED ON PURPOSE. With timing on, every line carries a leading
    # "%6d " stamp, so the anchored form `^--- smoke: ` matches nothing and the
    # driver COUNT -- the denominator the whole row is read against -- silently
    # becomes 0. Caught by exercising the pipeline against a fake tier before
    # spending three and a half hours on a real one.
    _jw_n=$(jcw_count -- '--- smoke: ' "$JCW_OUT/smoke.log")
    _jw_k=$(jcw_count 'KILLED at its' "$JCW_OUT/smoke.log")
    _jw_f=$(jcw_count 'FAILED (in suite)' "$JCW_OUT/smoke.log")
    note "    drivers reached: $_jw_n   killed at deadline: $_jw_k   failed: $_jw_f"
    if [ "$_jw_rc" -eq 0 ]; then
        ok "$_jw_label: smoke tier green ($_jw_n drivers)"
    else
        bad "$_jw_label: smoke tier not green -- $_jw_f failed, $_jw_k killed, of $_jw_n"
        note "    A KILLED driver is not a failed one and its output is not evidence:"
        note "    a killed shell continues past a command substitution the kill"
        note "    emptied and can print 'not ok' lines that accuse the product."
        grep 'FAILED (in suite)' "$JCW_OUT/smoke.log" 2>/dev/null \
            | sed 's/^/    | /' | head -12
    fi

    # THE ARTIFACT THE LONG RUN WAS BOUGHT FOR. Written whether the tier passed
    # or failed, for the same reason the driver count is: it is free now and
    # unrecoverable afterwards, and a row that failed is precisely the one whose
    # numbers a reader wants.
    if [ "$_jw_stamped" -eq 1 ]; then
        jcw_durations "$JCW_OUT/smoke.log" > "$JCW_OUT/durations.txt"
        note "    the ten dearest drivers on this platform (seconds):"
        head -10 "$JCW_OUT/durations.txt" | sed 's/^/      /'
        note "    A driver at or near ${_jw_lim}s did not take that long -- it was"
        note "    STOPPED there. That is a bound, not a duration, and its output is"
        note "    not evidence about jichi."
    fi
    return 0
}

# jcw_surfaces LABEL DIR -- the four offline surfaces every row reports.
jcw_surfaces() {
    _jw_label=$1
    _jw_dir=$2
    _jw_bad=0
    for _jw_c in --version doctor describe context; do
        # ONE LOG PER SURFACE. The first version wrote all four to a single path,
        # so each overwrote the last and only `context` survived -- discarding
        # `doctor`, which is the one a platform row is actually read for. The
        # illumos rig had already established that doctor's findings are a RECORD
        # of the platform rather than a gate on it, and a record that is
        # overwritten three times is not one.
        _jw_out="$JCW_OUT/surface-$(printf '%s' "$_jw_c" | sed 's/^--//').log"
        ( cd "$_jw_dir" && ./jichi "$_jw_c" ) > "$_jw_out" 2>&1
        _jw_rc=$?
        # DOCTOR IS READ BY ITS SUMMARY, NOT ITS EXIT CODE. It exits non-zero when
        # it FINDS something, which is it doing its job -- on a noacl mount it
        # correctly reports that private files are not private. An exit code
        # meaning "I looked and found a problem" cannot be read as "the surface is
        # broken" (measured on illumos at M660, where this rig's sibling got it
        # wrong and reported a working doctor as a failure).
        if [ "$_jw_rc" -le 1 ]; then
            note "    $_jw_c rc=$_jw_rc : $(tail -1 "$_jw_out" | cut -c1-70)"
        else
            bad "$_jw_label: ./jichi $_jw_c exited $_jw_rc"
            _jw_bad=1
        fi
    done

    # doctor's own findings, kept in the results file: they are what the row says
    # about this platform, and they are free here and gone afterwards.
    if [ -f "$JCW_OUT/surface-doctor.log" ]; then
        note "    doctor: $(grep -E '[0-9]+ ok, [0-9]+ warning' "$JCW_OUT/surface-doctor.log" | tail -1)"
        grep -E '^[!x]' "$JCW_OUT/surface-doctor.log" | sed 's/^/      /' | head -20
    fi

    if [ "$_jw_bad" -eq 0 ]; then
        ok "$_jw_label: all four offline surfaces answered"
    else
        bad "$_jw_label: at least one offline surface did not answer"
    fi
    return 0
}

# ============ THE LIVE KEY MUST NOT REACH THE TIER (M698, measured) ==========
#
# THE DEFECT THIS RIG SHIPPED WITH, and it accused the product of a fault it did
# not have. The operator exports the gateway key so the live turns can use it via
# `apiKeyEnv`. That export is inherited by everything the rig runs -- including
# the smoke tier -- and `setup_keyfile` drives the setup wizard through a pty,
# telling it to store a key in `JICHI_API_KEY`. jichi looks, finds the variable
# ALREADY SET, and correctly answers:
#
#     $JICHI_API_KEY is already set in this shell -- nothing to store.
#
# ...so it never asks for the key. The pty script then waits for a prompt that
# will never come, burns ~400 s of `expect` timeouts, and reports
# `PTY drive failed (rc=3)` plus five cascading checks. Measured on BOTH Cygwin
# and MSYS2, reproducible standalone on both, and the paired control settles it:
# with the variable unset the same driver passes **28 of 28** on the same tree.
#
# It was very nearly published as a defect in jichi's setup wizard on the
# Windows-family platforms, with M695 as the suspect, because every symptom
# pointed that way: it failed on exactly the two Partly-verified rows and never
# on the Linux bench -- where the gate simply has no gateway key exported.
#
# THE LESSON IS THE ONE THIS FILE KEEPS RELEARNING: the apparatus contaminated
# the measurement. A rig that exports a secret for its last step has changed the
# environment of every step before it. So the key is stashed under a name jichi
# has no opinion about, the operator's variable is UNSET for the whole offline
# half, and it is restored only inside the live turns.

# jcw_key_stash VARNAME -- take the key out of the environment, keep the value.
jcw_key_stash() {
    [ -n "${1:-}" ] || return 0
    JCW_STASHED_KEY=$(eval "printf '%s' \"\${$1:-}\"")
    if [ -n "$JCW_STASHED_KEY" ]; then
        unset "$1"
        note "    \$$1 unset for the offline half: an exported key makes the setup"
        note "    wizard answer \"already set -- nothing to store\", which reads as a"
        note "    pty failure in setup_keyfile. Restored for the live turns only."
    fi
    return 0
}

# jcw_key_restore VARNAME -- put it back, for the live turns and nothing else.
jcw_key_restore() {
    [ -n "${1:-}" ] || return 0
    [ -n "${JCW_STASHED_KEY:-}" ] || return 0
    eval "$1=\$JCW_STASHED_KEY; export $1"
    return 0
}

# ======================= THE DRIVEN HALF, LOCAL TRANSPORT ===================
#
# WHY THESE ROWS DO NOT CALL jc_rig_live. That function is the VM-SHAPED caller
# of the task: it assumes a guest at $HOME/jichi reached through g/gl, behind
# QEMU NAT, with a reverse forward carrying it to LM Studio on the host's
# loopback. None of that exists here. `scripts/_rig_live.sh` says so in its own
# header -- *"The transport is each rig's own business... A rig with a different
# shape calls the task functions directly -- see tier-b-device.sh"* -- and that
# is what these do. The TASK is unchanged and shared, which is the only property
# that makes two rows comparable; the transport is a local `sh`, because the
# platform under test is the machine running the rig.
#
# WHY A GATEWAY RATHER THAN THE TUNNEL. Measured 2026-09-22: LM Studio is not
# installed on this machine, and the JLU instance at 134.176.150.160:1234 times
# out from it. The HRZ gateway answers over TLS from all three environments. A
# row that implied the documented loopback arrangement when it used a gateway
# would be worth less than no row, so the rig names the transport on the row.

# jcw_free_or_die URL MODEL PREFIX KEY_ENV -- confirm the id is in the free
# namespace BEFORE the first billable request.
#
# In the rig rather than in a runbook on purpose: "check the namespace first" is
# a grep, not a virtue, and a step that lives only in a person's memory is a step
# that is skipped at 02:00. An unlisted id is refused rather than tried, because
# the failure mode of guessing is a charge, and the failure mode of refusing is a
# message.
jcw_free_or_die() {
    _jw_url=$1; _jw_model=$2; _jw_prefix=$3; _jw_env=$4
    [ -n "$_jw_prefix" ] || return 0
    case "$_jw_model" in
        "$_jw_prefix"*) ;;
        *)  bad "model '$_jw_model' is outside the free namespace '$_jw_prefix' -- refusing"
            return 1 ;;
    esac
    _jw_key=$(eval "printf '%s' \"\${$_jw_env:-}\"")
    if [ -z "$_jw_key" ]; then
        bad "\$$_jw_env is empty -- refusing to drive."
        note "    An empty key does not fail loudly: the turn 401s and the row reads"
        note "    like a platform that cannot reach a model. Set it, or do not drive."
        return 1
    fi
    _jw_list=$(curl -fsS -m 25 -H "Authorization: Bearer $_jw_key" \
                   "$_jw_url/models" 2>/dev/null)
    if [ -z "$_jw_list" ]; then
        bad "could not list models at $_jw_url -- refusing to drive blind"
        return 1
    fi
    if printf '%s' "$_jw_list" | tr ',' '\n' | grep -q "\"$_jw_model\""; then
        _jw_n=$(printf '%s' "$_jw_list" | tr ',' '\n' | grep -c "\"$_jw_prefix" || echo 0)
        ok "model $_jw_model is listed in the free namespace ($_jw_n ids under $_jw_prefix)"
        return 0
    fi
    bad "model '$_jw_model' is NOT listed at $_jw_url -- refusing"
    return 1
}

# jcw_live ROW REPO URL MODEL KEY_ENV DIR -- the two turns, run locally.
jcw_live() {
    _jw_row=$1; _jw_repo=$2; _jw_url=$3; _jw_model=$4; _jw_env=$5; _jw_dir=$6

    # The key comes back into the environment HERE and nowhere earlier.
    jcw_key_restore "$_jw_env"

    jc_rig_live_config "$_jw_model" "$_jw_url" "$_jw_env" > "$_jw_dir/live.json"
    ok "$_jw_row: live config written with apiKeyEnv=$_jw_env (no secret in the file)"

    _jw_phrase=$(jc_rig_live_phrase "TIER-V-$_jw_row")
    mkdir -p "$_jw_dir/ws"
    jc_rig_live_fixture "$_jw_phrase" > "$_jw_dir/ws/note.txt"

    _jw_pw=$(jc_rig_live_prompt_wire)
    _jw_pt=$(jc_rig_live_prompt_tool)

    # Turn 1 -- the wire. Proves the provider, the request and the SSE framing,
    # and nothing past that point, which is where every documented failure lives.
    if ( cd "$_jw_repo" && ./jichi --config "$_jw_dir/live.json" \
            --prompt-b64 "$_jw_pw" --output json ) \
            > "$_jw_dir/live-$_jw_row.txt" 2>&1 \
       && grep -q '"text"' "$_jw_dir/live-$_jw_row.txt"; then
        ok "$_jw_row: wire turn answered against $_jw_url ($_jw_model)"
    else
        bad "$_jw_row: wire turn did not answer -- see $_jw_dir/live-$_jw_row.txt"
        tail -5 "$_jw_dir/live-$_jw_row.txt" 2>/dev/null | sed 's/^/    /'
        return 1
    fi

    # Turn 2 -- the loop, the one the verdict is named for. The phrase was minted
    # this second and written where only a tool call reaches it, so returning it
    # cannot be a cached answer or a lucky sentence.
    if ( cd "$_jw_dir/ws" && "$_jw_repo/jichi" --config "$_jw_dir/live.json" \
            --auto -q --prompt-b64 "$_jw_pt" < /dev/null ) \
            > "$_jw_dir/live-tool-$_jw_row.txt" 2>&1 \
       && grep -q "$_jw_phrase" "$_jw_dir/live-tool-$_jw_row.txt"; then
        ok "$_jw_row: agentic turn -- the model called a tool and reported $_jw_phrase"
        note "    the tool ran and its result was consumed by a second turn"
    else
        bad "$_jw_row: agentic turn did NOT return $_jw_phrase -- the model may have \
described the tool call instead of invoking it (doctor --live calls that \`text\`), \
or the loop does not execute tools on this platform"
        tail -8 "$_jw_dir/live-tool-$_jw_row.txt" 2>/dev/null | sed 's/^/    /'
        return 1
    fi
    return 0
}
