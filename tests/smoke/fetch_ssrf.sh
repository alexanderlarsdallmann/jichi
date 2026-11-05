#!/bin/sh
# smoke: fetch_url refuses private, loopback and link-local addresses (M542).
#
# THE GAP. `jc_net_host_is_blocked` has EIGHT unit checks in tests/test_http.c and
# the predicate is well covered. Nothing tested that the TOOL calls it. That is the
# shape M439 named in its own header -- "the pure function's boundary behaviour...
# cannot show that main.c's path CALLS it -- the wiring is the other half, and the
# wiring is what shipped broken" -- and M536 found it twice more, once in jc_acp.c
# where the correct helper was used forty lines above the site that omitted it.
#
# Here the untested wiring is a SECURITY fence, and the claim jichi makes to the
# model is absolute:
#
#   "jichi will not fetch URLs that only resolve inside this machine or network,
#    because a page can choose the next URL and that would turn the agent into a
#    probe for internal services."
#
# A page choosing the next URL is the whole SSRF mechanism, and `fetch_url` is
# registered by default. So this driver asserts the sentence rather than the
# predicate: the model asks for a blocked address and must be told no.
#
# WHAT THIS TIER CANNOT DO, stated rather than faked. There is no positive control
# that fetches successfully: a real remote fetch is forbidden here (smoke_lint check
# 3 bans every network client from the tier by name -- and a driver that reaches the
# internet is not a driver), and the one HTTP server available -- mockmodel -- listens on
# 127.0.0.1, which is exactly what the fence blocks. Check 4 substitutes for it: a
# malformed URL must produce a DIFFERENT error, which is what proves checks 1-3 are
# matching the refusal and not a generic failure that would swallow anything.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Four fetches in one turn, then a closing text. Each rule answers with the next
# tool call, so one run exercises all four cases and the capture holds every
# tool result the model was shown.
cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool fetch_url {"url":"http://127.0.0.1:9/x"}
rule
  count 2
  tool fetch_url {"url":"http://169.254.169.254/latest/meta-data/"}
rule
  count 3
  tool fetch_url {"url":"http://10.0.0.1/admin"}
rule
  count 4
  tool fetch_url {"url":"notaurl"}
rule
  text SSRF_DONE
MM
mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --output jsonl \
    -p "fetch those urls" < /dev/null) > "$tmp/out.jsonl" 2>&1
mm_stop

# The tool results the MODEL was shown. Reading the jsonl stream rather than the
# captured requests because tool_result carries is_error, which is the field that
# says "refused" as opposed to "fetched something".
nres=$("$G" -c '"type":"tool_result"' "$tmp/out.jsonl" 2>/dev/null || echo 0)

# ---- 1: the run reached all four fetches (the denominator) ----------------
# Without this, every check below is clean because nothing was attempted -- and a
# tool that is not registered produces exactly that silence.
if [ "$nres" -ge 4 ]; then
    t_ok "the model called fetch_url four times and saw four results"
else
    t_fail "only $nres tool_result events (want >= 4) -- fetch_url may not be \
registered, so nothing below tests the fence: \
$("$G" -o '"type":"[a-z_]*"' "$tmp/out.jsonl" | sort | uniq -c | tr '\n' ' ')"
fi

# ---- 2: loopback is refused, and the error NAMES the reason --------------
# 127.0.0.1 with port 9 (discard): if the fence were absent, the connection would
# be refused by the OS and the error would say so -- a different message, which is
# why this check matches the fence's own words rather than merely `is_error`.
if "$G" '"type":"tool_result"' "$tmp/out.jsonl" \
   | "$G" -q 'refusing to fetch a private, loopback or link-local'; then
    t_ok "a loopback URL is refused, and the refusal names why"
else
    t_fail "no fence message in any tool result -- loopback was not refused by \
the fence: $("$G" '"type":"tool_result"' "$tmp/out.jsonl" | head -1 | head_bytes 200)"
fi

