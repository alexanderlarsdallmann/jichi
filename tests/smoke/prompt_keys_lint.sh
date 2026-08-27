#!/bin/sh
# smoke lint: no keypress prompt ships in bracket-only form (M551).
#
# THE DEFECT, found by ear and not by reading. `Allow? [y]es  [n]o  [a]lways`
# shows each accepted key INSIDE the word it names. That is a good visual
# affordance -- a sighted reader learns the keys without being told them -- and
# read aloud it is a stream of punctuation and single letters: a screen reader
# announces "bracket y bracket e s, bracket n bracket o". The operator's report,
# from the manual protocol: "every single character was read... the options were
# unintelligible with the brackets".
#
# WHY A LINT AND NOT AN AUDIT. The fix was first made only where the defect was
# reported -- the tool-approval prompt -- which left the two prompts guarding a
# `sudo` and a PHYSICAL ACTUATION still spelling themselves out. That is the
# shape this project keeps finding: a rule written down in one place and not
# applied in the neighbouring one (M536 x4, M544, M549, M462, and this). An
# audit found three sites once; this finds the fourth whenever someone adds it.
#
# UNIVERSE, measured 2026-08-23: lines in src/ carrying BOTH `[y]` and `[n]`.
# **11 lines in exactly 2 files** -- src/tui/jc_tui.c (the two red confirms) and
# src/util/jc_msg.c (the five-language catalog, whose own entries and comments
# account for nine). Small enough to enumerate, which is the precondition
# CLAUDE.md sets for a lint over an audit.
#
# THE PATTERN, and how it was verified. `[y]` alone is useless here: in a regex
# it is the character CLASS "y", so it matches every `buf[y]` -- a first attempt
# returned 65 KB of array subscripts. Requiring `[y]` and `[n]` on ONE line is
# specific: no single-letter-subscript line in src/ carries both. The one
# false-positive shape that exists in principle is a line like
# `x = arr[y]; y = buf[n];` -- checked, and there is none in the tree. If one
# ever appears, add it to the exemption in check 4 with that reason.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)        # M553: check 3 strips comments into a scratch copy
cd "$SMOKE_ROOT" || exit 1

# A keypress prompt in the visual form: one line naming both keys.
BRACKET='\[y\].*\[n\]'
# Its accessible counterpart. DELIBERATELY WORDING-AGNOSTIC: this lint's job is
# that a counterpart EXISTS, not how it is phrased. An earlier draft anchored on
# the exact phrase "Press y as in yes, n as in no" and would therefore have gone
# red on a legitimate rewording -- a lint that fails when the product improves
# teaches people to edit the lint.
#
# It does have to end at "no." though, and that is not cosmetic. jichi's VOICE
# branch uses the same "as in" wording (M552) and continues "...a as in
# always." -- and it is NOT an accessible rendering: it is jichi speaking
# through its own TTS, a different consumer (see the audit's "voice-mode
# pattern"). Anchoring on the period excludes it, so check 2 counts only
# prompts a screen reader will read. A future inline prompt offering more than
# y/n will not match and check 2 will fail on the count -- the right failure:
# somebody looks.
SPOKEN='Press y .*no\.'

# ---- 1: the universe is the one measured, and the pattern can fire --------
# Both floors. A pattern that matched nothing would make checks 2-4 vacuous, and
# so would a file list that had drifted to empty.
files=$("$G" -rlE "$BRACKET" src --include='*.c' --include='*.h' 2>/dev/null \
        | sort | tr '\n' ' ')
nlines=$("$G" -rhcE "$BRACKET" src --include='*.c' --include='*.h' 2>/dev/null \
         | awk '{s+=$1} END {print s+0}')
nfiles=$(printf '%s\n' $files | "$G" -c . || true)
if [ "$nlines" -ge 3 ] && [ "$nfiles" -eq 2 ]; then
    t_ok "$nlines bracket-key prompt lines in $nfiles files (measured: 11 / 2)"
else
    t_fail "universe drifted: $nlines lines in $nfiles files (want >= 3 lines, \
exactly 2 files). Files: ${files:-none}. If a prompt was legitimately added or \
removed, update this floor and say why -- an unexamined drift here is how the \
sudo prompt stayed unreadable."
fi

# ---- 2: every keypress prompt has an accessible counterpart -------------
# THIS CHECK HAS BEEN WRONG THREE TIMES, each time for the same reason in a new
# disguise, and the history is the point:
#
#   M552  pinned the PHRASE  `Press y as in yes, n as in no`  -> went red when
#         the wording legitimately shortened to fit 78 columns.
#   M553  pinned the SYNTAX  `c->accessible ? JC_MSG_...`     -> went red when
#         the ternary legitimately became an if/else.
#   M557  pinned the LOCATION -- it counted spoken literals in jc_tui.c -> went
#         red when the strings legitimately moved into the catalog, which was
#         the entire point of stage A2.
#
# **Pin the property, not the incidental.** The property that cannot move is a
# PAIRING: every prompt that blocks on a keypress has both renderings, and
# neither side is dead. Counted from two directions, because each catches what
# the other cannot:
#
#   2a  the INLINE sighted prompts are exactly the two known ones. A third
#       inline prompt -- a new confirm someone added -- moves this number, and
#       the question "does a listener hear its keys" then has to be asked once,
#       by a person.
#   2b  the accessible catalog entries are exactly three, and every one is
#       REACHED from code. An entry nothing selects is a translation nobody will
#       ever hear; a prompt with no entry is 2a's job.
#
# The SELECTION itself -- that --accessible actually picks the accessible form --
# is proven behaviourally and better, by six checks across three drivers that
# render each prompt through a real PTY with a sighted control:
# accessible.sh 10/11, privileged.sh 7/8, kinetic.sh 10/11.
ninline=$("$G" -cE "$BRACKET" src/tui/jc_tui.c 2>/dev/null || echo 0)
[ -n "$ninline" ] || ninline=0
stripped_tui="$tmp/jc_tui.stripped"
sed -e 's|/\*.*\*/||g' -e 's|^[[:space:]]*\*.*$||' -e 's|/\*.*$||' \
    src/tui/jc_tui.c > "$stripped_tui"
