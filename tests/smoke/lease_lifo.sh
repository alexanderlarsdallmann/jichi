#!/bin/sh
# smoke: a warn-take must never overwrite the live holder's lease record
# (D-17, M1101).
#
# THE DEFECT, confirmed by the lead with a real mockmodel repro on c6f13625.
# The workspace lease was ONE file, <key>.json. A second run that warned and
# proceeded wrote its record OVER the first's (jc_write_file_atomic), and on
# exit its release found its own run id in the record and removed the ONLY
# record -- the still-working first run was left with none, and a third run
# with --lease fail read "free" and was let in while the first was alive.
#
# THE FIX. One record per holder: <key>.<run>.json. A take scans every record
# of the workspace (any live one means held, and names the most recently
# started live holder), writes its OWN file beside the others, and a release
# removes only its own. A record left by a run whose process is gone is taken
# over QUIETLY (a stale lease must not wedge the workspace), but jichi ps
# counts it as evidence and removes nothing.
#
# The holder is a REAL live process, like lease.sh phase 2: a fixture naming a
# pid that is not alive would be read as stale and every "live" check below
# would pass for the wrong reason. Its record is planted in the OLD build's
# shape -- a BARE <key>.json -- so this driver also pins that the new scan
# still reads the legacy name. The key is captured from inside a live probe
# run (its verify hook lists the leases directory), never recomputed here: a
# second derivation in the test would be free to disagree with the one under
# test, the M431 lesson.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one"}
rule
  text LIFO_DONE
EOF
# A template config with the model slot filled per phase (the mock's port is
# not known until each mm_start). snapshots must be ON: the probe's verify
# hook -- the thing that lists the leases directory -- only runs in a
# snapshotted turn (M81, the same trap lease.sh phase 1 records).
cat > "$tmp/config.json" <<'EOF'
{"lowResource":false,"models":[],"snapshots":true,"repoMap":false,
"references":false,"toolProfile":"full","maxRetries":0}
EOF
cfg() {   # $1 = destination; pins the current mock's port into the template
    sed "s|\"models\":\[\]|\"models\":[{\"name\":\"m\",\"provider\":\"openai\",\"model\":\"mock\",\"apiBase\":\"http://127.0.0.1:$MM_PORT/v1\",\"apiKey\":\"x\",\"roles\":[\"chat\"]}]|" \
        "$tmp/config.json" > "$1"
}

(cd "$ws" && git init -q . 2>/dev/null)
mkdir -p "$HOME/.jichi.d/leases"

# --- a live holder, and its BARE legacy record ---------------------------------
# `sleep 120`, alive for the whole driver; the record is written in the OLD
# build's shape (<key>.json) once the key is known.
sleep 120 &
holder=$!

