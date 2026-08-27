#!/bin/sh
# smoke: the workspace path-containment fence (M24). Under --auto the mock
# orders a write_file aimed OUTSIDE the workspace: with the fence ON
# (default under --auto) the file must not appear yet the turn still
# completes; with --no-path-fence the same write succeeds -- proving the
# fence, not some other guard, blocked it.
# (Port of tests/e2e/pathfence.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
other=$(smoke_tmp)
outside="$other/escape.txt"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"$outside","content":"pwned"}
rule
  text FENCE_DONE
EOF

# Case 1: fence ON (the --auto default) -> the outside write is denied
mkdir -p "$tmp/cap1"
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "write the file" < /dev/null); rc=$?
mm_stop
if [ ! -e "$outside" ]; then
    t_ok "fence ON: the out-of-root write was denied"
else
    t_fail "fence ON but the outside file was written"
fi
case "$out" in
    *FENCE_DONE*) t_ok "the turn still completed after the denial" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# Case 2: --no-path-fence -> the same write succeeds (the control)
mkdir -p "$tmp/cap2"
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
      -q --no-session --auto --no-path-fence -p "write the file" \
      < /dev/null); rc=$?
mm_stop
if [ -e "$outside" ]; then
    t_ok "--no-path-fence: the same write succeeded (the fence was the guard)"
else
    t_fail "write failed even with the fence off (rc=$rc)"
fi

# Case 3 (M383): search_code is a READ tool -- it must consult the same fence,
# or the model can grep file *contents* from outside the workspace (worse than
# list_files, which M324 fenced when it leaked only names). The mock orders a
# search of an outside directory holding a secret; with the fence ON the tool
# result -- carried back in the SECOND request body (mockmodel captures it) --
# must NOT contain the secret's content marker, and the turn still completes.
secret="$other/secret.txt"
printf '%s\n' 'LEAKED_CONTENT_XYZZY' > "$secret"
cat > "$tmp/search.mm" <<EOF
wire openai
rule
  count 1
  tool search_code {"pattern":"LEAKED","path":"$other"}
rule
  text SEARCH_FENCE_DONE
EOF
mkdir -p "$tmp/cap3"
mm_start "$tmp/search.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
      -q --no-session --auto -p "search outside" < /dev/null); rc=$?
mm_stop
if [ -f "$tmp/cap3/req.2" ] && ! grep -q "XYZZY" "$tmp/cap3/req.2"; then
    t_ok "search_code fence: outside file content did not leak into the result"
else
    t_fail "search_code leaked outside content (req.2): $(grep -o 'XYZZY' "$tmp/cap3/req.2" 2>/dev/null | head -1)"
fi
case "$out" in
    *SEARCH_FENCE_DONE*) t_ok "the turn completed after the search fence denial" ;;
    *) t_fail "search turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

t_done
