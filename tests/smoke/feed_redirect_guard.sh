#!/bin/sh
# smoke: the redirect hops of an @rss: feed fetch and a documentation URL
# fetch are guarded against private addresses, HOP-AWARE (C-08, M1124): the
# user-named hop 0 may be a legitimate internal feed (HARDENING.md §6b), and a
# hop after a 3xx is allowed only if it is the SAME host:port opened at hop 0
# (a self-redirect), else refused at a private address. The guard counts hops
# by the 3xx STATUS LINES it sees (not the sockets libcurl opens), which is
# what M1120's per-socket counter miscounted.
#
# THE GAP. resolve_rss (jc_refs.c) and materialize_url (jc_docs.c) set
# follow_redirects = 1 and no block_private_addrs, so a feed host the user
# named could answer 302 to a loopback or link-local target and jichi
# followed it -- the lead's live reproduction on head 8cc6165d: the loopback
# target captured `GET /secret`. The body of a private target is what lands
# in the prompt (fenced as untrusted); the read channel is the connection
# itself.
#
# THE FIX (M1124, hop-aware). The per-request flag block_redirect_private
# installs a connect-time guard whose decision is extracted to a pure,
# unit-tested function (jc_http_redirect_guard_decide). It keeps two pieces of
# per-transfer state, both reset in apply_common next to the socket watch:
# (1) g_redirect_hop -- the count of 3xx STATUS LINES seen, advanced by a
# redirect_hop_header header callback installed for guarded requests (libcurl
# delivers a redirect's status line before it opens the redirect socket, so the
# hop number is known when the guard fires); and (2) the hop-0 set -- the
# (family, address, PORT) of every address opened at hop 0. Hop 0 is allowed
# (the user named it) and recorded; a hop after a 3xx is allowed only if it is
# that SAME (family, address, port) (a self-redirect), else refused at a
# private / link-local / reserved address. The PORT is in the key deliberately:
# a feed on 127.0.0.1:FEED 302ing to 127.0.0.1:TGT must stay refused (checks 1
# and 4 -- a different port on a private host is server-chosen), while a
# self-redirect to the SAME host:port (check 6) is allowed. This fixes the two
# C-08 false refusals M1120's counter made (a self-redirect -- the follow-up is
# a second socket; and a dual-stack name -- a dead ::1 attempt then 127.0.0.1).
# A loopback target needs no special network shape: 127/8 is exactly the class
# the test refuses.
#
# Documented limitation (SV:1437, option a): a redirect to ANOTHER port on the
# SAME private host -- e.g. an intranet feed at http://host/feed 301ing to
# https://host/feed, (ip, 80) -> (ip, 443) -- is refused, because the key
# includes the port and 443 is not the recorded hop-0 port. Pinned in the unit
# test, not fixed here.
#
# RED pre-fix (M1120 counter): checks 1, 2 and 4 (the target is reached, its
# marker body is in the model request, the docs target is reached) and 5 (the
# flag absent), AND check 6 (the self-redirect's body never reaches the model --
# the follow-up connection to the same host:port is miscounted as private hop 2).
# GREEN post-fix (hop-aware): the targets are never reached, the control feed
# (a plain 200 RSS on loopback) still fetches, the SELF-redirecting feed's RSS
# body REACHES the model (check 6), both callers set the flag.
#
# All three mocks sit on loopback: the feed and the target by design (that
# IS the scenario), the chat + embed mocks like every other driver.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# =============================================================================
# TARGET: the private target. Every GET answers 200 with a feed whose title
# is the unique marker -- RSS-shaped so the body survives the feed reducer
# and would reach the prompt if fetched.
# =============================================================================
mkdir -p "$tmp/tgt"
cat > "$tmp/tgt/tgt.mm" <<'EOF'
wire openai
rule
  status 200
  body <rss version="2.0"><channel><item><title>C08-TARGET-MARKER</title></item></channel></rss>
EOF
mm_start "$tmp/tgt/tgt.mm" "$tmp/tgt"
TGT_PORT=$MM_PORT; TGT_PID=$MM_PID

# =============================================================================
# FEED: the @rss: / docs source. Request 1 (the fence run) 302s to the
# target's own port (substituted in sh; the mock's own-port placeholder
# token would trip docs_placeholder_lint, so the name is the port variable).
# Request 2 (the control run) is a plain 200 RSS with no redirect. From
# request 3 on (the docs run) it 302s to the target again.
# =============================================================================
cat > "$tmp/feed.mm" <<EOF
wire openai
rule
  count 1
  status 302
  location http://127.0.0.1:$TGT_PORT/secret
