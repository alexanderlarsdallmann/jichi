#!/bin/sh
# smoke: `setup --context-length` (the flag half, M1200/G5 -- cells 1-6) and
# the inherited-config shape of `setup --from-global` (B7/B8 -- cells 7-8),
# and the WIZARD's ask (B4/B5/B6 -- cells 9-10, wave 2 / M1200-b, this file).
#
# Authority: docs/internal/drive/2026-11-05-harness-port/notes/M1200.md,
# driver plan 1-8 (wave 1) and 9-10 (wave 2, added below).
#
# Cells 1-3 and 6 KEEP the existing flag path (measured on head b7bde594):
# a literal number is OFFLINE (non-interactive flag-number mode makes zero
# requests -- cell 1 proves it against a mockmodel capture); 0 and junk are
# usage refusals (rc 2); `auto` without the key exported is a PREREQUISITE
# refusal (rc 2), a different failure class from "the server does not
# publish model limits".
#
# Cell 4 pins `auto` against a server that PUBLISHES the figure: one
# jc_net_model_limits request (`GET /v1/model/info`), the LiteLLM table body
# below. Measured on b7bde594 -- the stdout line is EXACT:
#
#   setup: context window 977232 tokens, as published by the server (its
#   output limit, 32768, is separate and is NOT part of this budget)
#
# Cell 5 pins `auto` against a server that does not (404): the refusal is
# REPORTED on stderr, setup still succeeds (rc 0), and contextLength stays
# ABSENT (jsonq missing path = rc 1, so `! "$JQ" -q` is the assertion).
#
# Cells 7-8 pin the _comment shape of --from-global. B7 (cell 7, RED on
# arrival, M1200): a global config carrying "_comment": "my global comment"
# used to inherit BOTH comments into local/config.json -- grep count 2, the
# inherited one first and the generated one last -- a last-wins reader's
# coin flip for foreign consumers, and a re-serialization keeping both. B8
# (cell 8, KEEPING): no _comment in the global still yields exactly one, the
# generated one.
#
# OFFLINE elsewhere: loopback mockmodel only, a closed port, and no live
# model. The wizard's closing validation is offline (setup_validate --
# "config: valid JSON" and friends), so cells 7-8 make no request at all.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

export JICHI_API_KEY=abc123

# --- cell 1 (B1, KEEPING): --context-length 4096, OFFLINE -------------------
# A closed port (127.0.0.1:1, connection refused) plus a mockmodel capture
# that must stay EMPTY: the flag-number path makes zero network requests.
# (The mock answers 404 to anything; if the capture dir ends up empty, it is
# because setup asked NOTHING, not because the endpoint refused.)
cat > "$tmp/nop.mm" <<'EOF'
wire openai
rule
  status 404
  body {"error":"unexpected request"}
EOF
ws1=$(smoke_tmp)
mm_start "$tmp/nop.mm" "$tmp/cap1"
(cd "$ws1" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model m1 \
    --api-base "http://127.0.0.1:1/v1" --key-env JICHI_API_KEY \
    --context-length 4096 --config-target local < /dev/null \
    > "$tmp/out1" 2>&1); rc1=$?
mm_stop
nreq=$(ls "$tmp/cap1"/req.* 2>/dev/null | wc -l)
if [ $rc1 -eq 0 ] && [ "$nreq" -eq 0 ] && \
   [ "$("$JQ" '.models[0].contextLength' "$ws1/local/config.json")" = "4096" ]; then
    t_ok "--context-length 4096 writes the window and makes no request (B1)"
else
    t_fail "cell 1: rc=$rc1 reqs=$nreq len=$("$JQ" '.models[0].contextLength' "$ws1/local/config.json" 2>/dev/null)"
fi

# --- cell 2 (B3, KEEPING): --context-length 0 is a refusal -------------------
ws2=$(smoke_tmp)
(cd "$ws2" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model m1 --key-env JICHI_API_KEY \
    --context-length 0 --config-target local < /dev/null \
    > "$tmp/out2" 2>&1); rc2=$?
if [ $rc2 -eq 2 ] && $G -q 'wants a positive token count or' "$tmp/out2"; then
    t_ok "--context-length 0 is refused rc 2 with the positive-or-auto message (B3)"
else
    t_fail "cell 2: rc=$rc2; $(head_bytes 200 "$tmp/out2")"
