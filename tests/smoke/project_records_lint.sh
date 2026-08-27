#!/bin/sh
# smoke lint: docs/PROJECT_RECORDS.md teaches commands that actually work.
#
# The page teaches a project-records practice in plain markdown, and its whole
# claim is that the reader can run what it shows on the smallest box they own.
# A tutorial whose commands are never executed rots silently -- the reader is
# the one who finds out, and they conclude the practice is broken rather than
# the page.
#
# So the page carries its own fixtures and its own expected output, and this
# driver runs them. Two marker kinds, both invisible when rendered:
#
#   <!-- file: NAME -->   before a ```markdown fence -- write it as fixture NAME
#   <!-- shell -->        before a ```console fence -- run it, compare output
#   <!-- fragment -->     before a ```console fence -- illustrative, not run
#
# A ```console fence with NO marker is a FAILURE (check 2), which is what makes
# an unchecked command impossible rather than merely discouraged. `fragment` is
# the deliberate escape hatch for a command that cannot be run offline (it
# spawns jichi, or writes a file with today's date in it) -- it is a claim that
# the author looked, not an exemption granted by silence.
#
# No jichi, no Emacs, no network: this driver exercises documentation, not a
# build, which is why it is a *_lint.sh (smoke_lint.sh requires every other
# driver to invoke "$BIN").
. "$(dirname "$0")/_smoke.sh"

t_plan 7

DOC="$SMOKE_ROOT/docs/PROJECT_RECORDS.md"
ws=$(smoke_tmp)
blk=$(smoke_tmp)

if [ ! -f "$DOC" ]; then
    t_fail "docs/PROJECT_RECORDS.md is missing"
    t_fail "(no page: fence classification not checked)"
    t_fail "(no page: fixture usage not checked)"
    t_fail "(no page: record tree not checked)"
    t_fail "(no page: documented commands not run)"
    t_fail "(no page: tool portability not checked)"
    t_fail "(no page: links not resolved)"
    t_done
fi

# --- extract fixtures and runnable blocks ------------------------------------
# One pass. `pend` is the marker seen on the previous line; it is consumed by
# the next fence opening and cleared, so a marker cannot leak past its block.
awk -v dir="$ws" -v bdir="$blk" '
/^<!-- file: /      { pend = "file"; pf = $3; next }
/^<!-- shell -->/   { pend = "shell"; next }
/^<!-- fragment/    { pend = "frag"; next }
/^```/ {
    if (inf) { close(out); inf = 0; pend = ""; next }
    if (pend == "file") {
        i = match(pf, /\/[^\/]*$/)
        if (i > 0) system("mkdir -p \"" dir "/" substr(pf, 1, i - 1) "\"")
        out = dir "/" pf; inf = 1; pend = ""; next
    }
    if (pend == "shell") {
        nb++; out = bdir "/blk." nb; inf = 1; pend = ""; next
    }
    pend = ""; next
}
inf { print > out }
' "$DOC"

nfix=$(find "$ws" -type f | wc -l | tr -d ' ')
nblk=$(ls "$blk" 2>/dev/null | grep -c '^blk\.' || true)

# --- 1: the extraction found something (the denominator) ---------------------
# Without this floor every check below passes vacuously on a page whose
# markers were renamed -- the failure mode docs/TEST_INTEGRITY.md is about.
if [ "$nfix" -ge 5 ] && [ "$nblk" -ge 5 ]; then
    t_ok "extracted $nfix fixture files and $nblk runnable blocks"
else
    t_fail "extraction is empty or thin ($nfix files, $nblk blocks) -- markers renamed?"
fi

# --- 2: every ```console fence is classified ---------------------------------
nconsole=$(grep -c '^```console$' "$DOC" || true)
nshell=$(grep -c '^<!-- shell -->$' "$DOC" || true)
nfrag=$(grep -c '^<!-- fragment -->$' "$DOC" || true)
if [ "$nconsole" -eq $((nshell + nfrag)) ]; then
    t_ok "all $nconsole console blocks are classified ($nshell run, $nfrag fragments)"
