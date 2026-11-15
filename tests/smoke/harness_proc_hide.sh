#!/bin/sh
# smoke: jichi-harness hides the secret it holds from another SAME-USER process,
# the way jichi does (M770) -- a same-user reader can neither read its
# /proc/<pid>/environ nor readlink its /proc/<pid>/exe, and JICHI_KEEP_DUMPABLE=1
# is the in-band witness that the probe reads the process that holds the secret
# (M1190, def-383, D-F8-1/D-F8-2; the port of the harness-side protection that
# jichi applies in src/main.c).
#
# THE DEFECT THIS IS HELD BY, measured on the current head (the run 0053 LOG,
# the brief's shared pin): in /tmp, a harness process started with a canary in
# its env (`env JH_PROC_KEY=$canary ./jichi-harness credential-helper get < fifo`,
# the fifo held open by `sleep N > fifo`) was read by another same-user process --
# grepping /proc/<pid>/environ for "$canary" printed 1 (rc=0, the canary IS readable)
# and `readlink /proc/<pid>/exe` printed the harness path (rc=0, it SUCCEEDS).
# That is cells 1 and 2, red. `credential-helper get` blocks on stdin, which is
# why the driver uses it -- `doctor` exits too fast to probe.
#
# THE FIX these cells witness (the other worker's half, T1): `jc_harness_main.c`
# main() calls jc_proc_harden_environ() first (before any parse or spawn), which
# on Linux makes the process non-dumpable (PR_SET_DUMPABLE 0), closing both
# routes. After it, the same-user reader finds the canary line ABSENT (or the
# read is refused) and readlink FAILS. The opt-out is honoured by the function
# itself: with JICHI_KEEP_DUMPABLE=1 the process stays dumpable and both come
# back (cell 3, green before AND after -- the unhardened tree IS the opt-out
# behaviour, and cell 3 is what keeps cells 1-2 from passing vacuously).
#
# ROUTE (copied from proc_key_hidden.sh's discipline). The read path here is
# /proc/<pid>/environ + /proc/<pid>/exe -- procfs only. Cells 1-3 are proc-only;
# cells 4-5 (the doctor's protection line) need no route but ride the same driver
# and only mean something where the process is probe-able, so the whole driver
# t_skips where /proc/<self>/environ is not readable (no procfs: the class is
# UNMEASURED here, not proven absent). The route detection runs BEFORE t_plan
# (the t_skip discipline: t_skip prints 1..0 and exits, so it must come first).
#
# WHAT IS CHECKED (five cells, the note's tooth):
#   1. the canary line is ABSENT from the blocking harness's /proc/<pid>/environ
#      (or the read is refused) -- red before T1's main() call: it is present.
#   2. `readlink /proc/<pid>/exe` of that harness process FAILS (rc != 0) -- red
#      before: it succeeds (the run 0053 LOG measured rc=0 on the current head).
#   3. the witness: with JICHI_KEEP_DUMPABLE=1 the canary line is present AGAIN
#      AND the readlink SUCCEEDS (the opt-out is honoured by the function AND the
#      probe reads the process that holds the canary -- without it cells 1-2 pass
#      vacuously). Green before AND after: the unhardened tree already behaves as
#      the opt-out.
#   4. doctor's new protection line, the two reachable states on a Linux build:
#      the default run reports "the harness's memory protection" with the
#      non-dumpable wording and names JICHI_KEEP_DUMPABLE=1 in the opt-out
#      suggestion, and the JICHI_KEEP_DUMPABLE=1 run reports the WARN opt-out
#      wording ("stays dumpable"). Red until T1's jc_harness_doctor.c line lands
#      (the label is absent from both).
#   5. the canary (a secret file's CONTENTS) never appears in doctor's output --
#      a valid config whose modelKeyFile/gitTokenFile contents are the canary
#      (the harness_doctor.sh cell-4 idiom: a path may be printed, the key's value
#      may not). The non-vacuous guard is the protection line's PRESENCE in the
#      output (the line that could carry the value); red until T1's line lands.
#
# SECRETS: the canary is a RANDOM string (od -An -N12 -tx1 /dev/urandom) -- never
# a real secret; it rides in the blocking process's ENV (cell 1-3) and in a
# fixture's secret-file CONTENTS (cell 4-5). Each probe kills its own fifo-holder
# and blocking harness before the next cell, and a trap reaps anything a failure
# path leaves behind, so a leftover `sleep 30` or a dangling harness is never a
# run leftover.
. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# Route detection (procfs only), BEFORE t_plan. The /proc/<self>/environ self-
# read is always permitted, so this measures the route's existence, not the
# protection -- that is measured against the BLOCKING process below.
if grep -q . "/proc/$$/environ" 2>/dev/null; then
    ROUTE=proc
