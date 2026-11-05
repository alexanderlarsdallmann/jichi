#!/bin/sh
# smoke: a model-issued command can neither read jichi's API key out of jichi's
# OWN /proc/<pid>/environ nor write into jichi's output through its
# /proc/<pid>/fd (M770).
#
# THE FINDING, measured 2026-09-25. M130 unsets every configured apiKeyEnv
# variable in each CHILD before exec, so `env | grep KEY` in a model-issued
# command finds nothing. But the child runs as the same user as jichi, and Linux
# lets a same-user process read /proc/<pid>/environ of any DUMPABLE process.
# jichi's own environment still holds the key -- it read it there -- so one
# command got it back from its parent's environ. The same route let it append a
# line to jichi's stdout through /proc/$PPID/fd/1. The fix makes jichi
# non-dumpable (PR_SET_DUMPABLE 0), which makes those entries root-owned and
# refused; the kernel resets the flag at exec, so jichi's children are unaffected.
#
# THE PROBE. One headless turn, two tool calls: one with a `timeout` (jichi's own
# fork/exec, run_command_watched) and one without (popen) -- the two exec paths
# child_fds.sh separates. Each command, whose parent ($PPID) is jichi:
#   - greps jichi's environ for the canary and writes 1 or 0 to a file. grep's
#     status folds the two ways the key is NOT exposed -- absent, or unreadable --
#     into 0, and the one way it is, readable and present, into 1. A missing file
#     means the command never ran.
#   - appends TAMPER_<PATH>_0770 to /proc/$PPID/fd/1. The command SPELLS it
#     TAM''PER, so the marker cannot reach jichi's stdout by being echoed as
#     command text (the M721 trap); only the write itself produces it.
# The canary key is set only in jichi's environment, never in the command text.
#
# THE WITNESS. JICHI_KEEP_DUMPABLE=1 is the opt-out, and checks 4-5 run the same
# probe with it: the key and the write must come back. That proves the fix is
# what hides them AND that the probe reads the process that holds the key --
# without it checks 2-3 could pass vacuously (a wrong $PPID, a write that never
# ran). Perturbed per check at M770: with the jc_proc_harden_environ() call
# removed from main(), checks 2, 3, 6, 7 and 8 went red (7 because doctor then
# reports "never applied", not the opt-out); with the opt-out ignored, 4, 5 and
# 7; with the opt-out read as a presence check, 8; with the first tool call
# replaced by text, 1.
#
# ROUTE (M771). The read path is per-platform: /proc/<pid>/environ (Linux, Hurd)
# or `ps -e` (FreeBSD, OpenBSD, macOS). The driver detects it and SKIPS only
# where neither is known -- reporting "unmeasured here", never "the class does
# not exist" (M770's first version skipped green on FreeBSD, where `ps -e` in
# fact reveals the key). Where a route exists and jichi has no protection for it
# (a platform with neither PR_SET_DUMPABLE nor procctl PROC_TRACE_CTL), check 2
# and the doctor checks (6-8) go red -- the class existing unfixed on that row,
# and doctor says the same. Measured on FreeBSD 15.1 (2026-09-25) both ways:
# green with the fix, and 2/6/7/8 red with the mechanism built out. The fd/
# tamper checks (3, 5b) skip off the proc route.
. "$(dirname "$0")/_smoke.sh"

# THE ROUTE (M771). A same-user child reads its parent's environment by a
# platform-specific path, and the ABSENCE of one route is not the absence of the
# class -- M770's first driver skipped where /proc/<pid>/environ was unreadable
# and reported "the class does not exist", which on FreeBSD/OpenBSD/macOS is a
# false all-clear: there `ps -e` shows a process's environment to its owner.
# So detect the route, and skip only where NONE is known -- and say "unmeasured
# here", not "absent".
#   proc -> /proc/<ppid>/environ    (Linux, GNU/Hurd, mounted procfs)
#   ps   -> `ps -e` reveals the env  (FreeBSD, OpenBSD, macOS)
# The fd/ tamper channel (checks 3, 5b) is a /proc feature: it is skipped, not
# absent, where there is no /proc/<pid>/fd.
ROUTE=none
if grep -q . "/proc/$$/environ" 2>/dev/null; then
    ROUTE=proc
