#!/bin/sh
# smoke lint: the language course's graded track cites the learner's OWN
# snapshot, and the citations are real (M674).
#
# WHAT THIS DEFENDS. docs/LANGUAGE_COURSE.md teaches a learner to download a
# language's official documentation and work through it with jichi, and the
# whole point of the exercise is that the answer to "where does this come
# from?" is a file on their disk rather than something a model remembered. The
# graded track (tasks 81-84) is where that promise is either kept or quietly
# dropped: each spec names the tutorial section it comes from, and a citation
# nobody checks is a citation that drifts.
#
# THE DEFECT THIS CAUGHT ON ITS FIRST RUN. Task 83 said the set comprehension
# was in `tutorial/introduction.txt` under "Sets". It is in
# `tutorial/datastructures.txt`. The path existed, the section existed, the
# sentence read perfectly -- and a learner following it would have opened the
# wrong file and found nothing. That is the failure mode this project calls
# "fluent and wrong", and it is precisely what a prose review does not catch.
#
# TWO GATES, AND ONLY ONE OF THEM CAN RUN EVERYWHERE. The snapshot is a 16 MB
# download the learner makes; it is not in this repository and it is not on the
# CI boxes or the BSD rigs. So the STRUCTURAL gate (a citation exists, and it
# has the shape of a path into the snapshot) runs everywhere, and the
# RESOLUTION gate (the file exists, and the named section is really in it) runs
# only where a snapshot is reachable and SKIPS with its reason otherwise. A
# check that cannot run here must say so rather than pass silently -- the
# difference between "verified" and "not contradicted".
#
# Extraction is by awk rather than `grep -o`: on illumos, `grep -o` returns
# only the FIRST match per line (DEFERRED.md, M661b), and a citation line may
# carry several section titles.
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
AD="$SMOKE_ROOT/docs/assignments"

# The snapshot, if the learner (or this operator) has one. The fetch script's
# own default, so the lint looks where the tool puts things.
CORP="${JC_COURSE_CORPORA:-$HOME/development/course-corpora}"
SNAP=""
for d in "$CORP"/python-*; do
    [ -d "$d/tutorial" ] || continue
    SNAP="$d"
done

# The universe: every spec whose frontmatter says `stage: python`. Derived,
# not listed, so a fifth task joins the check by existing.
specs=""
nspec=0
for f in "$AD"/8*-python-*.md; do
    [ -f "$f" ] || continue
    grep -q '^stage: python$' "$f" || continue
    specs="$specs $f"
    nspec=$((nspec+1))
done

# --- 1: every python-stage spec carries a citation block --------------------
# Extraction is POSITIONAL, and it has to be. The first version of check 4
# collected a spec's paths and its section titles into two separate lists and
# asked whether each title was in ANY of that spec's files. Task 83's real
# defect -- "Sets" cited to introduction.txt while datastructures.txt was also
# cited -- sailed straight through it, because the title WAS in one of the two.
# The check reported a property nobody had asked about, which is failure mode 9
# in docs/TEST_INTEGRITY.md: the assertion matched, but not the thing it named.
# So the scan walks each citation line left to right, carries the last path it
# saw, and attributes every title after it to THAT path.
: > "$tmp/nocite"
: > "$tmp/pairs"
: > "$tmp/paths"
for f in $specs; do
    b=$(basename "$f")
    awk -v spec="$b" '
        /^> \*\*Read this first/ { inblock = 1 }
        inblock && !/^>/           { inblock = 0 }
        inblock {
            s = $0
            while (1) {
                pi = match(s, /`tutorial\/[a-z0-9_]+\.txt`/)
                pl = RLENGTH; ps = RSTART
                ti = match(s, /\*"[^"]+"\*/)
                tl = RLENGTH; ts = RSTART
                if (pi == 0 && ti == 0) { break }
                if (pi != 0 && (ti == 0 || ps < ts)) {
                    path = substr(s, ps + 1, pl - 2)
                    print spec "|" path "|"
                    s = substr(s, ps + pl)
                } else {
                    title = substr(s, ts + 2, tl - 4)
                    print spec "|" path "|" title
                    s = substr(s, ts + tl)
                }
            }
        }' "$f" >> "$tmp/pairs.one"
    if [ -s "$tmp/pairs.one" ]; then
        cat "$tmp/pairs.one" >> "$tmp/pairs"
        awk -F'|' -v s="$b" '$2 != "" { print s " " $2 }' "$tmp/pairs.one" | sort -u >> "$tmp/paths"
    else
        echo "$b" >> "$tmp/nocite"
    fi
    : > "$tmp/pairs.one"
