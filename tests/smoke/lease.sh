#!/bin/sh
# smoke: the per-workspace run lease (M431e).
#
# THE HAZARD. jichi has no lock of any kind, while the autonomy envelope assumes
# ONE ACTOR PER TREE -- and `revertOutOfScope` makes that assumption load-bearing:
# the M83 end-of-turn sweep diffs the whole tree against a run-start baseline and
# cannot tell a sibling run's edits, or a human's mid-run merge, from an
# out-of-scope write by the model it polices. So a second run can make the first
# revert work nobody asked it to touch. The lease does not fix that assumption; it
# makes violating it LOUD.
#
# Advisory by design: WARN and proceed is the default, because two read-only runs
# over one tree are routine (it is how an operator inspects a running job) and
# refusing them to prevent a write-write hazard would be a cure worse than the
# disease. `--lease fail` is for a supervisor that wants serialisation.
#
# The lease PATH is captured from inside a live run (the verify hook lists the
# directory), rather than recomputed here from the djb2 workspace key -- a second
# derivation in the test would be free to disagree with the one under test, which
# is the M431 lesson about two surfaces describing one result.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one"}
rule
  text LEASE_DONE
EOF

(cd "$ws" && git init -q . 2>/dev/null)

# --- phase 1: a real run holds a lease WHILE it runs, and drops it after ------
# snapshots ON and a tool call, because the completion verify -- the hook used to
# observe the lease mid-run -- only runs in a snapshotted turn (M81). The first cut
# of this driver used write_config (snapshots:false) and a text-only reply, so the
# gate never fired and all three phase-1 checks failed for that reason rather than
# for anything about leases.
mm_start "$tmp/replies.mm" "$tmp/cap" 4
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 100k \
    --verify "ls $HOME/.jichi.d/leases/ > $tmp/held.txt 2>/dev/null; \
              cat $HOME/.jichi.d/leases/*.json > $tmp/rec.json 2>/dev/null; exit 0" \
    -p "say it" < /dev/null > /dev/null 2>&1); rc=$?
mm_stop

name=$(head -1 "$tmp/held.txt" 2>/dev/null)
if [ -n "$name" ]; then
    t_ok "a lease is held DURING the run ($name)"
else
    t_fail "no lease existed while the run was in flight (rc=$rc)"
fi

if [ -n "$name" ] && [ ! -f "$HOME/.jichi.d/leases/$name" ]; then
    t_ok "the lease is released when the run ends"
else
    t_fail "lease $name survived the run -- it would be read as a stale holder"
fi

# The record must name the holder well enough to act on: a pid to check and a run
# id to find in ~/.jichi.d/runs/<run>.jsonl.
if "$SMOKE_TOOLS/jsonq" -q '.pid' "$tmp/rec.json" 2>/dev/null &&
   "$SMOKE_TOOLS/jsonq" -q '.run' "$tmp/rec.json" 2>/dev/null; then
    t_ok "the record names a pid and a run id"
else
    t_fail "record missing pid/run: $(head_bytes 160 "$tmp/rec.json" 2>/dev/null)"
fi

# No early exit: a bad TAP count is itself a failure the runner reports, so the
# remaining phases must still emit their results. If phase 1 could not name the
# lease, fall back to the derived path so phases 2-3 still exercise the policy.
LP="$HOME/.jichi.d/leases/${name:-unknown.json}"

# --- phase 2: a LIVE holder, --lease fail -> refuse, exit 2 -------------------
# The holder has to be a process that ACTUALLY EXISTS. This block used to write
# `"pid":1` on the stated grounds that pid 1 "always exists on any POSIX system".
# That is false, and Cygwin is the counter-example (M477): there is no pid 1 at
# all -- `kill -0 1` gives ESRCH, `/proc/1` is absent, and a Cygwin shell reports
# PPID 1 for a parent that is not a process. jichi read the record, correctly
# found the named holder dead, took the lease and ran. So checks 4-6 failed while
# check 7 -- which REWARDS treating a dead holder as stale -- passed, on the same
# host, in the same run. The fixture was wrong; the policy was right.
#
# A real background process is portable, and it is also closer to the thing being
# modelled: another run holding this workspace. Reaped at the end of the phase.
sleep 120 &
holder=$!
mkdir -p "$HOME/.jichi.d/leases"
printf '{"v":1,"run":"11111111-2222-4333-8444-555555555555","pid":%s,"started":1,"mode":"auto"}\n' \
    "$holder" > "$LP"

