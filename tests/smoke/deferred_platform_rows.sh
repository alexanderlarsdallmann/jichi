#!/bin/sh
# smoke: the DEFERRED register carries the unclassified 2026-11-13 platform
# re-measure rows (SV:2307 card 6, board card 63; note
# docs/internal/drive/2026-11-05-harness-port/notes/M1185.md, batch REL3).
#
# It pins the SET, not a count. The unclassified set is enumerated a SECOND
# way here, independent of DEFERRED.md: the PLATFORMS.md lines that carry the
# re-measure date "2026-11-13, 0.14.0" AND the word "nclassified" (the M1181
# rows). The first column's **Name** is extracted with sed and floored at 8 --
# the known universe (the CHANGELOG [0.14.0] known-issues list names the same
# 8). For each name the driver asserts DEFERRED.md's section carries a row
# naming it together with a 5-digit measured figure, and the section's
# detail rows carry a floor of 5 so a collapsing extraction is caught rather
# than passing vacuously.
#
# Run standalone: TMPDIR=/tmp/jdw-1185 sh tests/smoke/deferred_platform_rows.sh
. "$(dirname "$0")/_smoke.sh"

t_plan 11

root=$(cd "$(dirname "$0")/../.." && pwd)
DF="$root/docs/DEFERRED.md"
PL="$root/docs/PLATFORMS.md"
tmp=$(smoke_tmp)

# --- 1: the section heading is present in the register -----------------------
# SV:2307 card 6: one '## Open' section for the unclassified set, named with
# the re-measure date.
if grep -q '2026-11-13 platform re-measure' "$DF"; then
    t_ok "DEFERRED.md has the unclassified-platform section heading"
else
    t_fail "DEFERRED.md has no '2026-11-13 platform re-measure' section -- SV:2307 card 6 asks for ONE '## Open' section naming each of the unclassified failures with its measured numbers"
fi

# The section body, for the per-name checks: from the heading to the next ##.
awk '/2026-11-13 platform re-measure/{s=1;next} s&&/^## /{exit} s{print}' \
    "$DF" > "$tmp/sec"

# --- 2: the second-way universe extraction, floored at 8 ---------------------
# PLATFORMS.md lines carrying BOTH the re-measure date AND "nclassified" (the
# substring catches "Unclassified" and "unclassified" alike); the first
# column's **Name** is the text of the first **pair** on the line, via sed.
grep -F '2026-11-13, 0.14.0' "$PL" | grep -F 'nclassified' \
    | awk -F'|' '{print $2}' \
    | sed -n 's/.*\*\*\([^*]*\)\*\*.*/\1/p' > "$tmp/names"
nnames=$(wc -l < "$tmp/names" | tr -d '[:space:]')
if [ "$nnames" -ge 8 ]; then
    t_ok "PLATFORMS.md re-enumerates the unclassified set: $nnames names (floor 8)"
else
    t_fail "PLATFORMS.md yielded only $nnames unclassified names (floor 8) -- the row shape or the date string changed and this driver is reading nothing; the set is 8 (the CHANGELOG [0.14.0] known-issues list)"
fi

# The 8 names, read into fixed slots (empty if the extraction came up short,
# so the per-name checks below still run exactly 8 times and t_done stays
# honest rather than silently abandoning the tail of the plan).
n1=$(sed -n '1p' "$tmp/names")
n2=$(sed -n '2p' "$tmp/names")
n3=$(sed -n '3p' "$tmp/names")
n4=$(sed -n '4p' "$tmp/names")
n5=$(sed -n '5p' "$tmp/names")
n6=$(sed -n '6p' "$tmp/names")
n7=$(sed -n '7p' "$tmp/names")
n8=$(sed -n '8p' "$tmp/names")

# --- 3..10: each name is carried by a row in the section with a 5-digit figure
_chk_name() {
    # $1 = the slot label, $2 = the name. A row passes when some section line
    # names $2 (literal, so "/" and "(" in the names are safe) AND holds a
    # 5-digit measured figure ([0-9]{5} -- the unit-suite check count).
    if [ -z "$2" ]; then
        t_fail "$1: no such name in the PLATFORMS.md extraction -- the universe came up short of 8"
        return 0
    fi
    if grep -F "$2" "$tmp/sec" 2>/dev/null | grep -E -q '[0-9]{5}'; then
        t_ok "$1: a section row names '$2' with a 5-digit measured figure"
    else
        t_fail "$1: the section carries no row naming '$2' with a 5-digit measured figure"
    fi
}
_chk_name "name 1 (Debian 12 v2e)" "$n1"
_chk_name "name 2 (Debian 9 v2f)" "$n2"
_chk_name "name 3 (FreeBSD)" "$n3"
_chk_name "name 4 (NetBSD)" "$n4"
_chk_name "name 5 (OpenBSD)" "$n5"
_chk_name "name 6 (illumos / Solaris)" "$n6"
_chk_name "name 7 (FreeMiNT / MiNTLib)" "$n7"
_chk_name "name 8 (FreeDOS / DJGPP)" "$n8"

# --- 11: the section's detail rows, floored at 5 ------------------------------
# The eight def-386..def-393 detail rows (### def-<n>-<name>) are what make the
# deferrals findable; a floor of 5 catches the section collapsing to the table
# alone. The floor is a BOUND, not a census, per the house rule.
ndet=$(grep -c '^### def-' "$tmp/sec" 2>/dev/null)
ndet=${ndet:-0}
case "$ndet" in
    ''|*[!0-9]*) ndet=0 ;;
esac
if [ "$ndet" -ge 5 ]; then
    t_ok "the section holds $ndet '### def-' detail rows (floor 5)"
else
    t_fail "the section has only $ndet '### def-' detail rows (floor 5) -- the eight def-386..def-393 rows (measured, why deferred, what it would take) are missing"
fi

t_done
