#!/bin/sh
# smoke: one line from an ACP client is bounded, and the bound says so (M661).
#
# THE SIBLING OF peer_line_cap.sh, and the half M659 shipped WITHOUT a driver.
# Both readers accumulate a newline-delimited message from a peer jichi does not
# control; both are capped at JC_PEER_LINE_MAX (include/jc_peercap.h). The MCP
# driver can lean on latency, because that reader has a 120-second deadline and
# an uncapped run blocks against it. THIS path has no deadline at all: with or
# without the cap, jichi ends when stdin closes. So there is no timing signal,
# and the ONLY observable is the message.
#
# That is not a weakness of the test, it is the argument for the convention.
# M659 wrote it as a decision -- "a cap that says nothing is a cap nobody can
# test, and nobody can debug either" -- and this driver is where the claim gets
# paid for: the one thing distinguishing "bounded" from "the client went away"
# here is that the bound says its own name and number.
#
# WHAT IS AND IS NOT COVERED:
#   checked -- the fixture really crosses the cap; jichi terminates rather than
#              hanging on it; the refusal NAMES the cap in bytes, which is the
#              check that actually fails when the cap is reverted; and a normal
#              ACP initialize still gets a response, so the bound cost nothing.
#   not checked -- peak RSS, for the reason its sibling gives: it was never the
#              property, and believing it was is what parked both rows from M609
#              to M659.
#   not checked -- that jichi stops READING at the cap rather than merely
#              reporting it. Distinguishing those needs the writer to observe
#              EPIPE, which needs SIGPIPE handling in a POSIX-sh fixture; the
#              discard is asserted by reading jc_acp.c, not by this driver.
. "$(dirname "$0")/_smoke.sh"

t_plan 4

smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"lowResource":false}
EOF

# 9 MiB with no newline. The cap is 8 MiB, so this crosses it with a margin.
flood() { dd if=/dev/zero bs=65536 count=144 2>/dev/null | tr '\0' 'x'; }

# --- 1: the fixture really crosses the cap ------------------------------------
bytes=$(flood | wc -c | tr -d '[:space:]')
if [ "${bytes:-0}" -gt 8388608 ]; then
    t_ok "the fixture is $bytes bytes with no newline (cap is 8388608)"
else
    t_fail "the fixture is only ${bytes:-0} bytes -- it never reaches the cap, \
so every check below would pass without the bound being crossed"
fi

# --- 2: jichi terminates rather than hanging on it ----------------------------
# A floor, not the property: an UNCAPPED jichi also terminates here, when stdin
# closes. What this rules out is the bound turning a flood into a hang.
t0=$(date +%s)
err=$(flood | with_deadline 60 "$BIN" --config "$tmp/config.json" --acp 2>&1 >/dev/null)
t1=$(date +%s)
if [ $((t1 - t0)) -lt 45 ]; then
    t_ok "the flood ends the session in $((t1 - t0))s rather than hanging"
else
    t_fail "took $((t1 - t0))s -- the deadline cut it, which is a hang"
fi

# --- 3: the refusal NAMES the cap -- the one discriminating check -------------
# Without the bound jichi buffers the whole 9 MiB, hands it on as one line and
# fails to parse it; the operator learns nothing about why. This is the check
# that goes red when the cap is reverted, and the reason the cap speaks at all.
if printf '%s' "$err" | grep -q 'no newline' && \
   printf '%s' "$err" | grep -q '8388608'; then
    t_ok "the refusal names the cap in bytes"
else
    t_fail "the refusal does not name the cap: $(printf '%s' "$err" | head_bytes 200)"
fi

# --- 4: a well-formed session is untouched ------------------------------------
init='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}'
good=$(printf '%s\n' "$init" | with_deadline 30 "$BIN" --config "$tmp/config.json" --acp 2>/dev/null)
# One LINE, not the whole stream: jichi also emits notifications, and handing
# jsonq a multi-line blob makes it reject a response that is plainly there --
# which is how this check first failed against correct output.
_resp=$(printf '%s\n' "$good" | grep '"id":1' | head -1)
if printf '%s\n' "$_resp" | "$JQ" -q '.result' >/dev/null 2>&1; then
    t_ok "a well-formed initialize still gets a result (the bound cost nothing)"
else
    t_fail "initialize no longer answers: $(printf '%s' "$good" | head_bytes 200)"
fi

t_done
