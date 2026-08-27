#!/bin/sh
# smoke: the wizard's optional key store -- no echo, mode 600, and a start
# script that actually loads it (M326e).
#
# `setup` used to end by telling the user to do the one step it never helped
# with: it wrote a config naming an environment variable, and a start script
# that never loaded one. The advice ("export it in ~/.bashrc") silently does
# nothing for a cron or systemd run, because the stock ~/.bashrc returns before
# reaching it when the shell is not interactive.
#
# THE NO-ECHO CHECK IS THE LOAD-BEARING ONE, and it needs a PTY: echo is a
# terminal attribute, so a piped run would "pass" while proving nothing. The
# whole point of this prompt is that it is the only one in the wizard that takes
# a key, so it is the only one that must not display what is typed.
. "$(dirname "$0")/_smoke.sh"

t_plan 28
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

KEY='sk-PTYSECRET-987654'

cat > "$tmp/drive.txt" <<EOF
expect "What are you working on?" 30
delay 400
send "5\r"
delay 500
expect "What are you walking into?" 30
delay 400
send "\r"
delay 500
expect "What are you running on?" 30
delay 400
send "\r"
delay 500
expect "How are you working?" 30
delay 400
send "\r"
delay 400
send "2\r"
delay 400
send "gpt-4o\r"
delay 500
expect "name of the env var holding your API key" 30
delay 400
send "JICHI_API_KEY\r"
delay 600
expect "store the key there now?" 30
delay 400
send "y\r"
delay 500
expect "paste your API key (not shown)" 30
delay 400
send "$KEY\r"
delay 800
expect "mode 600" 30
delay 500
send "\r"
delay 500
expect "Accept this role's default features?" 30
delay 400
send "n\r"
delay 500
expect "Nothing below is required" 30
delay 400
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
send "\r"
delay 250
expect "Next steps" 40
delay 500
EOF

# Env hygiene is the caller's job (ptydrive's header says so); _smoke.sh already
# exports NO_COLOR/LC_ALL, and TERM is set inside the subshell -- a POSIX sh
# cannot prefix a subshell with assignments.
(cd "$ws" && with_deadline 120 env TERM=dumb "$SMOKE_TOOLS/ptydrive" \
    --log "$tmp/pty.log" "$tmp/drive.txt" -- "$BIN" --no-lite setup) >/dev/null 2>&1
rc=$?

if [ "$rc" -eq 0 ]; then
    t_ok "the wizard offered to store the key and reported writing it"
else
    t_fail "PTY drive failed (rc=$rc): $(tail -c 200 "$tmp/pty.log" 2>/dev/null)"
fi

# The point of the exercise: a PTY echoes by default, so this is only true if
# the prompt cleared ECHO.
if [ -f "$tmp/pty.log" ] && ! grep -q "PTYSECRET" "$tmp/pty.log"; then
    t_ok "the key was never echoed to the terminal"
else
    t_fail "the key appeared in the terminal transcript"
fi

keyfile="$HOME/.jichi.env"
if [ -f "$keyfile" ] && grep -q "export JICHI_API_KEY='$KEY'" "$keyfile"; then
    t_ok "the key was written to ~/.jichi.env in loadable form"
else
    t_fail "key file missing or malformed: $(cat "$keyfile" 2>/dev/null | head_bytes 120)"
fi

# Owner-only. A key file anyone on the box can read is not a key file.
mode=$(ls -l "$keyfile" 2>/dev/null | cut -c1-10)
case "$mode" in
    -rw-------) t_ok "the key file is mode 600 ($mode)" ;;
    *)          t_fail "key file mode is $mode, want -rw-------" ;;
esac

# --- the loop must close: a generated script has to LOAD what setup stored ----
ws2=$(smoke_tmp)
(cd "$ws2" && with_deadline 60 "$BIN" --no-lite setup --non-interactive --preset generic \
    --provider openai --model gpt-4o --key-env JICHI_API_KEY < /dev/null \
    > "$tmp/noint.out" 2>&1)

if [ -f "$ws2/run.sh" ] && grep -q '\.jichi\.env' "$ws2/run.sh"; then
    t_ok "the generated start script loads the key file"
else
    t_fail "start script does not load the key file: $(cat "$ws2/run.sh" 2>/dev/null | head_bytes 200)"
fi

