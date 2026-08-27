#!/bin/sh
# smoke: the observability band (M158/M160) -- the `audit` and `runs`
# offline readers (text, --since windowing, --output json) over synthetic
# sink files, and `doctor --unattended` gating the POSTURE by exit code
# (a mockmodel status-200 catch-all keeps reachability green so the
# verdict reflects posture only).
# The Python original rewrote the shipped config.autonomous.json's models
# in place; JSON editing in sh is fragile, so this port builds the
# hardened config inline and separately asserts the SHIPPED example still
# declares that posture (the link the original provided).
# (Port of tests/e2e/observability.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- audit reader -------------------------------------------------------------
cat > "$tmp/privileged.jsonl" <<'EOF'
{"v":1,"ts":1753300000,"launcher":"sudo","decision":"unattended_refused","mode":"auto","command":"sudo apt-get update"}
{"v":1,"ts":1753300100,"launcher":"sudo","decision":"allowlist","mode":"auto","command":"sudo systemctl restart app"}
EOF

"$BIN" audit "$tmp/privileged.jsonl" < /dev/null > "$tmp/a.txt" 2>&1; rc=$?
if [ $rc -eq 0 ] && grep -q "2 privileged-command attempts" "$tmp/a.txt" \
   && grep -q "1 refused, 1 ran" "$tmp/a.txt" \
   && grep -q "sudo apt-get update" "$tmp/a.txt"; then
    t_ok "audit renders the summary (counts + commands)"
else
    t_fail "audit summary wrong (rc=$rc)"
fi

"$BIN" audit "$tmp/privileged.jsonl" --since 1s < /dev/null > "$tmp/a2.txt" 2>&1
if grep -q "no privileged-command attempts" "$tmp/a2.txt"; then
    t_ok "audit --since windows out old entries"
else
    t_fail "audit --since did not filter"
fi

"$BIN" audit "$tmp/privileged.jsonl" --output json < /dev/null \
    > "$tmp/a.json" 2>/dev/null
if [ "$("$JQ" '.v' "$tmp/a.json" 2>/dev/null)" = "1" ] \
   && [ "$("$JQ" '.total' "$tmp/a.json")" = "2" ] \
   && [ "$("$JQ" '.refused' "$tmp/a.json")" = "1" ] \
   && [ "$("$JQ" '.by_launcher.sudo' "$tmp/a.json")" = "2" ] \
   && "$JQ" -q '.recent[1]' "$tmp/a.json" \
   && ! "$JQ" -q '.recent[2]' "$tmp/a.json"; then
    t_ok "audit --output json carries the machine fields"
else
    t_fail "audit json wrong: $(head_bytes 200 "$tmp/a.json")"
fi

# --- runs reader ---------------------------------------------------------------
mkdir -p "$tmp/runs"
cat > "$tmp/runs/r1.jsonl" <<'EOF'
{"ts":100,"run":"r1","event":"start"}
{"ts":130,"run":"r1","event":"end","outcome":"ok","rolled_back":false,"tokens_used":5200,"tool_calls":7}
EOF
cat > "$tmp/runs/r2.jsonl" <<'EOF'
{"ts":200,"run":"r2","event":"budget","kind":"tokens"}
{"ts":201,"run":"r2","event":"end","outcome":"budget_exhausted","rolled_back":true,"tokens_used":400000,"tool_calls":80}
EOF

"$BIN" runs "$tmp/runs" < /dev/null > "$tmp/r.txt" 2>&1; rc=$?
if [ $rc -eq 0 ] && grep -q "r1" "$tmp/r.txt" && grep -q "r2" "$tmp/r.txt" \
   && grep -q "budget=tokens" "$tmp/r.txt" \
   && grep -q "rolled_back" "$tmp/r.txt" && grep -q "5.2k" "$tmp/r.txt"; then
    t_ok "runs renders the triage table"
else
    t_fail "runs table wrong (rc=$rc)"
fi

# M329, in its OWN fixture directory: adding a third run to the shared one above
# broke two assertions that counted rows, which is why this does not reuse it.
# A run where spend continued after the outcome was decided cannot have reported
# totals that include it, so the row must SAY they are short -- a quietly wrong
# number is worse than a missing one.
mkdir -p "$tmp/runs329"
cat > "$tmp/runs329/short.jsonl" <<'EOF'
{"ts":300,"run":"short","event":"start"}
{"ts":340,"run":"short","event":"end","outcome":"budget_exhausted","rolled_back":false,"tokens_used":1000000,"tool_calls":27}
{"ts":390,"run":"short","event":"post_outcome","outcome":"budget_exhausted"}
EOF
cat > "$tmp/runs329/clean.jsonl" <<'EOF'
{"ts":100,"run":"clean","event":"start"}
{"ts":130,"run":"clean","event":"end","outcome":"ok","rolled_back":false,"tokens_used":500,"tool_calls":2}
EOF
"$BIN" runs "$tmp/runs329" < /dev/null > "$tmp/r329.txt" 2>&1

if grep -q 'post_outcome(totals_short)' "$tmp/r329.txt"; then
    t_ok "runs flags a run whose totals are short (M329)"
else
    t_fail "post_outcome not surfaced: $(tail -2 "$tmp/r329.txt")"
