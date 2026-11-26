#!/bin/sh
# smoke: the same-model child's connect-fallback frees the lead's provider
# (M1219, the [130] UAF cell, the [143] REL-A READY follow-up).
#
# THE GAP THIS CLOSES. A same-model spawn_subagent child runs IN-PROCESS on
# the shared app; its provider ALIASES app->provider. If the child's model call
# is a connect failure, stream_once's M1038 fallback re-routes: jc_app_route_to
# -> jc_app_switch_model FREES app->provider and installs the fallback's. The
# lead's loop-local `prov` still points at the FREED provider, and the lead's
# re-syncs are all blind to it. The lead's next stream_once does
# `prov->vt->add_headers(prov, ...)` on freed memory: ASan heap-use-after-free.
# This is the [130] UAF, the [143] cell's focus. Non-SAN builds usually do not
# abort (the stale bytes survive), so the driver MUST be SAN-only.
#
# THE PROBE (run 0115, head 3ea2b1b93, before the fix). A (lead AND child,
# same entry): rule 1 catch-all text for the GET probe; rule 2 tool
# spawn_subagent {"task":"child: reply with the word ok"}; --max-requests 2 =>
# A serves the lead's round-1 tool call and DIES, so the child's model call is
# a genuine connect failure. B (the live fallback, A's fallback): rule 1
# match "lead: go" -> text LEAD_DONE_ON_B; rule 2 catch-all -> text
# CHILD_DONE_ON_B. Config: two entries (A fallback B), maxRetries 0,
# connectTimeout 1. Run: jichi --config cfg.json -q --auto --no-session -p
# "lead: go" < /dev/null
#
# THE MEASUREMENT. The child's connect-fallback fires IN-PROCESS (stderr
# carries `[fallback] http://127.0.0.1:$A ... -> B`), app->provider is freed
# and replaced, the child finishes on B, the lead's round-2 stream_once reads
# the freed provider, and ASan ABORTS the process:
#   ==<pid>==ERROR: AddressSanitizer: heap-use-after-free on address 0x...
#       #0 in stream_once
#       #1 in run_agent_loop
#       ...
#   freed by jc_app_switch_model <- jc_app_route_to <- stream_once <-
#   run_agent_loop <- run_subagent_posture <- jc_agent_run_subagent <-
#   subagent_run <- jc_tool_execute <- run_agent_loop (the LEAD's round-1
#   tool exec).
#
# PRE-RED. ASan heap-use-after-free in stream_once, freed by the child's
# jc_app_route_to. The run aborts (rc != 0) and stderr carries the ASan
# report. The fix is b1's cell.
#
# POST-GREEN. No ASan report, rc 0, the lead's round-2 reached B's mock,
# LEAD_DONE_ON_B on stdout. b2-b4 verify the fix does not break anything.
. "$(dirname "$0")/_smoke.sh"

# ---- preconditions (before t_plan: t_skip is a whole-driver verb) ------------
strings "$BIN" 2>/dev/null | grep -qE "LeakSanitizer|__asan_" \
    || t_skip "needs a SAN=1 build (make clean && make SAN=1 CC=gcc jichi)"

smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# For debugging, preserve the temp dir
echo "TMPDIR: $tmp" >&2

# ---- 0: the instrument must fire (a planted UAF must be detected) ------------
# Build a tiny UAF detector (malloc/free/deref). If it's not detected, the
# sanitizer is not armed, so the driver must skip. This is the driver's own
# sanitizer test, not the UAF under test.
cat > "$tmp/uaf.c" <<'EOF'
#include <stdlib.h>
int main(void)
{
    char *p = (char *)malloc(64);
    if (p == 0) return 1;
    p[0] = 'x';
    free(p);
    p[0] = 'y';         /* UAF: use after free */
    return 0;
}
EOF
${CC:-cc} -fsanitize=address -o "$tmp/uaf" "$tmp/uaf.c" 2>/dev/null \
    || t_skip "no working -fsanitize=address compiler, so the UAF detector cannot be armed"

ASAN_OPTIONS="detect_leaks=1:exitcode=0" "$tmp/uaf" > /dev/null 2> "$tmp/uaf.err"
if grep -q "ERROR: AddressSanitizer" "$tmp/uaf.err"; then
    t_ok "the sanitizer runtime detects a planted heap-use-after-free"
else
    t_fail "AddressSanitizer did not detect a planted UAF:
$(head_bytes 300 < "$tmp/uaf.err")
The sanitizer is not armed, so check 1 below cannot distinguish
a clean run from a disabled checker. Fix the environment, not the test."
fi

t_plan 9