else
    # Does `ps -e` reveal a process's environment to its owner? The canary is
    # set at EXEC (env), NOT with export: FreeBSD's `ps -e` exposes the
    # exec-time environment, and a shell's later export does not update what the
    # kernel shows (measured on FreeBSD 15.1). A self-read is always permitted,
    # so this measures the route's existence, not the protection -- that is
    # measured against the PARENT below. The value is grep'd whole, so the
    # ps command column (which never holds it) cannot cause a false match.
    _rc="jc_route_$$"
    if env "JC_ROUTE_CANARY=$_rc" sh -c 'ps -p $$ -ww -e 2>/dev/null' \
         | grep -q "JC_ROUTE_CANARY=$_rc"; then
        ROUTE=ps
    elif env "JC_ROUTE_CANARY=$_rc" sh -c 'pargs -e $$ 2>/dev/null' \
         | grep -q "JC_ROUTE_CANARY=$_rc"; then
        # illumos (M799): no /proc/<pid>/environ file and `ps -e` is not "show env"
        # here -- the env route is `pargs -e`, which reads /proc/<pid>/as. Without
        # this branch the driver skipped illumos as UNMEASURED while pargs in fact
        # revealed the key (the FreeBSD/M770 false-skip, one platform over).
        ROUTE=pargs
    fi
fi
# The command a same-user child would actually run to read the parent's
# environment, named so checks 2 and 2-fail report the route they measured, not a
# Linux-shaped `/proc/<pid>/environ` on a host where that file does not exist.
case "$ROUTE" in
    proc)  ROUTE_LABEL="/proc/<pid>/environ" ;;
    ps)    ROUTE_LABEL="ps -e" ;;
    pargs) ROUTE_LABEL="pargs -e" ;;
    *)     ROUTE_LABEL="(none)" ;;
esac
[ "$ROUTE" = none ] && t_skip "no route from a child to its parent's environment \
here (no readable /proc/<pid>/environ, and ps -e reveals none) -- the class this \
guards is UNMEASURED on this host, not proven absent"

# CAN jichi close the fd/ tamper channel here? Only where its mechanism is
# PR_SET_DUMPABLE (Linux, Hurd), which makes /proc/<pid>/fd root-owned. On NetBSD
# (M798, route /proc/<pid>/environ) and illumos (M799, route pargs) the environ
# SCRUB closes the KEY route -- but it does not touch /proc/<pid>/fd, and neither
# OS has PR_SET_DUMPABLE to close it, nor /proc/<pid>/{mem,as}. So there the fd/
# channel is present but UNCLOSABLE: checks 3 and 5b report that loudly (a
# documented limitation, HARDENING.md section 2), not a clean pass or a bare red.
FD_CLOSABLE=yes
case "$(uname -s)" in
    NetBSD) [ "$ROUTE" = proc ] && FD_CLOSABLE=no ;;
    SunOS)  FD_CLOSABLE=no ;;  # illumos: /proc/<pid>/fd present, pargs route, no flag
esac

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

KEYVAR=JC_M770_KEY
KEYVAL=canary-not-a-real-secret-0770

# config_for FILE PORT -- one model whose key lives in $KEYVAR.
config_for() {
    printf '{"models":[{"name":"m","provider":"openai","model":"mock",' > "$1"
    printf '"apiBase":"http://127.0.0.1:%s/v1","apiKeyEnv":"%s","roles":["chat"]}],' \
        "$2" "$KEYVAR" >> "$1"
    printf '"snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",' >> "$1"
    printf '"lowResource":false,"maxRetries":0}\n' >> "$1"
}

