#!/bin/sh
# smoke lint: the teaching docs tell a reader what they need BEFORE they need it
# (M404 locators in docs/reading/, M405 the learner corpus, M406 prerequisites).
#
# The subject is one thing stated three ways: a precondition placed after the
# command that requires it is a precondition the reader meets as a failure.
#
# THE DEFECT THIS EXISTS FOR. A reader of the source-reading guides reported that
# the shell commands never say which terminal or which directory. They were right,
# and the failure was reproducible IN ORDER: annai chapter 5 ends with
# `cd /tmp/annai5`, chapter 6 opens with `grep -n ... tests/test_provider.c` -- a
# relative path into the checkout, run from a scratch directory. "No such file or
# directory", with nothing to explain it.
#
# The learner corpus is worse, because it is bigger and it is where a self-learner
# starts: 56 of its 66 blocks are `jichi grade docs/assignments/NN-....md` or a
# `sh docs/assignments/.../test.sh`, all of which resolve relative to the
# REPOSITORY ROOT and all of which fail silently-looking from anywhere else --
# indistinguishable, to a beginner, from a broken assignment.
#
# THE CONVENTION: the first line inside every ```sh block is a locator comment
# beginning `# in ` or `# anywhere`. Chosen because it is
#   - a comment, so copy-pasting the whole block is unharmed;
#   - greppable, so a lint can hold it -- the deciding argument. A prose sentence
#     before the block would read as well and rot silently.
#
# Scope is the three teaching corpora and nothing else. Reference pages
# (docs/*.md) are deliberately excluded: their commands are shown to explain a
# flag, not to be followed in sequence by someone who has not built jichi yet, and
# a lint that demanded locators there would collect exceptions until it meant
# nothing. If that judgement turns out wrong, widening the list below is one line.
#
# Runs no jichi and compiles nothing (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 8

tmp=$(smoke_tmp)

