#!/bin/sh
# smoke: list_files' PATTERN walk reports the subtrees it could not read (M493).
#
# THE DEFECT THIS EXISTS FOR. The recursive walk behind `list_files {"pattern":...}`
# -- which is also what the `glob` alias resolves to (M324) -- did this:
#
#     if (jc_list_dir(dir, &names, ...) != JC_OK) {
#         jc_vec_free(&names);
#         return;              <- "unreadable subdirectory: skip, don't fail"
#     }
#
# Skipping is right: one unreadable subtree must not fail a whole listing. Skipping
# SILENTLY is the M483 defect in a second walk, and here the rendering made it
# maximally misleading, because the three existing notices are an else-if chain with
# the empty case FIRST:
#
#     nresults == 0  -> "(no files match <pattern>)"
#     truncated      -> "[... truncated at 1000 matches ...]"
#     exhausted      -> "[... stopped after scanning 200000 entries ...]"
#
# So a pattern whose matches all live in an unreadable directory reported "(no files
# match ...)" -- the "these files do not exist" answer, when the true answer was "I
# could not look everywhere". This file's own comment already states the principle:
# "a model told the wrong one will narrow a pattern that was already fine."
#
# WHY THE NOTE COMPOSES INSTEAD OF JOINING THE CHAIN. Those three are competing
# explanations of one result, so exclusivity is correct for them. An unreadable
# subtree is not a competing explanation -- it can be true alongside any of them --
# and the pair that matters most is exactly the one the chain would have hidden:
# zero matches AND a hole. Check 3 below is that pair.
#
# The flat (no-pattern) branch has always answered "error: could not list directory"
# for an unreadable path, so the defect was one tool telling the truth on one code
# path and not the other; the pattern branch now matches it for an unlistable ROOT.
. "$(dirname "$0")/_smoke.sh"

# Not just root: on Windows the OWNER keeps access to a 000 directory whatever the
# mode records, so the fixture cannot be built there either (MSYS2, M490/M491).
[ "$(smoke_can_fence_owner)" = no ] && \
    t_skip "this host cannot fence a directory against its owner"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/open" "$ws/shut"
printf 'int alpha(void){return 1;}\n' > "$ws/open/alpha.c"
printf 'int beta(void){return 2;}\n'  > "$ws/shut/beta.c"

# A chat request is the one carrying "messages"; the second round is the one
# carrying a "role":"tool" message (the lesson from index_coverage.sh -- `count N`
# would select the wrong request on a config that also embeds).
mk_script() {   # $1 = path, $2 = the pattern the model asks for
    cat > "$1" <<EOS
wire openai
rule
  match "\\"messages\\""
  nomatch "\\"role\\":\\"tool\\""
  tool list_files {"path":".","pattern":"$2"}
rule
  match "\\"messages\\""
  text DONE
EOS
}

# The tool RESULT is what this driver is about, and it travels in the NEXT request
# body -- stdout carries only the final answer. So every check reads the capture.
tool_result() {  # $1 = capture dir; prints every tool-role content found
    for _f in "$1"/req.*; do
        [ -f "$_f" ] || continue
        sed -n 's/.*"role":"tool"[^}]*"content":"\([^"]*\)".*/\1/p' "$_f"
    done
}

run_turn() {   # $1 = capture dir, $2 = pattern
    mk_script "$tmp/replies.mm" "$2"
    mm_start "$tmp/replies.mm" "$1" 6
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        -p 'list them' --no-session --auto < /dev/null \
        > "$tmp/out" 2>"$tmp/err")
    mm_stop
}

# ---- 1. a fully readable tree finds both files and says nothing extra --------
run_turn "$tmp/cap1" '**/*.c'
_r=$(tool_result "$tmp/cap1")
if printf '%s' "$_r" | grep -q 'alpha.c' && printf '%s' "$_r" | grep -q 'beta.c' &&
   ! printf '%s' "$_r" | grep -q 'could not be READ'; then
    t_ok "a readable tree matches both files and carries no coverage note"
else
    t_fail "baseline result was: $(printf '%s' "$_r" | head_bytes 200)"
fi

# ---- 2. a hole is reported ALONGSIDE the matches that were found -------------
chmod 000 "$ws/shut"
run_turn "$tmp/cap2" '**/*.c'
_r=$(tool_result "$tmp/cap2")
if printf '%s' "$_r" | grep -q 'alpha.c' &&
   printf '%s' "$_r" | grep -q 'could not be READ'; then
    t_ok "an unreadable subtree is reported next to the matches that were found"
else
    t_fail "no note beside a partial result: $(printf '%s' "$_r" | head_bytes 200)"
fi

# ---- 3. THE PAIR THE ELSE-IF CHAIN HID: zero matches AND a hole --------------
# Every match lives inside the unreadable directory, so the old code answered
# "(no files match ...)" and nothing else -- indistinguishable from a tree that
# genuinely contains no such file.
run_turn "$tmp/cap3" '**/beta*'
_r=$(tool_result "$tmp/cap3")
if printf '%s' "$_r" | grep -q 'could not be READ'; then
    t_ok "zero matches PLUS a hole still says it could not look everywhere"
else
    t_fail "a no-match answer with an unreadable subtree carried no note -- a \
model cannot tell this from \"the file does not exist\": \
$(printf '%s' "$_r" | head_bytes 200)"
fi
chmod 755 "$ws/shut"

# ---- 4. an unlistable ROOT is an error, matching the flat branch -------------
# A hole in a subtree is a partial result; the directory the caller NAMED failing
# to list is not a result at all. chmod 100 (traversable, not listable) reaches
# that without preventing the process from running there.
ws2=$(smoke_tmp)
mkdir -p "$ws2/sub"
printf 'int gamma(void){return 3;}\n' > "$ws2/sub/gamma.c"
mk_script "$tmp/replies.mm" '**/*.c'
mm_start "$tmp/replies.mm" "$tmp/cap4" 6
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws2" && chmod 100 . && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -p 'list them' --no-session --auto < /dev/null > "$tmp/out4" 2>"$tmp/err4")
chmod 755 "$ws2"
mm_stop
_r=$(tool_result "$tmp/cap4")
if printf '%s' "$_r" | grep -q 'could not list directory'; then
    t_ok "an unlistable root errors, as the flat branch has always done"
else
    t_fail "an unlistable root did not error (it used to answer an empty match \
list): $(printf '%s' "$_r" | head_bytes 200)"
fi

t_done
