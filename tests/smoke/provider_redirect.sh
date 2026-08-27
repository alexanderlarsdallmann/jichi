#!/bin/sh
# smoke: a provider endpoint's 3xx is NOT followed, and says why (M472).
#
# THE DEFECT THIS EXISTS FOR. apply_common set CURLOPT_FOLLOWLOCATION=1 on every
# request, while M131's redirect caps (MAXREDIRS, the protocol restriction, the
# connect-time guard) were all gated on req->block_private_addrs -- which exactly
# one caller sets, and it is fetch_url, the one request carrying NO credential.
# So every credentialed request followed unlimited redirects anywhere.
#
# Measured before the fix, with two loopback servers (A 302s to B, B records):
# B received `x-api-key: SUPER-SECRET-KEY-abc123`. libcurl strips Authorization
# on a cross-host hop, so the Bearer providers were saved by a libcurl default
# jichi neither requests nor tests -- and the Anthropic wire, this project's own
# primary provider, authenticates with x-api-key, which libcurl cannot know is a
# credential. See docs/analysis/2026-08-17-source-hardening-audit.md §H1.
#
# WHAT THIS DRIVER ASSERTS, and what it deliberately does not. It asserts the
# INVARIANT -- jichi does not follow -- by pointing the redirect at a second path
# on the same mock and checking no second request was captured. It does NOT
# re-stage the two-host key-disclosure measurement: that needed a second server,
# and the invariant is the thing a regression would break. A cross-host variant
# belongs in e2e if it is ever wanted.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The endpoint answers the model call with a redirect. `location` (M472) is what
# makes this a real 3xx -- without the header there is nothing to follow and the
# check would pass for the wrong reason.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  status 302
  location /v2/chat/completions
  body {}
rule
  text SHOULD_NOT_BE_REACHED
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"SUPER-SECRET-KEY-abc123",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,
"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session -v -p hello < /dev/null 2>&1)
mm_stop

# 1. The redirect was not followed: exactly ONE request reached the mock. A
#    second capture file means the transfer followed the Location -- and on a real
#    endpoint that second request would carry the key to another host.
nreq=$(ls "$tmp/cap" 2>/dev/null | grep -c '^req\.' || true)
if [ "$nreq" = "1" ]; then
    t_ok "the 302 was not followed (1 request, not 2)"
else
    t_fail "expected 1 captured request, got $nreq -- the redirect was followed"
fi

# 2. The operator is told what happened. A bare "HTTP 302" would send them
#    looking for the switch that makes the client follow it, which is the one
#    move that hands the key to the redirect target.
if printf '%s\n' "$out" | grep -q 'not followed'; then
    t_ok "the unfollowed redirect is reported"
else
    t_fail "no diagnostic naming the unfollowed redirect: $(printf '%s' "$out" | tail -c 300)"
fi

# 3. ...and the report names the target, so the fix (point the config at the
#    final URL) is actionable rather than a guess.
if printf '%s\n' "$out" | grep -q '/v2/chat/completions'; then
    t_ok "the diagnostic names the redirect target"
else
    t_fail "the diagnostic does not name the Location target"
fi

t_done