done
if [ "$nspec" -ge 4 ] && [ ! -s "$tmp/nocite" ]; then
    t_ok "all $nspec python-stage specs carry a 'Read this first' citation block"
else
    t_fail "$(wc -l < "$tmp/nocite") of $nspec python-stage specs cite no snapshot path: $(tr '\n' ' ' < "$tmp/nocite")
Each task names the tutorial section it comes from -- that is the course's
promise that a claim can be checked against a file rather than believed.
(A universe under 4 means the glob or the frontmatter key moved.)"
fi

# --- 2: every cited path has a section named against IT ---------------------
# A file is three hundred lines. "Read errors.txt" is not a citation, it is a
# gesture -- and a path with no title attributed to it is exactly that.
: > "$tmp/bare"
while read -r spec p; do
    grep -q "^$spec|$p|." "$tmp/pairs" || echo "$spec cites $p with no section named" >> "$tmp/bare"
done < "$tmp/paths"
if [ ! -s "$tmp/bare" ]; then
    t_ok "every cited path has at least one section named against it"
else
    t_fail "path(s) cited with no section:
$(cat "$tmp/bare")"
fi

# --- 3: the cited paths exist in a real snapshot (skips without one) ---------
if [ -z "$SNAP" ]; then
    t_skip_one "the cited paths resolve -- no Python snapshot under $CORP"
else
    : > "$tmp/badpath"
    while read -r spec p; do
        [ -f "$SNAP/$p" ] || echo "$spec cites $p, which is not in the snapshot" >> "$tmp/badpath"
    done < "$tmp/paths"
    np=$(wc -l < "$tmp/paths")
    if [ ! -s "$tmp/badpath" ] && [ "$np" -ge 4 ]; then
        t_ok "all $np cited paths resolve in $(basename "$SNAP")"
    else
        t_fail "citation(s) that do not resolve in $(basename "$SNAP"):
$(cat "$tmp/badpath")
(Fewer than 4 paths means the extraction broke, not the prose.)"
    fi
fi

# --- 4: each section is in the file it was cited AGAINST ---------------------
# The one that caught task 83. A title that exists somewhere in the tutorial is
# not a citation: it has to be in the file the sentence sent the reader to.
if [ -z "$SNAP" ]; then
    t_skip_one "the cited sections resolve -- no Python snapshot under $CORP"
else
    : > "$tmp/badtitle"
    nt=0
    while IFS='|' read -r spec p title; do
        [ -n "$title" ] || continue
        nt=$((nt+1))
        [ -f "$SNAP/$p" ] || continue          # check 3 owns that failure
        grep -q "$title" "$SNAP/$p" || \
            echo "$spec: \"$title\" is not in $p, the file it is cited against" >> "$tmp/badtitle"
    done < "$tmp/pairs"
    if [ ! -s "$tmp/badtitle" ] && [ "$nt" -ge 4 ]; then
        t_ok "all $nt cited sections are in the file their sentence points at"
    else
        t_fail "section(s) cited to the wrong file:
$(cat "$tmp/badtitle")
This is the shape the check exists for: the path is real, the section is real,
and a learner following the sentence opens the wrong file. (Under 4 titles
means the extraction broke.)"
    fi
fi

t_done
