#!/bin/sh
# smoke: the prose-tool-call nudge (M147). A small model's classic failure:
# it DESCRIBES a tool call (a fenced JSON block) instead of invoking it
# natively. Turn 1 the mock answers with that prose and no native call;
# the loop must detect it and inject the corrective nudge; turn 2 the mock
# answers NUDGE_OK only if the nudge text arrived in the request. A pass
# proves detection, injection, and the retry round trip.
# (Port of tests/e2e/prose_nudge.py, M211; the multi-line prose reply uses
# mockmodel's sse-file escape hatch.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Turn 1: prose narrating a read_file call inside a fenced json block --
# newlines live as \n INSIDE the JSON string, so the SSE stays one line
# per event.
cat > "$tmp/prose.sse" <<'EOF'
data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"I'll read the file now:\n\n```json\n{\"name\": \"read_file\", \"arguments\": {\"path\": \"a.txt\"}}\n```\n"},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]

EOF

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  sse-file $tmp/prose.sse
rule
  match "did not invoke it"
  text NUDGE_OK
rule
  text NO_NUDGE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "read a.txt" < /dev/null); rc=$?
mm_stop

case "$out" in
    *NUDGE_OK*) t_ok "prose call detected, nudged, retried (NUDGE_OK)" ;;
    *NO_NUDGE*) t_fail "retry happened but the nudge text never arrived" ;;
    *) t_fail "no marker (rc=$rc): $(printf '%s' "$out" | head_bytes 150)" ;;
esac

if [ -f "$tmp/req.2" ]; then
    t_ok "a second request reached the mock (the retry round trip)"
else
    t_fail "only one request -- no retry happened"
fi

if grep -q "did not invoke it" "$tmp/req.2" 2>/dev/null; then
    t_ok "the corrective nudge text is in the retry request"
else
    t_fail "nudge text missing from request 2"
fi

t_done