# env_read -- the snippet that sets e=1 iff the parent's ($PPID) environment
# exposes the key, by the route this host offers. No double quote, no backslash.
env_read() {
    if [ "$ROUTE" = proc ]; then
        printf "grep -q '%s=%s' /proc/\$PPID/environ 2>/dev/null && e=1" \
            "$KEYVAR" "$KEYVAL"
    elif [ "$ROUTE" = pargs ]; then
        printf "pargs -e \$PPID 2>/dev/null | grep -q '%s=%s' && e=1" \
            "$KEYVAR" "$KEYVAL"
    else
        printf "ps -p \$PPID -ww -e 2>/dev/null | grep -q '%s=%s' && e=1" \
            "$KEYVAR" "$KEYVAL"
    fi
}
# tamper -- the fd/ write, a /proc feature. Empty where there is no /proc/<pid>/fd.
tamper() {
    [ "$ROUTE" = proc ] && printf "{ echo TAM''PER_%s_0770 >> /proc/\$PPID/fd/1; \
} 2>/dev/null; " "$1"
}
# probe_cmd TAG PATH -- the command text for one exec path. No double quote and
# no backslash in it, so it drops into the JSON of the reply table as is.
probe_cmd() {
    printf "e=0; %s; %secho \$e > '%s/%s.%s.n'; echo PROC_DONE" \
        "$(env_read)" "$(tamper "$2")" "$tmp" "$1" "$2"
}

# probe TAG [VAR=VALUE] -- runs the turn; sets $e_exec, $e_popen (1 exposed,
# 0 hidden, "unread" never ran) and $rc.
probe() {
    cat > "$tmp/$1.mm" <<MM
wire openai
rule
  count 1
  tool run_terminal_command {"command":"$(probe_cmd "$1" EXEC)","timeout":20}
rule
  count 2
  tool run_terminal_command {"command":"$(probe_cmd "$1" POPEN)"}
rule
  text PROBE_DONE
MM
    mm_start "$tmp/$1.mm" "$tmp/$1.cap" 6
    config_for "$tmp/$1.json" "$MM_PORT"
    rm -f "$tmp/$1.out"
    # `>>`, not `>`: the tamper write appends through /proc/<pid>/fd/1, and
    # jichi writing at its own offset would overwrite the marker it is meant to
    # show. $2 is at most one VAR=VALUE word, by construction.
    # shellcheck disable=SC2086
    (cd "$ws" && with_deadline 90 env "$KEYVAR=$KEYVAL" $2 "$BIN" \
        --config "$tmp/$1.json" --no-session --auto -p probe < /dev/null) \
        >> "$tmp/$1.out" 2> "$tmp/$1.err"
    rc=$?
    mm_stop
    e_exec=$(cat "$tmp/$1.EXEC.n" 2>/dev/null || echo unread)
    e_popen=$(cat "$tmp/$1.POPEN.n" 2>/dev/null || echo unread)
}

# --- 1: the denominator -- both commands ran, and the turn finished --------------
probe hardened
if [ "$e_exec" != unread ] && [ "$e_popen" != unread ] && \
   grep -q PROBE_DONE "$tmp/hardened.out"; then
    t_ok "both exec paths ran the probe and the turn finished (rc=$rc)"
else
    t_fail "the probe did not run (exec=$e_exec popen=$e_popen rc=$rc), so checks 2-7 \
prove nothing: $(head_bytes 300 "$tmp/hardened.err" | tr '\n' ' ')"
fi

# --- 2: the key in jichi's environment is hidden, on both paths ------------------
if [ "$e_exec" = 0 ] && [ "$e_popen" = 0 ]; then
    t_ok "a model-issued command cannot read jichi's key via $ROUTE_LABEL"
else
    t_fail "the key read gave exec=$e_exec popen=$e_popen, not 0 0 -- 1 is the \
model's shell reading jichi's key via $ROUTE_LABEL (M770), unread a probe \
that never ran"
fi

# --- 3: jichi's descriptors are out of reach, on both paths ----------------------
# The fd/ tamper channel is a /proc feature. Where there is no /proc/<pid>/fd
# (the ps route) it is skipped, not passed: a check that cannot run must not
# report a clean result (the M519 else-branch rule).
if [ "$FD_CLOSABLE" = no ]; then
    t_skip_one "fd/ tamper channel is present on this procfs system (NetBSD/illumos) but \
no jichi mechanism closes it (no PR_SET_DUMPABLE; the scrub reaches only the environment) -- \
a documented, accepted limitation, HARDENING.md section 2"
elif [ "$ROUTE" != proc ]; then
    t_skip_one "fd/ tamper channel needs /proc/<pid>/fd -- absent on the ps route"
