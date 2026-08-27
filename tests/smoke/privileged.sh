#!/bin/sh
# smoke: the privileged-command posture (M153) + always-on audit (M154).
# The incident: an unattended agent ran `sudo apt-get upgrade`. Under the
# default posture an UNATTENDED --auto run must REFUSE the sudo command
# before it reaches the shell, and the audit log must record the attempt
# (full command, launcher, 0600 perms) even with telemetry off; under
# `privilegedCommands: allow` a harmless `sudo -n true` runs and is
# audited as allow.
# M551: checks 6-8 add the INTERACTIVE arm, which nothing here reached. Every
# check above runs headless `--auto`, so the prompt a human answers -- the whole
# point of the default `ask` posture -- was rendered by no test in the tree. It
# turned out to be reading `[y]es  [n]o` aloud, which a screen reader announces
# as "bracket y bracket e s bracket n bracket o": the accessible-mode defect
# found by ear in the manual protocol and fixed as M551, on the prompt that
# guards a sudo. See docs/analysis/2026-08-22-screen-reader-audit.md.
# (Port of tests/e2e/privileged.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 8
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The refuse path never executes its command, so it can carry the scary
# incident string; the allow path DOES run, so it must be harmless.
SUDO_REFUSE="sudo apt-get update && sudo apt-get upgrade -y"

run_posture() { # run_posture POSTURE CMD CAPDIR  (fresh HOME each time)
    HOME=$(smoke_tmp)
    export HOME
    mkdir -p "$3"
    cat > "$3/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"$2"}
rule
  text done
EOF
    mm_start "$3/replies.mm" "$3"
    write_config "$3/config.json" "$MM_PORT" "\"privilegedCommands\":\"$1\""
    (cd "$ws" && with_deadline 60 "$BIN" --config "$3/config.json" \
        -q --no-session --auto -p "update the system" \
        < /dev/null > /dev/null 2>&1)
    mm_stop
    AUDIT="$HOME/.jichi.d/audit/privileged.jsonl"
}

# --- (1)+(2) default-ask, unattended: refused + audited ---------------------
run_posture ask "$SUDO_REFUSE" "$tmp/cap1"
if grep -q '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null; then
    t_ok "the unattended sudo was refused and audited"
else
    t_fail "no unattended_refused audit record"
fi
if grep '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null \
   | grep -q "sudo apt-get update && sudo apt-get upgrade -y" \
   && grep '"decision":"unattended_refused"' "$AUDIT" 2>/dev/null \
   | grep -q '"launcher":"sudo"'; then
    t_ok "the audit record carries the full command + launcher"
else
    t_fail "audit record incomplete"
fi
if [ "$(ls -l "$AUDIT" 2>/dev/null | cut -c1-10)" = "-rw-------" ]; then
    t_ok "the audit file is 0600 (owner-only)"
else
    t_fail "audit perms: $(ls -l "$AUDIT" 2>/dev/null | cut -c1-10)"
fi
if [ ! -e "$ws/should-never-matter" ]; then
    :   # (placeholder guard; the refusal is proven via the audit above)
fi

# --- (3) allow posture: the harmless command runs + audited allow ------------
run_posture allow "sudo -n true" "$tmp/cap2"
if grep -q '"decision":"allow"' "$AUDIT" 2>/dev/null; then
    t_ok "privilegedCommands:allow audits an allow decision"
else
    t_fail "no allow audit record"
fi
if [ -f "$tmp/cap2/req.2" ]; then
    t_ok "the allowed command's result went back to the model"
else
    t_fail "no second model request on the allow run"
fi