fi

# --- cell 3 (B3, KEEPING): --context-length bogus is a refusal ---------------
ws3=$(smoke_tmp)
(cd "$ws3" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model m1 --key-env JICHI_API_KEY \
    --context-length bogus --config-target local < /dev/null \
    > "$tmp/out3" 2>&1); rc3=$?
if [ $rc3 -eq 2 ] && $G -q 'wants a positive token count or' "$tmp/out3"; then
    t_ok "--context-length bogus is refused rc 2 with the same message shape (B3)"
else
    t_fail "cell 3: rc=$rc3; $(head_bytes 200 "$tmp/out3")"
fi

# --- cells 4-5 (B2): `auto`, the published and the not-published arms --------
# One catch-all rule each: non-interactive `auto` makes exactly ONE request
# (measured on b7bde594: req.1 = GET /v1/model/info), so no count predicates.
cat > "$tmp/pub.mm" <<'EOF'
wire openai
rule
  status 200
  body {"data":[{"model_name":"jlu/m1","model_info":{"max_input_tokens":977232,"max_output_tokens":32768}}]}
EOF
mm_start "$tmp/pub.mm" "$tmp/cap4"
ws4=$(smoke_tmp)
(cd "$ws4" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model jlu/m1 \
    --api-base "http://127.0.0.1:$MM_PORT/v1" --key-env JICHI_API_KEY \
    --context-length auto --config-target local < /dev/null \
    > "$tmp/out4" 2>&1); rc4=$?
mm_stop
nreq4=$(ls "$tmp/cap4"/req.* 2>/dev/null | wc -l)
# The exact line, quoted in the note (measured on b7bde594).
if [ $rc4 -eq 0 ] && [ "$nreq4" -eq 1 ] && \
   $G -q 'setup: context window 977232 tokens, as published by the server (its output limit, 32768, is separate and is NOT part of this budget)' "$tmp/out4" && \
   [ "$("$JQ" '.models[0].contextLength' "$ws4/local/config.json")" = "977232" ]; then
    t_ok "auto against a publishing server: the exact line + the figure written (B2)"
else
    t_fail "cell 4: rc=$rc4 reqs=$nreq4; line=$(grep 'context window' "$tmp/out4" | head -1) len=$("$JQ" '.models[0].contextLength' "$ws4/local/config.json" 2>/dev/null)"
fi

cat > "$tmp/nopub.mm" <<'EOF'
wire openai
rule
  status 404
  body {"error":"not found"}
EOF
mm_start "$tmp/nopub.mm" "$tmp/cap5"
ws5=$(smoke_tmp)
(cd "$ws5" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model jlu/m1 \
    --api-base "http://127.0.0.1:$MM_PORT/v1" --key-env JICHI_API_KEY \
    --context-length auto --config-target local < /dev/null \
    > "$tmp/out5" 2>&1); rc5=$?
mm_stop
if [ $rc5 -eq 0 ] && \
   $G -q 'this server does not publish model limits (HTTP 404)' "$tmp/out5" && \
   $G -q 'will assume 32000 tokens' "$tmp/out5" && \
   ! "$JQ" -q '.models[0].contextLength' "$ws5/local/config.json"; then
    t_ok "auto against a 404 server: reported, still rc 0, contextLength absent (B2)"
else
    t_fail "cell 5: rc=$rc5; msg=$(grep 'context-length auto' "$tmp/out5" | head -1) len=$("$JQ" '.models[0].contextLength' "$ws5/local/config.json" 2>/dev/null)"
fi

# --- cell 6 (B2, KEEPING): `auto` without the key exported -------------------
# A REFUSAL (rc 2): missing prerequisites is a usage error, distinct from a
# server that simply does not publish.
ws6=$(smoke_tmp)
# The key must be ABSENT from the environment here (auto needs the key exported,
# so this is the "no key" refusal). env -u is a GNU extension (illumos exits 1,
# the rc would read as a legitimate result); the portable form is an unset in
# this shell for the run's duration, restored after (posix_utils_lint 23).
_save_key=$JICHI_API_KEY
unset JICHI_API_KEY
(cd "$ws6" && with_deadline 30 "$BIN" setup --preset generic \
    --provider openai --model jlu/m1 \
    --api-base "http://127.0.0.1:1/v1" --key-env JICHI_API_KEY \
    --context-length auto --config-target local < /dev/null \
    > "$tmp/out6" 2>&1); rc6=$?
