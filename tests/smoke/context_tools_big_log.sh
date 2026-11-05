#!/bin/sh
# smoke: F-04 -- telemetry readers must be honest past JC_READ_FILE_MAX (64 MiB).
#
# The defect. A workspace's telemetry log grows one compact JSON line per event,
# with no rotation (def-198). Past 64 MiB, jc_read_file (the whole-file slurp)
# refuses the file, and the readers:
#   - `jichi telemetry <path>` printed "could not read <path>" (exit 1)
#   - `jichi context tools` printed "no telemetry log for this workspace"
#
# Both readers now stream one line at a time (jc_telemetry_feed_file), so they
# stay honest at any log size. This driver builds a 420,000-line log (~69 MB,
# over JC_READ_FILE_MAX = 67,108,864 B) of valid metrics-tier tool_call events
# for one workspace, then asserts:
#   1. the fixture actually exceeds the slurp cap (size > 67,108,864 B)
#   2. `jichi telemetry <path> --workspace <ws>` exits 0 and reports the
#      per-tool count (420,000 read_file calls), not "could not read"
#   3. `jichi context tools` (in-process, via jc_app_load_telemetry) reports
#      the log's tool count, not "no telemetry log for this workspace"
#   4. a missing path still prints "could not read" (the reader's real error
#      is preserved -- the fix only removed the false error for a present but
#      oversized file)
#
# The fixture lives in $HOME/.jichi.d/telemetry/ under smoke_tmp, which the
# trap cleans up -- no >10 MB file is left behind.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$HOME/.jichi.d/telemetry" "$ws"

N=420000
log="$HOME/.jichi.d/telemetry/big.jsonl"

# Build the fixture: N lines, each a valid metrics-tier tool_call event for
# this workspace. Using awk for speed (no per-line shell process spawn).
# Each line is ~170 bytes, so N x 170 = ~71 MB, safely over the 64 MiB cap.
with_deadline 60 awk -v n="$N" -v ws="$ws" \
    'BEGIN{for(i=0;i<n;i++)
     printf "{\"v\":1,\"jichi\":\"jichi-test\",\"ts\":1761000000,\"seq\":%d,\"event\":\"tool_call\",\"name\":\"read_file\",\"ws\":\"%s\",\"ok\":true,\"duration_ms\":12,\"output_bytes\":2048}\n",i,ws}' \
    > "$log"

# --- 1: the fixture actually exceeds JC_READ_FILE_MAX ---------------------------
bytes=$(wc -c < "$log" | tr -d ' ')
if [ "$bytes" -gt 67108864 ]; then
    t_ok "the fixture is $bytes bytes (cap is 67,108,864)"
else
    t_fail "the fixture is only $bytes bytes -- it never crosses the cap; the \
test proves nothing"
fi

# --- 2: jichi telemetry <path> --workspace <ws> reads it ------------------------
# (the headless path: run_telemetry in main.c)
out="$tmp/out.telemetry"
with_deadline 120 "$BIN" telemetry "$log" --workspace "$ws" \
    < /dev/null > "$out" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && ! grep -q 'could not read' "$out" && \
   grep -qE 'read_file +calls=420000' "$out"; then
    t_ok "jichi telemetry reads the $bytes B log: exit 0, 420000 read_file calls, no 'could not read'"
else
    t_fail "jichi telemetry failed (rc=$rc): $(grep -E 'could not read|read_file +calls' "$out" | tr '\n' '|')"
fi

# --- 3: jichi context tools (in-process) reads it --------------------------------
# (jc_app_load_telemetry -> streaming path; the log must be at the DEFAULT
#  per-workspace path for this ws, or the app_newest_jsonl fallback must pick
#  it up. We placed it at big.jsonl which is the only .jsonl in the dir, so
#  the fallback picks it. The ws filter in jc_app_load_telemetry uses
#  app->root, which is $ws (the cwd jichi was launched from).)
out3="$tmp/out.context"
(cd "$ws" && with_deadline 120 "$BIN" context tools < /dev/null) > "$out3" 2>&1
rc3=$?
if [ "$rc3" -eq 0 ] && ! grep -q 'no telemetry log for this workspace' "$out3" && \
   grep -qE '420000 tool calls|420000' "$out3"; then
    t_ok "context tools reads the log: $rc3, no 'no telemetry', reports tool calls"
else
    t_fail "context tools failed (rc=$rc3): $(grep -E 'no telemetry|Use:' "$out3" | tr '\n' '|')"
fi

# --- 4: a genuinely missing path still prints 'could not read' -------------------
# (the reader's real error is preserved)
missing="$tmp/does_not_exist.jsonl"
out4="$tmp/out.missing"
with_deadline 30 "$BIN" telemetry "$missing" --workspace "$ws" \
    < /dev/null > "$out4" 2>&1
rc4=$?
if [ "$rc4" -ne 0 ] && grep -q 'could not read' "$out4"; then
    t_ok "a missing path still prints 'could not read' (rc=$rc4, the real error is preserved)"
else
    t_fail "missing path: rc=$rc4 -- expected 'could not read' in output, got: $(head -3 "$out4" | tr '\n' '|')"
fi

t_done