mm_start "$tmp/replies.mm" "$tmp/cap2" 4
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config2.json"
out2=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    -q --no-session --auto --budget-tokens 100k --lease fail \
    -p "say it" < /dev/null) 2>&1 ); rc2=$?
mm_stop

if [ "$rc2" -eq 2 ]; then
    t_ok "--lease fail refuses a workspace held by a live run (exit 2)"
else
    t_fail "expected exit 2, got $rc2: $(printf '%s' "$out2" | tail -c 160)"
fi

# The refusal must be ACTIONABLE: name the holder's pid and the way out.
if printf '%s' "$out2" | grep -q "pid $holder" &&
   printf '%s' "$out2" | grep -q -- "--lease warn"; then
    t_ok "the refusal names the holder and the way past it"
else
    t_fail "refusal not actionable: $(printf '%s' "$out2" | tail -c 200)"
fi

# The refused run must NOT have taken the lease for itself.
if grep -q "\"pid\":$holder" "$LP" 2>/dev/null; then
    t_ok "a refused run leaves the holder's lease untouched"
else
    t_fail "the refused run overwrote the holder's lease: $(cat "$LP" 2>/dev/null)"
fi

kill "$holder" 2>/dev/null
wait "$holder" 2>/dev/null

# --- phase 2b: the EPERM branch of the liveness check ------------------------
# pid 1 was not an arbitrary choice, and dropping it silently would have retired
# a branch. It is root-owned, so for a NON-ROOT user kill(1, 0) fails with EPERM
# instead of succeeding, and that is the only coverage jc_lease_pid_alive's
# "exists but belongs to someone else" path has:
#
#     /* EPERM: it exists and belongs to someone else. Still a live holder. */
#     return (errno == EPERM) ? 1 : 0;
#
# So it is kept, gated on the two conditions that make it mean what it claims.
# Note what this exposes about the old comment: it asserted EPERM coverage
# unconditionally, but as ROOT kill(1, 0) SUCCEEDS, so the branch taken is the
# ordinary one -- and this project's own WSL2 and container rows run as root.
# The claim was true only for non-root hosts, which was never stated.
#
# The gate does not parse kill's MESSAGE: a shell cannot separate EPERM from
# ESRCH by exit status, and the text is locale-dependent (first read here on a
# German-locale Cygwin, "kill: (1) - No such process" inside a German prefix).
# `ps -p 1` answers the existence question directly and is POSIX. Cost stated
# plainly: on a host with a pid 1, a non-root user and a ps that lacks -p, this
# skips and the branch loses its only coverage -- a lost check, never a false
# pass. Measured: Linux `ps -p 1` -> 0, Cygwin -> 1, correct on both.
if [ "$(id -u)" = 0 ]; then
    t_skip_one "running as root: kill(1,0) SUCCEEDS, so pid 1 would exercise the same branch as phase 2, not EPERM"
elif kill -0 1 2>/dev/null || ps -p 1 >/dev/null 2>&1; then
    printf '{"v":1,"run":"22222222-2222-4333-8444-555555555555","pid":1,"started":1,"mode":"auto"}\n' > "$LP"
    mm_start "$tmp/replies.mm" "$tmp/cap2b" 4
    sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config2b.json"
    out2b=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2b.json" \
        -q --no-session --auto --budget-tokens 100k --lease fail \
        -p "say it" < /dev/null) 2>&1 ); rc2b=$?
    mm_stop
    if [ "$rc2b" -eq 2 ]; then
        t_ok "a holder we may not signal (pid 1, EPERM) still counts as live"
    else
        t_fail "EPERM holder not treated as live: expected exit 2, got $rc2b: $(printf '%s' "$out2b" | tail -c 160)"
    fi
else
    t_skip_one "this host has no pid 1 (Cygwin), so the EPERM liveness branch is unreachable here"
fi

# --- phase 3: a DEAD holder is taken quietly, even under fail ----------------
# A crashed run must not block every later one -- the classic lockfile failure.
# pid 2147483646 is above any real pid_max on Linux.
printf '{"v":1,"run":"99999999-2222-4333-8444-555555555555","pid":2147483646,"started":1,"mode":"auto"}\n' > "$LP"

mm_start "$tmp/replies.mm" "$tmp/cap3" 4
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config3.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
    -q --no-session --auto --budget-tokens 100k --lease fail \
    -p "say it" < /dev/null) > /dev/null 2>&1; rc3=$?
mm_stop

if [ "$rc3" -eq 0 ]; then
    t_ok "a STALE lease does not block a run, even under --lease fail"
else
    t_fail "stale lease blocked the run (exit $rc3) -- a crashed run would wedge the workspace forever"
fi

t_done