JICHI_API_KEY=$_save_key
export JICHI_API_KEY
if [ $rc6 -eq 2 ] && $G -q 'needs the key exported' "$tmp/out6"; then
    t_ok "auto without the key exported is refused rc 2 (B2)"
else
    t_fail "cell 6: rc=$rc6; $(head_bytes 200 "$tmp/out6")"
fi

# --- cells 7-8 (B7/B8): the _comment shape of --from-global ------------------
# The global config lives at $HOME/.jichi (smoke_home gave a fresh HOME), one
# model object carrying the apiKeyEnv name. The generated local config must
# hold EXACTLY ONE "_comment" member -- jsonq has no count operator, so the
# count is a grep (one member per line in the emitted JSON) and the VALUE is
# pinned by substring: the generated text in, the inherited text out.

# Cell 7 (B7, RED on arrival): the global carries its own _comment.
cat > "$HOME/.jichi" <<'EOF'
{"_comment": "my global comment",
 "lowResource": false,
 "models": [{"name": "g", "provider": "openai", "model": "gpt",
             "apiKeyEnv": "JICHI_API_KEY", "roles": ["chat"]}]}
EOF
ws7=$(smoke_tmp)
(cd "$ws7" && with_deadline 30 "$BIN" setup --from-global --preset generic \
    --config-target local < /dev/null > "$tmp/out7" 2>&1); rc7=$?
CFG7="$ws7/local/config.json"
if [ $rc7 -eq 0 ] && [ -f "$CFG7" ] && \
   [ "$($G -c '"_comment"' "$CFG7")" = 1 ] && \
   $G -q 'inherits the global config' "$CFG7" && \
   ! $G -q 'my global comment' "$CFG7"; then
    t_ok "from-global with a global _comment: exactly one, the generated one (B7)"
else
    t_fail "cell 7: rc=$rc7 count=$(grep -c '"_comment"' "$CFG7" 2>/dev/null) gen=$(grep -c 'inherits the global config' "$CFG7" 2>/dev/null) kept=$(grep -c 'my global comment' "$CFG7" 2>/dev/null)"
fi

# Cell 8 (B8, KEEPING): the global carries NO _comment.
cat > "$HOME/.jichi" <<'EOF'
{"lowResource": false,
 "models": [{"name": "g", "provider": "openai", "model": "gpt",
             "apiKeyEnv": "JICHI_API_KEY", "roles": ["chat"]}]}
EOF
ws8=$(smoke_tmp)
(cd "$ws8" && with_deadline 30 "$BIN" setup --from-global --preset generic \
    --config-target local < /dev/null > "$tmp/out8" 2>&1); rc8=$?
CFG8="$ws8/local/config.json"
if [ $rc8 -eq 0 ] && [ -f "$CFG8" ] && \
   [ "$($G -c '"_comment"' "$CFG8")" = 1 ] && \
   $G -q 'inherits the global config' "$CFG8"; then
    t_ok "from-global with no global _comment: exactly one, the generated one (B8)"
else
    t_fail "cell 8: rc=$rc8 count=$(grep -c '"_comment"' "$CFG8" 2>/dev/null) gen=$(grep -c 'inherits the global config' "$CFG8" 2>/dev/null)"
fi

