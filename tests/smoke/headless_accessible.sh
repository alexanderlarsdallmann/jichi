#!/bin/sh
# smoke: the HEADLESS front-end honours --accessible, and the sighted form is
# unchanged (M566).
#
# THE DEFECT, and how it was found. Verifying a hand-off document -- the rule is
# "run every command you publish" -- a `jichi -p --accessible` probe printed:
#
#     [tokens in=3973 out=38]
#     [tool] read_file  src/greet.c
#     [tool read_file -> ok]
#
# That is the M549 defect verbatim, seventeen milestones after M549, in the
# front-end none of M549-M565 touched. `src/main.c` called `jc_msg()` ZERO times
# against `jc_tui.c`'s twenty-five, so every accessible sentence and every
# translation reached one front-end of two. `jc_msg_set_lang` was called only
# from the TUI, so a headless run never even resolved a language: JICHI_LANG=de
# served English no matter what.
#
# WHY M555's SEPARATOR FIX DID REACH HERE, which is the interesting half. That
# fix was `jc_group_sep_audience` -- a pure function on config, applied at each
# call site -- and a lint enumerates its call sites, so main.c was included.
# The PROSE fix was a `printf` shape inside the TUI's own render functions, with
# no shared surface to enumerate. One mechanism generalised across front-ends;
# the other could not. That is why the numbers were right and the punctuation
# around them was wrong: `[tokens in=3973` -- unseparated, and still bracketed.
#
# WHAT IS PINNED HERE, in both directions, because a one-sided fix is a
# regression for the other audience:
#   accessible -> prose from the catalog, no brackets, no `->`, no separators
#   default    -> the compact bracket form, separators INTACT
#
# WHAT IS NOT PINNED, and deliberately: the `[envelope] ...` summary and the
# startup `[jichi]`/`[mcp]`/`[model]` notices. Those are a bracketed LOG TAG
# followed by prose -- the information is in the prose, and the tag costs a
# listener two words once per run -- whereas `[tokens in=4,946 out=37]` carries
# its structure in `=` and `[]`. Different shape, different decision, and no
# catalog entry exists for them. Named here so a reader knows the omission was
# chosen rather than missed.
#
# THE LANGUAGE WIRE IS NOT OBSERVABLE FROM OUTPUT YET, and check 6 is honest
# about why: the German catalog holds 11 of 23 entries, and NONE of the twelve
# it lacks is one the headless path prints. So `JICHI_LANG=de` correctly
# produces English here today, and an output assertion would pin the gap rather
# than the behaviour. Check 6 therefore pins the CALL SITE -- a lint, per
# CLAUDE.md, for an effect that cannot yet be seen.
#
# THE WIRE WAS VERIFIED BY HAND, not asserted. Temporary German entries for ids
# 11-15 were added to msg_de, and a headless `--accessible` run printed:
#
#   JICHI_LANG=de LC_ALL=de_DE.UTF-8   PROBE-DE: Rufe das Werkzeug read_file
#                                      auf, mit .../greet.c.
#                                      The tool read_file finished successfully.
#   JICHI_LANG=de LC_ALL=C             Calling the tool read_file, with ...
#
# Three things in one capture. The German entries reach the headless renderers.
# TOOL_OK stayed English in the same run because id 16 was left NULL -- the
# fallback, working. And LC_ALL=C returns everything to English: jc_msg_lang_
# resolve's utf8 gate, which is why this driver's own runs (_smoke.sh exports
# LC_ALL=C) see English regardless. The entries were reverted; `git diff` on
# src/util/jc_msg.c is empty.
#
# When A4 lands, replace check 6 with an output assertion and delete this note.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"small.txt"}
rule
  text it says hello
EOF

printf 'hello\n' > "$ws/small.txt"

# Two runs of the SAME prompt, differing only in --accessible. Both are needed:
# check 5 is the regression guard, and without it a fix that simply deleted the
# compact form would be green.
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --accessible -p "read small.txt" < /dev/null \
    > "$tmp/acc.out" 2> "$tmp/acc.err") || true
mm_stop

mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto -p "read small.txt" < /dev/null \
    > "$tmp/pln.out" 2> "$tmp/pln.err") || true
mm_stop

# ---- 1: the denominator -- both runs produced chrome at all ----------------
# MEASURED, and not what I first wrote here. I claimed checks 2-5 "are all clean
# on a pair of empty files"; they are not -- each asserts a presence as well as
# an absence, so an empty capture reddens all four. What check 1 buys is the
# DIAGNOSIS: with `--quiet` added to both runs, 2-5 produce four messages each
# blaming a renderer, and only this one says the captures were empty. A check
# that turns four misleading failures into one true sentence earns its place;
# claiming it prevents a silent pass would have been a vacuous sub-clause of
# exactly the kind this project keeps finding in its own tests.
nacc=$(wc -c < "$tmp/acc.err" 2>/dev/null || echo 0)
npln=$(wc -c < "$tmp/pln.err" 2>/dev/null || echo 0)
if [ "$nacc" -gt 40 ] && [ "$npln" -gt 40 ] &&
   $G -q 'read_file' "$tmp/acc.err" && $G -q 'read_file' "$tmp/pln.err"
