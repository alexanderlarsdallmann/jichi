#!/bin/sh
# M1204 (board card 95, SV:0651): the jichi-harness link rule takes 26
# src/harness objects, but the single -include that reads their -MMD .d
# files named only 9 (HARNESS_MAIN + the 8 gate/land bodies). The other 17
# objects' .d files were written on every compile and never read, so a
# changed header left them stale: touch include/jc_harness_config.h and
# `make jichi-harness` rebuilt only 9 objects and re-linked a stale
# jc_harness_notify.o (measured: its .o byte-identical before/after).
#
# This is a lint: it reads the Makefile text and `make -np`, compiles
# nothing, spawns no PTY. It audits the universe twice and checks the
# -include covers it.
#
# Universe, stated:
#   (a) route 1 -- the harness SINGLE-object vars: Makefile lines of the
#       exact form `HARNESS_[A-Z_]+ = src/harness/<name>.o` (26 on this
#       head). The line-anchored form is load-bearing: a looser match would
#       also catch `HARNESS_WATCH = $(HARNESS_WATCH_GATE) $(HARNESS_WATCH_UNITS)`
#       and `HARNESS_BIN = jichi-harness`, neither of which is a single .o.
#   (b) route 2 -- the objects the `jichi-harness:` link rule takes
#       (`make -np`, the prerequisite list, kept where it starts
#       `src/harness/` and ends `.o`): 26 on this head.
#   (c) the .d names the -include reads (the vars in `$(VAR:.o=.d)`).
#
# Checks (t_plan 5):
#   C1: route 1 and route 2 agree -- the "audit the universe twice" leg.
#       A var a future edit stops linking, or links without defining, shows
#       up as an object on one side only, and the failure names it.
#   C2: THE TOOTH for SV:0651 -- every object in the link line has its .d
#       read by the -include. A missing one is named. Reverting the fix's
#       Makefile hunk turns this red with the 17 names (shown red-first in
#       commit A, before the fix lands in commit B).
#   C3: floors -- both routes read >= 26 objects (an extraction that reads
#       nothing fails loudly; the M519 lesson: a hollow check that can
#       never fail is worse than none).
#   C4: the dependency-include line is a `-include` (leading dash), not a
#       hard `include`: a hard include of a .d a fresh checkout has not
#       yet generated would make the first `make` fail to parse.
#   C5: (M1204-audit#5) the two-sided half of C2 -- C2 reads only the
#       $(HARNESS_X:.o=.d) vars that resolve to a route-1 single-object var,
#       so a var in the block that does NOT resolve (a typo'd HARNESS_NOTY, or
#       a multi-object $(HARNESS_WATCH:.o=.d)) is dropped silently from
#       'covered' and the lint stays green while that object's .d is genuinely
#       never read. C5 checks the other side: every $(VAR:.o=.d) the -include
#       names must resolve -- (a) a route-1 harness single-object var
#       (`VAR = src/harness/<name>.o`, the same line-anchored form route 1
#       uses), or (b) one of the non-harness vars the block legitimately
#       carries (the whitelist, enumerated in the check, M1204-audit#5).
#
# Floors are today's exact numbers (26/26), not a round one.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
MK="$SMOKE_ROOT/Makefile"
tmp=$(smoke_tmp)
_mke=$(smoke_make)

# --- route 1: the harness single-object vars (and their .o paths) -----------
# awk field 1 = the var name, field 2 = the src/harness/...o value, from the
# exact line form only. Default FS collapses the whitespace around `=`.
# The line is `VAR = src/harness/x.o` (space-equals-space), so the .o path is
# pulled with sed, not awk's $2 (which would be the `=`).
awk '/^HARNESS_[A-Z_]+[ \t]*=[ \t]*src\/harness\/[a-z_.]+\.o[ \t]*$/' "$MK" \
    | sed -n 's/.*\(src\/harness\/[a-z_.]*\.o\).*/\1/p' | sort -u > "$tmp/r1"

