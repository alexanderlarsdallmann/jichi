#!/bin/sh
# smoke: the reply header names the MODEL, not only the tier (M296).
#
# `fast` is a config intent label -- chosen so an agent profile can pin a tier
# without naming a vendor id -- so the old header (`fast - chat`) told you which
# tier was active and nothing about which model was answering. After M288 made
# escalation actually fire, `strong` was equally opaque.
#
# The header is emitted per model call (cb_message_begin runs before each
# stream_once), so it tracks an escalation as it happens and is the honest place to
# answer "which model wrote this". The PROMPT is deliberately left showing the tier:
# it is rebuilt once per turn BEFORE the turn runs, while routing mutates the active
# model DURING one, so it cannot be authoritative once routing is live. Both halves
# of that decision are asserted here -- the id present in the header, absent from
# the prompt -- because "deliberately unchanged" is a claim a test should hold.
#
# LC_ALL=C in the harness means the TUI renders ASCII separators, so the prompt is
# `[chat:fast:0%]` and the header `fast (id) - chat - <time>`.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/reply.mm" <<'EOF'
wire openai
rule
  text HEADER_CHECK_OK
EOF
mm_start "$tmp/reply.mm" "$tmp"

# An INTENT name plus a distinct, vendor-prefixed wire id -- the shape the whole
# milestone is about (and the shape of the zigodot config it came from).
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"fast","provider":"openai","model":"jlu/qwen3-coder-next",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

cat > "$tmp/hdr.pd" <<'EOF'
expect "] " 15
delay 300
send "hello\r"
expect "HEADER_CHECK_OK" 25
delay 600
send "/status\r"
expect "mode:" 20
delay 600
send "/exit\r"
waitexit 10
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/hdr.log" "$tmp/hdr.pd" -- \
    "$BIN" --config "$tmp/config.json"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "the TUI answers and serves /status against the mock"
else
    t_fail "driver rc=$rc"
    sed 's/^/    | /' "$tmp/hdr.log"
fi

# THE assertion: the reply header carries the full wire id beside the tier.
#
# Anchored on the header's own " - chat - <time>" tail, NOT on the bare pair. This
# driver's first revision grepped for "fast (jlu/qwen3-coder-next)" alone and stayed
# green with the header reverted to the tier only -- because /status, run later in
# the same session, prints that pair too. A PTY transcript holds every command's
# output, so an unanchored grep tests the transcript, not the surface.
if grep -aq "fast (jlu/qwen3-coder-next) - chat" "$tmp/hdr.log"; then
    t_ok "the reply header names the tier AND the model id"
else
    t_fail "reply header missing the model id"
    grep -a " - chat - " "$tmp/hdr.log" | head -5 | sed 's/^/    | /'
fi

# The id must be the FULL id. A short form would make two vendors' models with the
# same trailing segment indistinguishable -- the precision bug M296 also fixed.
if ! grep -aq "fast (qwen3-coder-next) - chat" "$tmp/hdr.log"; then
    t_ok "the id half is not shortened (vendor prefix kept)"
else
    t_fail "the vendor prefix was stripped from the header's id"
fi

# The prompt keeps the tier and does NOT gain the id: it is drawn before the turn
# runs, so once routing is live its model segment cannot be authoritative.
if grep -aq "chat:fast:" "$tmp/hdr.log"; then
    t_ok "the prompt still shows the tier name"
else
    t_fail "the prompt lost its model segment"
fi
if ! grep -aq "chat:jlu/qwen3-coder-next" "$tmp/hdr.log"; then
    t_ok "the prompt did NOT gain the wire id (deliberate)"
else
    t_fail "the prompt gained the wire id; M296 excluded it on purpose"
fi

# /status is the format the other surfaces adopted, so it must still read that way.
if grep -aq "model: *fast (jlu/qwen3-coder-next)" "$tmp/hdr.log"; then
    t_ok "/status shows name and id"
else
    t_fail "/status line changed shape"
    grep -a "model:" "$tmp/hdr.log" | sed 's/^/    | /'
fi

mm_stop

# --- a config with NO `name` ---------------------------------------------------
#
# The common case, and the one the M296 plan got wrong: it assumed jichi mirrors
# the wire id into `name`. It does not -- `name` stays NULL. So /status passed NULL
# to "%s" and printed "(null) (jlu/...)": undefined behaviour in C89, with glibc's
# placeholder leaking into the first line the subcommand prints.
cat > "$tmp/noname.json" <<'EOF'
{"models":[{"provider":"openai","model":"jlu/qwen3-coder-next",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# Offline is fine: these are all read-only views of the config.
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/noname.json" status \
    < /dev/null > "$tmp/st.out" 2>&1)
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/noname.json" config show \
    < /dev/null > "$tmp/cs.out" 2>&1)

if ! grep -q "(null)" "$tmp/st.out" "$tmp/cs.out" &&
   ! grep -q "(none)" "$tmp/cs.out"; then
    t_ok "a nameless model prints neither (null) nor (none)"
else
    t_fail "placeholder leaked: $(grep -h "model:" "$tmp/st.out" "$tmp/cs.out")"
fi

# And it prints the real thing -- the full id, so the vendor prefix survives here
# too (this is the surface where the prefix mattered most: `status` is what a user
# runs to find out which model they are actually talking to).
if grep -q "model: *jlu/qwen3-coder-next" "$tmp/st.out" &&
   grep -q "model: *jlu/qwen3-coder-next" "$tmp/cs.out"; then
    t_ok "a nameless model shows its full wire id in status and config show"
else
    t_fail "nameless model id wrong: $(grep -h "model:" "$tmp/st.out" \
        "$tmp/cs.out")"
fi

t_done