fi
# The control: the clean run must NOT carry the flag, or the check above would pass
# on a reader that flagged every row.
if [ "$(grep -c 'post_outcome' "$tmp/r329.txt")" = 1 ]; then
    t_ok "and only that run carries it"
else
    t_fail "flag on $(grep -c 'post_outcome' "$tmp/r329.txt") rows, expected exactly 1"
fi

"$BIN" runs "$tmp/runs" --output json < /dev/null > "$tmp/r.json" 2>/dev/null
r2_budget=""; r2_rb=""; r1_out=""
i=0
while [ $i -lt 4 ]; do
    name=$("$JQ" ".runs[$i].run" "$tmp/r.json" 2>/dev/null) || break
    case "$name" in
        r1) r1_out=$("$JQ" ".runs[$i].outcome" "$tmp/r.json") ;;
        r2) r2_budget=$("$JQ" ".runs[$i].budget" "$tmp/r.json")
            r2_rb=$("$JQ" ".runs[$i].rolled_back" "$tmp/r.json") ;;
    esac
    i=$((i + 1))
done
if [ "$("$JQ" '.v' "$tmp/r.json" 2>/dev/null)" = "1" ] \
   && [ "$("$JQ" '.total' "$tmp/r.json")" = "2" ] \
   && [ "$("$JQ" '.shown' "$tmp/r.json")" = "2" ] \
   && [ "$r1_out" = "ok" ] && [ "$r2_budget" = "tokens" ] \
   && [ "$r2_rb" = "true" ]; then
    t_ok "runs --output json envelope + rows are right"
else
    t_fail "runs json wrong: r1=$r1_out r2=$r2_budget/$r2_rb"
fi

"$BIN" runs "$tmp/runs" --since 1s --output json < /dev/null \
    > "$tmp/r2.json" 2>/dev/null
if [ "$("$JQ" '.shown' "$tmp/r2.json" 2>/dev/null)" = "0" ] \
   && [ "$("$JQ" '.windowed_out' "$tmp/r2.json")" = "2" ]; then
    t_ok "runs --since windows out old runs (shown=0, windowed_out=2)"
else
    t_fail "runs --since window wrong"
fi

# --- doctor --unattended gates the posture -------------------------------------
cat > "$tmp/ok.mm" <<'EOF'
wire openai
rule
  status 200
  body {}
EOF
mm_start "$tmp/ok.mm" "$tmp"

# the shipped example must still declare the hardened posture this
# inline config mirrors (the link the Python original made by rewriting
# the shipped file's models in place)
shipped="$SMOKE_ROOT/examples/autonomous-loop/config.autonomous.json"
if grep -q '"privilegedCommands": *"deny"' "$shipped" \
   && grep -q '"pathFence": *true' "$shipped"; then
    t_ok "the shipped autonomous config still declares the hardened posture"
else
    t_fail "shipped config posture drifted -- update this driver's mirror"
fi

cat > "$tmp/good.json" <<EOF
{"lowResource":false,"models":[{"name":"worker","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"pathFence":true,"privilegedCommands":"deny",
 "privilegedAudit":true,"verify":"true","editScope":["src/**"],
 "logging":true}
EOF
"$BIN" --config "$tmp/good.json" doctor --unattended < /dev/null \
    > "$tmp/dg.txt" 2>&1; rc=$?
# ROOT IS A DIFFERENT, EQUALLY CORRECT ANSWER (M459). doctor --unattended FAILS
# a posture running as uid 0, because `privilegedCommands: deny` cannot mean
# anything when the agent is already root. This driver silently assumed a
# non-root host, so on any root environment it failed for a reason that has
# nothing to do with what it tests -- found on Android under proot-distro,
# where EVERY guest is uid 0 because proot fakes root. A container or a CI
# running as root would have hit the same wall.
#
# Asserted rather than skipped: skipping would drop the check exactly where the
# behaviour is most interesting. Both branches test something real -- as
# non-root the hardened posture must pass, as root it must fail AND say why.
if [ "$(id -u)" -eq 0 ]; then
    if [ $rc -ne 0 ] && grep -q "running as root" "$tmp/dg.txt"; then
        t_ok "as root, the hardened posture is refused and names root"
    else
        t_fail "running as root, rc=$rc, and the refusal did not name root: \
$(grep -iE 'root|unattended' "$tmp/dg.txt" | head -2)"
    fi
elif [ $rc -eq 0 ] && grep -q "unattended: path fence on" "$tmp/dg.txt"; then
    t_ok "doctor --unattended passes the hardened posture (exit 0)"
else
    t_fail "hardened posture rc=$rc: $(grep -i unattended "$tmp/dg.txt" | head -2)"
fi

cat > "$tmp/bad.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"x",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "privilegedCommands":"allow","privilegedAudit":false,"pathFence":false}
EOF
"$BIN" --config "$tmp/bad.json" doctor --unattended < /dev/null \
    > "$tmp/db.txt" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "pathFence is explicitly off" "$tmp/db.txt"; then
    t_ok "doctor --unattended fails an unsafe posture (exit $rc)"
else
    t_fail "unsafe posture rc=$rc"
fi

"$BIN" --config "$tmp/bad.json" doctor < /dev/null > "$tmp/dp.txt" 2>&1
mm_stop
if ! grep -q "unattended:" "$tmp/dp.txt"; then
    t_ok "plain doctor does not leak the unattended-profile checks"
else
    t_fail "plain doctor printed unattended checks"
fi

t_done
