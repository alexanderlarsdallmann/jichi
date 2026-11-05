#!/bin/sh
# smoke: the chatbot example's configuration holds the posture its guide promises
# (M840, docs/CHATBOT.md, examples/chatbot/).
#
# WHY. The guide tells a reader to put jichi behind a website with
# examples/chatbot/config.json, where every message comes from a stranger. Its
# safety rests on four settings, and each of them was measured to matter:
#   * "pathFence": true -- the fence's default, auto, is ON only under --auto, so
#     in chat mode a model could otherwise read any file the service can read;
#   * --readonly (which examples/chatbot/server.py passes) -- no tool that changes
#     anything is offered or run;
#   * "toolProfile": "core" -- with --readonly the model is offered read_file,
#     list_files and search_code and nothing else;
#   * the deny list -- the second layer, for a profile switched to full: the
#     read-only tools a stranger should not reach: fetch_url
#     (it fetches arbitrary pages on a visitor's behalf), the git tools (a
#     repository's history names the people who wrote it), ask_user, the to-do list;
#   * no rules from above the knowledge folder -- jichi's rules walk climbs to the
#     nearest .git, and from this repository it collected jichi's own CLAUDE.md into
#     the bot's system prompt (measured while writing the guide).
# A guide's claims rot silently; this driver runs the SHIPPED files.
#
# HOW. The mock model scripts the calls a hostile visitor would try to cause: read a
# file outside the workspace, write a file, write a plan, run a shell command, fetch
# a URL, read the git history; then one legitimate read, then the answer; then a
# second turn that continues the session. Three CONTROL runs prove the refusals come
# from the settings and not from the mock: with the fence off the read succeeds,
# with the full profile and no deny list fetch_url and git_log run, and without the
# knowledge folder's own .git the rules above it arrive.
. "$(dirname "$0")/_smoke.sh"

t_plan 15
smoke_home
if ! command -v git > /dev/null 2>&1; then
    t_skip_all "git is not installed (the example's knowledge folder is a git repository)"
fi
tmp=$(smoke_tmp)
other=$(smoke_tmp)
cp -r "$SMOKE_ROOT/examples/chatbot" "$tmp/bot"
ws="$tmp/bot/knowledge"
git init -q "$ws"
# Rules one directory ABOVE the knowledge folder: the walk must stop before them.
printf '# PARENT_RULES_MARKER\n' > "$tmp/bot/CLAUDE.md"
printf 'SECRET_MARKER_7c1e\n' > "$other/secret.txt"
tree() { (cd "$ws" && find . -path ./.git -prune -o -type f -print | sort | tr '\n' ' '); }
before=$(tree)

# run_bot CONFIG OUT [ARGS...] -- one headless turn, as examples/chatbot/server.py runs it
run_bot() {
    _cfg=$1; _out=$2; shift 2
    (cd "$ws" && with_deadline 60 "$BIN" --config "$_cfg" --readonly --output json -q \
        --deadline 60s --max-tool-calls 12 --lease off "$@" < /dev/null \
        > "$_out" 2> "$_out.err")
}
# The server always sends "/ask <message>": the knowledge folder's own command,
# which runs the turn as the helpdesk profile (examples/chatbot/server.py).
b64() { printf '/ask %s' "$1" | base64 | tr -d '\n'; }

cat > "$tmp/replies.mm" <<MM
wire openai
rule
  count 1
  tool read_file {"path":"$other/secret.txt"}
rule
  count 2
  tool write_file {"path":"graffiti.txt","content":"no"}
rule
  count 3
  tool write_plan {"title":"t","plan":"no"}
rule
  count 4
  tool run_terminal_command {"command":"echo PWNED > pwned.txt"}
rule
  count 5
  tool fetch_url {"url":"http://127.0.0.1:9/x"}
rule
  count 6
  tool git_log {}
rule
  count 7
  tool read_file {"path":"opening-hours.md"}
rule
  count 8
  text ANSWER_OK
rule
  count 9
  text SECOND_OK
MM
mm_start "$tmp/replies.mm" "$tmp/cap" 9
sed "s#http://127.0.0.1:1234/v1#http://127.0.0.1:$MM_PORT/v1#" "$tmp/bot/config.json" > "$tmp/cfg.json"
run_bot "$tmp/cfg.json" "$tmp/out1.json" --prompt-b64 "$(b64 'When is the library open?')"; rc1=$?
sid=$(sed -n 's/.*"session_id":"\([^"]*\)".*/\1/p' "$tmp/out1.json" | head -n 1)
run_bot "$tmp/cfg.json" "$tmp/out2.json" --session "$sid" --prompt-b64 "$(b64 'And on Saturday?')"; rc2=$?
mm_stop
denied="Tool call denied by policy"