elif grep -q 'TAMPER_[A-Z]*_0770' "$tmp/hardened.out"; then
    t_fail "a model-issued command wrote into jichi's stdout through /proc/\$PPID/fd/1: \
$(grep 'TAMPER_' "$tmp/hardened.out" | tr '\n' ' ')"
else
    t_ok "a model-issued command cannot write into jichi's output through /proc/<pid>/fd"
fi

# --- 4-5: the opt-out restores both -- the in-band red witness -------------------
probe exposed JICHI_KEEP_DUMPABLE=1
if [ "$e_exec" = 1 ] && [ "$e_popen" = 1 ]; then
    t_ok "JICHI_KEEP_DUMPABLE=1 exposes the key again -- the fix is what hides it"
else
    t_fail "with JICHI_KEEP_DUMPABLE=1 the key read gave exec=$e_exec popen=$e_popen, \
not 1 1 -- the probe does not reach the key-holding process, so check 2 is vacuous"
fi
if [ "$FD_CLOSABLE" = no ]; then
    t_skip_one "fd/ tamper channel unclosable on NetBSD/illumos (see check 3) -- no witness to run"
elif [ "$ROUTE" != proc ]; then
    t_skip_one "fd/ tamper channel needs /proc/<pid>/fd -- absent on the ps route"
elif grep -q TAMPER_EXEC_0770 "$tmp/exposed.out" && \
   grep -q TAMPER_POPEN_0770 "$tmp/exposed.out"; then
    t_ok "JICHI_KEEP_DUMPABLE=1 lets the write land again -- the fix is what stops it"
else
    t_fail "with JICHI_KEEP_DUMPABLE=1 the tamper markers did not land in jichi's \
stdout, so check 3 is vacuous: $(head_bytes 300 "$tmp/exposed.out" | tr '\n' ' ')"
fi

# --- 6-8: doctor reports the state in force ---------------------------------------
config_for "$tmp/doc.json" 9
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/doc.json" doctor < /dev/null) \
    > "$tmp/doc.out" 2>&1
if grep -q 'keys are hidden from other same-user processes' "$tmp/doc.out"; then
    t_ok "doctor reports the keys hidden from other same-user processes"
else
    t_fail "doctor does not report the hardening in force: \
$(grep -i 'same-user' "$tmp/doc.out" | tr '\n' ' ')"
fi
(cd "$ws" && with_deadline 60 env JICHI_KEEP_DUMPABLE=1 "$BIN" --config "$tmp/doc.json" \
    doctor < /dev/null) > "$tmp/doc_optout.out" 2>&1
if grep -q 'keys are NOT hidden from other same-user processes' "$tmp/doc_optout.out"; then
    t_ok "doctor warns when JICHI_KEEP_DUMPABLE keeps them readable"
else
    t_fail "doctor does not warn under JICHI_KEEP_DUMPABLE=1: \
$(grep -i 'same-user' "$tmp/doc_optout.out" | tr '\n' ' ')"
fi
# A misspelled opt-out must not open the hole: the variable needs a YES in the one
# boolean dialect, and a presence check would read "ture" as one (the M519 class).
# jc_bool_from_word's vocabulary itself is unit-tested (tests/test_str.c).
(cd "$ws" && with_deadline 60 env JICHI_KEEP_DUMPABLE=ture "$BIN" --config "$tmp/doc.json" \
    doctor < /dev/null) > "$tmp/doc_typo.out" 2>&1
if grep -q 'keys are hidden from other same-user processes' "$tmp/doc_typo.out"; then
    t_ok "a misspelled JICHI_KEEP_DUMPABLE keeps the keys hidden"
else
    t_fail "JICHI_KEEP_DUMPABLE=ture turned the protection off: \
$(grep -i 'same-user' "$tmp/doc_typo.out" | tr '\n' ' ')"
fi

t_done
