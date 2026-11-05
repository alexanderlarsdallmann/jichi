#!/bin/sh
# smoke: `daemon` + `--control` is refused at startup (SV:2118, M1103-b).
#
# THE DEFECT. main() opens the mid-run control socket BEFORE `--daemon` is
# dispatched to run_daemon. The daemon's parent never services app->control;
# it forks one worker per PROMPT, and each worker runs a turn at agent_depth 0
# holding the inherited listening fd. AFTER M1103 (commit 9ca87561) the two
# service points answer only when getpid() matches the serve_pid recorded at
# jc_control_open -- so NOTHING ever answers the socket a daemon opened: the
# worker's pid != serve_pid, and the parent never polls it. A supervisor that
# starts a daemon with --control (or with `control` on in its config) would
# bind a socket nobody answers and silently see a dead channel, because no
# driver combined the two and neither docs page documents the combination.
#
# THE FIX (M1103-b): refuse at startup -- exit 2, the M644/M921 refusal
# idiom -- when a control socket was ACTUALLY opened (the control_open
# variable: the flag or the config key, AND jc_control_open succeeded). On a
# NO_SOCKETS build jc_control_open fails, control_open stays 0, and
# run_daemon already refuses with its own clear line; this check must not
# double-refuse there (it is guarded on control_open, not on the flag).
#
# RED FIRST: on the unfixed tree check 1's daemon opens the control socket,
# prints "[control] listening on ...", keeps serving and lives until its
# with_deadline 20 bound kills it -- rc is the deadline's 124, not 2. That is
# the red: the process did not DIE with rc 2, and it DID keep serving.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# The control socket path must fit AF_UNIX (~107 bytes): smoke_tmp can be
# long, so use a short private dir of our own (daemon_auth's measure).
short="/tmp/jdcctl.$$"
mkdir -p "$short" && chmod 0700 "$short"
ctlsock="$short/c.sock"
dsock="$short/d.sock"

# A daemon needs no model to LISTEN (workers fail their first model call);
# the standard throwaway config keeps the run offline and bounded.
cat > "$tmp/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"e2e","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:9/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "timeouts":{"connect":2,"request":5,"stall":5}}
EOF

# --- 1: `daemon --control` is refused at startup (rc 2, the combo named) -----
# with_deadline bounds the refusal to 20 s: the fixed daemon must DIE with rc 2
# before the bound, while the unfixed daemon keeps serving until the bound
# kills it (rc 124). rc 2 is therefore a real death, not a deadline.
with_deadline 20 "$BIN" --config "$tmp/config.json" \
    daemon --socket "$dsock" --control "$ctlsock" \
    < /dev/null > "$tmp/o1" 2>"$tmp/e1"
rc1=$?
if [ "$rc1" -eq 2 ] && grep -q "daemon" "$tmp/e1" && grep -q "control" "$tmp/e1"; then
    t_ok "daemon + --control is refused at startup (rc=2, the combination named)"
else
    t_fail "daemon + --control: rc=$rc1 (want 2); stderr: $(head_bytes 240 "$tmp/e1" 2>/dev/null)"
fi

# --- 2: the config path is refused too (`control` on, no --control flag) ----
# Pins that the check is on the opened socket (control_open), not on the flag:
# a supervisor's own config can open the channel. `control` is the config key
# parsed into app.config.control_on (src/config/jc_config.c): boolean true
# means the default path, a string means a socket path.
cat > "$tmp/config_ctl.json" <<'EOF'
{"lowResource":false,"models":[{"name":"e2e","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:9/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"control":true,
 "timeouts":{"connect":2,"request":5,"stall":5}}
EOF
# The config key sets control_on; the --control PATH is given here (the
# config's "true" means the DEFAULT path), because the default
# $HOME/.jichi.d/control/<pid>.sock with this tier's HOME exceeds AF_UNIX's
# ~107-byte bound and jc_control_open would then fail -- the guard is on the
# OPENED socket (control_open), so a short explicit path exercises the same
# arm.
with_deadline 20 "$BIN" --config "$tmp/config_ctl.json" \
    daemon --socket "$dsock" --control "$ctlsock" \
    < /dev/null > "$tmp/o2" 2>"$tmp/e2"
rc2=$?
if [ "$rc2" -eq 2 ] && grep -q "daemon" "$tmp/e2" && grep -q "control" "$tmp/e2"; then
    t_ok "a config with control on is refused too (rc=2, the combination named)"
else
    t_fail "daemon + config control: rc=$rc2 (want 2); stderr: $(head_bytes 240 "$tmp/e2" 2>/dev/null)"
fi

# --- 3: positive control -- `daemon` WITHOUT control still starts -----------
# The refusal is for the COMBINATION, not for daemon. Poll (bounded, 15 x 1 s
# -- fractional sleep is not POSIX) for the daemon's OWN listening socket to
# appear, then stop it cleanly with a shutdown request (the daemon.sh shape).
(cd "$tmp" && exec "$BIN" --config "$tmp/config.json" daemon --socket "$dsock" \
    < /dev/null > "$tmp/o3" 2>"$tmp/e3") &
pid3=$!
i=0
while [ ! -S "$dsock" ]; do
    kill -0 "$pid3" 2>/dev/null || break
    i=$((i + 1)); [ $i -gt 15 ] && break
    sleep 1
done
if [ -S "$dsock" ]; then
    t_ok "daemon without control still starts (its listening socket appears)"
    printf '{"type":"shutdown"}\n' | "$SMOKE_TOOLS/sockq" --deadline 15 \
        "$dsock" >/dev/null 2>&1
    i=0
    while kill -0 "$pid3" 2>/dev/null; do
        i=$((i + 1)); [ $i -gt 10 ] && { kill "$pid3" 2>/dev/null; break; }
        sleep 1
    done
    wait "$pid3" 2>/dev/null
else
    t_fail "daemon without control never started: $(head_bytes 240 "$tmp/e3" 2>/dev/null)"
    kill "$pid3" 2>/dev/null
    wait "$pid3" 2>/dev/null
fi

rm -rf "$short"
t_done
