#!/bin/sh
# smoke lint: no smoke assertion compares output with a literal non-zero UTC offset (2026-11-25).
#
# THE DEFECT. harness_gate_run cells 106, 109, 111 and 112 (M1202) required the landing.decision record's time=
# field to end in ' +0100', the drive host's offset in winter. The harness writes the HOST's offset (gf_now's %z),
# so the cells were green on this host from November to March and red everywhere else. On 2026-11-25 a hosted CI
# run of the 0.15.0 tree went red on them (GitHub run 45676610638: the runner is in UTC and wrote +0000, which is
# correct). They would also have gone red on the drive host in summer (+0200). The fix compares with `date +%z`.
# This lint keeps the class out.
#
# Universe, stated: tests/smoke/*.sh, every line that is not a comment. An ASSERTION line is one of:
#   (g) a grep invocation (`grep ...` up to the next `|` or `;`) whose arguments hold a literal offset;
#   (t) a test comparison `[ ... = ... ]` / `[ ... != ... ]` with a literal offset in it;
#   (c) a case pattern (`*...)`) with a literal offset in it.
# A LITERAL NON-ZERO OFFSET is [+-] then 01-14 hours then 00, 30 or 45 minutes, after a space, a quote or a
# backslash. +0000 is not flagged: a producer that formats UTC (gmtime, `date -u`) writes it on every host.
# Literal offsets in fixture data (a printf writing a record), in parser inputs (an assignment or a for list) and in
# messages are inputs, not assertions, and are not flagged (5 such lines today, harness_drive.sh and
# harness_gate_run.sh).
#
# Checks (t_plan 4):
#   C1: no assertion line in the universe pins a literal non-zero offset. Each hit is named file:line. At 6ed996bf
#       this lists exactly the four harness_gate_run.sh lines (2665, 2770, 2841, 2874).
#   C2: the floor: the universe holds >= 586 scripts (today's exact count, these two lints included).
#   C3: the instrument fires, per form: sample lines of forms (g), (t) and (c) with ' +0100' are each flagged, and
#       a fixture printf, an assignment and a ' +0000' grep are each passed.
#   C4: the universe is read: the line count read is > 0 for every script (an unreadable file fails, not passes).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
R="$SMOKE_ROOT"
tmp=$(smoke_tmp)
LC_ALL=C
export LC_ALL

OFF='[ "\\][+-](0[1-9]|1[0-4])(00|30|45)'
# pins FILE: the assertion lines of FILE that hold a literal non-zero offset, as 'line:text'.
pins() {
    grep -nE "grep[^|;]*$OFF|\[ [^]]*!?= [^]]*$OFF|\*[^)]*$OFF[^)]*\)" "$1" 2>/dev/null \
        | grep -vE '^[0-9]+:[[:space:]]*#'
}

ls "$R"/tests/smoke/*.sh > "$tmp/universe"
_n=$(grep -c . "$tmp/universe")
: > "$tmp/hits"
: > "$tmp/unread"
while IFS= read -r _f; do
    if [ ! -r "$_f" ] || [ "$(wc -l < "$_f")" -eq 0 ]; then
        printf '%s\n' "${_f#$R/}" >> "$tmp/unread"
        continue
    fi
    pins "$_f" | sed "s#^#${_f#$R/}:#" >> "$tmp/hits"
done < "$tmp/universe"

# --- C1 ---------------------------------------------------------------------------------------------------------
if [ ! -s "$tmp/hits" ]; then
    t_ok "C1: no assertion in the $_n smoke scripts pins a literal non-zero UTC offset"
else
    t_fail "C1: an assertion pins a literal UTC offset -- it passes only in that zone and season; compare with \$(date +%z) or force TZ for the producer: $(cut -c1-140 < "$tmp/hits" | tr '\n' '|')"
fi

# --- C2 ---------------------------------------------------------------------------------------------------------
if [ "$_n" -ge 586 ]; then
    t_ok "C2: the universe holds $_n smoke scripts (floor 586)"
else
    t_fail "C2: the universe read only $_n smoke scripts (floor 586) -- the lint broke, not the drivers"
fi

# --- C3: the instrument fires, per form ---------------------------------------------------------------------------
# The sample lines are built here from split strings, so this file's own source holds no literal offset (it is
# in the universe too, and a literal sample would flag the lint itself).
_o1="+01""00"; _o5="+05""30"; _o0="+00""00"
{
    printf '    && printf %s "$x" | grep -q " %s"; then\n' "'%s'" "$_o1"
    printf 'if [ "$y" = "2026-11-19 03:54:29 %s" ]; then\n' "$_o1"
    printf "case \"\$z\" in *' %s'*) ;; esac\n" "$_o5"
} > "$tmp/sample_red"
{
    printf "printf 'decision=landed time=2026-11-19 03:54:29 %s\\\\n' > \"\$f\"\n" "$_o1"
    printf '_mendv="2027-01-31 18:00:00 %s"\n' "$_o1"
    printf '    && printf %s "$x" | grep -q " %s"; then\n' "'%s'" "$_o0"
    printf '# a comment: grep -q " %s"\n' "$_o1"
} > "$tmp/sample_green"
_r=$(pins "$tmp/sample_red" | grep -c .)
_g=$(pins "$tmp/sample_green" | grep -c .)
if [ "$_r" -eq 3 ] && [ "$_g" -eq 0 ]; then
    t_ok "C3: the instrument fires on each assertion form (3 of 3 flagged) and passes inputs, +0000 and comments (0 of 4)"
else
    t_fail "C3: the instrument is wrong: $_r of 3 assertion forms flagged (want 3), $_g of 4 inputs flagged (want 0)"
fi

# --- C4 ---------------------------------------------------------------------------------------------------------
if [ ! -s "$tmp/unread" ]; then
    t_ok "C4: every one of the $_n scripts was read"
else
    t_fail "C4: unreadable or empty scripts (an unread file can hide a pin): $(tr '\n' ' ' < "$tmp/unread")"
fi