# 1: the bot's persona and rules, and only those, reach the model
if grep -q 'You are the help desk of the Riverside Community Library' "$tmp/cap/req.1" 2>/dev/null &&
   ! grep -q 'a command-line AI coding agent' "$tmp/cap/req.1" &&
   grep -q 'Help desk instructions' "$tmp/cap/req.1" &&
   ! grep -q 'PARENT_RULES_MARKER' "$tmp/cap/req.1"; then
    t_ok "the system prompt is the helpdesk persona (not the coding agent) with the folder's AGENTS.md, and no rules from above it"
else
    t_fail "prompt: persona $(grep -c 'You are the help desk' "$tmp/cap/req.1" 2>/dev/null), coding persona $(grep -c 'a command-line AI coding agent' "$tmp/cap/req.1" 2>/dev/null), rules $(grep -c 'Help desk instructions' "$tmp/cap/req.1" 2>/dev/null), parent $(grep -c 'PARENT_RULES_MARKER' "$tmp/cap/req.1" 2>/dev/null)"
fi
# 2: a read outside the workspace is refused, and the secret never reaches the model
if ! grep -q SECRET_MARKER "$tmp/cap/req.2" "$tmp/cap/req.8" 2>/dev/null &&
   grep -q 'refused by safety fence' "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "a read outside the knowledge folder is refused by the path fence"
else
    t_fail "the outside read was not refused: $(grep -o 'SECRET_MARKER[^"]*' "$tmp/cap/req.2" 2>/dev/null | head -n 1)"
fi
# 3-7: the five hostile calls are refused, and nothing is written
if grep -q -e "$denied" -e "read-only" "$tmp/cap/req.3" 2>/dev/null && [ ! -e "$ws/graffiti.txt" ]; then
    t_ok "write_file is refused on a --readonly run, and nothing is written"
else
    t_fail "write_file was not refused (graffiti.txt: $(ls "$ws/graffiti.txt" 2>&1))"
fi
if grep -q -e "$denied" -e "read-only" "$tmp/cap/req.4" 2>/dev/null; then
    t_ok "write_plan is refused (by --readonly, and by the deny list behind it)"
else
    t_fail "write_plan was not refused: $(tr -d '\n' < "$tmp/cap/req.4" | grep -o '"role":"tool"[^}]*' | tail -n 1 | head_bytes 200)"
fi
if grep -q -e "$denied" -e "read-only" "$tmp/cap/req.5" 2>/dev/null && [ ! -e "$ws/pwned.txt" ]; then
    t_ok "a shell command is refused, and it did not run"
else
    t_fail "the shell command was not refused (pwned.txt: $(ls "$ws/pwned.txt" 2>&1))"
fi
unavail="is not available to this agent"
if grep -q -e "$denied" -e "$unavail" "$tmp/cap/req.6" 2>/dev/null; then
    t_ok "fetch_url is refused (outside the core profile, and on the deny list behind it)"
else
    t_fail "fetch_url was not refused"
fi
if grep -q -e "$denied" -e "$unavail" "$tmp/cap/req.7" 2>/dev/null; then
    t_ok "git_log is refused (outside the core profile, and on the deny list behind it)"
else
    t_fail "git_log was not refused"
fi
# the tools the model is offered at all: exactly read, list and search
offered=$(tr -d '\n' < "$tmp/cap/req.1" 2>/dev/null | grep -o '"name":"[a-z_]*"' | sort -u | tr '\n' ' ')
if [ "$offered" = '"name":"list_files" "name":"read_file" "name":"search_code" ' ]; then
    t_ok "the model is offered exactly read_file, list_files and search_code"
else
    t_fail "offered tools: $offered"
fi
# 8: the legitimate read works, so the refusals above are not a broken tool loop
if grep -q '09:00 to 19:00' "$tmp/cap/req.8" 2>/dev/null; then
    t_ok "a read inside the knowledge folder returns the document"
else
    t_fail "the in-folder read did not return opening-hours.md"
fi
# 9: the answer and its session reach stdout as the stable JSON object
if [ "$rc1" -eq 0 ] && grep -q '"text":"ANSWER_OK"' "$tmp/out1.json" && [ -n "$sid" ]; then
    t_ok "the answer and its session_id arrive as --output json (rc 0)"
else
    t_fail "run 1: rc=$rc1, out: $(head_bytes 200 < "$tmp/out1.json")"