# ---- the fixture --------------------------------------------------------------
# A (lead AND child model, same entry): serves 2 requests then exits.
# Rule 1 (count 1): text PROBE_A - fires on request 1 (GET /v1/models probe).
# Rule 2 (count 2): tool spawn_subagent {"task":"child: reply with the word ok"}.
# --max-requests 2 => A serves both requests then DIES, so the child's
# model calls fail with connect error (not HTTP error).
# B (the live fallback, A's fallback): rule 1 match "lead: go" -> text
# LEAD_DONE_ON_B; rule 2 catch-all -> text CHILD_DONE_ON_B.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  text PROBE_A
rule
  count 2
  tool spawn_subagent {"task":"child: reply with the word ok"}
rule
  match "lead: go"
  text LEAD_DONE_ON_B
rule
  text CHILD_DONE_ON_B
EOF

# Two model entries: a (lead AND child) with fallback b; two mockmodels: A
# (dies after 2 requests) and B (the live fallback).
mm_start "$tmp/replies.mm" "$tmp/cap" 2
A_PORT=$MM_PORT
# B's mockmodel: the lead's round-2 (body carries the prompt 'lead: go') and
# the child's round-1 (everything else).
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  match "lead: go"
  text LEAD_DONE_ON_B
rule
  text CHILD_DONE_ON_B
EOF
mm_start "$tmp/b.mm" "$tmp/cap_b"
B_PORT=$MM_PORT
# Config: A with fallback B, both live endpoints
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"a","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","roles":["chat"],"fallback":"b"},
{"name":"b","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# halt_on_error=1: the first ASan error kills the run, so the report is THE one.
# env (not assignment prefix): OpenBSD's /bin/sh does not export prefixes.
(cd "$ws" && with_deadline 120 env ASAN_OPTIONS=halt_on_error=1 "$BIN" \
    --config "$tmp/config.json" --auto --no-session \
    -p "lead: go" < /dev/null > "$tmp/out" 2> "$tmp/err") || true

mm_stop

# ---- b1 (THE DEFECT, RED pre-fix) ---------------------------------------------
if grep -q 'heap-use-after-free' "$tmp/err"; then
    t_fail "AddressSanitizer heap-use-after-free in the lead's loop:
$(grep 'ERROR: AddressSanitizer' "$tmp/err" | head -1 | head_bytes 200)
freed-by frame: $(grep -E '(stream_once|jc_app_switch_model|jc_app_route_to)' "$tmp/err" | head -1 | head_bytes 240)"
elif grep -q 'LEAD_DONE_ON_B' "$tmp/out"; then
    t_ok "no ASan heap-use-after-free, the lead's round-2 reached B"
else
    t_fail "neither ASan report nor LEAD_DONE_ON_B:
out: $(head_bytes 200 < "$tmp/out")
err: $(head_bytes 300 < "$tmp/err")"
fi

# ---- 2 (the child's fallback itself) ------------------------------------------
# The child's fallback worked if B served requests. We now assert on the exact
# request shapes to verify the fallback worked correctly.
#   req.1: GET probe
#   req.2: POST with "the word ok" (child's tool call)
#   req.3: POST with "lead: go" and "CHILD_DONE_ON_B" (lead's r2 with tool result)
# Total: exactly 3 requests, 2 POSTs. Both POSTs must be on /v1/chat/completions.

# Count POST requests (req.2 and req.3 are POSTs, req.1 is GET)
_post_count_b=0
for _f in "$tmp/cap_b"/req.*; do
    [ -e "$_f" ] || continue
    _first_line=$(head -1 "$_f")
    case "$_first_line" in
        POST\ */v1/chat/completions*) _post_count_b=$((_post_count_b + 1)) ;;
    esac
done

# b2(a): B's capture holds exactly 2 POST requests (both /v1/chat/completions)
if [ "$_post_count_b" -eq 2 ]; then
    t_ok "b2(a): B's capture holds exactly 2 POST /v1/chat/completions requests"
else
    t_fail "b2(a): B's capture has $_post_count_b POST /v1/chat/completions, expected 2"
fi

# b2(b): req.2 body carries the child's task text ('the word ok')
_child_task_ok=0
if grep -q "the word ok" "$tmp/cap_b/req.2" 2>/dev/null; then
    _child_task_ok=1
fi
if [ "$_child_task_ok" -eq 1 ]; then
    t_ok "b2(b): req.2 body carries the child's task text 'the word ok'"
else
    t_fail "b2(b): req.2 body does not contain 'the word ok'"
fi

# b2(c): req.3 body carries 'lead: go' AND the child's answer 'CHILD_DONE_ON_B'
_lead_go_ok=0
_child_ans_ok=0
if grep -q "lead: go" "$tmp/cap_b/req.3" 2>/dev/null; then
    _lead_go_ok=1
