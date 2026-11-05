#!/bin/sh
# smoke: doctor reports a plaintext provider, and stays quiet about a local one (M472).
#
# THE GAP THIS EXISTS FOR. An `apiBase` of http:// sends the API key over the
# network in cleartext, and nothing in jichi said so: doctor's posture checks
# covered whether a key EXISTS, not whether it is protected in flight. The whole
# M472 audit was run against http://127.0.0.1 configs and jichi never mentioned it.
#
# The silence also amplified two other findings. A MITM on a plaintext apiBase can
# inject the 302 that used to walk `x-api-key` off to another host (H1), and the
# provider socket a child inherited (H2) carries readable plaintext where TLS would
# have carried ciphertext.
#
# THE HALF THAT MATTERS MOST IS CHECK 2. `http://127.0.0.1` is the documented,
# normal shape for a local model (docs/LOCAL_MODELS.md): there is no network to
# sniff, and a check that warned about it would train operators to ignore the
# check -- which is worse than not having one. So loopback must stay SILENT, and
# that is asserted as hard as the warning itself.
#
# See docs/analysis/2026-08-17-source-hardening-audit.md L4.
#
# M1132 (C-14) WIDENS THE UNIVERSE. The M472 check exempted every PRIVATE address,
# not just loopback, because it reused the SSRF guard (which answers "loopback OR
# private") as its local test -- so a key sent in clear over a LAN passed in
# silence. The fix exempts loopback ONLY (a private range is not loopback, so it
# warns), and it now checks every endpoint that can carry a credential over http://:
# each model's apiBase, each MCP server's url, and search.url. Checks 1-4 (the M472
# core, incl. the loopback-silence half) are unchanged; 5-8 cover the widening.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

mk_cfg() {  # $1 = path, $2 = apiBase
    cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"$2",
"apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF
}

# The check reads the CONFIG, not the network, so no model has to be reachable.
mk_cfg "$tmp/https.json"    "https://api.example.com/v1"
mk_cfg "$tmp/loopback.json" "http://127.0.0.1:8080/v1"
mk_cfg "$tmp/plain.json"    "http://api.example.com/v1"

status_of() {  # $1 = config -- the JSON report's status for this one check
    (unset HOME; "$BIN" doctor $2 --output json --config "$1" 2>/dev/null) \
        | tr ',' '\n' | grep -B1 '"label":"provider transport' \
        | sed -n 's/.*"status":"\([a-z]*\)".*/\1/p' | head -1
}

# 1. https is fine and says so.
if [ "$(status_of "$tmp/https.json")" = "ok" ]; then
    t_ok "https apiBase reports ok"
else
    t_fail "https apiBase reported '$(status_of "$tmp/https.json")', want ok"
fi

# 2. A LOCAL model over http is fine and must not be nagged about.
if [ "$(status_of "$tmp/loopback.json")" = "ok" ]; then
    t_ok "loopback http apiBase stays silent (a local model is not a leak)"
else
    t_fail "loopback http reported '$(status_of "$tmp/loopback.json")', want ok -- \
warning here would train operators to ignore this check"
fi

# 3. A REMOTE http endpoint is the actual defect.
if [ "$(status_of "$tmp/plain.json")" = "warn" ]; then
    t_ok "remote http apiBase warns"
else
    t_fail "remote http reported '$(status_of "$tmp/plain.json")', want warn"
fi

# 4. ...and escalates for an unattended loop, which cannot read a warning.
if [ "$(status_of "$tmp/plain.json" --unattended)" = "fail" ]; then
    t_ok "--unattended escalates the plaintext warning to a failure"
else
    t_fail "unattended reported '$(status_of "$tmp/plain.json" --unattended)', want fail"
fi

# --- M1132 (C-14): the exemption is loopback ONLY, and every endpoint counts ---
# A PRIVATE address (192.168/16 here) is NOT loopback, so the doctor must warn
# about it; today it reuses the SSRF guard and stays silent. The model-name detail
# is irrelevant to the check, so mk_cfg (which names the model "m") is reused.
mk_cfg "$tmp/private.json" "http://192.168.1.10:8080/v1"

# An MCP-only config: no models at all. Today the doctor prints NO transport line
# for it, so the check IS that a warn appears.
cat > "$tmp/mcp.json" <<EOF
{"mcpServers":[{"name":"m1","url":"http://10.0.0.5/mcp"}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

# A search-only config: no models, no mcp. search.url may carry a credential.
cat > "$tmp/search.json" <<EOF
{"search":{"url":"http://172.16.4.9/search"},
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

# 5. A PRIVATE-range model is not loopback: it warns.
if [ "$(status_of "$tmp/private.json")" = "warn" ]; then
    t_ok "private-range (192.168.0.0/16) http apiBase warns (not loopback)"
else
    t_fail "private-range http reported '$(status_of "$tmp/private.json")', want warn"
fi

# 6. An MCP server over http is checked too (today: no transport line at all).
if [ "$(status_of "$tmp/mcp.json")" = "warn" ]; then
    t_ok "http MCP server url warns"
else
    t_fail "mcp http reported '$(status_of "$tmp/mcp.json")', want warn"
fi

# 7. A search endpoint over http is checked too (today: no transport line at all).
if [ "$(status_of "$tmp/search.json")" = "warn" ]; then
    t_ok "http search url warns"
else
    t_fail "search http reported '$(status_of "$tmp/search.json")', want warn"
fi

# 8. ...and the private-range escalation reaches an unattended loop.
if [ "$(status_of "$tmp/private.json" --unattended)" = "fail" ]; then
    t_ok "--unattended escalates the private-range warning to a failure"
else
    t_fail "unattended private-range reported '$(status_of "$tmp/private.json" --unattended)', want fail"
fi

t_done