# End to end, with the key NOT exported in this shell: the script must supply it.
out=$(cd "$ws2" && with_deadline 60 env JICHI="$BIN" ./run.sh doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q "API key present"; then
    t_ok "running through the script, jichi finds the stored key"
else
    t_fail "the key did not reach jichi: $(printf '%s' "$out" | grep -i 'api key' | head -1)"
fi

# --- M326f: the wizard must not nag about a key it just stored ---------------
# It used to, three times over: a "! API key: env-var not set yet" warning, an
# `export ...` next-step, and a closing "Tip: set your API key" -- all printed
# seconds after the user handed it the key. The variable genuinely is not set in
# THIS shell; it is set for the runs that matter, because the start script
# sources the file.
plain=$(smoke_plain "$tmp/pty.log" | tr -d '\r')

if printf '%s' "$plain" | grep -q "API key: stored"; then
    t_ok "the checklist reports the key as stored, not missing"
else
    # Report the CHECKLIST line, not any line mentioning a key: the transcript
    # contains one prompt redraw per keystroke, and grepping loosely dumps all
    # of them into the failure message.
    t_fail "still reports the key as missing: $(printf '%s' "$plain" | grep -E '^(ok|!|.) API key' | head -1)"
fi

# The nag, in any of its three forms, must be gone from a stored run.
if ! printf '%s' "$plain" | grep -q 'env-var not set yet' &&
   ! printf '%s' "$plain" | grep -q 'export JICHI_API_KEY=\.\.\.' &&
   ! printf '%s' "$plain" | grep -q 'Tip: set your API key'; then
    t_ok "no leftover advice to supply a key that is already supplied"
else
    t_fail "still nagging: $(printf '%s' "$plain" | grep -E 'not set yet|export JICHI_API_KEY|Tip: set' | head -1)"
fi

# --- M326f: every menu shows the default that Enter will take ----------------
# setup_menu hardcoded "choice: " and ignored the dflt it was passed, so three
# of the four menus took option 1 (or 2, for the provider) on Enter without ever
# saying so. A bare "choice: " anywhere means one regressed.
if ! printf '%s' "$plain" | grep -qE '^  choice: '; then
    t_ok "no menu prompts without showing its default"
else
    t_fail "a menu still hides its default: $(printf '%s' "$plain" | grep -E '^  choice: ' | head -1)"
fi

# --- M326f: when the key IS missing, advise the form that persists -----------
# The non-interactive path never offers to store one, so it exercises the
# key-absent branch. A bare `export` lasts one terminal, and in ~/.bashrc it is
# still not read by cron/systemd (that shell is not interactive).
if grep -q 'umask 077' "$tmp/noint.out" && grep -q '\.jichi\.env' "$tmp/noint.out"; then
    t_ok "with no key stored, the advice is the persistent form"
else
    t_fail "unhelpful key advice: $(grep -A2 'Next steps' "$tmp/noint.out" | head -3)"
fi

# --- M326j: the two questions are actually two questions -------------------
# The source called journeys "orthogonal to the roles" since M183 while the
# code applied exactly ONE preset, so the screen promised a composition that
# did not exist. M326i made that single list prettier; this asks separately.
if printf '%s' "$plain" | grep -q "What are you working on?" &&
   printf '%s' "$plain" | grep -q "What are you walking into?"; then
    t_ok "the wizard asks what you build and what you are doing separately"
else
    t_fail "a question is missing: $(printf '%s' "$plain" | grep -c 'are you')"
fi

# The role menu must NOT carry journeys any more -- that was the confusion.
if ! printf '%s' "$plain" | grep -q "13) small-project"; then
    t_ok "the role list no longer runs on into the journeys"
else
    t_fail "journeys are still numbered into the role list"
fi

# Skippable, and skipping reaches exactly pre-M326j behaviour.
if printf '%s' "$plain" | grep -q "0 = nothing in particular"; then
    t_ok "the journey is optional and defaults to none"
else
    t_fail "no skip default: $(printf '%s' "$plain" | grep -o 'choice \[.*\]' | tail -1)"
fi


# --- M326k: three axes, and guidance the user can act on --------------------
if printf '%s' "$plain" | grep -q "What are you running on?"; then
    t_ok "the machine axis is its own question"
else
    t_fail "no machine question; small-local is still masquerading as a role"
fi

# small-local answered "what hardware", not "who are you", and composed with
# every role -- so a learner on a 7B model was unsayable.
if ! printf '%s' "$plain" | grep -qE "^ +[0-9]+\) small-local" ||
   printf '%s' "$plain" | grep -A30 "What are you running on" | grep -q "small-local"; then
    t_ok "small-local is offered under the machine question"
else
    t_fail "small-local is still in the role list"
fi

# The convention is stated once, before the optional block.
if grep -q "Nothing below is required" "$tmp/noint.out" ||
   printf '%s' "$plain" | grep -q "Nothing below is required"; then
    t_ok "the optional block states the rules once"
else
    t_fail "no guidance before the optional questions"
fi

# --- M326m: stance is its own question, and defaults to learner -------------
# tester/reviewer were journeys wearing role names (they differ only in mode
# and start script); learner/instructor are a stance -- how you work, not what
# you build or what you are doing to it.
if printf '%s' "$plain" | grep -q "How are you working?"; then
    t_ok "stance is a fourth question"
else
    t_fail "no stance question; learner/instructor still masquerade as roles"
fi

# Self-learners first: Enter takes learner, and a professional opts out. The
# person least able to configure their way out of a bad default gets the
# default that helps them.
if printf '%s' "$plain" | grep -q "choice \[1 = learner\]" &&
   printf '%s' "$plain" | grep -q "professionally"; then
    t_ok "the stance defaults to learner, with an explicit way out"
else
    t_fail "stance default wrong: $(printf '%s' "$plain" | grep -o 'choice \[.*\]' | tail -1)"
fi

# --- M326n: the wizard teaches the file it is writing -----------------------
# Every option shows the config keys it writes, DERIVED from the preset's
# feature bitmask, so a learner has read the key before they would type it.
if printf '%s' "$plain" | grep -q 'sets: "snapshots"'; then
    t_ok "each option shows the config keys it writes"
else
    t_fail "no derived key line: $(printf '%s' "$plain" | grep -c 'sets:')"
fi

if printf '%s' "$plain" | grep -q "why:"; then
    t_ok "options carry a reason, not only a description"
else
    t_fail "no why line in any menu"
fi

# The framing: a helper, not the only way; nothing permanent.
if printf '%s' "$plain" | grep -q "a helper, not the only way" &&
   printf '%s' "$plain" | grep -q "write by hand in any editor"; then
    t_ok "the wizard says it is a helper and the file is yours to edit"
else
    t_fail "framing paragraph missing"
fi

# WIDTH. Five lines used to run to 118 columns, which wraps mid-word in a
# narrow terminal and is worst under screen magnification.
#
# PROMPT lines are excluded, and the reason is the capture rather than the
# output: a terminal redraws the whole prompt on every keystroke, and stripping
# CR to read the transcript concatenates those redraws into one very long line.
# Every wizard prompt shows its default in [brackets] and ends "]: ", which is
# what this filters -- the no-echo key prompt has no redraws to merge.
wide=$(printf '%s' "$plain" | grep -v '\]: ' | awk 'length>76' | wc -l)
if [ "$wide" -eq 0 ]; then
    t_ok "no wizard line exceeds 76 columns"
else
    t_fail "$wide line(s) over 76 columns: $(printf '%s' "$plain" | grep -v '\]: ' | awk 'length>76' | head -1)"
fi

# The config is SHOWN, not just located -- the connection between the answers
# and the file is the lesson, and a path leaves it invisible.
if grep -q "This is your config" "$tmp/noint.out" &&
   grep -q "A config only needs this much" "$tmp/noint.out"; then
    t_ok "the run ends by showing the config and the minimal one"
else
    t_fail "no config dump: $(grep -c 'config' "$tmp/noint.out")"
fi

# --- M326p: the systems axis, widened, and the probe asking first -----------
# One entry (small-local) could not say "the machine is small but the model is
# not", nor "I must build against a tree I do not own".
if printf '%s' "$plain" | grep -q "constrained" &&
   printf '%s' "$plain" | grep -q "existing-tree"; then
    t_ok "the systems axis offers the host and existing-tree cases"
else
    t_fail "systems axis not widened: $(printf '%s' "$plain" | grep -c 'running on')"
fi

# The distinction that needed a new feature flag: a small HOST keeps the
# model's context window; a small MODEL does not.
if printf '%s' "$plain" | grep -q 'leaving your model.s context window alone'; then
    t_ok "the host case says it leaves the context window alone"
else
    t_fail "the host/model distinction is not explained in the menu"
fi

# The probe: jichi read CPU and RAM unconditionally and told nobody it was a
# choice. The reading is local; the WRITE is what deserved consent, because the
# result lands in a config the user may commit.
if grep -q "I looked at this machine" "$tmp/noint.out" &&
   grep -q "Nothing left your computer" "$tmp/noint.out"; then
    t_ok "the probe says what it read"
else
    t_fail "the probe is silent about what it read"
fi

# --- M326q: the platform, and a key that could not work ---------------------
# doctor now reports what this machine is, and warns when memBudgetMb is set
# somewhere the RSS watchdog cannot run. The warning branch itself is covered
# by tests/smoke/faults.sh, which can make /proc appear absent; here we check
# only the reporting, which every platform reaches.
# Against the config the non-interactive run actually wrote -- this driver
# never starts a mock model, so there is no $tmp/config.json to point at.
plat=$(cd "$ws2" && with_deadline 30 "$BIN" doctor < /dev/null 2>&1 \
       | grep -c "platform")
if [ "$plat" -ge 1 ]; then
    t_ok "doctor reports the platform"
else
    t_fail "doctor says nothing about the platform"
fi

# The sound/notify question exists and is OPT-IN: a "sound" key registers the
# play_audio/record_audio tools, so it must not appear unasked.
if printf '%s' "$plain" | grep -q "play sound / notify you"; then
    t_ok "sound/notify is offered, not assumed"
else
    t_fail "no sound/notify question in the optional block"
fi

t_done