# ---- 3: the cloud metadata address is refused ---------------------------
# 169.254.169.254 is the canonical SSRF target: link-local, and on most cloud
# providers it serves instance credentials to anything that asks. A fence that
# covers loopback and not this one covers the harmless case.
nref=$("$G" '"type":"tool_result"' "$tmp/out.jsonl" \
       | "$G" -c 'refusing to fetch a private' || true)
[ -n "$nref" ] || nref=0
if [ "$nref" -ge 3 ]; then
    t_ok "all three private/loopback/link-local URLs refused ($nref refusals)"
else
    t_fail "only $nref of 3 blocked addresses were refused -- 169.254.169.254 \
(cloud metadata) and 10.0.0.1 (private) must be refused as well as loopback"
fi

# ---- 4: the refusal is SPECIFIC, not a catch-all ------------------------
# The vacuity guard, and the reason this driver has five checks rather than three.
# `notaurl` has no scheme, so jc_url_host cannot parse it. If that ALSO produced
# the fence message, checks 2 and 3 would be matching a generic failure path and
# would pass on a build with no fence at all.
if "$G" '"type":"tool_result"' "$tmp/out.jsonl" \
   | "$G" -v 'refusing to fetch a private' | "$G" -q '"is_error":true'; then
    t_ok "a malformed URL fails with a DIFFERENT error (the fence is specific)"
else
    t_fail "every failing fetch produced the same message, so checks 2-3 cannot \
distinguish the fence from a generic failure: \
$("$G" '"type":"tool_result"' "$tmp/out.jsonl" | tr '\n' ' ' | head_bytes 300)"
fi

# ---- 5: the tool still calls the blocker (the wiring, structurally) -----
# CHECKS 2-3 AND CHECK 5 ARE COMPLEMENTARY, and the perturbation run proved it
# rather than the header asserting it. Deleting the fence outright makes check 5 red
# and leaves 2-3 green -- because the build then fails on an unused variable and the
# drivers run against the PREVIOUS binary, which is its own lesson. Making the fence
# DEAD while leaving the call in place (`&& ... && 0`) inverts it exactly: 2 and 3 go
# red, 5 stays green, and the tool result shows jichi actually attempting the
# forbidden connection -- `error: request failed (http error)` against 127.0.0.1:9.
# A structural check cannot see a dead fence; a behavioural check cannot see a
# removed one on a build that did not compile. Keep both.
# The behavioural checks above pass for any build whose fetch path happens to
# reject these four inputs. This one pins the mechanism: fetch_url consults
# jc_net_host_is_blocked, and it does so on the HOST extracted from the URL rather
# than on the URL text -- matching on the string would miss a userinfo trick like
# http://example.com@127.0.0.1/, which jc_url_host handles by ending the authority
# at '@'.
if "$G" -q 'jc_net_host_is_blocked(host)' "$SMOKE_ROOT/src/tools/jc_tool_fetch.c" &&
   "$G" -q 'jc_url_host(url, host' "$SMOKE_ROOT/src/tools/jc_tool_fetch.c"; then
    t_ok "fetch_url extracts the host and consults the blocker"
else
    t_fail "the fetch path no longer calls jc_url_host + jc_net_host_is_blocked: \
$("$G" -nE 'jc_net_host_is_blocked|jc_url_host' \
  "$SMOKE_ROOT/src/tools/jc_tool_fetch.c" | head -3 | tr '\n' ' ')"
fi