rule
  count 2
  status 200
  body <rss version="2.0"><channel><item><title>C08-CONTROL-ITEM</title></item></channel></rss>
rule
  status 302
  location http://127.0.0.1:$TGT_PORT/secret
EOF

# CHAT: one reply per run; @rss: expands during prompt construction, so the
# chat mock only witnesses the expanded prompt (the consequence check).
cat > "$tmp/chat.mm" <<'EOF'
wire openai
rule
  text DONE
EOF

# EMB: the docs run indexes the cached page through the embeddings endpoint.
cat > "$tmp/emb.mm" <<'EOF'
wire openai
rule
  embed hooks state
EOF

# =============================================================================
# SELF: a user-named loopback feed that 302s to ITSELF -- the SAME host:port, a
# second path. The mock's own-port placeholder token @PORT@ (substituted by the
# mock with its own listening port at parse time; the .mm is not a shipped
# Markdown page, so docs_placeholder_lint does not scan it) makes the redirect
# loop back onto the feed's own socket. This is the C-08 false refusal the
# M1124 hop-aware guard must fix: the follow-up connection to the SAME
# host:port is a second socket that M1120's per-socket counter miscounted as
# private hop 2 (and refused); the (family, address, port) hop-0 set allows it.
# =============================================================================
cat > "$tmp/self.mm" <<'EOF'
wire openai
rule
  count 1
  status 302
  location http://127.0.0.1:@PORT@/self2
rule
  status 200
  body <rss version="2.0"><channel><item><title>C08-SELF-MARKER</title></item></channel></rss>
EOF

mm_start "$tmp/feed.mm" "$tmp/feed"
FEED_PORT=$MM_PORT; FEED_PID=$MM_PID
mm_start "$tmp/self.mm" "$tmp/self"
SELF_PORT=$MM_PORT; SELF_PID=$MM_PID
mm_start "$tmp/chat.mm" "$tmp/chat"
CHAT_PORT=$MM_PORT; CHAT_PID=$MM_PID
mm_start "$tmp/emb.mm" "$tmp/emb"
EMB_PORT=$MM_PORT; EMB_PID=$MM_PID

cat > "$tmp/cfga.json" <<EOF
{"lowResource":false,"references":true,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$CHAT_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

# =============================================================================
# RUN A -- @rss: a feed whose reply 302s to the loopback TARGET
# =============================================================================
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cfga.json" \
    -q --no-session --auto --output jsonl \
    -p "@rss:http://127.0.0.1:$FEED_PORT/feed" \
    < /dev/null > "$tmp/outa.jsonl" 2>&1)

# =============================================================================
# RUN B -- the control: @rss: a feed that does NOT redirect (request 2 is a
# plain 200 RSS on the same loopback feed host). A legitimate internal feed
# must still fetch (the §6b case) -- the guard is not a blanket refusal.
# =============================================================================
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cfga.json" \
    -q --no-session --auto --output jsonl \
    -p "@rss:http://127.0.0.1:$FEED_PORT/feed" \
    < /dev/null > "$tmp/outb.jsonl" 2>&1)

