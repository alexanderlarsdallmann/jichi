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

t_plan 5
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
$("$G" -n 'jc_net_host_is_blocked\|jc_url_host' \
  "$SMOKE_ROOT/src/tools/jc_tool_fetch.c" | head -3 | tr '\n' ' ')"
fi

t_done
