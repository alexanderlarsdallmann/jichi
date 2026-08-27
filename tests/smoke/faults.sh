#!/bin/sh
# smoke: error paths must DEGRADE VISIBLY, not silently (M198 #4).
# Requires a FAULT=1 binary (`make clean && make FAULT=1`); SKIPS
# otherwise, so a normal build is unaffected. The assertion is
# deliberately not "it survived": surviving an allocation failure by
# silently dropping sessions from the listing with exit 0 is the DEFECT --
# each case asserts a diagnostic reaches the user.
# (Port of tests/e2e/faults.py, M210.)
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- a far more reliable probe
# than inferring it from injected behaviour.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 10
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9

store="$HOME/.jichi.d/sessions"
mkdir -p "$store"
for i in 1 2 3 4; do
    sid="aaaaaaaa-0000-4000-8000-00000000000$i"
    printf '{"sessionId":"%s","title":"synth %d","workspaceDirectory":"%s",' \
        "$sid" "$i" "$ws" > "$store/$sid.json"
    printf '"mode":"chat","history":[{"role":"user","content":"q"}]}' \
        >> "$store/$sid.json"
done

# baseline: without injection all four sessions list cleanly
(cd "$ws" && with_deadline 20 "$BIN" --config "$tmp/config.json" \
    ls --all < /dev/null > "$tmp/base" 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$(grep -c "aaaaaaaa" "$tmp/base")" -eq 4 ]; then
    t_ok "baseline: four synthetic sessions list cleanly"
else
    t_fail "baseline rc=$rc, $(grep -c aaaaaaaa "$tmp/base") listed"
fi

# READ failure: every jc_read_file fails; the user must be TOLD (pre-M198
# this printed "(no saved sessions)" and exited 0 -- silent degradation)
(cd "$ws" && with_deadline 20 env JICHI_FAULT_READ_AFTER=0 "$BIN" \
    --config "$tmp/config.json" ls --all < /dev/null \
    > "$tmp/r0" 2>&1); rc=$?
if [ $rc -ge 124 ]; then
    t_fail "read-fault: HUNG"
elif grep -q "could not be read" "$tmp/r0"; then
    t_ok "total read failure is reported, not silent"
else
    t_fail "read-fault: sessions vanished with no diagnostic"
fi

# partial READ failure: some list, the shortfall is reported
(cd "$ws" && with_deadline 20 env JICHI_FAULT_READ_AFTER=2 "$BIN" \
    --config "$tmp/config.json" ls --all < /dev/null \
    > "$tmp/r2" 2>&1); rc=$?
if [ $rc -ge 124 ]; then
    t_fail "partial-read-fault: HUNG"
elif grep -q "could not be read" "$tmp/r2"; then
    t_ok "partial read failure reports the shortfall"
else
    t_fail "partial-read-fault: unreadable sessions not reported"
fi

# --- ALLOC failure (rewritten at M482) ---------------------------------------
# WHAT WAS WRONG WITH THE OLD VERSION OF THESE CHECKS, because it is the whole
# reason the defect below survived. It ran three fixed budgets -- 200, 800,
# 3000 -- and asserted only "did not hang, did not die on a signal", passing for
# ANY exit code from 0 to 123. Two independent faults in one loop:
#
#   1. `ls --all` makes exactly TWO counted allocations, so at 200/800/3000
#      NOTHING WAS INJECTED. All three checks described the behaviour of an
#      ordinary run. Measured: the output at AFTER=200 is byte-identical to no
#      injection at all, and so is the output with the environment variable's
#      name MISSPELLED -- the strongest possible demonstration that the checks
#      were not looking at the injector.
#   2. Even had it fired, "rc in 0..123" cannot fail on the defect this file's
#      own header names: "surviving an allocation failure by silently dropping
#      sessions from the listing with exit 0 is the DEFECT". Exit 0 with an empty
#      listing was inside the passing range.
#
# And that is exactly what jichi did: at a firing budget, `ls` printed
# "(no saved sessions)" and exited 0, and `ls --output json` printed a
# well-formed `{"v":1,"sessions":[]}` -- a machine-readable lie on an interface
# docs/EMBEDDING.md calls stable. Fixed in run_ls / run_ls_json / the TUI at M482.
#
# THE INVARIANT THESE CHECKS NOW ASSERT, which is a property rather than a
# number: at every budget, the run either produces the WHOLE listing or TELLS
# the user it could not. Never a plausible partial answer with a success status.
# Sweeping budgets instead of naming them means the checks keep their teeth when
# the allocation count of this path drifts, which is what defeated the old ones.
_alloc_budgets="1 2 3 4 6 10 20 60 200"

# `grep -c` counts matching LINES, and the JSON surface prints the whole listing
# on ONE line (cJSON_PrintUnformatted), so it reported 1 for four sessions and
# every budget looked like a truncated listing. Count OCCURRENCES instead. The
# `grep -o` pattern here carries a mandatory atom, which is the rule M481 added
# to posix_utils_lint: a nullable -o pattern prints nothing on OpenBSD's grep.
_n_sessions() { grep -o 'aaaaaaaa' "$1" 2>/dev/null | wc -l | tr -d '[:space:]'; }

# The first element of a space-prefixed accumulator list. `${list# }` strips one
# leading space and leaves the REST of the list, which as a filename is a path
# with spaces in it -- measured, it produced "cannot open .../j.3 4 6 10".
_first() { set -- $1; printf '%s' "$1"; }


# check: the injector demonstrably CHANGES the run at some budget. This is the
# anti-vacuity floor, and it is deliberately NOT derived from the sweep's own
# verdicts -- M479's lesson: a floor gated on the thing it guards disables
# itself exactly when it is needed.
_fires=0
for after in $_alloc_budgets; do
    (cd "$ws" && with_deadline 20 env JICHI_FAULT_ALLOC_AFTER=$after "$BIN" \
        --config "$tmp/config.json" ls --all < /dev/null \
        > "$tmp/a.$after" 2>&1); echo $? > "$tmp/rc.$after"
    if ! cmp -s "$tmp/a.$after" "$tmp/base"; then
        _fires=1
    fi
done
if [ "$_fires" -eq 1 ]; then
    t_ok "the allocation injector changes the run at some swept budget"
else
    t_fail "JICHI_FAULT_ALLOC_AFTER changed NOTHING at any of: $_alloc_budgets \
-- the injection is not reaching this path, so the checks below prove nothing \
(this is the state the pre-M482 checks were in, and they reported ok)"
fi

# check: no budget yields a silent wrong answer on the TEXT surface.
_silent=""
for after in $_alloc_budgets; do
    _rc=$(cat "$tmp/rc.$after")
    if [ "$(_n_sessions "$tmp/a.$after")" -eq 4 ]; then
        continue                      # full listing: the fault was absorbed
    fi
    if [ "$_rc" -ne 0 ]; then
        continue                      # it failed loudly, which is the contract
    fi
    if grep -q 'could not' "$tmp/a.$after"; then
        continue                      # degraded, and said so
    fi
    _silent="$_silent $after"
done
if [ -z "$_silent" ]; then
    t_ok "no swept budget loses sessions silently (text: full listing, or it says so)"
else
    t_fail "silent degradation at budget(s):$_silent -- an incomplete listing \
with exit 0 and no diagnostic, which is the defect this driver exists for: \
$(head_bytes 120 < "$tmp/a.$(_first "$_silent")")"
fi

# check: the same on the JSON surface, which is a STABLE interface a supervisor
# parses rather than reads -- so a well-formed empty array is worse there.
_jsilent=""
for after in $_alloc_budgets; do
    (cd "$ws" && with_deadline 20 env JICHI_FAULT_ALLOC_AFTER=$after "$BIN" \
        --config "$tmp/config.json" ls --all --output json < /dev/null \
        > "$tmp/j.$after" 2>&1); _rc=$?
    if [ "$(_n_sessions "$tmp/j.$after")" -eq 4 ]; then
        continue
    fi
    [ "$_rc" -ne 0 ] && continue
    _jsilent="$_jsilent $after"
done
if [ -z "$_jsilent" ]; then
    t_ok "no swept budget returns a well-formed but false JSON listing"
else
    t_fail "ls --output json returned a successful, incomplete listing at \
budget(s):$_jsilent -- a supervisor cannot tell this from an empty store: \
$(head_bytes 120 < "$tmp/j.$(_first "$_jsilent")")"
fi

# check: and none of it hangs or dies on a signal (the old checks' only real
# content, kept, because a crash under allocation failure is still a defect).
_bad=""
for after in $_alloc_budgets; do
    _rc=$(cat "$tmp/rc.$after")
    if [ "$_rc" -ge 124 ] && [ "$_rc" -le 128 ]; then
        _bad="$_bad ${after}:HUNG"
    elif [ "$_rc" -gt 128 ]; then
        _bad="$_bad ${after}:SIG"
    fi
done
if [ -z "$_bad" ]; then
    t_ok "no swept allocation budget hangs or dies on a signal"
else
    t_fail "allocation failure crashed or hung at:$_bad"
fi

# WRITE failure is NOT tested here, deliberately: jc_write_file_atomic's
# two callers are reachable only after a completed turn (a model call), so
# driving it via -p against a dead port measures the network timeout, not
# the write path. The write contract lives in the unit suite
# (test_session.c, under #ifdef JC_FAULT).

# --- M326q: memBudgetMb where the watchdog cannot run -----------------------
# The RSS watchdog walks /proc/<pid>/stat. Where /proc is absent -- a non-Linux
# system, or a container without it -- the budget NEVER FIRES, and until M326q
# nothing said so: a safety key silently doing nothing. JC_FAULT_PROCFS is the
# only way to reach that branch here, short of privileges to unmount /proc.
cat > "$tmp/mb.json" <<'JSONEOF'
{"models":[{"name":"m","provider":"openai","model":"x",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"k"}],
"lowResource":false,"memBudgetMb":2048}
JSONEOF
out=$(with_deadline 30 env JICHI_FAULT_PROCFS_AFTER=0 "$BIN" --config "$tmp/mb.json" \
      doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q "memBudgetMb is set but cannot be enforced"; then
    t_ok "memBudgetMb warns where the RSS watchdog cannot run"
else
    t_fail "no warning for an unenforceable memBudgetMb"
fi

# --- M503: a filesystem that ACCEPTS chmod and ignores it -------------------
# Measured on MSYS2's default `noacl` mount: `chmod 0600` returns SUCCESS and
# the mode stays 0644, so the API key file, the daemon socket and the audit log
# are readable by any local user while jichi reports nothing at all. There is no
# such mount on this bench, and JC_FAULT_CHMOD is the only way to reach the
# branch -- the same reason JC_FAULT_PROCFS exists: the guarantee this protects
# is a SAFETY property silently doing nothing.
#
# Note the fault makes jc_make_private return JC_OK. That is the defect, not a
# shortcut: callers already ignore the status, so success and no-op are
# indistinguishable, which is why doctor has to read the mode back.
d_home="$tmp/privhome"
mkdir -p "$d_home"
out=$(with_deadline 30 env HOME="$d_home" JICHI_FAULT_CHMOD_AFTER=0 "$BIN" \
      doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q "private files are NOT private" &&
   printf '%s' "$out" | grep -q "readable by other local users"; then
    t_ok "doctor reads the mode back and names what is exposed"
else
    t_fail "a chmod that did nothing went unreported -- the key file, the \
daemon socket and the audit log are world-readable and doctor is silent: \
$(printf '%s' "$out" | grep -i priv | head_bytes 200)"
fi

# Under --unattended it must be FATAL, not advisory: an unattended run whose
# daemon socket is world-readable lets any local user drive a process that runs
# shell commands. M158b's escalation set is explicit and per-check, so this is a
# decision, not a blanket rule -- and a decision worth a test.
out=$(with_deadline 30 env HOME="$d_home" JICHI_FAULT_CHMOD_AFTER=0 "$BIN" \
      doctor --unattended < /dev/null 2>&1); rc=$?
# Asserted on the exit code and the SUMMARY, never on the glyph: this tier runs
# in a C locale where the mark is the ASCII fallback `x`, not `✗` (M310's pair).
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q "private files are NOT private" &&
   printf '%s' "$out" | grep -qE '[1-9][0-9]* problem'; then
    t_ok "--unattended escalates it to a failure (exit $rc)"
else
    t_fail "an unattended supervisor would start anyway (rc=$rc): \
$(printf '%s' "$out" | grep -i priv | head_bytes 200)"
fi

t_done