fi
# 10: the second turn continues the conversation
if [ "$rc2" -eq 0 ] && grep -q ANSWER_OK "$tmp/cap/req.9" 2>/dev/null &&
   grep -q "\"session_id\":\"$sid\"" "$tmp/out2.json"; then
    t_ok "--session continues the conversation: turn 2 carries turn 1's answer"
else
    t_fail "run 2: rc=$rc2, history carried: $(grep -c ANSWER_OK "$tmp/cap/req.9" 2>/dev/null)"
fi
# 11: the workspace is exactly as it was
after=$(tree)
if [ "$before" = "$after" ]; then
    t_ok "the knowledge folder is unchanged after both turns ($after)"
else
    t_fail "the knowledge folder changed: before [$before] after [$after]"
fi

# 12: CONTROL -- with the fence off, the same read succeeds. It proves check 2 is
# the fence's work and not the mock's.
cat > "$tmp/c1.mm" <<MM
wire openai
rule
  count 1
  tool read_file {"path":"$other/secret.txt"}
rule
  count 2
  text C1_OK
MM
mm_start "$tmp/c1.mm" "$tmp/c1cap" 2
sed -e "s#http://127.0.0.1:1234/v1#http://127.0.0.1:$MM_PORT/v1#" \
    -e 's#"pathFence": true#"pathFence": false#' "$tmp/bot/config.json" > "$tmp/c1.json"
run_bot "$tmp/c1.json" "$tmp/c1.out" --no-session --prompt-b64 "$(b64 'read it')"
mm_stop
if grep -q '"pathFence": false' "$tmp/c1.json" && grep -q SECRET_MARKER "$tmp/c1cap/req.2" 2>/dev/null; then
    t_ok "control: with pathFence false the same read reaches the secret (so check 2 is the fence)"
else
    t_fail "control 1 could not show the read without the fence -- check 2 may be vacuous"
fi

# 13: CONTROL -- with "toolProfile": "full" and no deny list, fetch_url and
# git_log run. It proves the deny list refuses them when the profile would not.
cat > "$tmp/c2.mm" <<MM
wire openai
rule
  count 1
  tool fetch_url {"url":"http://127.0.0.1:9/x"}
rule
  count 2
  tool git_log {}
rule
  count 3
  text C2_OK
MM
mm_start "$tmp/c2.mm" "$tmp/c2cap" 3
awk '/"permissions"/ { skip = 1 } skip && /^  },?$/ { skip = 0; next } !skip' "$tmp/bot/config.json" |
    sed -e "s#http://127.0.0.1:1234/v1#http://127.0.0.1:$MM_PORT/v1#" \
        -e 's#"toolProfile": "core"#"toolProfile": "full"#' > "$tmp/c2.json"
run_bot "$tmp/c2.json" "$tmp/c2.out" --no-session --prompt-b64 "$(b64 'plan and fetch')"
mm_stop
if ! grep -q '"permissions"' "$tmp/c2.json" && grep -q '"toolProfile": "full"' "$tmp/c2.json" &&
   ! grep -q -e "$denied" -e "$unavail" "$tmp/c2cap/req.2" 2>/dev/null &&
   ! grep -q -e "$denied" -e "$unavail" "$tmp/c2cap/req.3" 2>/dev/null &&
   [ -s "$tmp/c2cap/req.3" ]; then
    t_ok "control: with the full profile and no deny list, fetch_url and git_log run (the deny list is the second layer)"
else
    t_fail "control 2: permissions still present, or a call was refused without the deny list"
fi

# 14: CONTROL -- without its own .git the knowledge folder inherits the rules
# above it. It proves check 1 is the .git marker's work.
cat > "$tmp/c3.mm" <<MM
wire openai
rule
  text C3_OK
MM
mm_start "$tmp/c3.mm" "$tmp/c3cap" 1
sed "s#http://127.0.0.1:1234/v1#http://127.0.0.1:$MM_PORT/v1#" "$tmp/bot/config.json" > "$tmp/c3.json"
mv "$ws/.git" "$tmp/git.saved"
run_bot "$tmp/c3.json" "$tmp/c3.out" --no-session --prompt-b64 "$(b64 'hello')"
mv "$tmp/git.saved" "$ws/.git"
mm_stop
if grep -q 'PARENT_RULES_MARKER' "$tmp/c3cap/req.1" 2>/dev/null; then
    t_ok "control: without its own .git the folder inherits the rules above it (so check 1 is the marker)"
else
    t_fail "control 3 could not show the rules walk climbing -- check 1 may be vacuous"
fi
t_done