# --- route 2: the objects the jichi-harness link rule takes ------------------
# `make -np` expands every rule; the `jichi-harness:` prerequisite line is the
# second, different route (it expands $(HARNESS_WATCH) into its two .o).
$_mke -np 2>/dev/null \
    | awk '/^jichi-harness:/{for (i = 2; i <= NF; i++) if ($i ~ /^src\/harness\/.*\.o$/) print $i}' \
    | sort -u > "$tmp/r2"

# resolve VAR -> its src/harness/...o path (single-object vars only; a var
# that is not one of those resolves to nothing and drops out).
resolve_var() {
    sed -n "s/^$1[ \t]*=[ \t]*\(src\/harness\/[a-z_.]*\.o\)[ \t]*$/\1/p" "$MK"
}

# --- C1: the two census routes agree ----------------------------------------
comm -23 "$tmp/r1" "$tmp/r2" > "$tmp/c1_only_r1"   # in route 1, not route 2
comm -13 "$tmp/r1" "$tmp/r2" > "$tmp/c1_only_r2"   # in route 2, not route 1
if [ ! -s "$tmp/c1_only_r1" ] && [ ! -s "$tmp/c1_only_r2" ]; then
    t_ok "C1: route 1 (vars) and route 2 (link rule) agree on the same objects"
else
    _c1="route 1 and route 2 disagree -- an object the census sees twice differently: "
    [ -s "$tmp/c1_only_r1" ] && _c1="$_c1 in vars only: $(tr '\n' ' ' < "$tmp/c1_only_r1")"
    [ -s "$tmp/c1_only_r2" ] && _c1="$_c1 in link rule only: $(tr '\n' ' ' < "$tmp/c1_only_r2")"
    t_fail "$_c1"
fi

# --- C2 (the tooth): every link-line object has its .d in the -include ------
# Find the -include continuation block: the line reading `include $(LIB_OBJ`
# plus its backslash-continued lines. Then every $(VAR:.o=.d) harness var in it,
# resolved to its .o path, is the set of link-line objects whose .d IS read.
_inc=$(grep -n 'include \$(LIB_OBJ' "$MK" | head -n 1)
_inc_lno=${_inc%%:*}
if [ -z "$_inc_lno" ]; then
    t_fail "C2: no dependency-include line reading 'include \$(LIB_OBJ' found in the Makefile -- the -include moved or vanished"
else
    awk -v start="$_inc_lno" '
        NR < start { next }
        { print; if ($0 ~ /\\$/) next; exit }' "$MK" > "$tmp/incblock"
    # the harness vars the -include names (the $(HARNESS_X:.o=.d) idiom)
    grep -oE '\$\(HARNESS_[A-Z_]+:\.o=\.d\)' "$tmp/incblock" \
        | sed 's/^$(//; s/:.o=.d)$//' | sort -u > "$tmp/incvars"
    : > "$tmp/covered"
    while IFS= read -r _v; do
        [ -n "$_v" ] || continue
        _p=$(resolve_var "$_v")
        [ -n "$_p" ] && printf '%s\n' "$_p" >> "$tmp/covered"
    done < "$tmp/incvars"
    sort -u "$tmp/covered" > "$tmp/covered.s"
    comm -23 "$tmp/r2" "$tmp/covered.s" > "$tmp/c2_missing"   # linked, .d unread
    _ncov=$(grep -c . "$tmp/covered.s")
    _nlink=$(grep -c . "$tmp/r2")
    if [ ! -s "$tmp/c2_missing" ]; then
        t_ok "C2: every one of the $_nlink link-line objects has its .d read by the -include ($_nlink/$_nlink covered)"
    else
        t_fail "C2: $_ncov of $_nlink link-line objects have their .d in the -include; $(grep -c . "$tmp/c2_missing") stale on a header touch (their .d is written and never read): $(tr '\n' ' ' < "$tmp/c2_missing")"
    fi