then
    t_ok "both headless runs emitted tool chrome (${nacc}b accessible, ${npln}b plain)"
else
    t_fail "one of the runs produced no chrome, so nothing below tests \
anything: accessible=${nacc}b plain=${npln}b. accessible tail: \
$(tail -2 "$tmp/acc.err" 2>/dev/null | tr '\n' ' ' | head_bytes 160)"
fi

# ---- 2: the token line is a sentence, and the bracket form is GONE ---------
# Both halves. Asserting only the prose would pass a build that printed both.
if $G -q 'input tokens used' "$tmp/acc.err" &&
   ! $G -q 'tokens in=' "$tmp/acc.err"
then
    t_ok "accessible: the token line is prose, with no bracket form left"
else
    t_fail "the headless token line is not accessible prose. Saw: \
$($G -o 'tokens in=[^]]*]\|[0-9,]* input tokens used' "$tmp/acc.err" \
  | head -2 | tr '\n' ' '). M566 routes hl_usage through JC_MSG_TOKENS when \
config.accessible is set."
fi

# ---- 3: the tool CALL is a sentence ---------------------------------------
if $G -q 'Calling the tool' "$tmp/acc.err" &&
   ! $G -q '\[tool\]' "$tmp/acc.err"
then
    t_ok "accessible: the tool call is prose, with no [tool] label left"
else
    t_fail "the headless tool-call line is not accessible prose. Saw: \
$($G -o '\[tool\][^$]*\|Calling the tool [a-z_]*' "$tmp/acc.err" | head -2 \
  | tr '\n' ' ')"
fi

# ---- 4: the tool RESULT is a sentence, and the ARROW is gone -------------
# The arrow specifically: it is the glyph whose spoken form varies most between
# punctuation settings, so `-> ok` is the least predictable thing on the line.
if $G -q 'finished successfully' "$tmp/acc.err" &&
   ! $G -q -- '->' "$tmp/acc.err"
then
    t_ok "accessible: the tool result is prose, with no -> left"
else
    t_fail "the headless tool-result line still uses the arrow form. Saw: \
$($G -o -- '\[tool [^]]*]\|finished successfully' "$tmp/acc.err" | head -2 \
  | tr '\n' ' ')"
fi

# ---- 5: THE REGRESSION GUARD -- the sighted form is untouched ------------
# Including the thousands separator, which is the OPPOSITE decision from the
# accessible arm (M555): a sighted reader keeps the grouping because a bare
# six-digit integer is harder to scan. A fix that made everyone accessible
# would be green on checks 2-4 and red here.
if $G -q 'tokens in=' "$tmp/pln.err" &&
   $G -q '\[tool\]' "$tmp/pln.err" &&
   $G -q -- '->' "$tmp/pln.err" &&
   ! $G -q 'input tokens used' "$tmp/pln.err"
then
    t_ok "default: the compact bracket chrome is unchanged"
else
    t_fail "the DEFAULT headless output changed. --accessible must add an arm, \
not replace the compact form -- a sighted user keeps the brackets and the \
thousands separator on purpose. Saw: \
$(head -4 "$tmp/pln.err" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 6: the UI language is resolved OUTSIDE the TUI ---------------------
# Pinned as a call site, not as output: see the header. Two assertions, because
# either alone is satisfiable by the broken state -- main.c could call the
# resolver while the TUI kept its own duplicate (drift restored), or the TUI's
# copy could be gone while nobody replaced it (English forever).
cd "$SMOKE_ROOT" || exit 1
nmain=$($G -c 'jc_msg_set_lang(jc_msg_lang_resolve(' src/main.c 2>/dev/null || echo 0)
ntui=$($G -c 'jc_msg_set_lang(jc_msg_lang_resolve(' src/tui/jc_tui.c 2>/dev/null || echo 0)
if [ "$nmain" -ge 1 ] && [ "$ntui" -eq 1 ] &&
   $G -q 'int jc_locale_is_utf8(void);' include/jc_platform.h
then
    t_ok "the language is resolved in main.c ($nmain site) with $ntui runtime \
re-resolve in the TUI"
else
    t_fail "the UI language is not resolved for every front-end: main.c has \
$nmain resolve call(s) (want >= 1), jc_tui.c has $ntui (want exactly 1 -- the \
/language command). If main.c has 0, a headless run serves English whatever \
JICHI_LANG says. If jc_tui.c has 2, the startup duplicate is back and the two \
front-ends can disagree about the same environment."
fi

t_done
