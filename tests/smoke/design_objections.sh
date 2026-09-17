#!/bin/sh
# smoke: task 10's grader checks the SHAPE of an objection -- stated before it is
# answered -- when, and only when, the design carries an `## Objections`
# section (M633).
#
# THE GAP. Module 06's gate says /check feedback must be "applied or explicitly
# rebutted in the doc (an ## Objections note)". Nothing required the objection
# be STATED before the rebuttal, and the one fallacy the curriculum names --
# straw men -- was named to the instructor (INSTRUCTOR.md, M6 failure modes),
# never to the learner who will commit it. The grader now asks for two labels
# per entry, `Objection:` and `Reply:`. It cannot see whether the objection is
# stated in its strongest form (a judgement); it can see whether it was stated
# at all before being answered. The section stays optional: a design with none
# is graded exactly as before.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(smoke_tmp)
G="$root/docs/assignments/10-design-before-code/test.sh"

# A design that passes the five structural checks, with no Objections section.
base() {
    cat <<'EOF'
# linelog -- design

## Problem
A team wants a shared, chronological log of one-line notes kept in the repo.
Appending must be one command; finding a note weeks later must be one command.

## Requirements
- `linelog add <text>` appends one timestamped line; never edits history.
- `linelog find <word>` prints matching lines, oldest first, with dates.
- Storage is one plain-text file in the repository, mergeable by git.
- Non-goals: no encryption, no per-user identity beyond git's, no editing.

## Design
One file, `NOTES.log`, one record per line:

```
2026-07-28T14:02:11Z  switched the parser to a two-pass design
2026-07-29T09:15:40Z  flaky test traced to PATH ordering
```

`add` opens the file in append mode and writes `<ISO-8601 UTC>  <text>`;
`find` is a case-insensitive substring scan printing matches in file order.
A missing file is created by `add` and treated as empty by `find`.

## Alternatives considered
- One file per note -- rejected: thousands of tiny files make `find` slow.
- SQLite storage -- rejected: binary blobs do not merge in git.
- JSON lines -- rejected: quoting prose in JSON buys nothing and hurts grep.

## Test plan
- `add` then `find` round-trips a note verbatim.
- Two `add`s preserve order; `find` prints oldest first.
- `find` on a fresh checkout (no log file) prints nothing, exit 0.
- An unwritable log file is a plain error naming the path.
EOF
    # pad past the 1500-byte floor with honest prose, not filler characters
    i=0
    while [ $i -lt 6 ]; do
        echo "Failure behaviour, case $i: the command prints the path it could not open and exits 1, so a script can tell the two apart."
        i=$((i + 1))
    done
}

run() { # $1 = dir with DESIGN.md. The grader cds to its own directory
    # (it is written to sit beside the learner's DESIGN.md), so a copy sits there.
    cp "$G" "$1/test.sh"
    (cd "$1" && sh ./test.sh > "$1/out.tap" 2>&1); echo $? > "$1/rc"
}

# --- 1: no Objections section: graded as before, check 6 passes by absence -------
d1=$(smoke_tmp); base > "$d1/DESIGN.md"; run "$d1"
if [ "$(cat "$d1/rc")" = 0 ] && grep -q "^ok 6 - no ## Objections section" "$d1/out.tap"; then
    t_ok "a design without ## Objections passes; check 6 says the section is optional"
else
    t_fail "rc=$(cat "$d1/rc"): $(grep ' 6 ' "$d1/out.tap")"
fi

# --- 2: both labels on every entry: passes ------------------------------------------
d2=$(smoke_tmp); { base; cat <<'EOF'

## Objections
- Objection: /check says a shared file will see merge conflicts on every
  concurrent append, and the design waves this away.
  Reply: two appends to different lines of a text file are the trivial
  merge git resolves without a conflict marker; the test plan's order test
  covers the one case that matters (same second, two checkouts).
- Objection: the reviewer wants an `edit` command for typos.
  Reply: declined as a non-goal on purpose -- history is the audit trail,
  and a typo is corrected by a new note that says so.
EOF
} > "$d2/DESIGN.md"; run "$d2"
if [ "$(cat "$d2/rc")" = 0 ] && grep -q "^ok 6 - every objection is stated" "$d2/out.tap"; then
    t_ok "objections with Objection: and Reply: on each entry pass check 6"