# --- cells 9-10 (B4/B5/B6, wave 2): the wizard's ask, driven on a pty --------
# The measured openai-by-wire arm: role 5 (generic -- no journey/language
# prompts), walking / running / stance defaults, provider "1" (any
# OpenAI-compatible endpoint -- the arm that asks for apiBase; provider "2"
# is anthropic, which has NO apiBase prompt), the mock's base, the one-model
# listing default (Enter), JICHI_API_KEY as the key-env NAME, then the NEW
# "context length in tokens (blank = skip)" ask, then the config-path default
# and the optional block + machine prompt on trailing Enters (the
# setup_keyfile.sh idiom).
#
# The store offer is key-state dependent: when JICHI_API_KEY is EXPORTED
# (cells 9 and 10-A) setup_offer_key_file takes its no-offer branch ("already
# set in this shell") and no prompt appears; when it is UNSET (cell 10-B) the
# offer DOES appear and gets a "n". Two drive variants encode this.
#
# The mock's request order is MEASURED: the wizard's request 1 is the model
# LISTING (GET /v1/models, count 1) and request 2 is the figure (GET
# /v1/model/info, count 2); the two are separate connections, so mockmodel's
# global 1-based count numbers them in that order. The closing validation
# (setup_validate) is offline, so mm_stop happens before the asserts and no
# request N>=3 may exist.
ctxdrive() {
    # $1 = the context-length arm's send line, $2 = the store-offer step
    # (empty when the key is already exported), $3 = output drive file
    {
        printf '%s\n' 'expect "What are you working on?" 30' 'delay 400' 'send "5\r"'
        printf '%s\n' 'delay 500' 'expect "What are you walking into?" 30' 'delay 400' 'send "\r"'
        printf '%s\n' 'delay 500' 'expect "What are you running on?" 30' 'delay 400' 'send "\r"'
        printf '%s\n' 'delay 500' 'expect "How are you working?" 30' 'delay 400' 'send "\r"'
        printf '%s\n' 'expect "Model provider:" 30' 'delay 400' 'send "1\r"'
        printf '%s\n' 'expect "apiBase URL" 30' "delay 400" "send \"http://127.0.0.1:$MM_PORT/v1\r\""
        printf '%s\n' 'delay 800' 'expect "Models this endpoint serves" 30' 'delay 400' 'send "1\r"'
        printf '%s\n' 'expect "name of the env var holding your API key" 30' 'delay 400' 'send "JICHI_API_KEY\r"'
        if [ -n "$2" ]; then
            printf '%s\n' "delay 600" "$2"
        fi
        printf '%s\n' 'delay 800' 'expect "context length in tokens" 30' "delay 400" "$1"
        printf '%s\n' 'expect "Write the config where?" 30' 'delay 400' 'send "\r"'
        printf '%s\n' 'delay 500' 'expect "Accept this role'"'"'s default features?" 30' 'delay 400' 'send "n\r"'
        printf '%s\n' 'expect "Nothing below is required" 30' 'delay 300' 'send "\r"'
        k=1
        while [ $k -le 16 ]; do
            printf '%s\n' 'delay 300' 'send "\r"'
            k=$((k + 1))
        done
        printf '%s\n' 'expect "Next steps" 60' 'delay 500'
    } > "$3"
}
# Cell 9 (B4): the endpoint PUBLISHES the figure; the hint line names it and
# the pre-filled default is the figure; Enter on it writes 977232.
cat > "$tmp/wz9.mm" <<'EOF'
wire openai
rule
  count 1
  status 200
  body {"data":[{"id":"jlu/m1"}]}
rule
  count 2
  status 200
  body {"data":[{"model_name":"jlu/m1","model_info":{"max_input_tokens":977232,"max_output_tokens":32768}}]}
EOF
mm_start "$tmp/wz9.mm" "$tmp/cap9"
ctxdrive 'send "\r"' '' "$tmp/drive9.txt"
ws9=$(smoke_tmp)
(cd "$ws9" && with_deadline 120 env TERM=dumb "$SMOKE_TOOLS/ptydrive" \
    --log "$tmp/pty9.log" "$tmp/drive9.txt" -- "$BIN" --no-lite setup) >/dev/null 2>&1
rc9=$?
mm_stop
nreq9=$(ls "$tmp/cap9"/req.* 2>/dev/null | wc -l)
if [ $rc9 -eq 0 ] && [ -f "$tmp/pty9.log" ] && \
   $G -q 'context length in tokens' "$tmp/pty9.log" && \
   $G -q 'that endpoint lists 977232 tokens for jlu/m1' "$tmp/pty9.log" && \
   [ "$("$JQ" '.models[0].contextLength' "$ws9/local/config.json")" = "977232" ] && \
   [ "$nreq9" -eq 2 ]; then
    t_ok "wizard: the published figure is hinted, pre-filled and written on Enter (B4)"
else
    t_fail "cell 9: rc=$rc9 hint=$(grep -c 'that endpoint lists' "$tmp/pty9.log" 2>/dev/null) len=$("$JQ" '.models[0].contextLength' "$ws9/local/config.json" 2>/dev/null) reqs=$nreq9"
fi

