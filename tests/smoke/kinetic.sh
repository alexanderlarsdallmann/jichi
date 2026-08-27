#!/bin/sh
# smoke: the kinetic gate + audit (M163a, mirrors privileged.sh). A
# kinetic user tool `motor` touches a marker when it runs. Across six
# scenarios the gate must let motion through exactly when it should, with
# every decision audited:
#   A ask+unattended -> refused, audit unattended_refused, launcher tool:motor
#   B allow          -> runs, audit allow
#   C deny           -> refused, audit deny
#   D allowlist+ask  -> runs unattended (the E-stop guarantee), audit allowlist
#   E shell bypass   -> run_terminal_command "<abs>/motor.sh" refused pre-shell,
#                       audit launcher shell
#   F chain evasion  -> "<abs>/motor.sh ; echo x" is NOT allowlist-matched
# M551: scenarios G-I add the INTERACTIVE arm. All six above run headless
# `--auto`, so the prompt a human answers -- the entire reason the default
# posture is `ask` -- was rendered by no test in the tree, and it was reading
# `[y]es  [n]o` aloud. A prompt that moves something in the physical world is
# the last place a key list should be unreadable by the person authorising it.
# (Port of tests/e2e/kinetic.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 11
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

motor="$tmp/motor.sh"
printf '#!/bin/sh\n: > "$MARKER"\necho moved\n' > "$motor"
chmod +x "$motor"

# run_scenario CAP POSTURE ALLOWLIST_JSON TOOLNAME ARGS_JSON
# -> fresh HOME; sets MARKER (path), AUDIT (path); returns 0
run_scenario() {
    _cap="$tmp/$1"; mkdir -p "$_cap"
    HOME=$(smoke_tmp)
    export HOME
    MARKER="$HOME/moved"
    AUDIT="$HOME/.jichi.d/audit/kinetic.jsonl"
    cat > "$_cap/replies.mm" <<EOF
wire openai
rule
  count 1
  tool $4 $5
rule
  text done
EOF
    mm_start "$_cap/replies.mm" "$_cap"
    # Build the allowlist fragment OUTSIDE the heredoc: a backslash-escaped
    # quote inside ${3:+...} within a heredoc is a POSIX gray zone -- dash
    # 0.5.8 (stretch, V2f) leaves the backslashes in, producing invalid JSON,
    # while modern dash strips them. Plain double-quote context is unambiguous.
    _allow=""
    if [ -n "$3" ]; then
        _allow=",\"kineticCommandsAllow\":$3"
    fi
    cat > "$_cap/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"mock","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "kineticCommands":"$2"$_allow,
 "tools":[{"name":"motor","description":"drive the motor",
           "kinetic":true,"command":"$motor",
           "env":{"MARKER":"$MARKER"}}]}
EOF
    (cd "$ws" && with_deadline 60 "$BIN" --config "$_cap/config.json" \
        -q --no-session --auto -p "move" < /dev/null > /dev/null 2>&1)
    mm_stop
}

# A: ask + unattended -> refused, audited, launcher tool:motor
run_scenario A ask '' motor '{}'
if [ ! -e "$MARKER" ] && grep -q '"decision":"unattended_refused"' "$AUDIT" \
   2>/dev/null; then
    t_ok "A: ask/unattended refused the motor"
else
    t_fail "A: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi
if grep '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null \
   | grep -q '"launcher":"tool:motor"'; then
    t_ok "A: audit launcher is tool:motor"
else
    t_fail "A: wrong launcher in the audit"
fi
if [ "$(ls -l "$AUDIT" 2>/dev/null | cut -c1-10)" = "-rw-------" ]; then
    t_ok "the kinetic audit file is 0600"
else
    t_fail "kinetic audit perms: $(ls -l "$AUDIT" 2>/dev/null | cut -c1-10)"
fi

# B: allow -> runs + audited allow
run_scenario B allow '' motor '{}'
if [ -e "$MARKER" ] && grep -q '"decision":"allow"' "$AUDIT" 2>/dev/null; then
    t_ok "B: allow posture ran the motor (audited allow)"
else
    t_fail "B: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi

# C: deny -> refused + audited deny
run_scenario C deny '' motor '{}'
if [ ! -e "$MARKER" ] && grep -q '"decision":"deny"' "$AUDIT" 2>/dev/null; then
    t_ok "C: deny posture refused the motor (audited deny)"
else
    t_fail "C: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi

# D: allowlist under ask -> runs unattended (the E-stop guarantee)
run_scenario D ask '["motor"]' motor '{}'
if [ -e "$MARKER" ] && grep -q '"decision":"allowlist"' "$AUDIT" 2>/dev/null
then
    t_ok "D: allowlisted motor ran unattended (the E-stop survives)"
else
    t_fail "D: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi

