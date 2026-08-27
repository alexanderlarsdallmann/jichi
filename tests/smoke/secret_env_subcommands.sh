#!/bin/sh
# smoke: the API key is scrubbed from EVERY child jichi forks, including the
# ones a subcommand forks before the agent loop exists (M608).
#
# M130 scrubs the configured key variable from a child's environment, and a
# built-in list drops the well-known provider names even when unconfigured ("so
# a stray export in the parent's shell can't leak either"). Two holes, one
# shape: a correct mitigation whose scope excluded the highest-value asset.
#
#   1. The registry is armed AFTER the subcommand dispatch chain in main(), so
#      `brief-check --verify CMD` -- which forks the gate once, before any model
#      call -- ran CMD with the configured key variable intact. (M444 fixed the
#      same ordering for the envelope's own arming; this is the sibling.)
#   2. The built-in list names thirteen third-party variables and neither of
#      jichi's own -- JICHI_API_KEY, which the wizard and the scaffolder write,
#      nor JLU_API_KEY, the HRZ onboarding name. A stray export of either was
#      passed to every shell tool, hook, verifier and MCP server.
#
# Both probes print the COUNT of the variable in the child's environment to a
# file the driver reads; a control shows the scrub working where it always did.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'Make the tests pass.\n' > "$ws/brief.md"

# --- 1: brief-check's verifier must not see the configured key -------------------
cat > "$tmp/c.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKeyEnv":"JICHI_API_KEY","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
CFG
(cd "$ws" && with_deadline 60 env JICHI_API_KEY=canary-1 "$BIN" --config "$tmp/c.json" \
    brief-check brief.md --verify "env | grep -c -e '^JICHI_API_KEY=' > '$tmp/n1'; exit 0" \
    < /dev/null > /dev/null 2>&1)
n1=$(cat "$tmp/n1" 2>/dev/null || echo unread)
if [ "$n1" = "0" ]; then
    t_ok "brief-check's verifier ran without the configured key variable"
else
    t_fail "brief-check's verifier saw JICHI_API_KEY (count=$n1) -- forked before arming"
fi

# --- 2: a stray export of jichi's OWN key names is dropped, unconfigured ----------
# The config names a THIRD variable, so neither JICHI_API_KEY nor JLU_API_KEY is
# registered from config; only the built-in list can drop them.
cat > "$tmp/m.mm" <<MM
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '^JLU_API_KEY=' -e '^JICHI_API_KEY=' > '$tmp/n2'; exit 0"}
rule
  text SCRUB_DONE
MM
mm_start "$tmp/m.mm" "$tmp/cap2" 2
cat > "$tmp/c2.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKeyEnv":"MOCK_KEY","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
"lowResource":false,"maxRetries":0}
CFG
out=$(cd "$ws" && with_deadline 60 env MOCK_KEY=x JLU_API_KEY=canary-2 JICHI_API_KEY=canary-3 \
    "$BIN" --config "$tmp/c2.json" -q --no-session --auto \
    -p "check the env" < /dev/null 2>/dev/null); rc=$?
mm_stop
n2=$(cat "$tmp/n2" 2>/dev/null || echo unread)
if [ "$n2" = "0" ]; then
    t_ok "a stray JLU_API_KEY / JICHI_API_KEY export is dropped from the shell tool's child"
else
    t_fail "the shell tool's child saw jichi's own key names (count=$n2, rc=$rc)"
fi

# --- 3: the control -- the CONFIGURED name is scrubbed from the shell tool ---------
case "$out" in
    *SCRUB_DONE*) ;;
    *) t_fail "control turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)"; t_done; exit 0 ;;
esac
cat > "$tmp/m3.mm" <<MM
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '^MOCK_KEY=' > '$tmp/n3'; exit 0"}
rule
  text SCRUB_DONE
MM
mm_start "$tmp/m3.mm" "$tmp/cap3" 2
sed "s|127.0.0.1:[0-9]*/v1|127.0.0.1:$MM_PORT/v1|" "$tmp/c2.json" > "$tmp/c3.json"
(cd "$ws" && with_deadline 60 env MOCK_KEY=x "$BIN" --config "$tmp/c3.json" -q --no-session \
    --auto -p "check the env" < /dev/null > /dev/null 2>&1)
mm_stop
n3=$(cat "$tmp/n3" 2>/dev/null || echo unread)
if [ "$n3" = "0" ]; then
    t_ok "control: the configured key variable is scrubbed from the shell tool's child"
else
    t_fail "control failed: the configured MOCK_KEY reached the child (count=$n3)"
fi
t_done