else
    t_fail "$nconsole console blocks but $nshell+$nfrag markers -- an unchecked command exists"
fi

# --- 3: every fixture is used by a documented command ------------------------
# A fixture nobody reads back is an example nobody verifies.
cat "$blk"/blk.* > "$blk/all" 2>/dev/null || : > "$blk/all"
unused=""
for f in $(cd "$ws" && find . -type f | sed 's|^\./||'); do
    grep -q "$f" "$blk/all" || unused="$unused $f"
done
if [ -z "$unused" ]; then
    t_ok "every extracted fixture is named by a runnable command"
else
    t_fail "fixtures written but never read back:$unused"
fi

# --- 4: the record tree the page describes is complete -----------------------
missing=""
for f in INBOX.md BOARD.md DECISIONS.md DEFERRED.md; do
    [ -s "$ws/$f" ] || missing="$missing $f"
done
njournal=$(ls "$ws/journal" 2>/dev/null | grep -c '\.md$' || true)
if [ -z "$missing" ] && [ "$njournal" -ge 2 ]; then
    t_ok "the four registers and $njournal journal entries are all shown in full"
else
    t_fail "incomplete example tree: missing[$missing] journal entries=$njournal"
fi

# --- 5: every documented command produces the documented output --------------
# Doctest form: a line beginning "$ " is the command; the lines after it, up
# to the next "$ " or the end of the block, are its expected stdout.
ncmd=0
mismatch=""
got=""
_cmd=""

flush_cmd() {
    [ -n "$_cmd" ] || return 0
    ncmd=$((ncmd + 1))
    ( cd "$ws" && eval "$_cmd" ) > "$blk/got" 2>/dev/null
    if ! cmp -s "$blk/got" "$blk/exp"; then
        if [ -z "$mismatch" ]; then
            mismatch="$_cmd"
            got=$(tr '\n' '/' < "$blk/got")
        fi
    fi
    _cmd=""
}

for b in "$blk"/blk.*; do
    [ -f "$b" ] || continue
    : > "$blk/exp"
    while IFS= read -r line; do
        case "$line" in
        '$ '*)
            flush_cmd
            _cmd=${line#'$ '}
            : > "$blk/exp"
            ;;
        *)
            printf '%s\n' "$line" >> "$blk/exp"
            ;;
        esac
    done < "$b"
    flush_cmd
done

if [ "$ncmd" -ge 10 ] && [ -z "$mismatch" ]; then
    t_ok "all $ncmd documented commands produce the documented output"
elif [ -n "$mismatch" ]; then
    t_fail "documented output is wrong for: $mismatch -- got [$got]"
else
    t_fail "only $ncmd commands checked -- the page's examples went missing"
fi

# --- 6: the page teaches only portable tools ---------------------------------
# The page's premise is a box with nothing installed. GNU-only options are the
# way that premise breaks without anyone noticing: they work here and fail on
# the machine the page was written for.
if grep -n '^\$ ' "$DOC" \
    | grep -E 'grep -[ABCr]|sed -i|[^a-z]rg |[^a-z]jq |readarray|mapfile|echo -e|<\(' \
    > "$blk/unportable" 2>/dev/null; then
    t_fail "non-POSIX tool taught: $(head -1 "$blk/unportable")"
else
    t_ok "every taught command sticks to POSIX tools"
fi

# --- 7: every relative doc link resolves -------------------------------------
# The page ends by handing the reader on; a dead link there is the worst place
# for one, because it is the moment they leave.
dead=""
for l in $(grep -o '](\([A-Za-z0-9_./-]*\.md\))' "$DOC" | sed 's/^](//; s/)$//' | sort -u); do
    [ -f "$SMOKE_ROOT/docs/$l" ] || dead="$dead $l"
done
if [ -z "$dead" ]; then
    t_ok "every relative documentation link resolves"
else
    t_fail "dead links:$dead"
fi

t_done