# --- (6)-(8) the INTERACTIVE ask prompt, in both render modes ---------------
# Nothing above reaches this path: `cb->confirm_privileged` is called only when
# the posture is `ask` AND the run is not `--auto`, so every check above skips
# it. Driven through a real PTY.
#
# SAFE BY CONSTRUCTION, and worth being explicit about in a driver that types
# the word sudo: the command is `sudo -n true` (harmless even if it ran) and the
# PRIVILEGED prompt itself is answered `n`, so the escalation is refused and
# nothing is executed.
#
# THE PROMPT ORDER, measured rather than assumed. The first draft of this driver
# answered `y` then `n`, on the reading that jc_agent.c's "evaluated BELOW the
# verdict" meant the ordinary tool approval prompts first. It does not: the
# PRIVILEGED prompt comes first, so that `y` GRANTED the escalation and the
# refusal came from the tool prompt afterwards. Check 6 caught it -- checks 7
# and 8 were green, because the prompt had rendered correctly; only the
# denominator could see that it had been ANSWERED wrong. A driver that grants
# privilege in order to test refusing it is a bad driver even when it passes.
HOME=$(smoke_tmp); export HOME
cat > "$tmp/ask.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sudo -n true"}
rule
  text i was denied
EOF
cat > "$tmp/ask.pd" <<'EOF'
delay 1200
send "run it\r"
expect "elevated privilege" 25
delay 500
send "n"
delay 1500
send "/exit\r"
waitexit 20
EOF
for mode in def acc; do
    flag=""
    [ "$mode" = acc ] && flag="--accessible"
    mm_start "$tmp/ask.mm" "$tmp/cap_$mode"
    write_config "$tmp/config_$mode.json" "$MM_PORT" '"privilegedCommands":"ask"'
    (cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
        --deadline 65 --cols 100 --log "$tmp/i_$mode.log" "$tmp/ask.pd" -- \
        "$BIN" --config "$tmp/config_$mode.json" --no-session $flag \
        > /dev/null 2>&1)
    mm_stop
done

# --- 6: both interactive runs actually REACHED the privileged prompt --------
# The denominator, and it must name something only THIS prompt produces.
# `confirm_echo` prints `privileged: denied` on the deny branch; the unattended
# path above never prints it (it records `unattended_refused` to the audit), and
# the mock's own reply text cannot produce it.
# M553 made the echo mode-dependent: the sighted arm keeps `privileged: denied`
# and the accessible arm reads "The privileged command was denied." -- prose,
# because a colon between two words is a symbol a reader must speak. Asserted
# per arm, which also proves each rendering rather than a shared substring.
if grep -q 'privileged: denied' "$tmp/i_def.log" \
   && grep -q 'The privileged command was denied' "$tmp/i_acc.log"; then
    t_ok "the interactive privileged prompt was reached and denied in both modes"
else
    t_fail "the privileged prompt was never answered -- checks 7-8 would pass \
on nothing: $(tail -4 "$tmp/i_acc.log" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# --- 7: accessible mode names the keys as WORDS, not brackets ---------------
pline=$(grep 'elevated privilege' "$tmp/i_acc.log" 2>/dev/null | head -1)
ok=1
case "$pline" in *'[y]'*|*'[n]'*) ok=0 ;; esac
case "$pline" in *' y '*) ;; *) ok=0 ;; esac
case "$pline" in *' n '*) ;; *) ok=0 ;; esac
if [ -n "$pline" ] && [ "$ok" -eq 1 ]; then
    t_ok "the sudo prompt is bracket-free in accessible mode and offers y/n"
else
    t_fail "the sudo prompt still spells its keys out: [$pline]"
fi

# --- 8: default mode kept the bracket form (the control) -------------------
# Without this, deleting the key list entirely would turn check 7 green.
dline=$(grep 'elevated privilege' "$tmp/i_def.log" 2>/dev/null | head -1)
if printf '%s' "$dline" | grep -q '\[y\]es' \
   && printf '%s' "$dline" | grep -q '\[n\]o'; then
    t_ok "the sighted sudo prompt keeps its bracket key list"
else
    t_fail "the sighted prompt lost its key list -- check 7 would pass for a \
prompt that offers nothing: [$dline]"
fi

t_done
