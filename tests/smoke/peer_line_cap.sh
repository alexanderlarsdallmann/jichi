#!/bin/sh
# smoke: one line from an MCP server is bounded, and the bound says so (M659).
#
# THE DEFECT. `read_line` in src/mcp/jc_mcp_stdio.c accumulated an inbound
# message into a growable builder with no byte bound: a server that streams
# without ever sending a newline grew it until the OOM killer fired. Found by
# the 2026-08-27 hardening survey and reported at M609, which fixed the LSP
# framer (a pure function with a born-red unit test) and left this one.
#
# WHY IT SAT FOR FIFTY MILESTONES, AND WHY THAT REASON WAS WRONG. The deferred
# row said the block was "a born-red test, which needs to feed >cap bytes and
# assert bounded RSS" -- a memory-pressure harness the smoke tier does not have.
# RSS was never the property. The fix returns JC_ERR_IO past the ceiling, which
# is the path a closed server already takes, so crossing the bound has a
# BEHAVIOURAL signature: before the fix the reader blocks to its 120-second
# deadline and reports a timeout; after it, it refuses in about a second and
# names the cap. That is the M438 lesson in the same words -- there the first
# liveness test asserted that `status` ANSWERED and passed with the fix
# reverted, because the property was latency, not the answer.
#
# WHAT IS AND IS NOT COVERED:
#   checked -- the mock really exceeds the cap (so nothing below is vacuous);
#              jichi refuses PROMPTLY rather than at the 120 s deadline, which
#              is the property the fix actually changes; the message names the
#              byte cap, so a reader can tell this from a network stall; and a
#              well-formed server still works, so the bound did not cost the
#              happy path.
#   not checked -- peak RSS. Deliberately: it is the thing this driver was
#              blocked on for fifty milestones and it was never the property.
#              What the cap guarantees is that the builder cannot pass
#              JC_PEER_LINE_MAX before the read loop gives up.
#   not checked -- the ACP side of the same family. It has no deadline, so the
#              same fixture cannot tell "bounded" from "the client went away"
#              by latency; jc_acp.c carries the same cap and its red is owed.
. "$(dirname "$0")/_smoke.sh"

t_plan 4

smoke_home
tmp=$(smoke_tmp)

# 9 MiB with no newline, from POSIX tools. The cap is 8 MiB (jc_peercap.h), so
# this crosses it with a margin and still costs well under a second to produce.
cat > "$tmp/runaway_mcp.sh" <<'MOCK'
#!/bin/sh
# Read the initialize request, then answer with a flood that never ends a line.
read -r _line
dd if=/dev/zero bs=65536 count=144 2>/dev/null | tr '\0' 'x'
# Stay alive: if this exits, jichi sees EOF and takes the closed-server path,
# which would make the check below pass for the wrong reason.
sleep 60
MOCK

# The good server is the tier's own shared mock, not a hand-written one: it
# echoes each request's id, which jichi matches on, and the first version of
# this driver did not -- so check 4 failed for a defect in my fixture rather
# than in the bound. Reuse the mock the other MCP drivers already prove.
smoke_write_mock_mcp "$tmp/good_mcp.sh"
chmod +x "$tmp/runaway_mcp.sh" "$tmp/good_mcp.sh"

write_cfg() {
    cat > "$tmp/config-$1.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1"}],
 "mcpServers":[{"name":"mock","command":"/bin/sh","args":["$tmp/$1_mcp.sh"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"lowResource":false}
EOF
}
write_cfg runaway
write_cfg good

# --- 1: the fixture really crosses the cap ------------------------------------
# Without this the whole driver can pass while the mock emits 40 bytes.
# `wc -c` straight off the pipe: head_bytes caps at its own buffer and reported
# 8192 here, which would have failed this check for the wrong reason.
bytes=$(dd if=/dev/zero bs=65536 count=144 2>/dev/null | tr '\0' 'x' | wc -c \
        | tr -d '[:space:]')
if [ "${bytes:-0}" -gt 8388608 ]; then
    t_ok "the mock emits $bytes bytes with no newline (cap is 8388608)"
else
    t_fail "the mock emitted only ${bytes:-0} bytes -- it does not reach the cap, \
so every check below would pass without the bound ever being crossed"
fi

# --- 2: jichi refuses PROMPTLY, not at the 120 s deadline ---------------------
# The property the fix changes is latency. The unbounded reader blocks until
# MCP_IO_TIMEOUT_SECS (120) because the server is still alive and still sending.
t0=$(date +%s)
out=$(with_deadline 90 "$BIN" --config "$tmp/config-runaway.json" mcp resources \
      < /dev/null 2>&1)
t1=$(date +%s)
elapsed=$((t1 - t0))
if [ "$elapsed" -lt 30 ]; then
    t_ok "the runaway server is refused in ${elapsed}s, not at the 120 s deadline"
else
    t_fail "took ${elapsed}s -- that is the deadline path, which is what an \
UNBOUNDED reader does; the cap did not fire"
fi

# --- 3: the message names the bound -------------------------------------------
# A bare "mcp: timed out" is indistinguishable from a slow network. The number
# is what tells a reader this was a protocol violation with a known limit.
if printf '%s' "$out" | grep -q 'no newline' && \
   printf '%s' "$out" | grep -q '8388608'; then
    t_ok "the refusal names the cap in bytes"
else
    t_fail "the refusal does not name the cap: $(printf '%s' "$out" | head_bytes 200)"
fi

# --- 4: the happy path is untouched -------------------------------------------
good=$(with_deadline 30 "$BIN" --config "$tmp/config-good.json" mcp resources \
       < /dev/null 2>&1)
if printf '%s' "$good" | grep -q 'mem://notes'; then
    t_ok "a well-formed server still lists its resources (the bound cost nothing)"
else
    t_fail "the good server no longer works: $(printf '%s' "$good" | head_bytes 200)"
fi

t_done