else
    t_skip "no procfs here (/proc/<self>/environ unreadable) -- the same-user \
/proc/<pid>/environ + readlink /proc/<pid>/exe routes this guards are \
UNMEASURED on this host, not proven absent (cells 1-3 are proc-only)"
fi

[ -x "$HBIN" ] || { echo "not ok - the harness binary $HBIN is not built (run make jichi-harness first)" >&2; exit 1; }

t_plan 5
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once.
"$BIN" --version > "$tmp/binv" 2>/dev/null

# A fresh random canary (never a fixed string). JH_PROC_KEY is the env var that
# carries it into the blocking harness process.
canary=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n')
[ -n "$canary" ] || canary="deadbeefc0ffee$$_deadbeefc0ffee"
KEYVAR=JH_PROC_KEY

_hph_fifo=""
_hph_sleep=""
_hph_hb=""
# _hph_kill -- kill this probe's blocking harness (job 1) and fifo-holder sleep
# (job 2) and reap them; called at the end of every probe so nothing lingers.
_hph_kill() {
    if [ -n "$_hph_hb" ]; then
        kill "$_hph_hb" 2>/dev/null
        wait "$_hph_hb" 2>/dev/null
        _hph_hb=""
    fi
    if [ -n "$_hph_sleep" ]; then
        kill "$_hph_sleep" 2>/dev/null
        wait "$_hph_sleep" 2>/dev/null
        _hph_sleep=""
    fi
    [ -n "$_hph_fifo" ] && rm -f "$_hph_fifo"
    _hph_fifo=""
}
# The tier's cleanup plus this driver's leftover jobs.
_hph_cleanup() {
    _hph_kill
    smoke_cleanup
}
trap _hph_cleanup EXIT INT TERM