# --- learn the real <key> from one throwaway probe run -------------------------
# It takes its own per-run record and releases it on the way out, so when it is
# gone the leases directory holds only the probe's lock file.
mm_start "$tmp/replies.mm" "$tmp/cap0" 4
cfg "$tmp/cap0.json"
rm -f "$tmp/held.txt"
( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cap0.json" \
    -q --no-session --auto --budget-tokens 100k \
    --verify "ls $HOME/.jichi.d/leases/ > $tmp/held.txt 2>/dev/null; exit 0" \
    -p "say it" < /dev/null) >/dev/null 2>&1 ); rc0=$?
mm_stop
name=$(grep '\.json$' "$tmp/held.txt" 2>/dev/null | head -1)
if [ "$rc0" -eq 0 ] && [ -n "$name" ]; then
    t_ok "a probe run took a lease and released it ($name)"
else
    t_fail "no lease was seen while the probe run was in flight (rc=$rc0)"
    t_done
fi

# The key is the record name minus its ".<run>.json" (or legacy ".json") tail:
# the last ".<...>" is the run id, everything before it is <key>.
key=${name%.json}
key=${key%%.*}
LP="$HOME/.jichi.d/leases/$key.json"
printf '{"v":1,"run":"11111111-2222-4333-8444-555555555555","pid":%s,"started":1,"mode":"auto"}\n' \
    "$holder" > "$LP"
printf '%s\n' "$(cat "$LP")" > "$tmp/expect.json"   # the bytes the diff compares

# --- 2: --lease warn says the tree is held by the LIVE holder -------------------
# A warn-take must name the record it found. On the OLD build this is where the
# holder's record was overwritten (and the next check goes red). The warning is
# jc_logf(WARN), which -q suppresses (the refusal below is fprintf, so it is not
# suppressed by -q); this run drops -q to hear it.
mm_start "$tmp/replies.mm" "$tmp/capb" 4
cfg "$tmp/capb.json"
outb=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/capb.json" \
    --no-session --auto --budget-tokens 100k --lease warn \
    -p "say it" < /dev/null) 2>&1 ); rc_b=$?
mm_stop
if printf '%s\n' "$outb" | grep -q "ALREADY held by a live run (pid $holder"; then
    t_ok "a warn-take says the tree is held by the live holder (pid $holder)"
else
    t_fail "no warn-take named the live holder (rc=$rc_b): $(printf '%s' "$outb" | tail -c 200)"
fi

# --- 3: the legacy record SURVIVES byte-exact ------------------------------------
# THE CORE of D-17's first half: the warn-take wrote its OWN <key>.<run>.json
# beside <key>.json, not over it. A diff against the planted bytes is exact.
if diff -q "$LP" "$tmp/expect.json" >/dev/null 2>&1; then
    t_ok "the live holder's record survives the warn-take, byte-exact"
else
    t_fail "the warn-take overwrote the live holder's record: $(head_bytes 160 "$LP" 2>/dev/null)"
fi

# --- 4: --lease fail refuses, and names the live holder --------------------------
# The holder is still recorded, so a supervisor asking for serialisation must
# be turned away. On the OLD build, after B's release erased the only record,
# C read "free" and was let in while the holder was still working -- the defect.
# A refusal exits before any model call, so no mock is needed for this phase.
cfg "$tmp/capc.json"
outc=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/capc.json" \
    -q --no-session --auto --budget-tokens 100k --lease fail \
    -p "say it" < /dev/null) 2>&1 ); rc_c=$?
if [ "$rc_c" -eq 2 ] && printf '%s\n' "$outc" | grep -q "pid $holder"; then
    t_ok "--lease fail refuses while the live holder is recorded (exit 2, pid named)"
else
    t_fail "fail mode did not refuse the live holder (exit $rc_c): $(printf '%s' "$outc" | tail -c 200)"
fi

# --- 5: ps names the live holder while it works ----------------------------------
# A recorded, still-alive holder must show up as a held lease. The refused run
# wrote no record (a refusal takes nothing), so the holder is the only live one.
with_deadline 20 "$BIN" ps < /dev/null > "$tmp/ps5.out" 2>&1
rc_ps5=$?
psout=$(cat "$tmp/ps5.out")
if printf '%s\n' "$psout" | grep "^$holder " | grep -q "held"; then
    t_ok "ps lists the live holder with a held lease"
else
    t_fail "ps did not show the live holder as held: $(head_bytes 240 <<EOF
$psout
EOF
)"
fi

# --- 6: the holder dies; the stale record is taken over and ps keeps it ---------
# D (fail) finds the holder gone, takes the stale lease QUIETLY (exit 0), and
# leaves the evidence in place: ps counts exactly one stale lease and removes
# nothing, the M759 rule -- a record of how a run ended is not litter. (D's OWN
# per-run record is released when D exits, so the bare one is what survives.)
kill "$holder" 2>/dev/null
wait "$holder" 2>/dev/null
mm_start "$tmp/replies.mm" "$tmp/capd" 4
cfg "$tmp/capd.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/capd.json" \
    -q --no-session --auto --budget-tokens 100k --lease fail \
    -p "say it" < /dev/null) >/dev/null 2>&1; rc_d=$?
mm_stop
# No assignment prefixed to with_deadline (smoke_lint check 13): FreeBSD's sh
# would not export it to the child, so the rc and the capture are separate.
with_deadline 20 "$BIN" ps --output json < /dev/null > "$tmp/ps.json" 2>/dev/null
rc_ps=$?
stale=$( "$JQ" '.stale.leases' "$tmp/ps.json" 2>/dev/null )
if [ "$rc_d" -eq 0 ] && [ -f "$LP" ] && [ "$stale" = "1" ]; then
    t_ok "a stale lease is taken quietly (exit 0) and ps keeps the evidence (1 stale)"
else
    t_fail "the stale take went wrong (rc=$rc_d; record present: $([ -f "$LP" ] && echo yes || echo no); stale=$stale)"
fi

t_done
