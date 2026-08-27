#!/bin/sh
# smoke: mid-turn elision leaves a claim ticket (M348).
#
# The lossy mid-turn pass used to replace a tool result's middle with
# "[N bytes elided to fit the context window]" -- a receipt with no claim
# ticket. The model that still needed those bytes re-ran the original call at
# full price, which is the measured re-read loop (72% of reads were re-reads,
# one path 216x, and 82 of 142 advisory-firing re-reads immediately followed
# another read_file). M348: the full original content is preserved to the
# M339 store (fence-readable, 0600, outside every workspace) and the marker
# names the path.
#
# The scenario forces a real elision: a ~9 KB read under contextLimit 1500
# trips the 80% high-water, and four filler rounds age the big result past the
# keep-recent window. Checks pair presence with absence per M310: request 2
# must carry the RAW result and no ticket; a later request must carry the
# ticket; and the ticketed file must hold the sentinel from the elided middle.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
snap=$(smoke_tmp)

# ~9 KB fixture with a sentinel in the middle (inside the elided span: the
# marker keeps 400 head + 200 tail bytes, the sentinel sits ~4.5 KB in).
i=1
while [ "$i" -le 200 ]; do
    if [ "$i" -eq 100 ]; then
        printf 'ELIDE_TICKET_SENTINEL_4242 lives in the middle\n'
    else
        printf 'line %04d pad pad pad pad pad pad pad pad\n' "$i"
    fi
    i=$((i + 1))
done > "$ws/big.txt"
printf 'tiny\n' > "$ws/small.txt"

cat > "$tmp/e.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"big.txt"}
rule
  count 2
  tool read_file {"path":"small.txt"}
rule
  count 3
  tool read_file {"path":"small.txt"}
rule
  count 4
  tool read_file {"path":"small.txt"}
rule
  count 5
  tool read_file {"path":"small.txt"}
rule
  text TICKET_DONE
EOF
mm_start "$tmp/e.mm" "$tmp/cap" 7
# Tickets are TURN-LIVED: jc_toolout_cleanup removes the per-PID store at
# teardown (the store stays bounded; check 6 asserts it). So the mid-run view
# -- the one the model actually has -- is captured by a PostToolUse hook that
# snapshots the store after every call.
write_config "$tmp/c.json" "$MM_PORT" '"contextLimit":1500,
 "hooksEnabled":true,
 "hooks":{"PostToolUse":[{"commands":[{"shell":"cp -r \"$HOME/.jichi.d/tool-output/.\" '"$snap"'/ 2>/dev/null; exit 0"}]}]}'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session -p "study the files" < /dev/null) >/dev/null 2>"$tmp/err"
mm_stop

# --- 1: before any elision, the raw result and no ticket (M310 pairing) -------
if [ -s "$tmp/cap/req.2" ] && \
   grep -q "ELIDE_TICKET_SENTINEL_4242" "$tmp/cap/req.2" && \
   ! grep -q "preserved at" "$tmp/cap/req.2"; then
    t_ok "request 2 carries the raw result and no ticket"
else
    t_fail "request 2 wrong shape (missing sentinel or premature ticket)"
fi

# --- 2: a later request carries the claim ticket ------------------------------
if grep -l "the complete result is preserved at" "$tmp/cap"/req.* \
        > "$tmp/hit" 2>/dev/null && [ -s "$tmp/hit" ]; then
    t_ok "a later request's marker names a preservation path"
else
    t_fail "no claim ticket in any captured request"
fi

# --- 3: the ticket file existed MID-RUN (the hook's snapshot) ------------------
tik=$(grep -oh 'preserved at [^ ]*' "$tmp/cap"/req.* 2>/dev/null \
      | tail -1 | sed 's/^preserved at //')
base=$(basename "$tik" 2>/dev/null)
got=$(find "$snap" -name "$base" 2>/dev/null | head -1)
if [ -n "$base" ] && [ -n "$got" ] && [ -s "$got" ]; then
    t_ok "the ticket file existed while the run needed it ($base)"
else
    t_fail "ticket '$base' not found in the mid-run snapshot"
fi

# --- 4: ...and holds the FULL original, elided middle included ----------------
if [ -n "$got" ] && grep -q "ELIDE_TICKET_SENTINEL_4242" "$got" 2>/dev/null; then
    t_ok "the elided middle is retrievable from the ticket"
else
    t_fail "the sentinel is not in the ticket file"
fi

# --- 5: the ticket lives in jichi's private store, not the workspace ----------
case "$tik" in
    "$HOME"/.jichi.d/tool-output/*) t_ok "ticket is in the private store" ;;
    *) t_fail "ticket landed outside the store: '$tik'" ;;
esac

# --- 6: ...and the store is bounded: the per-PID dir is gone after exit -------
if [ -n "$tik" ] && [ ! -e "$(dirname "$tik")" ]; then
    t_ok "teardown cleaned the per-run store (tickets are turn-lived)"
else
    t_fail "the per-run store survived exit: $(dirname "$tik")"
fi

t_done