fi
if grep -q "CHILD_DONE_ON_B" "$tmp/cap_b/req.3" 2>/dev/null; then
    _child_ans_ok=1
fi
if [ "$_lead_go_ok" -eq 1 ] && [ "$_child_ans_ok" -eq 1 ]; then
    t_ok "b2(c): req.3 body carries both 'lead: go' and 'CHILD_DONE_ON_B'"
else
    t_fail "b2(c): req.3 missing 'lead: go' ($_lead_go_ok) or 'CHILD_DONE_ON_B' ($_child_ans_ok)"
fi

# ---- 3: no-clobber: same-model child, both live, no fallback ------------------
# Same-model child, A with enough --max-requests (4: probe + lead r1 + child r1 + lead r2),
# B live but receives 0 requests. The lead's round-2 POST should reach A (names model a).
# A's script:
#   rule 1 count 1 text PROBE_A (GET probe)
#   rule 2 count 2 tool spawn_subagent {"task":"child: reply with the word ok"}
#   rule 3 count 3 text CHILD_DONE_ON_A (child's answer, no tool)
#   rule 4 count 4 text LEAD_DONE_ON_A (lead's r2 answer)
# B's script: one catch-all (never used, B receives 0 requests)

# Restart mocks with fresh dirs
cat > "$tmp/replies2.mm" <<'EOF'
wire openai
rule
  count 1
  text PROBE_A
rule
  count 2
  tool spawn_subagent {"task":"child: reply with the word ok"}
rule
  count 3
  text CHILD_DONE_ON_A
rule
  count 4
  text LEAD_DONE_ON_A
EOF

# B's mock receives 0 requests - just needs to be alive
cat > "$tmp/b2.mm" <<'EOF'
wire openai
rule
  text NEVER_USED
EOF

mm_start "$tmp/replies2.mm" "$tmp/cap2" 4
A_PORT=$MM_PORT
mm_start "$tmp/b2.mm" "$tmp/cap_b2"
B_PORT=$MM_PORT

cat > "$tmp/config2.json" <<EOF
{"models":[{"name":"a","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","roles":["chat"],"fallback":"b"},
{"name":"b","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# halt_on_error=1: the first ASan error kills the run
(cd "$ws" && with_deadline 120 env ASAN_OPTIONS=halt_on_error=1 "$BIN" \
    --config "$tmp/config2.json" --auto --no-session \
    -p "lead: go" < /dev/null > "$tmp/out2" 2> "$tmp/err2") || true

mm_stop

# b3(a): run rc 0, no ASan line in stderr
if grep -qE "ERROR: AddressSanitizer|heap-use-after-free" "$tmp/err2" 2>/dev/null; then
    t_fail "b3(a): ASan error in stderr"
else
    t_ok "b3(a): no ASan error in stderr"
fi

# b3(b): stdout carries LEAD_DONE_ON_A
if grep -q "LEAD_DONE_ON_A" "$tmp/out2"; then
    t_ok "b3(b): stdout carries LEAD_DONE_ON_A"
else
    t_fail "b3(b): stdout missing LEAD_DONE_ON_A"
fi

# b3(c): A's capture holds exactly 4 requests (GET + 3 POSTs), req.4 contains CHILD_DONE_ON_A
_nreq_a=$(ls "$tmp/cap2"/req.* 2>/dev/null | wc -l | tr -d '[:space:]')
[ -n "$_nreq_a" ] || _nreq_a=0
_req4_ok=0
if [ "$_nreq_a" -eq 4 ]; then
    if grep -q "CHILD_DONE_ON_A" "$tmp/cap2/req.4" 2>/dev/null; then
        _req4_ok=1
    fi
fi
if [ "$_nreq_a" -eq 4 ] && [ "$_req4_ok" -eq 1 ]; then
    t_ok "b3(c): A's capture holds exactly 4 requests, req.4 contains CHILD_DONE_ON_A"
else
    t_fail "b3(c): A's capture has $_nreq_a requests (expected 4), req.4 CHILD_DONE_ON_A: $_req4_ok"
fi

# b3(d): B's capture holds exactly 0 POST /v1/chat/completions requests
_post_count_b2=0
for _f in "$tmp/cap_b2"/req.*; do
    [ -e "$_f" ] || continue
    _first_line=$(head -1 "$_f")
    case "$_first_line" in
        POST\ */v1/chat/completions*) _post_count_b2=$((_post_count_b2 + 1)) ;;
    esac
done
if [ "$_post_count_b2" -eq 0 ]; then
    t_ok "b3(d): B's capture holds exactly 0 POST /v1/chat/completions requests"
else
    t_fail "b3(d): B's capture has $_post_count_b2 POST /v1/chat/completions requests (expected 0)"
fi

t_done
