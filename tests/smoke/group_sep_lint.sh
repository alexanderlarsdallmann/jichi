#!/bin/sh
# smoke lint: no number reaches a human without the audience rule (M555).
#
# THE DEFECT, reported by ear. The operator ran a German-locale screen reader
# over jichi's token counts:
#
#     "The reader reads 4.946 as digits, and reading the ."
#
# `4.946` came out as digits with the dot read aloud -- four digits and the name
# of a punctuation mark instead of a number.
#
# WHICH LAYER, corrected at M559. The first version of this header blamed the
# synthesizer's number parser. It is not: `espeak-ng -v de -q -x 4.946` says
# "vier tausend neunhundert sechsundvierzig", correctly, in German and English.
# The layer is **Orca's punctuation verbalisation** -- measured at
# `verbalizePunctuationStyle` SOME with `speakNumbersAsDigits` False -- which
# voices embedded symbols and so splits the numeral at the dot before the
# synthesizer ever sees it. The fix is unaffected; the explanation was asserted
# without measurement.
#
# AND IT IS NOT A GERMAN PROBLEM, which is the part that makes a lint worth
# having. `jc_config.c` sets `.` as the fallback separator **when the locale
# supplies none** -- exactly what LC_ALL=C gives -- so an English user with no
# locale configured hears the same thing. The comma form `4,946` is the same
# shape. The fix is `jc_group_sep_audience`, which returns the configured
# separator normally and none in accessible mode, and the SIGHTED rendering keeps
# its grouping because a bare six-digit integer is harder to scan. Two audiences,
# opposite needs, one decision point.
#
# WHY A LINT: I ENUMERATED FIVE CALL SITES AND THERE WERE EIGHT. Six in
# src/tui/jc_tui.c -- the sixth is /promptcache's hit-rate line -- and two in
# src/main.c, the headless token line and the envelope summary, which honour
# --accessible just as the TUI does. The three I missed were found by grepping
# for what was LEFT OVER after changing the first five, not by reading. A list
# written from reading was short by 37%, and "a rule applied in one place and not
# the neighbouring one" is the shape this project has now recorded six times
# (M536 x4, M544, M549, M462, M551, M555).
#
# UNIVERSE, measured 2026-08-23: 3 code lines naming `config.group_sep` outside
# jc_config.c, in 2 files, every one of them an argument to
# jc_group_sep_audience. Small enough to enumerate, which is CLAUDE.md's
# precondition for a lint over an audit.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1

# Comments stripped: every one of these lines is explained by a comment that also
# names the field, and a build whose only mention was the explanation must not
# pass. jc_config.c is excluded because that is where the field is ASSIGNED from
# the locale -- the one legitimate raw use.
: > "$tmp/uses"
for f in $($G -rl 'config\.group_sep' src --include='*.c' --include='*.h' \
           2>/dev/null | $G -v 'jc_config\.c'); do
    sed -e 's|/\*.*\*/||g' -e 's|^[[:space:]]*\*.*$||' -e 's|/\*.*$||' "$f" \
        | $G -n 'config\.group_sep' /dev/null - \
        | sed "s|^|$f:|" >> "$tmp/uses"
done
nuses=$($G -c . "$tmp/uses" 2>/dev/null || true)
[ -n "$nuses" ] || nuses=0
nfiles=$($G -rl 'config\.group_sep' src --include='*.c' --include='*.h' \
         2>/dev/null | $G -vc 'jc_config\.c' || true)
[ -n "$nfiles" ] || nfiles=0

# ---- 1: the universe is the one measured, and the matcher can say no ------
# Both floors. An empty use list makes check 2 vacuous, and so would a pattern
# that matched nothing anywhere -- so a string known to be absent is proved
# absent as well.
if [ "$nuses" -ge 3 ] && [ "$nfiles" -eq 2 ] &&
   ! $G -rq 'config\.grouping_separator_that_does_not_exist' src 2>/dev/null
