#!/bin/sh
# smoke: a dangling symlink cannot carry a write out of the workspace (M607).
#
# jc_path_resolve canonicalizes with realpath(); when the target does not exist
# yet -- every fresh write -- it canonicalizes the PARENT and re-appends the leaf
# verbatim. A leaf that is a symlink to a path that does not exist yet is exactly
# that case: realpath() fails on the dangling link, the parent resolves inside
# the workspace, the verdict is "inside", and fopen("wb") then FOLLOWS the link
# and creates the target outside. A link to an EXISTING outside file was always
# caught (realpath resolves it), which is why pathfence.sh and the fuzz corpus,
# which plant existing targets, could not see this.
#
# The mock orders a write to `notes.md`, which the driver has planted as a link
# to a not-yet-existing file in another directory. With the fence ON the outside
# file must not appear; the turn still completes; the tool result names the
# denial. --no-path-fence is the control: the same write then lands outside,
# proving the fence -- not a missing directory -- decided it.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
other=$(smoke_tmp)
outside="$other/escaped.txt"
ln -s "$outside" "$ws/notes.md"

cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool write_file {"path":"notes.md","content":"pwned"}
rule
  text FENCE_DONE
MM

mm_start "$tmp/replies.mm" "$tmp/cap1" 2
write_config "$tmp/config.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "write the file" < /dev/null); rc=$?
mm_stop
if [ ! -e "$outside" ]; then
    t_ok "fence ON: the write through a dangling link was denied"
else
    t_fail "fence ON but the link's outside target was created: $outside"
fi
case "$out" in
    *FENCE_DONE*) t_ok "the turn still completed after the denial (rc=$rc)" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep -q -e 'refused by safety fence' -e 'outside the workspace' "$tmp/cap1/req.2" 2>/dev/null; then
    t_ok "the tool result told the model the path was refused"
else
    t_fail "no refusal in the tool result: $(head_bytes 200 "$tmp/cap1/req.2" 2>/dev/null)"
fi

rm -f "$outside"
mm_start "$tmp/replies.mm" "$tmp/cap2" 2
write_config "$tmp/config2.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
      -q --no-session --auto --no-path-fence -p "write the file" < /dev/null); rc=$?
mm_stop
if [ -e "$outside" ]; then
    t_ok "--no-path-fence: the same write followed the link (the fence was the guard)"
else
    t_fail "control: the write failed even with the fence off (rc=$rc)"
fi
t_done
