#!/bin/sh
# smoke: the run journal records WHICH out-of-scope paths are tracked (M684).
#
# WHY THIS EXISTS, and it is a measurement asking for a field rather than a
# feature looking for a use. DEFERRED.md's `--strict-green` row was reopened at
# M662 with a number that reversed its own recommendation: on 91 journals,
# strict-green would downgrade **16 of 35** successful runs -- 46% -- because
# **85% of what it flags is the work's own output**. A downloaded PDF, a `.o`,
# jichi's own `.jichi/` state. Only 15% is source at all.
#
# The measurement's own conclusion was that the sharp line is TRACKED vs
# UNTRACKED: a gate file is in version control, a downloaded PDF is not. And
# `tests/measure/strict_green_fp.py` says so in a comment above a classifier it
# calls "deliberately crude" -- it guesses from the file extension, because the
# journal does not record the one fact that would settle it.
#
# So this is the field, and this driver is what keeps it honest.
#
# WHAT IS RECORDED, and why this shape: the `out_of_scope` record keeps its
# `paths` array unchanged and gains a `tracked` array naming the subset that git
# knows about. Not a parallel array of booleans -- those desynchronise silently
# and a reader cannot tell a short array from a false. Not a rewrite of `paths`
# into objects -- that breaks every existing reader, including the measurement
# script this exists to serve. An ABSENT `tracked` key means "not recorded"
# (an older journal, or a workspace with no git), which is different from an
# empty one meaning "none of them are tracked", and the reader must be able to
# tell those apart.
. "$(dirname "$0")/_smoke.sh"

command -v git > /dev/null 2>&1 || t_skip "git is required for snapshots"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/src" "$ws/docs"
printf 'int main(void){return 0;}\n' > "$ws/src/a.c"
printf 'ORIGINAL_DOC\n'             > "$ws/docs/tracked.md"
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) \
    > /dev/null 2>&1

# THE COLLEAGUE, DETERMINISTICALLY -- the same device revert_provenance.sh uses
# and for the same reason: a PostToolUse hook runs mid-turn from a child jichi
# did not write through, so the turn-end sweep sees its change and `shell_ran`
# stays 0. Here it touches TWO files outside the scope, one tracked and one
# that has never been added, which is the whole distinction under test.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"src/a.c","content":"int main(void){return 1;}\n"}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"revertOutOfScope":false,"maxRetries":0,
"hooksEnabled":true,
"hooks":{"PostToolUse":[{"commands":[{"shell":"printf CHANGED > docs/tracked.md; printf BINARYISH > docs/untracked.bin"}]}]}}
EOF
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --edit-scope 'src/**' --journal "$tmp/j.jsonl" \
    -p "update src/a.c" \
    < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

rec=$(grep out_of_scope "$tmp/j.jsonl" 2>/dev/null | head -1)

# ---- 1: the denominator -- BOTH paths reached the record ------------------
# Without this every assertion below holds trivially in a record that named
# neither, and a sweep that never ran looks exactly like a clean tree.
if printf '%s' "$rec" | grep -q 'docs/tracked.md' &&
   printf '%s' "$rec" | grep -q 'docs/untracked.bin'; then
    t_ok "the sweep recorded both out-of-scope paths (the case is live)"
else
    t_fail "the out_of_scope record does not name both files, so checks 2-5 \
would pass for the wrong reason. Record: $(printf '%s' "$rec" | head_bytes 240) \
stderr: $(head_bytes 160 "$tmp/err")"
fi

# ---- 2: the field exists at all ------------------------------------------
if printf '%s' "$rec" | grep -q '"tracked"'; then
    t_ok "the out_of_scope record carries a tracked array"
else
    t_fail "no \"tracked\" key in the out_of_scope record -- the strict-green \
rule the M662 measurement asked for cannot be written from this journal. \
Record: $(printf '%s' "$rec" | head_bytes 240)"
fi

# ---- 3: the tracked file is named in it ----------------------------------
# Extracted from the tracked array ALONE, not from the record: `paths` names it
# too, so grepping the whole line would pass on a build that recorded nothing.
trk=$(printf '%s' "$rec" | sed -n 's/.*"tracked":\[\([^]]*\)\].*/\1/p')
if printf '%s' "$trk" | grep -q 'docs/tracked.md'; then
    t_ok "a tracked out-of-scope file is recorded as tracked"
else
    t_fail "docs/tracked.md is committed in the fixture and the tracked array \
does not name it: [$trk]"
fi

# ---- 4: and the untracked one is NOT -------------------------------------
# The half that carries the finding. A build that put every path in the array
# passes check 3 and fails here, which is the point of testing both.
if printf '%s' "$trk" | grep -q 'docs/untracked.bin'; then
    t_fail "docs/untracked.bin was never added to git and the tracked array \
claims it: [$trk] -- a rule built on this field would treat a downloaded \
artifact as a gate file, which is the 85% the M662 measurement found"
else
    t_ok "an untracked out-of-scope file is not recorded as tracked"
fi

# ---- 5: the array is a STRICT subset -------------------------------------
# Neither everything nor nothing. Both degenerate answers pass one of checks
# 3-4, and a field that is always empty or always full carries no information
# while looking exactly like one that does.
np=$(printf '%s' "$rec" | sed -n 's/.*"paths":\[\([^]]*\)\].*/\1/p' | tr ',' '\n' | grep -c '"')
nt=$(printf '%s' "$trk" | tr ',' '\n' | grep -c '"')
if [ "$nt" -gt 0 ] && [ "$np" -gt "$nt" ]; then
    t_ok "tracked is a strict subset: $nt of $np out-of-scope paths"
else
    t_fail "tracked ($nt) against paths ($np) -- an array that is always empty \
or always equal to paths distinguishes nothing, and would read as a working \
field in every journal"
fi

t_done