then
    t_ok "$nuses raw uses of config.group_sep in $nfiles files (measured: 3 / 2)"
else
    t_fail "universe drifted: $nuses uses in $nfiles files (want >= 3 uses, \
exactly 2 files besides jc_config.c). If a front-end legitimately gained or lost \
one, update this floor and say why -- an unexamined drift here is how the \
headless path kept speaking separators for a milestone."
fi

# ---- 2: every raw use is wrapped in the audience rule --------------------
# THE ACTUAL PROPERTY, and it is deliberately about the rule being APPLIED
# rather than about any particular spelling of the call: a site may read the
# separator only in order to hand it to jc_group_sep_audience.
bad=""
while IFS= read -r ln; do
    [ -n "$ln" ] || continue
    case "$ln" in *jc_group_sep_audience*) ;; *) bad="$bad
  $ln" ;; esac
done < "$tmp/uses"
if [ -z "$bad" ]; then
    t_ok "every raw config.group_sep is handed to jc_group_sep_audience"
else
    t_fail "config.group_sep used without the audience rule:$bad
A number formatted with the configured separator is read to a screen-reader user
as digits plus the name of a punctuation mark. Wrap it in
jc_group_sep_audience(sep, accessible)."
fi

# ---- 3: the rule exists, is declared, and is REACHED ---------------------
# A predicate nobody calls is a comment. Checked against stripped sources so a
# build whose only mention is the doc comment fails.
sed -e 's|/\*.*\*/||g' -e 's|^[[:space:]]*\*.*$||' -e 's|/\*.*$||' \
    src/util/jc_str.c > "$tmp/str.stripped"
ncalls=$($G -rc 'jc_group_sep_audience' src --include='*.c' 2>/dev/null \
         | $G -v ':0$' | $G -c . || true)
[ -n "$ncalls" ] || ncalls=0
if $G -q 'char jc_group_sep_audience(char configured, int accessible);' \
        include/jc_str.h &&
   $G -q 'jc_group_sep_audience' "$tmp/str.stripped" &&
   [ "$ncalls" -ge 2 ]
then
    t_ok "the audience rule is declared, defined, and called from $ncalls files"
else
    t_fail "jc_group_sep_audience is missing its declaration, its definition, \
or its callers ($ncalls files call it; want >= 2 -- the TUI and main.c both \
format numbers for a human)."
fi

# ---- 4: the rule is unit-tested, in BOTH directions ---------------------
# A pure predicate with no test is how a one-line rule silently inverts, and
# only one direction is the fix: the other -- a sighted user KEEPING the
# separator -- is what must not regress. Grepping for both assertions rather
# than for the function name, because naming it in a test proves nothing.
if $G -q "jc_group_sep_audience('\\.', 0) == '\\.'" tests/test_str.c &&
   $G -q "jc_group_sep_audience('\\.', 1) == 0" tests/test_str.c
then
    t_ok "both directions of the rule are asserted in tests/test_str.c"
else
    t_fail "tests/test_str.c does not assert BOTH directions of \
jc_group_sep_audience. The accessible case is the fix; the sighted case is the \
regression that a fix-everything change would cause: \
$($G -c 'jc_group_sep_audience' tests/test_str.c 2>/dev/null) mention(s)"
fi

# ---- 5: and jc_group_num still has callers to protect -------------------
# The vacuity guard for the whole driver. Checks 1-4 are all clean on a tree
# where nobody formats numbers at all, which is exactly the state a refactor
# could produce while the lint reports success.
nfmt=$($G -rc 'jc_group_num(' src --include='*.c' 2>/dev/null | $G -v ':0$' \
       | awk -F: '{s+=$2} END {print s+0}')
if [ "$nfmt" -ge 10 ]; then
    t_ok "$nfmt jc_group_num call sites exist for the rule to govern"
else
    t_fail "only $nfmt jc_group_num call sites (want >= 10) -- checks 1-4 would \
pass on a tree that formats no numbers at all"
fi

t_done
