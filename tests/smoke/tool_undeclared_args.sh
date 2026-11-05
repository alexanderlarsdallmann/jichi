#!/bin/sh
# smoke: an undeclared tool argument is named on the call's own result (M1067).
#
# The executor silently dropped any top-level argument key a tool's schema
# did not declare (SV:0957.2; the run-0078 incident, where git_commit's
# schema declares only message/all and the git_commit tool dropped the
# `trailer` block). The model then ran on arguments it did not send, saw a
# clean result, and kept the habit. M1067 rides one note on the call's own
# result, naming each undeclared key -- the M353 idiom (a per-call fact rides
# its own result, like the exit status).
#
# mockmodel embeds the rule's argument text VERBATIM (no re-serialization),
# so the undeclared `bogus` key travels the real SSE wire into the real
# executor. The valid second call is the pair -- asserted as a note-count of
# exactly one in the third request (the first call's note legitimately lives
# in the history; the M350/M351 count-not-absence pattern).
#
# The git_add tool declares only `paths` and `all`. The bogus call passes an
# extra top-level key `bogus`; it must still run on the declared arguments
# only (staging the file), and its result must name `bogus`.
. "$(dirname "$0")/_smoke.sh"

# git is a hard dependency of the product's git tools; no git, nothing to
# exercise -- a whole-driver skip, not a weakened pass. Decided BEFORE t_plan
# (smoke_lint check 20): a skip after the plan emits a second TAP plan.
command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A real repo with one untracked file so git_add a.txt SUCCEEDS and stages
# it (the call runs on the declared arguments; the bogus one is dropped).
git -C "$ws" init -q
git -C "$ws" config user.email "t@t"
git -C "$ws" config user.name "t"
echo "a" > "$ws/a.txt"

cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool git_add {"paths":["a.txt"],"bogus":1}
rule
  count 2
  tool git_add {"paths":["a.txt"]}
rule
  text UNDECL_DONE
EOF
mm_start "$tmp/r.mm" "$tmp/cap" 5
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session -p "stage a.txt twice" < /dev/null) >/dev/null 2>&1
rc=$?
mm_stop

# --- 1: the bogus call still ran on the declared arguments ------------------
# git_add staged a.txt; with no git output the tool's own ok message is what
# rides in the result (request 2 carries call 1's RESULT).
if grep -q "Staged changes." "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "the undeclared-arg call still ran and staged the file"
else
    t_fail "no staged result in request 2: $(head_bytes 200 "$tmp/cap/req.2" 2>/dev/null)"
fi

# --- 2: ...and its result names the undeclared key bogus (RED pre-fix) ------
# The argument text is embedded verbatim, so "bogus" already appears in the
# request's arguments; the note is what must NAME it. Grep the note line for
# the key, not the request for either word separately.
if grep 'not declared' "$tmp/cap/req.2" 2>/dev/null | grep -q bogus; then
    t_ok "the undeclared-arg call's result names the key bogus"
else
    t_fail "no undeclared-args note naming bogus in request 2"
fi

# --- 3: the VALID call adds no second note (count, not absence) -------------
n=$(grep -o "the declared arguments only" "$tmp/cap/req.3" 2>/dev/null \
    | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "strictly valid arguments ride clean -- exactly one note in history"
else
    t_fail "expected 1 undeclared-args note in request 3, found '$n'"
fi

# --- 4: advisory only (the note is not is_error) ----------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "the run landed clean (exit 0)"
else
    t_fail "run exited $rc"
fi

t_done