# Cell 10 (B5 + B6): two drives, separate ws dirs + pty logs.
#   A -- the endpoint does NOT publish (count 2 = 404): no hint line, no
#        default; a TYPED number (131072) is written. Key still exported.
#   B -- no key anywhere (JICHI_API_KEY unexported, smoke_home's fresh HOME
#        has no ~/.jichi.env): the store offer appears and is declined, the
#        figure request never happens (exactly ONE captured request, the
#        listing), no hint line, BLANK skip -> config valid, contextLength
#        ABSENT, rc 0.
cat > "$tmp/wz10a.mm" <<'EOF'
wire openai
rule
  count 1
  status 200
  body {"data":[{"id":"jlu/m1"}]}
rule
  count 2
  status 404
  body {"error":"not found"}
EOF
mm_start "$tmp/wz10a.mm" "$tmp/cap10a"
ctxdrive 'send "131072\r"' '' "$tmp/drive10a.txt"
ws10a=$(smoke_tmp)
(cd "$ws10a" && with_deadline 120 env TERM=dumb "$SMOKE_TOOLS/ptydrive" \
    --log "$tmp/pty10a.log" "$tmp/drive10a.txt" -- "$BIN" --no-lite setup) >/dev/null 2>&1
rc10a=$?
mm_stop
ok10a=0
if [ $rc10a -eq 0 ] && [ -f "$tmp/pty10a.log" ] && \
   $G -q 'context length in tokens' "$tmp/pty10a.log" && \
   ! $G -q 'that endpoint lists' "$tmp/pty10a.log" && \
   [ "$("$JQ" '.models[0].contextLength' "$ws10a/local/config.json")" = "131072" ]; then
    ok10a=1
fi

cat > "$tmp/wz10b.mm" <<'EOF'
wire openai
rule
  count 1
  status 200
  body {"data":[{"id":"jlu/m1"}]}
rule
  count 2
  status 200
  body {"data":[{"model_name":"jlu/m1","model_info":{"max_input_tokens":977232,"max_output_tokens":32768}}]}
EOF
mm_start "$tmp/wz10b.mm" "$tmp/cap10b"
# store offer present (key unset) and declined:
ctxdrive 'send "\r"' 'expect "store the key there now?" 30
delay 400
send "n\r"' "$tmp/drive10b.txt"
ws10b=$(smoke_tmp)
# No key anywhere: JICHI_API_KEY unset for this drive (the store offer then
# appears and is declined), restored after. env -u is GNU-only (illumos exits 1,
# posix_utils_lint 23); the key is unset in THIS shell and env only ADDS TERM,
# so with_deadline still has a real command to run (env TERM=dumb cmd).
_save_key=$JICHI_API_KEY
unset JICHI_API_KEY
(cd "$ws10b" && with_deadline 120 env TERM=dumb \
    "$SMOKE_TOOLS/ptydrive" --log "$tmp/pty10b.log" "$tmp/drive10b.txt" \
    -- "$BIN" --no-lite setup) >/dev/null 2>&1
rc10b=$?
JICHI_API_KEY=$_save_key
export JICHI_API_KEY
mm_stop
nreq10b=$(ls "$tmp/cap10b"/req.* 2>/dev/null | wc -l)
ok10b=0
if [ $rc10b -eq 0 ] && [ -f "$tmp/pty10b.log" ] && \
   $G -q 'context length in tokens' "$tmp/pty10b.log" && \
   ! $G -q 'that endpoint lists' "$tmp/pty10b.log" && \
   [ "$nreq10b" -eq 1 ] && \
   "$JQ" -q '.' "$ws10b/local/config.json" && \
   ! "$JQ" -q '.models[0].contextLength' "$ws10b/local/config.json"; then
    ok10b=1
fi
# ONE check, two drives: A (404, no hint, typed number written) and B
# (no key anywhere, no figure request, no hint, blank skip -> contextLength
# absent). Two wizard runs in one cell is fine: separate ws dirs + logs.
if [ $ok10a -eq 1 ] && [ $ok10b -eq 1 ]; then
    t_ok "wizard: no figure means no hint (404: typed number written; no key: request never happens, blank skips) (B5+B6)"
else
    t_fail "cell 10: driveA rc=$rc10a ok=$ok10a len=$("$JQ" '.models[0].contextLength' "$ws10a/local/config.json" 2>/dev/null); driveB rc=$rc10b ok=$ok10b reqs=$nreq10b"
fi

t_done