else
    t_fail "rc=$(cat "$d2/rc"): $(grep ' 6 ' "$d2/out.tap")"
fi

# --- 3: a rebuttal with no stated objection: fails, and says which label ---------------
d3=$(smoke_tmp); { base; cat <<'EOF'

## Objections
- Reply: the reviewer is wrong about merge conflicts; git handles appends.
- Objection: an `edit` command was requested.
  Reply: declined as a non-goal.
EOF
} > "$d3/DESIGN.md"; run "$d3"
if [ "$(cat "$d3/rc")" != 0 ] && grep -q "^not ok 6 - 1 objection(s)" "$d3/out.tap" && \
   grep -q "Objection:" "$d3/out.tap"; then
    t_ok "an answer with no stated objection fails check 6, counting the entry and naming the label"
else
    t_fail "rc=$(cat "$d3/rc"): $(grep ' 6 ' "$d3/out.tap")"
fi

# --- 4: an objection with no reply: fails too (stated, never answered) ---------------------
d4=$(smoke_tmp); { base; cat <<'EOF'

## Objections
- Objection: the reviewer says one file will not scale past ten thousand notes.
EOF
} > "$d4/DESIGN.md"; run "$d4"
if [ "$(cat "$d4/rc")" != 0 ] && grep -q "^not ok 6 - 1 objection(s)" "$d4/out.tap"; then
    t_ok "an objection with no Reply: fails check 6 (stated is not answered)"
else
    t_fail "rc=$(cat "$d4/rc"): $(grep ' 6 ' "$d4/out.tap")"
fi

# --- 5: an empty section is not a rebuttal ----------------------------------------------
d5=$(smoke_tmp); { base; printf '\n## Objections\n\nNone worth recording.\n'; } > "$d5/DESIGN.md"; run "$d5"
if [ "$(cat "$d5/rc")" != 0 ] && grep -q '^not ok 6 - ## Objections has no "- " entry' "$d5/out.tap"; then
    t_ok "an ## Objections section with no entry fails: the heading alone rebuts nothing"
else
    t_fail "rc=$(cat "$d5/rc"): $(grep ' 6 ' "$d5/out.tap")"
fi

# --- 6-7: through `jichi grade`, the path a learner takes -------------------------------
# The spec's `verify:` runs the grader in its own directory; jichi folds the TAP
# into a PASS/FAIL verdict. The straw-man design must FAIL there and the
# well-formed one PASS -- so the shape check reaches the learner, not only the
# script run by hand above.
smoke_home
ws=$(smoke_tmp)
mkdir -p "$ws/docs/assignments/10-design-before-code"
cp "$root/docs/assignments/10-design-before-code.md" "$ws/docs/assignments/"
cp "$G" "$ws/docs/assignments/10-design-before-code/test.sh"
cp "$d3/DESIGN.md" "$ws/docs/assignments/10-design-before-code/DESIGN.md"
out=$(cd "$ws" && with_deadline 40 "$BIN" grade docs/assignments/10-design-before-code.md < /dev/null 2>&1); rc=$?
if [ $rc -ne 0 ] && printf '%s' "$out" | grep -q "FAIL"; then
    t_ok "jichi grade FAILs the design whose entry replies to an unstated objection"
else
    t_fail "grade rc=$rc: $(printf '%s' "$out" | head_bytes 200)"
fi
cp "$d2/DESIGN.md" "$ws/docs/assignments/10-design-before-code/DESIGN.md"
out=$(cd "$ws" && with_deadline 40 "$BIN" grade docs/assignments/10-design-before-code.md < /dev/null 2>&1); rc=$?
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q "PASS"; then
    t_ok "jichi grade PASSes the design that states each objection before answering it"
else
    t_fail "grade rc=$rc: $(printf '%s' "$out" | head_bytes 200)"
fi
t_done