# _hph_run TAG EXTRA_ENV -- start ONE blocking harness (credential-helper get,
# fed through a fifo held open by `sleep 30`) with KEYVAR=$canary (+ any
# EXTRA_ENV, at most one VAR=VALUE word), wait until it is ALIVE and blocked on
# the fifo's open, then read /proc/<pid>/environ (canary present?) and readlink
# /proc/<pid>/exe (rc), and kill it. Sets _hph_present (1/0) and _hph_rl
# (readlink rc). The measured idiom from the run 0053 LOG:
#   mkfifo f; (sleep 30 > f &); env KEY=$canary "$HBIN" credential-helper get < f &
# The driver writes the pid it captured from $! to $tmp/$_tag.pid -- the pid of
# the env -> harness process, since env execs into the binary -- so the probe
# reads the process that actually holds the canary; a wrong pid would turn cell
# 3 (the witness) red. The two background jobs (the sleep
# holder and the harness subshell) are $1 and $2 of the probe -- the brief's
# `kill %2` re-expressed positionally.
_hph_run() {
    _tag="$1"; _extra="$2"
    _hph_fifo="$tmp/$_tag.fifo"
    rm -f "$_hph_fifo" "$tmp/$_tag.pid"
    mkfifo "$_hph_fifo" || { _hph_present=0; _hph_rl=2; return 1; }
    (sleep 30 > "$_hph_fifo" &)   # job: the fifo-holder (writes nothing, stays 30 s)
    _hph_sleep=$!
    _hph_hb=""
    # shellcheck disable=SC2086 -- _extra is one optional VAR=VALUE word
    (
        env $_extra "$KEYVAR=$canary" "$HBIN" credential-helper get < "$_hph_fifo" \
            > /dev/null 2>&1 &
        echo $! > "$tmp/$_tag.pid"
        wait $!
    ) &
    _hph_hb=$!
    _i=0
    while [ "$_i" -lt 8 ]; do
        [ -s "$tmp/$_tag.pid" ] && break
        _i=$((_i + 1)); sleep 1
    done
    if [ ! -s "$tmp/$_tag.pid" ]; then
        # the harness died before it could record its pid (a parse/refusal):
        # report absent so the cell is not vacuously green.
        _hph_kill
        _hph_present=0; _hph_rl=2
        return 1
    fi
    _hpid=$(cat "$tmp/$_tag.pid" 2>/dev/null)
    _j=0
    while [ "$_j" -lt 8 ]; do
        kill -0 "$_hpid" 2>/dev/null && break
        _j=$((_j + 1)); sleep 1
    done
    if ! kill -0 "$_hpid" 2>/dev/null; then
        _hph_kill
        _hph_present=0; _hph_rl=2
        return 1
    fi
    # 1: the canary line in the blocking harness's environ (present? absent? or
    # the read refused -- grep folds "not found" and "read error" into 0).
    # POSIX-clean read (proc_key_hidden.sh's idiom, smoke_lint check 21 bans
    # the GNU-only -a on this row): tr strips the NUL byte separators, so the
    # grep sees a plain text stream of one VAR=VALUE per line, and "not found"
    # and "read error" both fold into 0.
    if tr '\0' '\n' < "/proc/$_hpid/environ" 2>/dev/null \
        | grep -qF "$KEYVAR=$canary" 2>/dev/null; then
        _hph_present=1
    else
        _hph_present=0
    fi
    # 2: readlink the exe (rc != 0 is the hide; the value is never printed).
    readlink "/proc/$_hpid/exe" > "$tmp/$_tag.exe" 2>/dev/null
    _hph_rl=$?
    _hph_kill
    return 0
}

# --- 1: the canary is ABSENT from the blocking harness's environ -------------
_hph_run def ""
if [ "$_hph_present" -eq 0 ]; then
    t_ok "1: a same-user process cannot read the harness's canary from /proc/<pid>/environ (the line is absent, or the read is refused)"
else
    t_fail "1: the canary IS in the blocking harness's /proc/<pid>/environ (the $KEYVAR=<canary> line is present) -- a same-user reader reads the key the harness holds (M1190/D-F8-1); red until T1's main() jc_proc_harden_environ() call lands"
fi

# --- 2: readlink /proc/<pid>/exe of the harness FAILS ------------------------
# (a fresh blocking process; cell 1's was killed at the end of its probe)
_hph_run exe ""
if [ "$_hph_rl" -ne 0 ]; then
    t_ok "2: readlink /proc/<pid>/exe of the blocking harness fails (rc=$_hph_rl) -- the second route to the key's binary is closed"
else
    t_fail "2: readlink /proc/<pid>/exe of the blocking harness SUCCEEDED (rc=0, it printed: $(cat "$tmp/exe.exe" 2>/dev/null)) -- the run 0053 LOG measured exactly this on the current head; red until T1's main() jc_proc_harden_environ() call lands"
fi

