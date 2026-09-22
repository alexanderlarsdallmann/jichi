#!/bin/sh
# smoke: ONE REAL AGENT TURN under LeakSanitizer (M694).
#
# THE GAP THIS CLOSES. `scripts/leakcheck.sh` (M693) runs seven subcommands
# under LSan -- `--version doctor models describe context assignments skills`.
# Every one of them is READ-ONLY and none of them calls a model. So the path
# that is actually jichi -- build_request, jc_http's upload, the SSE framing,
# the provider's on_event, jc_tool_execute, the history append, and the loop
# back around for a second request -- has never been run under a leak checker
# by anything in the gate. `make ci`'s sanitizer stage is `make SAN=1 test`,
# the unit suite, which does not enter main()'s dispatch either.
#
# That is a large blind spot for the three-arena design. A lifetime bug in the
# turn loop is exactly the class `docs/analysis/2026-07-29-tool-arena.md` was
# written about, and exactly the class a unit test does not reach.
#
# WHY A FULL TOOL ROUND AND NOT A PLAIN ANSWER. A text-only turn exercises one
# request and one parse. The tool round is what makes it worth the fixture: it
# runs jc_tool_execute, resets app->tool_scratch, appends a tool-role message,
# and calls build_request a SECOND time with a longer history. Check 2 asserts
# the second request reached the server, because that is the cheapest proof
# that the whole round happened rather than the first half of it.
#
# THE FLOORS COME FIRST, AND THAT IS THE POINT. A clean LeakSanitizer report
# from a turn that never ran is not evidence of anything -- it is the same
# vacuous green as a lint scanning zero files (ANECDOTES #89). So checks 1-3
# establish that the instrument fires and that the turn really happened, and
# only check 4 reads the verdict.
#
# Check 1 is the instrument proof: a planted leak, compiled here, must be
# reported by this host's sanitizer runtime. Without it, check 4 cannot tell
# "no leaks" from "leak detection is off" -- and detect_leaks is off by default
# on some platforms, which is precisely the silent-vacuous-pass shape.
. "$(dirname "$0")/_smoke.sh"

# ---- preconditions (before t_plan: t_skip is a whole-driver verb) ------------
strings "$BIN" 2>/dev/null | grep -qE "LeakSanitizer|__asan_" \
    || t_skip "needs a SAN=1 build (make clean && make SAN=1 CC=clang jichi)"

smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/leaker.c" <<'EOF'
#include <stdlib.h>
char *g;
int main(void)
{
    g = (char *)malloc(64);
    if (g == 0) return 1;
    g[0] = 'x';
    g = 0;              /* unreachable at exit -- a DIRECT leak, not "still reachable" */
    return 0;
}
EOF
${CC:-cc} -fsanitize=address -o "$tmp/leaker" "$tmp/leaker.c" 2>/dev/null \
    || t_skip "no working -fsanitize=address compiler, so the leak detector cannot be armed -- every verdict here would be unvalidated"

t_plan 4

# ---- 1: the instrument fires ------------------------------------------------
ASAN_OPTIONS="detect_leaks=1:exitcode=0" "$tmp/leaker" > /dev/null 2> "$tmp/leaker.err"
if grep -q "LeakSanitizer: detected memory leaks" "$tmp/leaker.err"; then
    t_ok "the sanitizer runtime reports a planted 64-byte leak"
else
    t_fail "a planted, deliberately-unreachable 64-byte allocation was NOT reported:
$(head_bytes 300 < "$tmp/leaker.err")
Leak detection is not armed on this host, so check 4 below could not distinguish
a clean run from a disabled checker. Fix the environment, not the floor."
fi

# ---- the turn ---------------------------------------------------------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"hello.txt"}
rule
  text LEAKTURN_DONE
EOF
printf 'the quick brown fox\n' > "$ws/hello.txt"

mm_start "$tmp/replies.mm" "$tmp/cap" 4
write_config "$tmp/config.json" "$MM_PORT"

# The exit code is deliberately not asserted, for leakcheck.sh's reason: what is
# under test is the absence of a leak report, and conflating the two makes the
# fixture fail for the wrong reason. ASAN_OPTIONS keeps exitcode=0 so a leak
# shows up as a REPORT here rather than as a process failure the deadline eats.
(cd "$ws" && ASAN_OPTIONS="detect_leaks=1:exitcode=0" \
    with_deadline 120 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 200k \
    -p "read hello.txt and tell me what it says" < /dev/null) \
    > "$tmp/out.txt" 2> "$tmp/err.txt"
mm_stop

# ---- 2: the whole tool round happened ---------------------------------------
_nreq=$(ls "$tmp/cap"/req.* 2>/dev/null | wc -l | tr -d '[:space:]')
[ -n "$_nreq" ] || _nreq=0
if [ "$_nreq" -ge 2 ]; then
    t_ok "the turn made $_nreq requests -- the tool result was fed back and rebuilt"
else
    t_fail "only $_nreq request(s) reached the mock server. The tool round did not
complete, so a clean leak report below would cover the first half of one turn.
stderr: $(head_bytes 300 < "$tmp/err.txt")"
fi

# ---- 3: the turn produced its answer ----------------------------------------
if grep -q "LEAKTURN_DONE" "$tmp/out.txt"; then
    t_ok "the final answer reached stdout after the tool round"
else
    t_fail "no final answer on stdout -- the run did not finish the turn:
out: $(head_bytes 200 < "$tmp/out.txt")
err: $(head_bytes 300 < "$tmp/err.txt")"
fi

# ---- 4: the verdict ---------------------------------------------------------
if grep -q "LeakSanitizer: detected memory leaks" "$tmp/err.txt"; then
    t_fail "LeakSanitizer reported a leak on one agent turn:
$(grep -A12 'detected memory leaks' "$tmp/err.txt" | head_bytes 900)"
else
    t_ok "no leak reported across one full tool round ($_nreq requests)"
fi

t_done