# E: shell bypass -> shadow-matched, refused pre-shell, launcher shell
run_scenario E ask '' run_terminal_command "{\"command\":\"$motor 1 1\"}"
if [ ! -e "$MARKER" ] \
   && grep '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null \
      | grep -q '"launcher":"shell"'; then
    t_ok "E: the shell bypass was caught pre-shell (launcher shell)"
else
    t_fail "E: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi

# F: allowlist chain-evasion -> chaining is NOT allowlist-matched
run_scenario F ask "[\"$motor\"]" run_terminal_command \
    "{\"command\":\"$motor ; echo x\"}"
if [ ! -e "$MARKER" ] \
   && grep -q '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null; then
    t_ok "F: the chained command was not allowlisted"
else
    t_fail "F: moved=$([ -e "$MARKER" ] && echo yes || echo no)"
fi

# --- G-I: the INTERACTIVE ask prompt, in both render modes -----------------
# `cb->confirm_kinetic` is reached only with posture `ask` AND no `--auto`, so
# nothing above touches it. Answered `n`, so the motor never moves -- and the
# marker is checked, which is a stronger statement than the audit alone.
#
# The kinetic prompt fires BEFORE the ordinary tool approval. That was measured
# on its twin in privileged.sh, where assuming the opposite made a driver answer
# `y` to a sudo escalation and still go green on two of three checks.
for mode in def acc; do
    flag=""
    [ "$mode" = acc ] && flag="--accessible"
    _cap="$tmp/i_$mode"; mkdir -p "$_cap"
    HOME=$(smoke_tmp); export HOME
    KMARK="$HOME/moved-interactive"
    cat > "$_cap/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool motor {}
rule
  text i was denied
MM
    mm_start "$_cap/replies.mm" "$_cap"
    cat > "$_cap/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"mock","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "kineticCommands":"ask",
 "tools":[{"name":"motor","description":"drive the motor",
           "kinetic":true,"command":"$motor",
           "env":{"MARKER":"$KMARK"}}]}
EOF
    printf 'delay 1200\nsend "move it\\r"\nexpect "physical actuation" 25\ndelay 500\nsend "n"\ndelay 1500\nsend "/exit\\r"\nwaitexit 20\n' > "$_cap/run.pd"
    (cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
        --deadline 65 --cols 100 --log "$tmp/k_$mode.log" "$_cap/run.pd" -- \
        "$BIN" --config "$_cap/config.json" --no-session $flag \
        > /dev/null 2>&1)
    mm_stop
    eval "KM_$mode=\$KMARK"
done

# --- 9: both interactive runs reached the prompt AND nothing moved ----------
# The denominator, plus the thing that actually matters. `kinetic: denied` is
# printed only by confirm_echo on this prompt's deny branch -- the unattended
# path records `unattended_refused` to the audit and prints nothing -- and the
# mock's own reply text cannot produce it.
# M553: mode-dependent echo -- sighted keeps `kinetic: denied`, accessible reads
# "The physical actuation was denied." Asserted per arm.
if grep -q 'kinetic: denied' "$tmp/k_def.log" \
   && grep -q 'The physical actuation was denied' "$tmp/k_acc.log" \
   && [ ! -e "$KM_def" ] && [ ! -e "$KM_acc" ]; then
    t_ok "the interactive kinetic prompt was reached, denied, and nothing moved"
else
    t_fail "the kinetic prompt was never answered, or the motor RAN \
(def_moved=$([ -e "$KM_def" ] && echo yes || echo no) \
acc_moved=$([ -e "$KM_acc" ] && echo yes || echo no)) -- checks 10-11 would \
pass on nothing: $(tail -4 "$tmp/k_acc.log" 2>/dev/null | tr '\\n' ' ' | head_bytes 200)"
fi

# --- 10: accessible mode names the keys as WORDS ---------------------------
kline=$(grep 'physical actuation' "$tmp/k_acc.log" 2>/dev/null | head -1)
ok=1
case "$kline" in *'[y]'*|*'[n]'*) ok=0 ;; esac
case "$kline" in *' y '*) ;; *) ok=0 ;; esac
case "$kline" in *' n '*) ;; *) ok=0 ;; esac
if [ -n "$kline" ] && [ "$ok" -eq 1 ]; then
    t_ok "the actuation prompt is bracket-free in accessible mode, offers y/n"
else
    t_fail "the actuation prompt still spells its keys out: [$kline]"
fi

# --- 11: default mode kept the bracket form (the control) -----------------
kdef=$(grep 'physical actuation' "$tmp/k_def.log" 2>/dev/null | head -1)
if printf '%s' "$kdef" | grep -q '\[y\]es' \
   && printf '%s' "$kdef" | grep -q '\[n\]o'; then
    t_ok "the sighted actuation prompt keeps its bracket key list"
else
    t_fail "the sighted prompt lost its key list -- check 10 would pass for a \
prompt that offers nothing: [$kdef]"
fi

t_done