fi

# --- C3: floors on both extractions ------------------------------------------
_n1=$(grep -c . "$tmp/r1")
_n2=$(grep -c . "$tmp/r2")
if [ "$_n1" -ge 26 ] && [ "$_n2" -ge 26 ]; then
    t_ok "C3: census floors hold (route 1 read $_n1 vars, route 2 read $_n2 objects; floors 26/26)"
else
    t_fail "C3: extraction too thin (route 1 read $_n1 vars, route 2 read $_n2 objects; floors 26/26) -- the census broke, not the -include"
fi

# --- C4: the dependency include is a -include, not a hard include ------------
if [ -n "${_inc_lno:-}" ]; then
    _inc_body=${_inc#*:}
    case "$_inc_body" in
        -*) t_ok "C4: the dependency-include line ($MK:$_inc_lno) is a -include, so a fresh checkout parses" ;;
        *)  t_fail "C4: the dependency-include line ($MK:$_inc_lno) is a hard include -- a fresh checkout with no .d yet would fail to parse: $_inc_body" ;;
    esac
else
    t_fail "C4: no dependency-include line found in the Makefile -- cannot confirm it is a -include"
fi

# --- C5 (M1204-audit#5): every $(VAR:.o=.d) the -include names must resolve ---
# C2 is one-sided: it reads only the $(HARNESS_X:.o=.d) vars that resolve to a
# route-1 single-object var into 'covered', and a var in the block that does NOT
# resolve (a typo'd HARNESS_NOTY, or a multi-object $(HARNESS_WATCH:.o=.d)) is
# dropped silently -- the lint stays green while that object's .d is genuinely
# never read. C5 is the other side: every var the -include names must resolve,
# i.e. be either (a) a route-1 harness single-object var, or (b) one of the
# non-harness vars the block legitimately carries. The whitelist is today's exact
# set, enumerated here with M1204-audit#5 named, so a new non-harness var added to
# the -include is a finding until it is added to the list.
_C5_WHITELIST="LIB_OBJ MAIN_OBJ TEST_OBJ CONVERT_MAIN CONVERT_CORE TT_CORE_OBJ"
if [ -n "${_inc_lno:-}" ] && [ -s "$tmp/incblock" ]; then
    # every $(VAR:.o=.d) the block names (any var, not just HARNESS_*)
    grep -oE '\$\([A-Z_][A-Z_0-9]*:\.o=\.d\)' "$tmp/incblock" \
        | sed 's/^$(//; s/:.o=.d)$//' | sort -u > "$tmp/c5vars"
    _c5_unres=""
    while IFS= read -r _v; do
        [ -n "$_v" ] || continue
        case " $_C5_WHITELIST " in
            *" $_v "*) continue ;;
        esac
        # (a) a route-1 harness single-object var: the exact line form (-E:
        # a BRE `grep -q` would read the `+` as a literal and resolve nothing)
        if grep -Eq "^$_v[ \t]*=[ \t]*src/harness/[a-z_.]+\.o[ \t]*$" "$MK"; then
            continue
        fi
        if [ -n "$_c5_unres" ]; then _c5_unres="$_c5_unres $_v"; else _c5_unres="$_v"; fi
    done < "$tmp/c5vars"
    if [ -z "$_c5_unres" ]; then
        _c5_n=$(grep -c . "$tmp/c5vars")
        t_ok "C5: every one of the $_c5_n \$(VAR:.o=.d) vars in the -include resolves (route-1 harness var or whitelist) -- M1204-audit#5"
    else
        t_fail "C5: \$(VAR:.o=.d) var(s) in the -include name a .d that is never read -- not a route-1 single-object var and not in the whitelist: $_c5_unres -- M1204-audit#5"
    fi
else
    t_fail "C5: no -include block to audit (the dependency-include line is missing or empty) -- M1204-audit#5"
fi

t_done
