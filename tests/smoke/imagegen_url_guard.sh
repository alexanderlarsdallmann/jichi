#!/bin/sh
# smoke: the image tool's fetch of a server-named reply URL is guarded against
# private addresses (C-07, M1119) -- the same block_private_addrs house
# standard fetch_url applies to an untrusted URL (jc_tool_fetch.c).
#
# THE GAP. When the image backend answers the POST leg with a url (no
# b64_json), jichi GETs that URL and writes the body into the workspace at the
# model-chosen path. The URL is chosen by the SERVER (or anyone able to alter
# its replies), and fetch_image_url never set block_private_addrs, so the GET
# reached any private address the reply named -- the lead's scratch
# reproduction: a reply of {"data":[{"url":"http://127.0.0.1:<port>/secret"}]}
# was fetched and its body landed in the workspace. Loopback (127/8) is
# exactly the private class jc_net_ipv4_blocked refuses, so a loopback target
# needs no special network shape.
#
# THE POST LEG STAYS UNGUARDED. jc_imagegen_run's own request to m->api_base is
# the operator-configured, trusted endpoint (a local image server is the normal
# case, as for providers) -- only the reply-URL GET gains the guard.
#
# THE FIX. fetch_image_url sets req.block_private_addrs = 1. RED pre-fix: the
# target mock captures the GET and the workspace holds its body. GREEN post-fix:
# the GET is refused at connect, no file is written, the tool reports a failure.
#
# mockmodel note (lead-confirmed): a `match` directive matches the request
# BODY, not the path, so a backend that serves exactly one POST uses `count 1`,
# not a path match.
#
# Scenario A (the guard) + scenario B (the b64 control: the tool still works
# when the backend answers inline, so the guard is not a blanket refusal).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws1=$(smoke_tmp)
ws2=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# base64 of the 8 PNG magic bytes \x89 P N G \r \n \x1a \n (imagegen.sh)
PNG_B64="iVBORw0KGgo="
printf '\211PNG\r\n\032\n' > "$tmp/magic.png"

# =============================================================================
# SCENARIO A -- the reply URL names a private (loopback) address
# =============================================================================
# TARGET: the private target. Serves one GET /secret, 200, body TARGET-HIT
# (10 bytes -- the lead's repro wrote exactly 10 bytes into img.png).
mkdir -p "$tmp/tgt"
cat > "$tmp/tgt/tgt.mm" <<EOF
wire openai
rule
  status 200
  body TARGET-HIT
EOF
mm_start "$tmp/tgt/tgt.mm" "$tmp/tgt"
TGT_PORT=$MM_PORT; TGT_PID=$MM_PID

# IMG (A): the image backend. Serves the one POST (count 1 -- match matches
# the body, not the path, so no path match) and answers with a url naming the
# target's OWN port (substituted in sh; @PORT@ would be the mock's own).
cat > "$tmp/imga.mm" <<EOF
wire openai
rule
  count 1
  status 200
  body {"data":[{"url":"http://127.0.0.1:$TGT_PORT/secret"}]}
EOF

# CHAT (A): one generate_image tool call, then the closing text.
cat > "$tmp/chata.mm" <<EOF
wire openai
rule
  count 1
  tool generate_image {"prompt":"x","path":"img.png"}
rule
  text DONE
EOF
mm_start "$tmp/imga.mm" "$tmp/imga"
I_PORT=$MM_PORT; I_PID=$MM_PID
mm_start "$tmp/chata.mm" "$tmp/chata"
cat > "$tmp/cfga.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"img","provider":"openai","model":"mock-image",
   "apiBase":"http://127.0.0.1:$I_PORT/v1","apiKey":"x","roles":["image"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF
(cd "$ws1" && with_deadline 60 "$BIN" --config "$tmp/cfga.json" \
    -q --no-session --auto -p "make an image" \
    < /dev/null > "$tmp/outa" 2>&1)
kill "$I_PID" 2>/dev/null
wait "$I_PID" 2>/dev/null
mm_stop
kill "$TGT_PID" 2>/dev/null
wait "$TGT_PID" 2>/dev/null

# ---- 1: the fence -- the private target was never reached ------------------
# RED pre-fix: the unguarded GET opened a socket to 127.0.0.1 (the target
# captured the request). GREEN post-fix: opensocket_guard refused at connect.
if [ -f "$tmp/tgt/req.1" ]; then
    t_fail "fence: the reply-URL GET reached the private (loopback) target -- its mock captured the request"
else
    t_ok "fence: the reply-URL GET never reached the private (loopback) target (no captured request)"
fi

# ---- 2: the consequence -- no workspace file from the refused URL ----------
# RED pre-fix: img.png holds the target's 10-byte body, readable by the model.
if [ -e "$ws1/img.png" ]; then
    t_fail "fence: the refused URL's body was written into the workspace (img.png exists)"
else
    t_ok "fence: no image file was written when the guard refused (img.png absent)"
fi

# ---- 3: the control -- the POST leg still reached the configured backend ----
# The guard is on the reply-URL GET only: jc_imagegen_run's own request to the
# image model's api_base (here 127.0.0.1, the trusted configured endpoint) must
# be unaffected.
if [ -f "$tmp/imga/req.1" ] && "$G" -q "images/generations" "$tmp/imga/req.1"; then
    t_ok "control: the POST leg still reached the configured image backend (api_base unguarded)"
else
    t_fail "control: the POST leg never reached the image backend -- \
$("$G" -c 'images/generations' "$tmp/imga"/req.* 2>/dev/null | tr '\n' ' ')"
fi

# =============================================================================
# SCENARIO B -- the b64_json reply still works (the guard is not a blanket no)
# =============================================================================
cat > "$tmp/imgb.mm" <<EOF
wire openai
rule
  count 1
  status 200
  body {"data":[{"b64_json":"$PNG_B64"}]}
EOF
cat > "$tmp/chatb.mm" <<EOF
wire openai
rule
  count 1
  tool generate_image {"prompt":"x","path":"b64.png"}
rule
  text DONE
EOF
mm_start "$tmp/imgb.mm" "$tmp/imgb"
I2_PORT=$MM_PORT; I2_PID=$MM_PID
mm_start "$tmp/chatb.mm" "$tmp/chatb"
cat > "$tmp/cfgb.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"img","provider":"openai","model":"mock-image",
   "apiBase":"http://127.0.0.1:$I2_PORT/v1","apiKey":"x","roles":["image"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/cfgb.json" \
    -q --no-session --auto -p "make an image" \
    < /dev/null > "$tmp/outb" 2>&1)
kill "$I2_PID" 2>/dev/null
wait "$I2_PID" 2>/dev/null
mm_stop

# ---- 4: the b64 path is byte-identical (same shape as imagegen.sh) ----------
if [ -f "$tmp/imgb/req.1" ] && "$G" -q "images/generations" "$tmp/imgb/req.1"; then
    t_ok "b64: the backend was queried and answered b64_json (no URL leg)"
else
    t_fail "b64: the b64 backend was never queried"
fi
if cmp -s "$ws2/b64.png" "$tmp/magic.png"; then
    t_ok "b64: the decoded PNG bytes were written verbatim (the guard cannot be a blanket refusal)"
else
    t_fail "b64: b64.png missing or wrong bytes"
fi

t_done