nacc_decl=$("$G" -c '_PROMPT_ACC,' include/jc_msg.h 2>/dev/null || echo 0)
[ -n "$nacc_decl" ] || nacc_decl=0
nacc_used=0
for e in JC_MSG_ALLOW_PROMPT_ACC JC_MSG_PRIV_PROMPT_ACC \
         JC_MSG_KINETIC_PROMPT_ACC; do
    "$G" -q "$e" "$stripped_tui" && nacc_used=$((nacc_used + 1))
done
if [ "$ninline" -eq 2 ] && [ "$nacc_decl" -eq 3 ] && [ "$nacc_used" -eq 3 ]; then
    t_ok "prompts are paired: 2 inline sighted, 3 accessible entries, all reached"
else
    t_fail "prompt pairing broke: $ninline inline bracket prompts (want 2), \
$nacc_decl accessible entries declared (want 3), $nacc_used of them reached from \
code (want 3).
  A new blocking prompt needs an accessible form, branched on c->accessible.
  An accessible entry nothing selects is a translation nobody will hear.
  Inline prompts found: $("$G" -nE "$BRACKET" src/tui/jc_tui.c | head -3 | tr '\n' ' ')"
fi

# ---- 3: NEITHER catalog entry is an orphan ------------------------------
# THIS CHECK WAS TOO STRICT AND M553 CAUGHT IT. It asserted the literal string
# `c->accessible ? JC_MSG_ALLOW_PROMPT_ACC` -- one syntactic form of the branch.
# M553 legitimately replaced that ternary with an if/else, because the
# accessible arm now also omits the input-arrow line, and the lint went red on a
# change that improved the thing it was guarding. Same mistake as this file's
# first SPOKEN pattern, in the same file, two milestones apart: **pin the
# property, not the syntax.**
#
# So this now asserts only what grep can honestly assert -- that both entries
# are reached from real CODE rather than surviving as comments -- and names what
# covers the rest. Comments are stripped first, which also matters: without it a
# build whose only mention of the accessible entry was the comment explaining it
# would pass.
#
# THE SELECTION ITSELF IS PROVEN BEHAVIOURALLY, and better than here:
#   accessible.sh 10/11   the tool-approval prompt, both arms
#   privileged.sh  7/8    the sudo prompt, both arms
#   kinetic.sh    10/11   the actuation prompt, both arms
# Each of those renders the prompt through a real PTY and asserts the accessible
# form AND a sighted control. A structural check cannot beat that; what it adds
# is catching an entry that is declared and never referenced at all.
stripped="$tmp/jc_tui.stripped"
sed -e 's|/\*.*\*/||g' -e 's|^[[:space:]]*\*.*$||' -e 's|/\*.*$||' \
    src/tui/jc_tui.c > "$stripped"
if "$G" -q 'JC_MSG_ALLOW_PROMPT_ACC' include/jc_msg.h \
   && "$G" -q 'JC_MSG_ALLOW_PROMPT_ACC' "$stripped" \
   && "$G" -q 'JC_MSG_ALLOW_PROMPT[^_]' "$stripped" \
   && "$G" -q 'accessible' "$stripped"; then
    t_ok "both approval-prompt entries are reached from code, not just comments"
else
    t_fail "an approval-prompt catalog entry is declared and never referenced \
from code -- it would exist and never be spoken. Code lines mentioning it: \
$("$G" -n 'ALLOW_PROMPT' "$stripped" | head -4 | tr '\n' ' ')"
fi

# ---- 4: the file set is exactly the known two ---------------------------
# A fence around the exemptions rather than a fix, on the priced_model_lint
# pattern. A THIRD file acquiring a keypress prompt is a thing to look at: it
# means a new blocking prompt exists, and the question "does a listener hear the
# keys" has to be asked about it once, by a person. Extend this list with a
# reason -- never silently.
known='src/tui/jc_tui.c src/util/jc_msg.c'
want=$(printf '%s\n' $known | sort | tr '\n' ' ')
if [ "$files" = "$want" ]; then
    t_ok "the 2 files carrying keypress prompts are unchanged"
else
    t_fail "the set of files with a bracket-key prompt changed.
  found: ${files:-none}
  known: $want
A new one means a new blocking prompt. Give it an accessible form branched on
c->accessible, then add it here."
fi

t_done
