#!/bin/sh
# lint: every jc_message field survives the session codec (M367). The store
# is written after every turn and read by resume/fork/export/ACP-load; a
# field the codec forgets is a resumed conversation that silently changes
# meaning. It has happened: M334 added `truncated` (the output-ceiling
# verdict) and the codec never learned it, so a cut-off session reloaded as
# completed -- found by the M367 survey, sixteen milestones later. This lint
# is the tripwire for the NEXT field: each field of struct jc_message must
# either map to a wire key that appears on BOTH codec sides (>= 2 mentions in
# jc_session.c) or be explicitly ephemeral (the codec's own 'turn-ephemeral'
# comment marks images). A field this lint does not know fails the build
# until the author teaches the codec AND this mapping -- that is the point.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
HDR="$SMOKE_ROOT/include/jc_message.h"
SES="$SMOKE_ROOT/src/session/jc_session.c"
tmp=$(smoke_tmp)

sed -n '/^struct jc_message {/,/^};/p' "$HDR" \
    | sed 's|/\*.*||' | grep ';' \
    | sed -E 's/.*[ *]([a-z_]+);.*/\1/' | grep -E '^[a-z_]+$' \
    > "$tmp/fields"
nf=$(grep -c . "$tmp/fields")

# --- 1: extraction floor --------------------------------------------------------
if [ "$nf" -ge 7 ]; then
    t_ok "extracted $nf jc_message fields (floor 7)"
else
    t_fail "field extraction too thin ($nf) -- the struct scrape broke"
fi

# --- 2: every field carried or explicitly ephemeral ------------------------------
# wire-key map: field -> the JSON key both codec sides must mention.
bad=""
while IFS= read -r f; do
    key=""
    case "$f" in
        role)         key='"role"' ;;
        content)      key='"content"' ;;
        tool_calls)   key='"toolCalls"' ;;
        tool_call_id) key='"toolCallId"' ;;
        is_error)     key='"isError"' ;;
        truncated)    key='"truncated"' ;;
        images)
            # deliberately turn-ephemeral: the codec must SAY so.
            if ! grep -q 'turn-ephemeral' "$SES"; then
                bad="$bad images(no-ephemeral-marker)"
            fi
            continue ;;
        *)
            bad="$bad $f(unknown-to-this-lint)"
            continue ;;
    esac
    n=$(grep -c -- "$key" "$SES")
    if [ "$n" -lt 2 ]; then
        bad="$bad $f($key x$n, need save+load)"
    fi
done < "$tmp/fields"
if [ -z "$bad" ]; then
    t_ok "every field is carried on both codec sides or marked ephemeral"
else
    t_fail "codec fidelity holes:$bad -- teach jc_session.c and this map"
fi

# --- 3: the round-trip unit test exists and covers the newest field --------------
# (a lint can prove mentions; the unit test proves VALUES survive -- keep the
#  pair linked so neither is deleted without the other noticing)
if grep -q 'test_session_roundtrip' "$SMOKE_ROOT/tests/test_session.c" \
   && grep -q 'a1->truncated == 1' "$SMOKE_ROOT/tests/test_session.c"; then
    t_ok "the round-trip unit test exists and pins the truncated field"
else
    t_fail "test_session_roundtrip missing or no longer pins truncated"
fi

# --- 4 (M606): SESSION-level state the struct-jc_message scrape cannot see ------
# The task list is a field of struct jc_session, not of a message, so checks 1-3
# would never notice it going missing from the codec. Pin the three codec sites
# (save: the "todos" key; load: the same key; fork: jc_todo_copy) and the
# round-trip test's three-item assertion, the way check 3 pins truncated.
_todos_mentions=$(grep -c -- '"todos"' "$SES")
if [ "$_todos_mentions" -ge 2 ] && grep -q 'jc_todo_copy' "$SES" \
   && grep -q 'back.todos.items.len == 3' "$SMOKE_ROOT/tests/test_session.c"; then
    t_ok "the task list is carried by save, load and fork, and the round-trip pins it"
else
    t_fail "task-list codec hole: \"todos\" x$_todos_mentions in jc_session.c \
(need save+load), fork copy $(grep -c jc_todo_copy "$SES"), round-trip pin \
$(grep -c 'back.todos.items.len == 3' "$SMOKE_ROOT/tests/test_session.c")"
fi

t_done