# =============================================================================
# RUN C -- the docs half: a docs source whose url 302s to the TARGET. The
# fresh per-run HOME that smoke_home gives the whole driver is what makes the
# url cache miss, so materialize_url must fetch; the explicit --config
# outranks the $JC_CONFIG smoke_home pins (M465: no env prefix onto
# with_deadline).
# =============================================================================
cat > "$tmp/cfgc.json" <<EOF
{"lowResource":false,"references":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$CHAT_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$EMB_PORT/v1","apiKey":"x","roles":["embed"]}],
 "docs":[{"name":"d","url":"http://127.0.0.1:$FEED_PORT/feed"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF
tgt_before=$(ls "$tmp/tgt" 2>/dev/null | "$G" -c '^req\.' || true)
[ -n "$tgt_before" ] || tgt_before=0
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cfgc.json" \
    -q --no-session docs search d q \
    < /dev/null > "$tmp/outc" 2>&1)
tgt_after=$(ls "$tmp/tgt" 2>/dev/null | "$G" -c '^req\.' || true)
[ -n "$tgt_after" ] || tgt_after=0

# =============================================================================
# RUN D -- the self-redirect (SV:1417 shape, check 6): a user-named loopback
# feed whose reply 302s to ITSELF, the SAME host:port, a second path. This is a
# legitimate feed (HARDENING.md §6b) that M1120's per-socket counter false-
# refused: the follow-up connection to the same host:port is a second socket,
# counted as private hop 2. A dedicated ws + out file so check 6's assertions
# do not touch checks 1-5's captures; the chat mock's next request is req.3.
# =============================================================================
wsd=$(smoke_tmp)
(cd "$wsd" && with_deadline 60 "$BIN" --config "$tmp/cfga.json" \
    -q --no-session --auto --output jsonl \
    -p "@rss:http://127.0.0.1:$SELF_PORT/feed" \
    < /dev/null > "$tmp/outd.jsonl" 2>&1)

kill "$TGT_PID" 2>/dev/null; wait "$TGT_PID" 2>/dev/null
kill "$FEED_PID" 2>/dev/null; wait "$FEED_PID" 2>/dev/null
kill "$SELF_PID" 2>/dev/null; wait "$SELF_PID" 2>/dev/null
kill "$CHAT_PID" 2>/dev/null; wait "$CHAT_PID" 2>/dev/null
kill "$EMB_PID" 2>/dev/null; wait "$EMB_PID" 2>/dev/null

# ---- 1: the fence (rss) -- the private target was never reached ----------
# Floor: the feed mock captured run A's first hop (feed/req.1), so the @rss:
# fetch really ran (and the 302 really was answered) -- an empty feed capture
# would make the target-absence assertion pass for the wrong reason.
# CAVEAT (SV:1032 LOW, reworded to the truth): the assertion reads the target's
# FIRST capture, tgt/req.1, AFTER all runs. The rss run (A) and the docs run (C)
# both 302 to the SAME target mock, so tgt/req.1 is shared: if the DOCS fence
# alone is broken (run C reaches the target) but the rss fence is fine,
# tgt/req.1 exists and check 1 (rss) reds too. The checks are not perfectly
# independent on the target capture; check 4 disambiguates the docs half.
# RED pre-fix: the unguarded redirect opened a socket to the loopback target
# (its mock captured the request). GREEN post-fix: the guard refused it.
if [ -f "$tmp/feed/req.1" ] && [ ! -f "$tmp/tgt/req.1" ]; then
    t_ok "fence: the @rss: feed's 302 to the private (loopback) target was refused (target capture empty)"
else
    nreq=$([ -f "$tmp/feed/req.1" ] && echo yes || echo no)
    t_fail "fence: the @rss: redirect reached the private (loopback) target -- \
target captured request ($([ -f "$tmp/tgt/req.1" ] && echo yes || echo no)); \
feed first-hop captured: $nreq"
fi

# ---- 2: the consequence -- the target's marker body never reached the prompt
# Floor: run A's model request was captured (the @rss: block was expanded and
# sent), so absence is measured against a real prompt.
# RED pre-fix: the target's RSS title survived the reducer into the prompt.
if [ -f "$tmp/chat/req.1" ] && \
   ! (cat "$tmp/chat/req.1" "$tmp/outa.jsonl" 2>/dev/null | "$G" -q 'C08-TARGET-MARKER'); then
    t_ok "consequence: the private target's marker is absent from the model request and run output"
else
    t_fail "consequence: the private target's body (C08-TARGET-MARKER) reached \
the model request or the run output: \
$(cat "$tmp/chat"/req.* "$tmp/outa.jsonl" 2>/dev/null | "$G" -o 'C08-TARGET-MARKER' | "$G" -c . || true)"
fi

# ---- 3: the control -- a non-redirecting local feed still fetches ----------
# The first hop (the user-named loopback feed) stays allowed: the feed mock
# served run B, its title reached the prompt, and no fetch failure was
# reported. A guard that refused this would be a blanket refusal, not C-08's
# first-hop-allowed rule.
if [ -f "$tmp/feed/req.2" ] && [ -f "$tmp/chat/req.2" ] && \
   "$G" -q 'C08-CONTROL-ITEM' "$tmp/chat/req.2" && \
   ! (cat "$tmp/chat/req.2" "$tmp/outb.jsonl" 2>/dev/null | "$G" -q 'could not fetch feed'); then
    t_ok "control: a plain 200 local feed still fetches and reaches the prompt (first hop allowed)"
else
    t_fail "control: the non-redirecting local feed was not fetched: \
feed req.2 $([ -f "$tmp/feed/req.2" ] && echo yes || echo no), \
chat req.2 $([ -f "$tmp/chat/req.2" ] && echo yes || echo no), \
marker $([ -f "$tmp/chat/req.2" ] && { "$G" -q 'C08-CONTROL-ITEM' "$tmp/chat/req.2" && echo yes || echo no; } || echo no), \
outb: $(tail -c 200 "$tmp/outb.jsonl" | tr '\n' ' ')"
fi

# ---- 4: the fence (docs) -- the docs url's 302 to the target was refused ---
# Floor: the feed mock captured run C's first hop (feed req.3), so
# materialize_url really fetched. RED pre-fix: the target's capture grew by
# one; GREEN post-fix: it did not.
if [ -f "$tmp/feed/req.3" ] && [ "$tgt_before" = "$tgt_after" ]; then
    t_ok "fence: the docs url's 302 to the private (loopback) target was refused (target capture unchanged at $tgt_after)"
else
    t_fail "fence: the docs redirect reached the private (loopback) target -- \
target captured $tgt_before before the docs run, $tgt_after after (feed req.3 \
$([ -f "$tmp/feed/req.3" ] && echo yes || echo no)): $(tail -c 200 "$tmp/outc" | tr '\n' ' ')"
fi

# ---- 5: the structural pin -- both callers set the flag -------------------
# This greps the flag NAME, `block_redirect_private`, in both caller files -- a
# line that reads the name but assigns 0 (disabled) would still pass it, and it
# pins name-presence, not value. It is NOT the only thing that catches a flag
# removed or disabled in one caller: checks 1 and 4 (the rss and docs fences)
# and check 6 (the self-redirect) are per-caller BEHAVIOURAL teeth that would
# red if that caller's guard stopped refusing a private redirect. So the premise
# "a behavioural check cannot see a flag deleted in one caller" is false --
# reworded to what this structural pin actually adds (the name exists in both
# files, the fetch_ssrf.sh check-5 precedent) on top of the behavioural fences.
if "$G" -q 'block_redirect_private' "$SMOKE_ROOT/src/command/jc_refs.c" && \
   "$G" -q 'block_redirect_private' "$SMOKE_ROOT/src/index/jc_docs.c"; then
    t_ok "structural: both resolve_rss and materialize_url set block_redirect_private"
else
    t_fail "structural: a caller no longer sets block_redirect_private: \
$("$G" -c 'block_redirect_private' "$SMOKE_ROOT/src/command/jc_refs.c" 2>/dev/null || true) \
$("$G" -c 'block_redirect_private' "$SMOKE_ROOT/src/index/jc_docs.c" 2>/dev/null || true)"
fi

# ---- 6: the self-redirect (SV:1417 shape) -- the SAME host:port is allowed ---
# A user-named loopback feed that 302s to ITSELF (the SAME host:port, a second
# path) is a legitimate internal feed: hop 0 opens 127.0.0.1:SELF_PORT, the
# 302 status line advances the hop count, and the follow-up connection to the
# SAME host:port is in the (family, address, port) hop-0 set, so it is allowed
# and its RSS body reaches the model request (chat/req.3).
# Floor: the self feed's first hop was captured (self/req.1), so run D's fetch
# really ran.
# RED pre-fix (M1120 counter): the follow-up connection to the same host:port
# is a second socket, counted as private hop 2 and refused -- the body is ABSENT
# from chat/req.3. GREEN post-fix (hop-aware): it is in the set and the body
# reaches the prompt.
if [ -f "$tmp/self/req.1" ] && [ -f "$tmp/chat/req.3" ] && \
   "$G" -q 'C08-SELF-MARKER' "$tmp/chat/req.3"; then
    t_ok "self-redirect: a feed that 302s to the SAME host:port still fetches (body reaches the prompt)"
else
    t_fail "self-redirect: a feed that 302s to ITSELF was refused (the C-08 false refusal) -- \
self first-hop captured: $([ -f "$tmp/self/req.1" ] && echo yes || echo no), \
chat req.3: $([ -f "$tmp/chat/req.3" ] && echo yes || echo no), \
marker in req.3: $([ -f "$tmp/chat/req.3" ] && { "$G" -q 'C08-SELF-MARKER' "$tmp/chat/req.3" && echo yes || echo no; } || echo no), \
outd: $(tail -c 200 "$tmp/outd.jsonl" 2>/dev/null | tr '\n' ' ')"
fi

t_done
