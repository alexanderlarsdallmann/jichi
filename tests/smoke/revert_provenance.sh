#!/bin/sh
# smoke: `revertOutOfScope` reverts what the RUN changed and leaves what it
# provably did not (M501).
#
# THE NEAR-MISS THIS EXISTS FOR, from DEFERRED. Run B was going with
# `revertOutOfScope: true`; the operator merged reviewed files into the working
# tree; run B's end-of-turn M83 sweep would have seen the merge as out-of-scope
# changes made DURING its run and reverted them to ITS baseline. The envelope had
# no provenance for a working-tree change: "changed since my baseline" and
# "changed by me" were the same predicate. An earlier run had already reverted a
# stray edit exactly this way, doing its job.
#
# WHY THE RULE IS PROVABLE AND NOT A GUESS. jichi has exactly two ways to change
# a file: the write chokepoint (`jc_app_write_file`, which every file tool and the
# ACP delegate pass through, and which the edit-scope fence already restricts) and
# a shell command (which the fence does NOT cover -- `sed -i` is invisible to it).
# So when a run has written nothing through the chokepoint AND has run no shell
# command, an out-of-scope change cannot be its work. That case is left alone.
# When a shell command DID run, the change is attributable and M142's revert still
# applies -- stated rather than silently narrowed.
#
# WHAT IS ASSERTED: an external change is left ALONE and SAID to be left alone (a
# silent non-revert would be worse than the bug: an operator who asked for
# reverting would assume it happened), and the run still ends cleanly.
. "$(dirname "$0")/_smoke.sh"

command -v git > /dev/null 2>&1 || t_skip "git is required for snapshots"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/src" "$ws/docs"
printf 'int main(void){return 0;}\n' > "$ws/src/a.c"
printf 'ORIGINAL_DOC\n' > "$ws/docs/notes.md"
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) \
    > /dev/null 2>&1

# THE COLLEAGUE, DETERMINISTICALLY. A background `sleep && edit` is a race the
# mock always wins (the run finishes in milliseconds), so the stand-in for
# "another process changed the tree during the run" is a PostToolUse hook: it
# runs mid-turn, from a child jichi did not write through, and the turn-end sweep
# sees its change. Not a shell TOOL call, so `shell_ran` stays 0 -- which is the
# state under test.
# Content-based, with a trailing catch-all: `count N` breaks the moment the
# envelope makes an extra model call (measured -- request 3 hit no rule and the
# run died with a 500 before the turn even ended, which is how this comment got
# written).
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
"revertOutOfScope":true,"maxRetries":0,
"hooksEnabled":true,
"hooks":{"PostToolUse":[{"commands":[{"shell":"printf MERGED_BY_A_HUMAN > docs/notes.md"}]}]}}
EOF
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --edit-scope 'src/**' --journal "$tmp/j.jsonl" \
    -p "update src/a.c" \
    < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# ---- 1. the sweep ran and saw it (without this the rest is vacuous) --------
if grep -qi 'out.of.scope' "$tmp/err"; then
    t_ok "the sweep saw the external change (the case is live)"
else
    t_fail "no out-of-scope detection -- no baseline was taken or the hook did \
not fire, so checks 2-3 would pass for the wrong reason: \
$(head_bytes 220 "$tmp/err")"
fi

# ---- 2. THE DEFECT: work this run did not do survives ----------------------
if grep -q 'MERGED_BY_A_HUMAN' "$ws/docs/notes.md"; then
    t_ok "an external edit is NOT reverted (no chokepoint write, no shell call)"
else
    t_fail "the envelope reverted a change this run did not make -- \
docs/notes.md now reads '$(head_bytes 40 "$ws/docs/notes.md")'. That is the \
DEFERRED near-miss, suffered."
fi

# ---- 3. and it SAYS so ----------------------------------------------------
if grep -q 'NOT reverted' "$tmp/err" &&
   grep -q '"not_ours":1' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "the decision not to revert is reported, and journalled as not_ours"
else
    t_fail "a file was left alone with no mention of it -- stderr: \
$(head_bytes 160 "$tmp/err") journal: \
$(grep out_of_scope "$tmp/j.jsonl" 2>/dev/null | head_bytes 160)"
fi

# ---- 4. the run still completed ------------------------------------------
if grep -q 'DONE' "$tmp/out"; then
    t_ok "the run completed normally"
else
    t_fail "run did not finish: $(head_bytes 150 "$tmp/out")"
fi

# ---- 5+6. THE OTHER HALF: the feature is not disabled ---------------------
# The whole risk of this fix is narrowing M142 into a no-op. When the run HAS run
# a shell command, the shell is the one writer the edit-scope fence does not
# cover, so an out-of-scope change IS attributable to the run and is still
# reverted. Same fixture, one extra tool call.
tmp2=$(smoke_tmp); ws2=$(smoke_tmp)
mkdir -p "$ws2/src" "$ws2/docs"
printf 'int main(void){return 0;}\n' > "$ws2/src/a.c"
printf 'ORIGINAL_DOC\n' > "$ws2/docs/notes.md"
(cd "$ws2" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) > /dev/null 2>&1
cat > "$tmp2/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"src/a.c","content":"int main(void){return 2;}\n"}
rule
  count 2
  tool run_terminal_command {"command":"printf SHELL_WROTE_THIS > docs/notes.md"}
rule
  text DONE
EOF
mm_start "$tmp2/replies.mm" "$tmp2/cap"
cat > "$tmp2/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"revertOutOfScope":true,"maxRetries":0}
EOF
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp2/config.json" --no-session \
    --auto --edit-scope 'src/**' --journal "$tmp2/j.jsonl" \
    -p "update src/a.c then touch the doc" \
    < /dev/null > "$tmp2/out" 2> "$tmp2/err") || true
mm_stop
if ! grep -q 'run_terminal_command' "$tmp2/j.jsonl" 2>/dev/null; then
    # The floor. First version of this check passed while the shell call never
    # happened at all -- the file read ORIGINAL_DOC because nothing had written
    # it, not because the revert worked.
    t_fail "the fixture never ran a shell command, so 'still reverted' would \
pass on a file nothing touched: $(cut -c1-120 "$tmp2/j.jsonl" | tr '\n' ' ' \
| head_bytes 200)"
elif grep -q 'ORIGINAL_DOC' "$ws2/docs/notes.md" 2>/dev/null; then
    t_ok "a shell-introduced out-of-scope change IS still reverted (M142 intact)"
else
    t_fail "the shell wrote outside the scope and the revert did not undo it -- \
this fix narrowed M142 into a no-op: '$(head_bytes 60 "$ws2/docs/notes.md")'"
fi
if grep -q '"reverted":1' "$tmp2/j.jsonl" 2>/dev/null; then
    t_ok "the journal records one file reverted (the supervisor surface)"
else
    t_fail "the run journal does not record the revert: \
$(grep out_of_scope "$tmp2/j.jsonl" 2>/dev/null | head_bytes 200)"
fi

t_done