# --- 3: the witness -- JICHI_KEEP_DUMPABLE=1 restores BOTH (green either way) -
_hph_run opt "JICHI_KEEP_DUMPABLE=1"
if [ "$_hph_present" -eq 1 ] && [ "$_hph_rl" -eq 0 ]; then
    t_ok "3: with JICHI_KEEP_DUMPABLE=1 the canary line is present again AND readlink succeeds -- the opt-out is honoured by the function AND the probe reads the process that holds the canary (without it cells 1-2 could pass vacuously)"
else
    t_fail "3: the witness is broken (present=$_hph_present, readlink rc=$_hph_rl) -- with JICHI_KEEP_DUMPABLE=1 BOTH must come back; if only one does, the probe does not reach the canary-holding process, so cell 1's absence is vacuous"
fi

# --- the doctor fixture for cells 4-5 (a valid config whose secret CONTENTS
# are the canary; doctor stats the paths, never reads or prints the values) ---
printf '%s\n' "$canary" > "$tmp/model.key"
printf '%s\n' "$canary" > "$tmp/git.token"
printf '{}\n' > "$tmp/jichi.json"
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp","modelKeyFile":"$tmp/model.key","gitTokenFile":"$tmp/git.token","jichiConfig":"$tmp/jichi.json"}
EOF

# --- 4: doctor's protection line, the two reachable states on Linux ---------
with_deadline 60 "$HBIN" --config "$tmp/cfg.json" doctor < /dev/null \
    > "$tmp/doc" 2>&1
with_deadline 60 env JICHI_KEEP_DUMPABLE=1 "$HBIN" --config "$tmp/cfg.json" doctor < /dev/null \
    > "$tmp/doc_opt" 2>&1
_d4default=0
grep -qF "the harness's memory protection" "$tmp/doc" 2>/dev/null || _d4default=1
grep -qF "non-dumpable" "$tmp/doc" 2>/dev/null || _d4default=1
grep -qF "JICHI_KEEP_DUMPABLE=1" "$tmp/doc" 2>/dev/null || _d4default=1
_d4optout=0
grep -qF "the harness's memory protection" "$tmp/doc_opt" 2>/dev/null || _d4optout=1
grep -qF "stays dumpable" "$tmp/doc_opt" 2>/dev/null || _d4optout=1
if [ "$_d4default" -eq 0 ] && [ "$_d4optout" -eq 0 ]; then
    t_ok "4: doctor reports 'the harness's memory protection' (default: non-dumpable wording naming JICHI_KEEP_DUMPABLE=1; opt-out run: the WARN 'stays dumpable' wording)"
else
    t_fail "4: doctor's protection line is missing or mis-worded (default class $_d4default: 0=ok, else 'the harness's memory protection'/'non-dumpable'/'JICHI_KEEP_DUMPABLE=1' absent; opt-out class $_d4optout: 0=ok, else the label/'stays dumpable' absent) -- red until T1's jc_harness_doctor.c line lands; default doc: $(grep -i 'memory protection' "$tmp/doc" 2>/dev/null | head -n 1 | tr '\n' ' ')"
fi

# --- 5: the canary (a secret file's CONTENTS) never appears in doctor's output
# Non-vacuous: the protection line that could carry the value is PRESENT in the
# output; the canary string is absent from both the default and the opt-out run.
_d5=0
grep -qF "the harness's memory protection" "$tmp/doc" 2>/dev/null || _d5=1
grep -qF "$canary" "$tmp/doc" "$tmp/doc_opt" 2>/dev/null && _d5=2
if [ "$_d5" -eq 0 ]; then
    t_ok "5: with a valid config whose secret files' CONTENTS are the canary, doctor's output carries the protection line but NEVER the canary (the state is reported, the key's value is not -- the harness_doctor.sh cell-4 idiom)"
else
    t_fail "5: the canary rule (class $_d5: 1=the protection line is absent from doctor's output -- non-vacuous guard, red until T1's line lands 2=the canary -- a secret file's CONTENTS -- appeared in doctor's output: the state is reported, never the key's value)"
fi

t_done
