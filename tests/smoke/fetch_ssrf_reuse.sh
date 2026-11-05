#!/bin/sh
# smoke: fetch_url guard bypassed by keep-alive connection reuse (C-01, M1026).
#
# THE GAP. The SSRF connect-time guard (opensocket_guard) runs only when libcurl
# OPENS a socket. jichi's requests share one cached easy handle (g_reuse, M227);
# curl_easy_reset clears options but keeps its connection cache. A guarded
# fetch_url that reuses a live pooled connection to the same scheme://host:port
# opens NO socket, so the guard never runs and the fetch reaches an internal
# address (the provider's own host:port, once it resolves to one).
#
# WHY THE REPRODUCTION IS THE WHOLE POINT. The run-0047 first measurement was
# hollow: the fetch URL named a DIFFERENT port than the provider, so it never
# hit the pooled connection and the "no GET /probe" check passed for the wrong
# reason. This driver makes the fetch URL the provider's OWN host:port by
# construction (@PORT@ is the announced mock port), so a reuse is possible; the
# conns precondition then proves a reuse actually existed, and the fence checks
# are teeth against a real bypass.
#
# THE FIX. apply_common sets CURLOPT_FRESH_CONNECT + CURLOPT_FORBID_REUSE for
# block_private_addrs requests, so a guarded fetch opens (and is checked on) a
# fresh socket and never rides a pooled one. Provider (unguarded) requests keep
# their M227 reuse.
#
# MECHANISM NOTES. unshare -rm (NOT -rmn: -rmn isolates the netns and the mock
# becomes unreachable). The mock writes cap/conns when the client closes the
# pooled connection (jichi exit), so the driver waits for it before mm_stop.
# SKIPs (not a failure) when unshare -rm is unavailable.
. "$(dirname "$0")/_smoke.sh"

unshare -rm true 2>/dev/null || t_skip "unshare -rm unavailable (no user-ns mount isolation) - the reuse repro needs a private /etc/hosts"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# The fetch URL is the provider's OWN host:port: @PORT@ is substituted by
# mockmodel to the announced port, matching the config's apiBase.
cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool fetch_url {"url":"http://testname.local:@PORT@/probe"}
rule
  count 2
  text done
MM

MM_KEEPALIVE=1 mm_start "$tmp/replies.mm" "$tmp/cap" 3
# The provider's apiBase MUST be the SAME hostname as the fetch URL, because
# libcurl pools by hostname, not resolved IP. write_config hardcodes 127.0.0.1,
# which would pool on a DIFFERENT key than testname.local and make the reuse
# impossible -- exactly the hollow shape the run-0047 measurement had.
cat > "$tmp/config.json" <<'CFG'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://testname.local:@PORTCFG@/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG
sed "s/@PORTCFG@/$MM_PORT/" "$tmp/config.json" > "$tmp/config.json.tmp" && mv "$tmp/config.json.tmp" "$tmp/config.json"

# testname.local -> 127.0.0.1 in a throwaway mount namespace (same network).
# with_deadline is a _smoke.sh function, so it must wrap the unshare in THIS
# shell -- a bare `sh -c` child (the unshare target) has no functions.
with_deadline 90 unshare -rm sh -c 'mount -t tmpfs tmpfs /etc; echo "127.0.0.1 testname.local" > /etc/hosts; cd "$1" && exec "$2" --config "$3" -q --no-session --auto --output jsonl -p "fetch the probe" < /dev/null > "$4" 2> "$5"' _ "$ws" "$BIN" "$tmp/config.json" "$tmp/out.jsonl" "$tmp/err"

# The mock flushes cap/conns when jichi closes the pooled connection (jichi
# exit). Wait for it (poll, ~2s) BEFORE mm_stop kills the mock, so the
# precondition reads a real conns, not a lost race.
_c=0
while [ ! -f "$tmp/cap/conns" ] && [ "$_c" -lt 3 ]; do
    kill -0 "$MM_PID" 2>/dev/null || break
    sleep 1
    _c=$((_c + 1))
done
mm_stop

# 1: THE PRECONDITION -- the provider's model calls rode ONE pooled
# connection, so a reuse opportunity existed. If this fails, the fence checks
# are vacuous (no reuse to bypass), so it must be read distinctly.
if [ -f "$tmp/cap/conns" ]; then
    reqs=$("$G" -o 'requests=[0-9]*' "$tmp/cap/conns" | head -n 1 | sed 's/requests=//')
    if [ -n "$reqs" ] && [ "$reqs" -ge 2 ]; then
        t_ok "precondition: the provider connection was pooled ($reqs requests on one connection)"
    else
        t_fail "precondition: the provider connection was NOT pooled (conns requests=$reqs) -- instrument vacuous"
    fi
else
    t_fail "precondition: no cap/conns file -- the provider connection was not pooled, instrument vacuous"
fi

# 2: THE FENCE -- no captured request is a GET /probe, i.e. the guarded fetch
# never rode the pooled connection. RED on the unfixed tree (the probe is
# captured alongside the POSTs).
if "$G" -l 'GET /probe' "$tmp/cap"/req.* >/dev/null 2>&1; then
    t_fail "fence: a pooled connection served the probe (GET /probe captured) -- the SSRF guard was bypassed by connection reuse"
else
    t_ok "fence: the guarded fetch never rode the pooled connection (no GET /probe captured)"
fi

# 3: THE FETCH REFUSED -- with the fix, the fresh socket is refused by the
# connect-time guard (is_error true). RED on the unfixed tree (is_error false,
# WITH the page body -- the bypass succeeded).
is_err=$("$G" -c '"name":"fetch_url","is_error":true' "$tmp/out.jsonl")
if [ "$is_err" -ge 1 ]; then
    t_ok "fetch: the probe was refused by the guard (is_error true)"
else
    t_fail "fetch: the probe was NOT refused (no is_error true) -- the guarded fetch succeeded, i.e. it bypassed the guard"
fi

# 4: THE RUN COMPLETED -- the fix must not break the pooled provider path.
if "$G" -q '"type":"done"' "$tmp/out.jsonl"; then
    t_ok "provider: the model completed its turn (done row present)"
else
    t_fail "provider: the model did not complete (no done row)"
fi

t_done
