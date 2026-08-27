#!/bin/sh
# smoke: session persistence -- a completed headless turn saves a session;
# `ls --output json` (M165) lists it for this workspace and `export`
# renders a transcript carrying the answer.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text SESSION_ANSWER_9
EOF

mm_start "$tmp/replies.mm" "$tmp" 1
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
      -q -p "remember me" < /dev/null); rc=$?
mm_stop

case "$out" in
    *SESSION_ANSWER_9*) t_ok "turn completed (rc=$rc)" ;;
    *) t_fail "turn failed rc=$rc: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

(cd "$ws" && "$BIN" ls --output json < /dev/null > "$tmp/ls.json" 2>/dev/null)
sid=$("$JQ" '.sessions[0].id' "$tmp/ls.json" 2>/dev/null)
if [ -n "$sid" ]; then
    t_ok "ls --output json lists the session"
else
    t_fail "no session in ls --output json: $(head_bytes 120 "$tmp/ls.json")"
fi

(cd "$ws" && "$BIN" ls < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "ls (text) exits 0"
else
    t_fail "ls rc=$rc"
fi

(cd "$ws" && with_deadline 30 "$BIN" export < /dev/null \
    > "$tmp/export.md" 2>/dev/null); rc=$?
if [ $rc -eq 0 ] && grep -q "SESSION_ANSWER_9" "$tmp/export.md"; then
    t_ok "export renders the transcript with the answer"
else
    t_fail "export rc=$rc; answer missing from the transcript"
fi

# --- M482: an UNREADABLE store is not an EMPTY store ------------------------
# THE DEFECT THIS EXISTS FOR, and why it lives here rather than in faults.sh.
# `jc_list_dir` returned JC_ERR_NOTFOUND whenever opendir failed -- for a missing
# directory AND for a permissions failure, ENOTDIR, or EMFILE alike -- and both
# `ls` and the TUI then printed "(no saved sessions)" with a SUCCESS exit. So a
# store the user could not read was reported as a store with nothing in it.
#
# That needs no fault injection to reach: `chmod 000` is enough -- on a host where
# chmod actually fences its OWNER. It was first written as "which means every
# platform can check it", and MSYS2 disproved that: on Windows the owner keeps
# access to a 000 directory at any privilege level, so the fixture cannot exist
# there (docs/analysis/2026-08-19-msys2-first-row.md). smoke_can_fence_owner asks.
# It was
# found while running faults.sh (the FAULT=1 tier no gate built until M482) and
# the fix distinguishes three states: absent -> empty listing, exit 0;
# unreadable -> a stderr diagnostic, exit 1; listed -> the listing.
#
# Skipped where the host cannot fence its owner: root (the CI containers that run
# as root would otherwise see a legitimate empty listing and fail) and Windows.
_sdir="$HOME/.jichi.d/sessions"
if [ "$(smoke_can_fence_owner)" = no ]; then
    t_skip_one "an unreadable store is distinguished from an empty one \
(skipped: this host cannot make a directory unreadable to its owner -- \
root ignores modes, and on Windows the owner keeps access regardless)"
elif [ ! -d "$_sdir" ]; then
    t_fail "no session store at $_sdir -- the earlier checks should have made one"
else
    chmod 000 "$_sdir" 2>/dev/null
    (cd "$ws" && "$BIN" ls --all < /dev/null > "$tmp/unread.out" 2>"$tmp/unread.err")
    _urc=$?
    chmod 755 "$_sdir" 2>/dev/null
    if [ "$_urc" -ne 0 ] && grep -q 'could not list sessions' "$tmp/unread.err"; then
        t_ok "an unreadable store exits nonzero and says so, instead of \"no sessions\""
    else
        t_fail "an unreadable session store reported rc=$_urc with \
stdout='$(head_bytes 40 "$tmp/unread.out")' stderr='$(head_bytes 60 "$tmp/unread.err")' \
-- a store that cannot be read must not look like an empty one"
    fi
fi

t_done