# --- the scan, shared by checks 1-3 -----------------------------------------
# Emits "file: <first line>" for every block whose first line is not a locator.
scan_dir() {
    _sd_dir="$1"
    : > "$tmp/bad"
    _sd_n=0
    for f in "$SMOKE_ROOT/$_sd_dir"/*.md; do
        [ -f "$f" ] || continue
        awk -v F="$(basename "$f")" '
            /^```sh$/ { inb = 1; want = 1; next }
            /^```/    { inb = 0; next }
            inb && want {
                want = 0
                if ($0 !~ /^# (in|anywhere)/) print F ": " substr($0, 1, 44)
            }
        ' "$f" >> "$tmp/bad"
        _sd_n=$((_sd_n + $(grep -c '^```sh$' "$f")))
    done
    # `grep -c` prints 0 AND exits 1 on no matches, so `|| echo 0` would print
    # twice and poison the arithmetic below (it did, first time round).
    _sd_bad=$(grep -c . "$tmp/bad" || true)
    BLOCKS=$_sd_n
    UNLOCATED=$_sd_bad
}

check_dir() {
    _cd_dir="$1"; _cd_floor="$2"
    scan_dir "$_cd_dir"
    if [ "$BLOCKS" -lt "$_cd_floor" ]; then
        t_fail "$_cd_dir: found only $BLOCKS shell blocks (floor $_cd_floor) -- the extraction broke; fix it rather than the floor"
    elif [ "$UNLOCATED" -eq 0 ]; then
        t_ok "$_cd_dir: all $BLOCKS shell blocks say where to run ($(basename "$_cd_dir"))"
    else
        t_fail "$_cd_dir: $UNLOCATED of $BLOCKS blocks do not say where to run: $(tr '\n' ' ' < "$tmp/bad" | head_bytes 170)"
    fi
}

# The floors are the counts at M405. A floor exists so a broken extraction fails
# loudly instead of passing over zero files -- the M295 lesson.
check_dir docs/reading 15
check_dir docs/curriculum 5
check_dir docs/assignments 60

# --- 4: the corpora that carry commands also carry the orientation ----------
# A per-block locator answers "where"; it does not answer "why is there more than
# one place", "jichi or ./jichi", or "how do I undo my fixture edits". Each corpus
# needs that said once, on the page a reader opens first.
oriented=0
nindex=0
for f in "$SMOKE_ROOT/docs/reading/ANNAI.md" \
         "$SMOKE_ROOT/docs/reading/FUKABORI.md" \
         "$SMOKE_ROOT/docs/reading/TSUISEKI.md" \
         "$SMOKE_ROOT/docs/assignments/INDEX.md"; do
    nindex=$((nindex + 1))
    grep -qiE 'where to (run|type)' "$f" && grep -q './jichi' "$f" &&
        oriented=$((oriented + 1))
done
if [ "$oriented" -eq "$nindex" ]; then
    t_ok "all $nindex index pages explain where commands run and which binary"
else
    t_fail "only $oriented of $nindex index pages explain where commands run (reading x3, assignments)"
fi

# --- 5: a toolchain prerequisite precedes the command that needs it (M406) ---
# THE DEFECT THIS EXISTS FOR. Thirty-two graded tasks stated their toolchain
# ("Prerequisite: GHC (`runghc`)") in a blockquote 5-25 lines BELOW the command
# that runs it, and the four Racket tasks -- the first course in the functional
# family -- stated it nowhere at all, while their grader guards on `raco`. So the
# learner ran the command, got `command not found`, and had to keep reading PAST
# the failure to learn it was expected. Set A's older tasks already did it right
# (06-make-the-test-pass puts the note straight after the frontmatter), so the
# convention existed and had simply not been carried into the language courses.
late=""
for f in "$SMOKE_ROOT"/docs/assignments/*.md; do
    b=$(grep -n '^```sh$' "$f" | head -1 | cut -d: -f1)
    p=$(grep -n '^> \*\*Prerequisite' "$f" | head -1 | cut -d: -f1)
    [ -n "$b" ] && [ -n "$p" ] && [ "$p" -gt "$b" ] &&
        late="$late $(basename "$f")"
done
# Every task whose grader guards on a tool must SAY so. Ground truth is the guard
# itself (`command -v <tool>` in the task's own scripts) -- not a list here, which
# would rot the first time a course was added.
missing=""
for d in "$SMOKE_ROOT"/docs/assignments/*/; do
    grep -qh 'command -v' "$d"*.sh 2>/dev/null || continue
    page="${d%/}.md"
    [ -f "$page" ] || continue
    grep -q '^> \*\*Prerequisite' "$page" || missing="$missing $(basename "$page")"
done
if [ -z "$late" ] && [ -z "$missing" ]; then
    t_ok "every guarded task states its toolchain, before the command that needs it"
else
    t_fail "prerequisite after the command:$late; guarded but unstated:$missing"
fi

# --- 6: every curriculum module can be left (M406) --------------------------
# Pinned rather than fixed: all twelve already carry
# `[Prev] · [Curriculum map] · [Next]`. This check exists because a
# RECOMMENDATION to add them was raised on a wrong measurement -- an ad-hoc grep
# for `Next:` and `→` missed the real `[Next ▶]` form and reported 9 of 12
# missing. The convention is real, unanimous, and was one regex away from being
# "fixed" into duplication; a true invariant is worth a line of proof.
navless=""
for f in "$SMOKE_ROOT"/docs/curriculum/[0-1][0-9]-*.md; do
    grep -q 'Curriculum map' "$f" || navless="$navless $(basename "$f")"
done
nmod=$(ls "$SMOKE_ROOT"/docs/curriculum/[0-1][0-9]-*.md 2>/dev/null | wc -l)
if [ "$nmod" -ge 10 ] && [ -z "$navless" ]; then
    t_ok "all $nmod curriculum modules carry a prev/map/next footer"
else
    t_fail "modules found=$nmod, without a nav footer:$navless"
fi

# --- 7: the plain-register tier is reachable and located (M408) --------------
# THE DEFECT THIS EXISTS FOR. The three plain tasks were invisible to check 3 for
# two milestones: their fences carried NO LANGUAGE TAG, so a scan for ```sh never
# saw them, and the corpus that most needs "where do I type this" was the one the
# convention skipped. Twice before, the same tier was the one that inherited the
# least care -- it was missing the working-directory footer at M405 and had no
# prerequisite notes until M406 -- and both times it was reached by a sweep rather
# than by anyone reading it.
#
# Two things are pinned. The tasks must route to their own hub page (a reader who
# lands on p1 from `jichi assignments` has no other way to learn that `jichi`
# starts the agent), and their grade command must be tagged so check 3 keeps
# seeing it. English and German both: the German edition is the original, not a
# translation, so it cannot be allowed to drift into being the untended one.
plain_bad=""
for f in "$SMOKE_ROOT"/docs/assignments/p[0-9]-*.md; do
    grep -q 'PLAIN_LANGUAGE\.md' "$f" || plain_bad="$plain_bad en:$(basename "$f")"
    grep -q '^```sh$' "$f" || plain_bad="$plain_bad en-untagged:$(basename "$f")"
done
for f in "$SMOKE_ROOT"/docs/i18n/de/assignments/p[0-9]-*.md; do
    grep -q 'EINFACHE_SPRACHE\.md' "$f" || plain_bad="$plain_bad de:$(basename "$f")"
    grep -q '^```sh$' "$f" || plain_bad="$plain_bad de-untagged:$(basename "$f")"
done
nplain=$(ls "$SMOKE_ROOT"/docs/assignments/p[0-9]-*.md \
            "$SMOKE_ROOT"/docs/i18n/de/assignments/p[0-9]-*.md 2>/dev/null | wc -l)
if [ "$nplain" -ge 6 ] && [ -z "$plain_bad" ]; then
    t_ok "all $nplain plain-register tasks route to their hub and carry a tagged command"
else
    t_fail "plain tier: found $nplain pages, problems:$plain_bad"
fi

# 8: every tutorial routes its reader to the vocabulary (M657).
#
# Measured before it was written, because the population is the argument:
# VOCABULARY.md was reachable from **0 of 10** tutorial pages and 0 of 14
# curriculum module pages, while `self_learner_lint` check 4 held it to DEFINING
# 24 required words. Defining a word and routing a reader to the definition are
# different guarantees, and only the first was ever checked -- so the page that
# exists to be met BEFORE the word is met was, for a tutorial reader, met never.
# A policy nobody is routed to is a policy nobody applies (M510).
#
# The trigger was a reader rather than a sweep: the first native
# Japanese-speaking reviewer of the localized pages asked for the English
# *idioms* to be explained -- dogfooding, blast radius, born red -- which is a
# person needing that page and not having it in front of her.
#
# Universe: docs/*TUTORIAL*.md, floored at today's exact 10. The curriculum
# module pages are deliberately NOT in it: they are a second population with a
# second navigation convention (the [Prev]/[Curriculum map]/[Next] locator row
# that check 6 pins), and folding them in here would mean one check reporting on
# two universes -- which is how a green check ends up meaning neither.
JC_LOC_MIN_TUTS=10
_ntut=$(ls "$SMOKE_ROOT"/docs/*TUTORIAL*.md 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_ntut" -lt "$JC_LOC_MIN_TUTS" ]; then
    t_fail "only $_ntut tutorial pages found (floor $JC_LOC_MIN_TUTS) -- the \
naming changed and this check is reading almost nothing. Fix the extraction, \
not the floor."
else
    vocab_bad=""
    for f in "$SMOKE_ROOT"/docs/*TUTORIAL*.md; do
        grep -q 'VOCABULARY\.md' "$f" || vocab_bad="$vocab_bad $(basename "$f")"
    done
    if [ -z "$vocab_bad" ]; then
        t_ok "all $_ntut tutorials route the reader to VOCABULARY.md"
    else
        t_fail "tutorial(s) with no route to VOCABULARY.md:$vocab_bad -- a \
reader who meets an unfamiliar word in a tutorial has nowhere to go, and the \
words this project leans on are defined in exactly one place"
    fi
fi

t_done