# ---- 6: the doctor prints proxy environment variables with userinfo stripped (M1027/C-02) ---
# RED FIRST: this checks that when http_proxy has credentials (us3r:p4ssw0rd@...),
# the doctor output contains the proxy line with the credentials stripped.
# On 481fe620^ (no proxy check) there is no proxy line at all, so this fails.
# On head it prints `! proxy environment variable set` with host:port and 0 occurrences of the user and password.
# The strip is scheme-agnostic and covers socks5:// and scheme-less values; the var list is the set libcurl reads.
# This is the one offline-achievable behavioural tooth: jichi doctor must print the proxy env vars
# without their credentials.
#
# Check 10 pins the M1032 var list: with ONLY HTTPS_PROXY set, the doctor must still name the host without the credential.
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:9999/v1","apiKey":"fake","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,"lowResource":false}
EOF
unset http_proxy https_proxy ALL_PROXY NO_PROXY
export http_proxy="http://us3r:p4ssw0rd@127.0.0.1:9"
("$BIN" --config "$tmp/config.json" -q doctor < /dev/null) > "$tmp/doctor.txt" 2>&1
# Check 6: output contains proxy line, does NOT contain username, does NOT contain password
proxy_line=$("$G" -c 'proxy environment variable set' "$tmp/doctor.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_user=$("$G" -c 'us3r' "$tmp/doctor.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_pass=$("$G" -c 'p4ssw0rd' "$tmp/doctor.txt" 2>/dev/null | tr -d '\n' || echo 0)
if [ "$proxy_line" -gt 0 ] && [ "$has_user" -eq 0 ] && [ "$has_pass" -eq 0 ]; then
    t_ok "doctor prints proxy env var with userinfo stripped (has line, no user, no pass)"
else
    t_fail "doctor proxy userinfo strip test failed: has_line=$proxy_line, has_user=$has_user, has_pass=$has_pass -- \
$("$G" 'proxy environment variable set' "$tmp/doctor.txt" | head -1 | tr '\n' ' ')"
fi

# ---- 7: vacuity check - the output names the host (127.0.0.1), not silence ---
# This guards against check 6 passing because the doctor printed nothing at all.
# The doctor prints "proxy environment variable set" on one line, then the URL with host:port on the next.
# We verify that both appear in sequence (the host is stripped of userinfo, not absent).
if "$G" -q 'proxy environment variable set' "$tmp/doctor.txt" && 
   "$G" -A1 'proxy environment variable set' "$tmp/doctor.txt" | "$G" -q '127.0.0.1'; then
    t_ok "doctor output contains the proxy host '127.0.0.1' with userinfo stripped"
else
    t_fail "doctor output does not contain '127.0.0.1' after 'proxy environment variable set'"
fi

# ---- 8: scheme-less proxy value (PUSER:PPASS@127.0.0.1:3128) ---
# RED FIRST on e8c00495: the current strip prints credentials as-is for no-scheme values.
# After the fix, the doctor must strip userinfo and show only 127.0.0.1:3128.
unset http_proxy https_proxy ALL_PROXY NO_PROXY
export http_proxy="PUSER:PPASS@127.0.0.1:3128"
("$BIN" --config "$tmp/config.json" -q doctor < /dev/null) > "$tmp/doctor2.txt" 2>&1
proxy_line8=$("$G" -c 'proxy environment variable set' "$tmp/doctor2.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_user8=$("$G" -c 'PUSER' "$tmp/doctor2.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_pass8=$("$G" -c 'PPASS' "$tmp/doctor2.txt" 2>/dev/null | tr -d '\n' || echo 0)
host8=$("$G" -c '127.0.0.1:3128' "$tmp/doctor2.txt" 2>/dev/null | tr -d '\n' || echo 0)
if [ "$proxy_line8" -gt 0 ] && [ "$has_user8" -eq 0 ] && [ "$has_pass8" -eq 0 ] && [ "$host8" -gt 0 ]; then
    t_ok "doctor strips scheme-less proxy userinfo (has line, no user, no pass, has host)"
else
    t_fail "doctor scheme-less proxy strip test failed: has_line=$proxy_line8, has_user=$has_user8, has_pass=$has_pass8, has_host=$host8 -- \
$("$G" 'proxy environment variable set' "$tmp/doctor2.txt" | head -1 | tr '\n' ' ')"
fi

# ---- 9: socks5:// proxy value (socks5://SSUSER:SSPASS@127.0.0.1:1080) ---
# RED FIRST on e8c00495: the current strip prints credentials as-is for socks5:// values.
# After the fix, the doctor must strip userinfo and show socks5://127.0.0.1:1080.
unset http_proxy https_proxy ALL_PROXY NO_PROXY
export http_proxy="socks5://SSUSER:SSPASS@127.0.0.1:1080"
("$BIN" --config "$tmp/config.json" -q doctor < /dev/null) > "$tmp/doctor3.txt" 2>&1
proxy_line9=$("$G" -c 'proxy environment variable set' "$tmp/doctor3.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_user9=$("$G" -c 'SSUSER' "$tmp/doctor3.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_pass9=$("$G" -c 'SSPASS' "$tmp/doctor3.txt" 2>/dev/null | tr -d '\n' || echo 0)
host9=$("$G" -c '127.0.0.1:1080' "$tmp/doctor3.txt" 2>/dev/null | tr -d '\n' || echo 0)
if [ "$proxy_line9" -gt 0 ] && [ "$has_user9" -eq 0 ] && [ "$has_pass9" -eq 0 ] && [ "$host9" -gt 0 ]; then
    t_ok "doctor strips socks5:// proxy userinfo (has line, no user, no pass, has host)"
else
    t_fail "doctor socks5:// proxy strip test failed: has_line=$proxy_line9, has_user=$has_user9, has_pass=$has_pass9, has_host=$host9 -- \
$("$G" 'proxy environment variable set' "$tmp/doctor3.txt" | head -1 | tr '\n' ' ')"
fi

# ---- 10: with ONLY HTTPS_PROXY set (uppercase), the doctor must still name the host without the credential ---
# RED FIRST: on the unfixed tree (missing HTTPS_PROXY from the var list), this will show has_line=0 because
# the doctor only checks http_proxy, https_proxy, ALL_PROXY, NO_PROXY and not HTTPS_PROXY.
# The unfixed list in main.c was {"http_proxy", "https_proxy", "ALL_PROXY", "NO_PROXY", NULL} (missing HTTPS_PROXY, all_proxy, no_proxy).
# After the fix (M1032), the complete list includes HTTPS_PROXY and all_proxy, so setting ONLY HTTPS_PROXY triggers the warning.
unset http_proxy https_proxy HTTPS_PROXY all_proxy ALL_PROXY no_proxy NO_PROXY
export HTTPS_PROXY="http://HUPUSER:HUPPASS@127.0.0.1:4433"
("$BIN" --config "$tmp/config.json" -q doctor < /dev/null) > "$tmp/doctor4.txt" 2>&1
proxy_line10=$("$G" -c 'proxy environment variable set' "$tmp/doctor4.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_user10=$("$G" -c 'HUPUSER' "$tmp/doctor4.txt" 2>/dev/null | tr -d '\n' || echo 0)
has_pass10=$("$G" -c 'HUPPASS' "$tmp/doctor4.txt" 2>/dev/null | tr -d '\n' || echo 0)
host10=$("$G" -c '127.0.0.1:4433' "$tmp/doctor4.txt" 2>/dev/null | tr -d '\n' || echo 0)
if [ "$proxy_line10" -ge 1 ] && [ "$has_user10" -eq 0 ] && [ "$has_pass10" -eq 0 ] && [ "$host10" -gt 0 ]; then
    t_ok "doctor with ONLY HTTPS_PROXY set: has line, no user, no pass, has host (pins M1032 var list)"
else
    t_fail "doctor HTTPS_PROXY-only test failed: has_line=$proxy_line10, has_user=$has_user10, has_pass=$has_pass10, has_host=$host10 -- \
$("$G" 'proxy environment variable set' "$tmp/doctor4.txt" | head -1 | tr '\n' ' ')"
fi

t_done
