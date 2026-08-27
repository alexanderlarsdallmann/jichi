#!/bin/sh
# smoke: an unreadable directory is a HOLE in the index, and both the operator
# and the model are told so (M483).
#
# THE DEFECT THIS EXISTS FOR. The index walk did this:
#
#     if (jc_list_dir(dir, &names, a) != JC_OK) {
#         jc_vec_free(&names);
#         return JC_OK;            <- "unreadable dir: skip quietly"
#     }
#
# so a directory the process could not read simply vanished from the index. What
# the operator saw was a smaller, entirely plausible number -- measured on a
# two-file workspace with one directory chmod 000:
#
#     readable:    Indexed 2 file(s), 2 chunk(s)   exit 0
#     unreadable:  Indexed 1 file(s), 1 chunk(s)   exit 0    (no mention at all)
#
# and what the MODEL then saw, searching for something in that subtree, was
# "No matching code found in the index." -- which reads as "the code does not
# contain this". That is the same failure shape as the OpenBSD `search_code`
# defect (M461, ANECDOTES): the tool did not fail, it lied quietly, and a wrong
# answer delivered confidently is worse than an error.
#
# WHY chmod AND NOT FAULT INJECTION. This state needs no special build: a
# permissions mistake, a directory owned by another user, an NFS hiccup or EMFILE
# all reach it. So this driver runs on every platform, unlike the FAULT=1 tier
# (M482) whose absence from the gate is what surfaced this whole class.
#
# WHAT IS ASSERTED: the count reaches the operator (`index` warns on stderr), the
# note reaches the model (it is inside the tool result, which travels in the NEXT
# request body -- so the assertion reads the capture, not stdout), a healthy
# workspace says NOTHING (a warning that always fires is noise), and an unreadable
# ROOT is a hard error rather than an empty index.
. "$(dirname "$0")/_smoke.sh"

# root ignores directory permissions, so chmod 000 is not a fence for it and every
# check below would measure a readable tree.
# Not just root: on Windows the OWNER keeps access to a 000 directory whatever
# the mode records, so the fixture cannot be built there either (MSYS2, 2026-08-19).
[ "$(smoke_can_fence_owner)" = no ] && \
    t_skip "this host cannot fence a directory against its owner (root ignores modes; on Windows the owner keeps access)"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/visible" "$ws/secret"
printf 'int alpha_marker(void) { return 1; }\n' > "$ws/visible/a.c"
printf 'int beta_marker(void) { return 2; }\n'  > "$ws/secret/b.c"

# The embed action answers any /v1/embeddings request; the chat rules make the
# model call codebase_search once and then finish, which is how the tool result
# gets into a request body we can read.
# Rules are selected by CONTENT, not by request number: an embeddings call and a
# chat call both arrive here, so `count N` counts the wrong thing (the embed
# request took count 1 and the tool rule never fired -- measured: one captured
# request and "the model returned no tool call and no text"). A chat request is
# the one carrying "messages"; the SECOND chat round is the one carrying a
# "role":"tool" message, which is how the two are told apart.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool codebase_search {"query":"beta_marker"}
rule
  match "\"messages\""
  text DONE
rule
  embed alpha beta marker code
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 8
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["embed"]}],
 "snapshots":false,"repoMap":false,"references":false,
 "toolProfile":"full","maxRetries":0}
EOF

# ---- 1. a healthy workspace says nothing -----------------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    index --reindex < /dev/null > "$tmp/ok.out" 2>"$tmp/ok.err"); rc=$?
if [ $rc -eq 0 ] && grep -q 'Indexed 2 file' "$tmp/ok.out" &&
   ! grep -q 'could not be read' "$tmp/ok.err"; then
    t_ok "a fully readable workspace indexes both files and warns about nothing"
else
    t_fail "baseline rc=$rc out='$(head_bytes 80 "$tmp/ok.out")' \
err='$(head_bytes 80 "$tmp/ok.err")'"
fi

# ---- 2. the operator is told ------------------------------------------------
chmod 000 "$ws/secret"
rm -rf "$HOME/.jichi.d/index"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    index --reindex < /dev/null > "$tmp/hole.out" 2>"$tmp/hole.err"); rc=$?
if grep -q 'could not be read' "$tmp/hole.err"; then
    t_ok "an unreadable directory is reported to the operator, not swallowed"
else
    t_fail "index reported nothing about an unreadable directory: \
out='$(head_bytes 90 "$tmp/hole.out")' err='$(head_bytes 90 "$tmp/hole.err")'"
fi

# ---- 3. the count is the number of holes, not a boolean ---------------------
if grep -q '1 directory could not be read' "$tmp/hole.err"; then
    t_ok "the warning carries the count (1 directory)"
else
    t_fail "no count in the warning: $(head_bytes 120 "$tmp/hole.err")"
fi

# ---- 4. THE MODEL IS TOLD, which is the half that matters -------------------
# The tool result travels in the NEXT request body, so this reads the capture
# rather than stdout -- stdout carries the final answer, which is "DONE".
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -p 'find beta_marker' --no-session --auto < /dev/null \
    > "$tmp/turn.out" 2>"$tmp/turn.err")
_noted=0
for _f in "$tmp/cap"/req.*; do
    [ -f "$_f" ] || continue
    if grep -q 'could not be read' "$_f"; then _noted=1; fi
done
if [ "$_noted" -eq 1 ]; then
    t_ok "the codebase_search result tells the MODEL the index is incomplete"
else
    t_fail "no coverage note in any request body -- a model reading these results \
cannot tell an absent match from an unindexed one, which is the M461 \
search_code failure repeated: $(ls "$tmp/cap" | tr '\n' ' ')"
fi

# ---- 5. an unreadable ROOT is an error, not an empty index ------------------
# A hole in a subtree is a partial result worth reporting; a root that cannot be
# listed is not a result at all, and the old code returned JC_OK for both.
chmod 755 "$ws/secret"
root2=$(smoke_tmp)
mkdir -p "$root2/sub"
printf 'int gamma_marker(void){return 3;}\n' > "$root2/sub/c.c"
# `chmod 100`, not 000, and entered BEFORE the chmod: mode 100 is traversable but
# not listable, which is exactly "the root cannot be enumerated" without also
# breaking the shell's ability to be there or the binary's ability to start.
#
# The first version of this check passed `--cwd`, WHICH IS NOT A FLAG. jichi
# answered "error: unknown option '--cwd'" and exited non-zero, so the check went
# green while measuring nothing but its own typo -- the exact vacuity this
# milestone is about, caught only because the flag looked unfamiliar and was
# checked against `--help`. Assert on a REASON, not just on a non-zero status.
(cd "$root2" && chmod 100 . && with_deadline 60 "$BIN" \
    --config "$tmp/config.json" index --reindex < /dev/null \
    > "$tmp/root.out" 2>"$tmp/root.err"); rc=$?
chmod 755 "$root2"
if [ $rc -ne 0 ] && ! grep -q 'unknown option' "$tmp/root.err" &&
   grep -q 'index build failed' "$tmp/root.err"; then
    t_ok "an unreadable workspace root fails instead of reporting an empty index"
else
    t_fail "an unlistable root did not fail with a build error (rc=$rc): \
out='$(head_bytes 70 "$tmp/root.out")' err='$(head_bytes 90 "$tmp/root.err")'"
fi

mm_stop
t_done
