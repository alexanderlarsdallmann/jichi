#!/bin/sh
# smoke lint: jc_snprintf / jc_vsnprintf return-value checks that are the ONLY
# check on the conversion (M1205, board card 97, SV:1235, batch G).
#
# DEFECT. jc_snprintf(dst, cap, ...) returns the WOULD-BE length (negative on
# an internal fault). A check that tests only r < 0 treats a CUT (0 <= r < cap)
# as success: the string is truncated silently and the reader downstream gets
# a half-value. The correct check is r < 0 || r >= cap (or an explicit bound
# such as strlen(x) + n + 1 >= cap BEFORE the call). jc_hp_gate.c:560 is the
# good form the Python census (route 1) misses: its < 0 clause shares its
# enclosing if-condition with strlen(cmd) + strlen(r->s8) + 2 >= sizeof c, so
# the conversion IS bounded -- by a clause on the OTHER side of the < 0, one
# the census's fixed 160-char before-window cannot see. This lint's boundary
# rule (C3) reads the enclosing condition, not a fixed window.
#
# UNIVERSE (measured 2026-11-19 at head 667ac819): src/, .c and .h, 234
# files. Route 1 -- the census oracle
# docs/internal/drive/2026-11-05-harness-port/notes/snprintf_census.py (Python;
# the smoke tier is Python-free, M209, so the oracle is the REFERENCE, not a
# dependency): 1843 call sites, of which 90 sole-<0 candidates = 88
# NEG_ONLY_DIRECT + 2 NEG_ONLY_ASSIGNED, all in src/harness/. (M1206: the 88
# former findings fixed; the residual is the two bound-exclusions below.)
# Route 2 -- this
# driver's own awk extraction: a masked, paren-matching walk that re-uses the
# census's exact branch logic (mask; DEFINITION / VOID_CAST / SIZING_CALL /
# direct-< 0 (with the census's NEG_PLUS_BOUND rest-of-condition branch) /
# COMPARED_OTHER / assigned-var tail up to its next assignment) plus the
# boundary-exclusion rule. The two routes are diffed in C1/C2: an extraction
# that reads nothing, or misses a site the census sees (the multi-line ||
# forms, the assigned forms), is red.
#
# FLOOR: 2 sole-<0 candidate sites (recorded in C2) -- the two bound-
# exclusions; 1843 total call sites (recorded in C1). The 88 former findings
# are fixed (M1206). Every candidate is in src/harness/.
#
# BOUND RULE (the exclusion): a DIRECT candidate is NOT a finding when its
# enclosing condition (the if (...) whose opening paren precedes the call and
# whose matching close follows it; scanned upward from the call, at most 20
# lines -- sufficient in this tree) carries a size-like bound -- a >= / > /
# <= comparison whose operand is sizeof ... or a name containing
# cap/max/size/len -- anywhere in that condition, on EITHER side of the < 0
# clause (jc_hp_gate.c:560's bound sits in the clause BEFORE the < 0). A
# comparison is a bound when one of its two operands is size-like (the other
# operand is walked over arithmetic and calls to its named term; sizeof
# counts with any argument). The rule cannot silently over-drop: every
# exclusion is printed with the bound text it matched (C3 asserts that, and
# asserts :560 positively). The two ASSIGNED candidates
# (jc_harness_handoff.c:154, jc_hp_gatesweep.c:1248) keep the census's
# assigned semantics -- no truncation check on the variable up to its next
# assignment -- and are findings; an outer pre-check like handoff's
# err_cap > 0 guards the ARGUMENT, not the RESULT, and is not a bound.
#
# Checks (t_plan 5):
#   C1 the extraction floor (M519): route-2 total call sites == 1925 (1843 at
#      2ef85325, recounted forward as src/ grew; provenance in the t_ok). An awk
#      that reads nothing is red.
#   C2 the universe, twice: route-2 candidate file:line set == the recorded
#      2-line residual (the two bound-exclusions; the 88 former findings
#      fixed, M1206). Both directions: a site the census sees that the awk
#      misses (the multi-line || forms, the assigned forms), and a phantom
#      the census does not see.
#   C3 the exclusion rule fires: jc_hp_gate.c:560 is excluded (NOT in the
#      finding set), and every bound-exclusion carries a demonstrable bound
#      text (an exclusion without one is red -- the rule cannot silently
#      over-drop).
#   C4 the findings, two-sided (sprintf_lint's allowlist shape): the finding
#      set (candidates minus bound-exclusions) == the allowlist below,
#      file:line by file:line. A NEW finding not in the allowlist is red; a
#      REMOVED/REPAIRED allowlisted site is red (stale entry).
#   C5 the instrument can fire (self-test in $TMPDIR, never the tree): three
#      planted fixtures run through the driver's own classifier -- (a) a bare
#      if (jc_snprintf(...) < 0) MUST be a finding; (b) the :560 shape, the
#      < 0 clause sharing its enclosing condition with a strlen(...) >=
#      sizeof bound BEFORE it, MUST NOT be a finding; (c) n =
#      jc_snprintf(...); if (n < 0 || n >= sized) MUST NOT be a finding (the
#      census's ASSIGNED_TRUNC_CHECKED branch).
#
# No build, no git, no network, no Python (M209, the smoke tier is
# Python-free). POSIX sh + awk only; reads
# tree text only; runs in a few seconds. Greps use /usr/bin/grep on purpose
# (CLAUDE.md: a session's bare grep is a shim that once read a lint's own
# pattern as matching 0 lines where /usr/bin/grep matched 1,443).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

# Guarded /usr/bin/grep for portability (systems without merged /usr have
# grep at /bin/grep) and because a session's bare grep is a shim (CLAUDE.md).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# --- route 2: the masked, paren-matching awk classifier -----------------------
# A port of the census oracle's branch logic (mask + find every jc_v?snprintf(
# + match its closing paren + classify) with the boundary-exclusion rule added
# on the direct-< 0 branch. Kept as a program file so C5 can run the SAME
# classifier over planted fixtures as C1-C4 do over the tree. run_classify
# writes one line per sole-<0 candidate, <F|E>\t<rel:line>\t<kind>\t<bound>:
# F = finding, E = bound-excluded (field 4 names the bound text matched).
cat > "$tmp/classify.awk" <<'AWK_EOF'
# Mask one line: string/char literals and comments become spaces, same length,
# so parens and keywords inside them do not count (the census masks the whole
# file; a C line cannot carry an unterminated literal, so per-line masking
# plus a carried block-comment flag is equivalent).
function maskl(line,    n, i, c, j, k, d, out) {
    n = length(line); out = ""; i = 1
    while (i <= n) {
        c = substr(line, i, 1)
        if (iblk) {
            j = index(substr(line, i), "*/")
            if (j == 0) { for (k = i; k <= n; k++) out = out " "; break }
            for (k = i; k < i + j; k++) out = out " "
            i = i + j + 1; iblk = 0
        } else if (c == "/" && substr(line, i + 1, 1) == "*") {
            out = out "  "; i = i + 2; iblk = 1
        } else if (c == "/" && substr(line, i + 1, 1) == "/") {
            for (k = i; k <= n; k++) out = out " "
            break
        } else if (c == "\"" || c == "\047") {
            d = c; out = out d; i = i + 1
            while (i <= n) {
                c = substr(line, i, 1)
                if (c == "\\") { out = out "  "; i = i + 2; continue }
                out = out (c == d ? d : " ")
                i = i + 1
                if (c == d) break
            }
        } else {
            out = out c; i = i + 1
        }
    }
    return out
}
# THE FILE IS NEVER ONE STRING. The masked file is the array ma[1..nm], one
# line each, and a position is a (line, column) pair. An earlier form joined
# the file into one string m and walked it with substr(m, i, 1): that is
# linear per character on an awk whose substr() or length() measures the
# string each time (the one true awk of the BSDs and of illumos, and busybox
# awk), so the walk was quadratic in the file's size and the driver was
# killed at its deadline there. Every helper below reads a line, or a
# bounded piece put together from lines; between two lines stands the one
# "\n" the joined string had, and after the last line nothing.
#
# Up to n characters starting at (ln, col). col may be one past the line's
# end: that position is the newline.
function span(ln, col, n,    t, i) {
    if (ln > nm) return ""
    t = substr(ma[ln], col)
    i = ln
    while (length(t) < n && i < nm) { i++; t = t "\n" ma[i] }
    return substr(t, 1, n)
}
# The up to 160 characters before (ln, col).
function beforetext(ln, col,    t, i) {
    t = substr(ma[ln], 1, col - 1)
    i = ln - 1
    while (length(t) < 160 && i >= 1) { t = ma[i] "\n" t; i-- }
    if (length(t) > 160) t = substr(t, length(t) - 159)
    return t
}
# The text from (l1, c1) to (l2, c2), both ends included.
function spanto(l1, c1, l2, c2,    t, i) {
    if (l1 == l2) return substr(ma[l1], c1, c2 - c1 + 1)
    t = substr(ma[l1], c1)
    for (i = l1 + 1; i < l2; i++) t = t "\n" ma[i]
    return t "\n" substr(ma[l2], 1, c2)
}
# Match the closing paren of the opening paren at (ln, col). Returns 1 and
# leaves the close in the globals (mcl, mcc); 0 when unbalanced (degenerate;
# the census walks to EOF).
function matchclose(ln, col,    L, i, n, d, line, c) {
    d = 0
    for (L = ln; L <= nm; L++) {
        line = ma[L]; n = length(line)
        for (i = (L == ln ? col : 1); i <= n; i++) {
            c = substr(line, i, 1)
            if (c == "(") d++
            else if (c == ")") { d--; if (d == 0) { mcl = L; mcc = i; return 1 } }
        }
    }
    return 0
}
# A >= or a bare > (not ->, not >>=, not part of ==) anywhere in t; the
# census's `>=|(?<![-=>])>(?![>=])` as an awk walk.
function hasgegt(t,    i, n, c, pn, nx) {
    n = length(t)
    for (i = 1; i <= n; i++) {
        c = substr(t, i, 1)
        if (c != ">") continue
        nx = (i < n) ? substr(t, i + 1, 1) : ""
        pn = (i > 1) ? substr(t, i - 1, 1) : ""
        if (nx == "=") return 1
        if (nx != ">" && pn != "-" && pn != "=" && pn != ">") return 1
    }
    return 0
}
# The operand TEXT before an operator starting at j: skip blanks left of
# j-1 (a newline is a clause boundary -- the operand never spans it), then
# up to 40 chars of the trailing term.
function lefttext(t, j,    s, k) {
    s = j - 1
    while (s >= 1) { c = substr(t, s, 1); if (c == " " || c == "\t" || c == "\n") s--; else break }
    k = 0
    while (s >= 1 && k < 40) {
        c = substr(t, s, 1)
        if (c == "\n") break
        s--; k++
    }
    return substr(t, s + 1, j - 1 - s)
}
# The operand TEXT after an operator ending at j: skip blanks right of j
# (a newline is a clause boundary), then up to 40 chars of the leading term,
# never crossing the next newline.
function righttext(t, j,    n, s, k, s0) {
    n = length(t); s = j + 1
    while (s <= n) { c = substr(t, s, 1); if (c == " " || c == "\t" || c == "\n") s++; else break }
    s0 = s
    k = 0
    while (s <= n && k < 40) {
        c = substr(t, s, 1)
        if (c == "\n") break
        s++; k++
    }
    return substr(t, s0, s - s0)
}
# The FIRST size-like bound in t: a >= / > / <= whose left or right operand
# is sizeof ... or ends on a size-like name; "" when none. Returns short
# bound text (what C3 prints as the demonstrable bound).
function findbound(t,    i, n, c, op, os, oe, pn, nx, L, R, b) {
    n = length(t); i = 1
    while (i <= n) {
        c = substr(t, i, 1)
        op = ""; os = 0; oe = 0
        if (c == ">" && i < n && substr(t, i + 1, 1) == "=") { op = ">="; os = i; oe = i + 1; i = i + 2 }
        else if (c == ">") {
            pn = (i > 1) ? substr(t, i - 1, 1) : ""
            nx = (i < n) ? substr(t, i + 1, 1) : ""
            if (pn != "-" && pn != "=" && pn != ">" && nx != "=" && nx != ">") { op = ">"; os = i; oe = i }
            i = i + 1
        }
        else if (c == "<" && i < n && substr(t, i + 1, 1) == "=") { op = "<="; os = i; oe = i + 1; i = i + 2 }
        else i = i + 1
        if (op == "") continue
        L = lefttext(t, os)
        R = righttext(t, oe)
        if (L == "" || R == "") continue
        if (boundhit(L) || boundhit(R)) {
            b = trim(L " " op " " R)
            if (length(b) > 80) b = substr(b, 1, 80)
            return b
        }
    }
    return ""
}
function boundhit(x) {
    if (x ~ /sizeof/) return 1
    if (x ~ /cap/ || x ~ /max/ || x ~ /size/ || x ~ /len/) return 1
    return 0
}
function trim(s) { sub(/^[ \t\n]+/, "", s); sub(/[ \t\n]+$/, "", s); return s }
# Positions of real `if (` / `if(` keywords in one masked line (not preceded
# by an identifier char, so no `->if` member or `if_` name). Returns the
# count; positions of the ( in occp[1..n].
function ifocc(line,    n, i, j, pc, nocc) {
    nocc = 0
    n = length(line); i = 1
    while (i <= n) {
        if (substr(line, i, 1) != "i") { i++; continue }
        if (substr(line, i, 2) != "if") { i++; continue }
        if (i > 1) {
            pc = substr(line, i - 1, 1)
            if (pc ~ /[A-Za-z0-9_]/) { i++; continue }
        }
        j = i + 2
        while (j <= n && (substr(line, j, 1) == " " || substr(line, j, 1) == "\t")) j++
        if (j <= n && substr(line, j, 1) == "(") { nocc++; occp[nocc] = j }
        i = j
    }
    return nocc
}
# The enclosing condition: from the call line upward (at most 20 lines -- the
# census notes state a +/-15-line window suffices in this tree), the NEAREST
# if whose opening paren precedes the call start (ln, sc) and whose matching
# close follows it; its text from the ( to the close.
function condwin(ln, sc,    i, k, nocc) {
    for (i = ln; i >= 1 && i > ln - 20; i--) {
        nocc = ifocc(ma[i])
        for (k = nocc; k >= 1; k--) {
            if (i < ln || occp[k] < sc) {
                if (matchclose(i, occp[k]) && (mcl > ln || (mcl == ln && mcc > sc)))
                    return spanto(i, occp[k], mcl, mcc)
            }
        }
    }
    return ""
}
# Classify one call: (ln, sc) = line and column of the j, (pl, pc) = the
# opening paren, (cl, cc) = its close. The branches are the census's own
# (DEFINITION / VOID_CAST / SIZING_CALL / direct-< 0 with the NEG_PLUS rest-
# of-condition branch / COMPARED_OTHER / assigned tail up to the next
# assignment); DIRECT candidates additionally get the boundary-exclusion
# rule. Prints the F/E candidate line.
function classify(ln, sc, pl, pc, cl, cc,    i, before, after, br, rest, ci, si, w, bound, v, t, c, tail, nln, tl, line, trunc, neg) {
    before = beforetext(ln, sc)
    after = span(cl, cc + 1, 200)
    br = before; sub(/[ \t\n]+$/, "", br)
    # DEFINITION
    if (br ~ /(^|[^A-Za-z0-9_])(int|void)[ \t\n]*$/ || (br ~ /(^|[^A-Za-z0-9_])int[ \t\n]*$/ && after ~ /^[ \t\n]*(JC_PRINTF|;|\{)/)) return
    # VOID_CAST
    if (br ~ /\([ \t]*void[ \t]*\)$/) return
    # SIZING_CALL (the census's \s*NULL\s*,\s*0\s*, -- \s* matches zero too)
    if (span(pl, pc + 1, 40) ~ /^[ \t\n]*NULL[ \t\n]*,[ \t\n]*0[ \t\n]*,/) return
    if (after ~ /^[ \t\n]*<[ \t\n]*0([^A-Za-z0-9_]|$)/) {
        # the < 0 is the check right after the call
        rest = after
        ci = index(rest, "{"); si = index(rest, ";")
        if (ci > 0 && (si == 0 || ci < si)) rest = substr(rest, 1, ci - 1)
        else if (si > 0) rest = substr(rest, 1, si - 1)
        # the census's NEG_PLUS_BOUND_IN_CONDITION: rest starts < 0 || and
        # carries a size-like bound -> checked, not a candidate
        if (rest ~ /^[ \t\n]*<[ \t\n]*0[ \t\n]*\|\|/ && hasgegt(rest)) return
        w = condwin(ln, sc)
        bound = findbound(w)
        printf "%s\t%s:%d\tdirect\t%s\n", (bound == "" ? "F" : "E"), cur, ln, bound
    } else if (after ~ /^[ \t\n]*(>=|<=|==|!=|>|<)/) {
        # COMPARED_OTHER: checked another way
    } else {
        # assigned: before ends in `var =` (optionally a (cast) after)
        v = ""
        if (match(br, /[A-Za-z_][A-Za-z0-9_]*[ \t\n]*=[ \t\n]*(\([ \t\n]*[A-Za-z_][A-Za-z0-9_]*[ \t\n]\)[ \t\n]*)?$/)) {
            t = substr(br, RSTART, RLENGTH)
            sub(/\([ \t\n]*[A-Za-z_][A-Za-z0-9_]*[ \t\n]\)[ \t\n]*$/, "", t)
            sub(/[ \t\n]*=$/, "", t)
            v = trim(t)
            c = (RSTART > 1) ? substr(br, RSTART - 1, 1) : ""
            if (c ~ /[A-Za-z0-9_]/) v = ""
        }
        if (v == "") return
        # the census's `[=!<>]=\s*$`: ==, !=, <=, >= are not assignments
        # (two chars -- a lone = is the assignment form)
        if (br ~ /[=!<>]=[ \t\n]*$/) return
        tail = span(cl, cc + 1, 1200)
        # the uses of v after the call, up to its next assignment
        if (match(tail, "(^|[^A-Za-z0-9_])" v "[ \t\n]*=[^=]")) tail = substr(tail, 1, RSTART - 1)
        # the census's truncation pattern: v as either operand of >= / > /
        # <= / <, or v < sizeof|*cap|*max|*size|*len
        trunc = 0
        if (match(tail, "(\\([ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]\\)[ \t\n]*)?(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)[ \t\n]*(>=|>)" \
                    "|(<=|<)[ \t\n]*(\\([ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]\\)[ \t\n]*)?(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)" \
                    "|(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)[ \t\n]*<[ \t\n]*(\\([ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]\\)[ \t\n])?(sizeof|[A-Za-z0-9_]*cap|[A-Za-z0-9_]*max|[A-Za-z0-9_]*size|[A-Za-z0-9_]*len)")) trunc = 1
        if (!trunc) {
            # a compound bound line naming v and comparing with >= / >
            nln = split(tail, tl, "\n")
            for (i = 1; i <= nln; i++) {
                line = tl[i]
                if (line ~ ("(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)") && hasgegt(line)) { trunc = 1; break }
            }
        }
        if (trunc) return
        neg = 0
        if (match(tail, "(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)[ \t\n]*<[ \t\n]*0([^A-Za-z0-9_]|$)")) neg = 1
        if (!neg) return
        # the census's NEG_ONLY_ASSIGNED: the assigned candidate; the bound
        # rule does not look at an assigned tail (a pre-check there guards the
        # argument, not the result)
        printf "F\t%s:%d\tassigned\t\n", cur, ln
    }
}
# Per file: find every jc_v?snprintf(, count it (total), classify it.
function file(    L, line, base, j, col, s, pl, pc, k) {
    total = 0
    for (L = 1; L <= nm; L++) {
        line = ma[L]; base = 0
        # every jc_ of the line, left to right, never overlapping
        while ((j = index(substr(line, base + 1), "jc_")) > 0) {
            col = base + j
            base = col + 2
            s = col + 3
            if (substr(line, s, 1) == "v") s = s + 1
            if (substr(line, s, 8) != "snprintf") continue
            # the word boundary: the character before the j, on this line
            # (at column 1 it is a newline or the file's start)
            if (col > 1 && substr(line, col - 1, 1) ~ /[A-Za-z0-9_]/) continue
            # blanks and newlines between the name and its (
            pl = L; pc = s + 8
            while (1) {
                if (pc > length(ma[pl])) {
                    if (pl == nm) break
                    pl++; pc = 1
                } else if (substr(ma[pl], pc, 1) ~ /[ \t]/) pc++
                else break
            }
            if (substr(ma[pl], pc, 1) != "(") continue
            total++
            if (matchclose(pl, pc)) classify(L, col, pl, pc, mcl, mcc)
        }
    }
    print "total " total > totf
    for (k = 1; k <= nm; k++) delete ma[k]
    nm = 0; iblk = 0
}
FNR == 1 {
    if (nm > 0) file()
    cur = FILENAME
    sub("^" root "/", "", cur)
    iblk = 0
}
{
    nm++
    ma[nm] = maskl($0)
}
END {
    if (nm > 0) file()
}
AWK_EOF

# run_classify ROOT TOTS_OUT FILES... -- run the classifier over FILES; one
# F/E candidate line per stdout line (candidates), the per-file totals in
# TOTS_OUT (reset first).
run_classify() {
    _rc_root="$1"; _rc_tot="$2"; shift 2
    : > "$_rc_tot"
    awk -v root="$_rc_root" -v totf="$_rc_tot" -f "$tmp/classify.awk" "$@" < /dev/null
}

# route 2 over the tree (the universe: src/, .c and .h)
find "$SMOKE_ROOT/src" -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort > "$tmp/srcfiles"
run_classify "$SMOKE_ROOT/src" "$tmp/tot" $(cat "$tmp/srcfiles") > "$tmp/cand"

# --- C1: the extraction floor (M519) -----------------------------------------
total2=$(awk '{ s += $2 } END { printf "%d\n", s }' "$tmp/tot")
if [ "$total2" -eq 2025 ]; then
    t_ok "C1: route-2 total call sites == 2025 (the census figure at 2ef85325, run 0083, was 1843; W1 d2e59e02 moved three jc_snprintf sites, jc_hp_gateci.c:727/744/761, to jc_sb_append_fmt, 1846 -> 1843; M1203's repin verb jc_harness_repin.c added 67 jc_snprintf call sites, 1843 -> 1910, recounted with the census oracle; wave-2e's b20 default live-doctor site (M1203-H11, now in the M1178 assigned form) added one, 1910 -> 1911, recounted with the census oracle; M1211's push-tick refresh added 14 jc_snprintf call sites, 1911 -> 1925, recounted with the census oracle (run 0098); M1213 S1 stretch added 16 jc_snprintf call sites, 1925 -> 1941, recounted with the census oracle (run 0101); M1213 S2 stretch added 21 jc_snprintf call sites, 1941 -> 1962, recounted with the census oracle (run 0103); T2d park half added 4 jc_snprintf call sites, 1962 -> 1966, recounted with the census oracle (run 0107); T3c (this work) added 44 jc_snprintf call sites, 1966 -> 2010, recounted with the census oracle; T3c-d (run 0108) added 2 stop_path builds (the park and restart normal forms) + 1 restart default is-failed form (review finding 2), 2010 -> 2013, recounted with the census oracle; H12's park pinned-line half added 3 jc_snprintf call sites (jc_harness_park.c:173/:181/:195, the pins/pinned line builders, each the direct->= (int)sizeof checked form) + the page --park arm added 2 (jc_harness_page.c:566/:571, the touch/would-touch lines, the assigned n<0||n>=sizeof form), 2013 -> 2018, recounted with the census oracle (run 0114, baseline bb8852ae3); the jichi docs self-path fix (2026-11-25, the operator's decision) added 2 in jc_path_from_argv0 (jc_path.c:461/:463, the PATH walk's candidate builds, the assigned n>0 && n<sizeof form) and made docs_dir_for_exe's existing one assigned and checked, 2018 -> 2020, recounted with the census oracle (supervisor, base d56465e2); in REL-A, threadwork's P8 (the ledger pages: 4 sites in jc_hp_gateledger code) and R4b (the relay leg range: 1 site in main.c) added 5, 2020 -> 2025, recounted with the census oracle (supervisor, REL-A on team head d2aa30059; no new NEG_ONLY site); M1221 (threadwork's F8, the quoted cd) exported one command builder per gate runner for its unit pins, jc_hp_gateref.c and jc_hp_gatesweep.c, one jc_snprintf each, 2025 -> 2027, recounted with the census oracle (supervisor, on 6adcce21; no new NEG_ONLY site); M1221-b (the hunk audit's finding) made gref_run/gsw_run CALL that export instead of carrying the inline "cd '%s' && %s" join, so the two production jc_snprintf sites moved into the builder already counted above -- net -2, 2027 -> 2025, recounted with the census oracle (run 0117; the export is now the single production path); an awk that reads nothing is red)"
else
    t_fail "C1: route-2 total call sites == $total2, expected 2025 -- the awk extraction broke (or the universe moved; recount with the census oracle before touching the floor)"
fi

# --- C2: the universe, twice ---------------------------------------------------
awk -F '\t' '{ print $2 }' "$tmp/cand" | sort > "$tmp/cand.set"
cat > "$tmp/record90" <<'EOF'
harness/jc_harness_main.c:3209
harness/jc_hp_gate.c:560
EOF
# the heredoc's last line may lack a trailing newline; make both sets newline-
# terminated so the counts and the comm both see 2 lines
awk '1' "$tmp/record90" > "$tmp/record90.x" && mv "$tmp/record90.x" "$tmp/record90"
sort "$tmp/record90" > "$tmp/record90.s"
n_cand=$($G -c . "$tmp/cand.set")
n_rec=$($G -c . "$tmp/record90.s")
comm -23 "$tmp/cand.set" "$tmp/record90.s" > "$tmp/c2_missed"
comm -13 "$tmp/cand.set" "$tmp/record90.s" > "$tmp/c2_phantom"
if [ "$n_cand" -eq "$n_rec" ] && [ ! -s "$tmp/c2_missed" ] && [ ! -s "$tmp/c2_phantom" ]; then
    t_ok "C2: the universe, twice -- route-2 candidates ($n_cand) equal the 2-site residual (the two bound-exclusions)"
else
    t_fail "C2: candidate set disagrees with the 2-site residual record (route-2 read $n_cand; census sees, awk missed: $(tr '\n' ' ' < "$tmp/c2_missed"); awk sees, census does not: $(tr '\n' ' ' < "$tmp/c2_phantom")) -- the census and the awk diverged"
fi

# --- C3: the exclusion rule fires ----------------------------------------------
awk -F '\t' '$1 == "E" { print $2 "\t" $4 }' "$tmp/cand" > "$tmp/excl"
awk -F '\t' '$1 == "F" { print $2 }' "$tmp/cand" | sort > "$tmp/findings"
n_excl=$($G -c . "$tmp/excl")
_excl560=$(awk -F '\t' '$1 == "harness/jc_hp_gate.c:560" { print $2 }' "$tmp/excl")
_excl_nobound=$(awk -F '\t' '$2 == ""' "$tmp/excl")
if [ -n "$_excl560" ] && [ -z "$_excl_nobound" ]; then
    t_ok "C3: the bound rule fired ($n_excl bound-exclusion(s), each printed with its bound text); jc_hp_gate.c:560 is excluded, not a finding"
else
    _excl_bad=""
    [ -z "$_excl560" ] && _excl_bad="jc_hp_gate.c:560 not excluded"
    [ -n "$_excl_nobound" ] && _excl_bad="$_excl_bad; exclusion without a demonstrable bound: $_excl_nobound"
    t_fail "C3: the exclusion rule misfired (${_excl_bad:-the exclusion set is wrong}) -- it must drop at least jc_hp_gate.c:560 and print every drop with its bound"
fi

# --- C4: the findings, two-sided (sprintf_lint's allowlist shape) ---------------
cat > "$tmp/allow" <<'EOF'
EOF
sort "$tmp/findings" > "$tmp/findings.s"
sort "$tmp/allow" > "$tmp/allow.s"
n_find=$($G -c . "$tmp/findings.s")
n_allow=$($G -c . "$tmp/allow.s")
comm -23 "$tmp/findings.s" "$tmp/allow.s" > "$tmp/c4_new"
comm -13 "$tmp/findings.s" "$tmp/allow.s" > "$tmp/c4_stale"
if [ ! -s "$tmp/c4_new" ] && [ ! -s "$tmp/c4_stale" ]; then
    t_ok "C4: finding set ($n_find sites) equals the allowlist ($n_allow entries) -- no new sole-<0 site, no stale entry"
else
    t_fail "C4: finding set has $n_find entries, allowlist has $n_allow -- new: $(tr '\n' ' ' < "$tmp/c4_new"); stale: $(tr '\n' ' ' < "$tmp/c4_stale")"
fi

# --- C5: the instrument can fire (self-test in $TMPDIR, never the tree) ----------
c5fix="$tmp/fixtures"
mkdir -p "$c5fix"
cat > "$c5fix/a.c" <<'EOF'
int f_a(const char *y) {
    char dst[8];
    if (jc_snprintf(dst, sizeof dst, "x", y) < 0) {
        return 1;
    }
    return 0;
}
EOF
cat > "$c5fix/b.c" <<'EOF'
int f_b(const char *cmd, const char *s8) {
    char buf[64];
    if (jc_snprintf(buf, sizeof buf, "%s", cmd) < 0 ||
        strlen(cmd) + strlen(s8) + 2 >= sizeof buf ||
        jc_snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %s", s8) < 0) {
        return 1;
    }
    return 0;
}
EOF
cat > "$c5fix/c.c" <<'EOF'
int f_c(const char *y) {
    char dst[32];
    int n = jc_snprintf(dst, sizeof dst, "x", y);
    if (n < 0 || n >= sizeof dst) {
        return 1;
    }
    return 0;
}
EOF
# Three fixtures in which the NEWLINE between two lines decides the class.
# The classifier reads bounded pieces of text that it puts together from
# lines (span, spanto, beforetext); a piece joined without its newline still
# finds every site of the tree and of a.c, b.c and c.c, so nothing above
# would notice. Each fixture below is a finding only while one of the three
# keeps the newline:
#   d.c  the tail after the call (span): "if (n < 0) return 1;" and, on the
#        next line, a comparison with > that does not name n. Joined, the one
#        line names n and has a >, which reads as a truncation check.
#   e.c  the enclosing condition (spanto): the operand left of "a > c" ends
#        at the line's start. Joined, it reaches back to len_ok, a size-like
#        name, and the site is excluded as bounded.
#   g.c  the text before the call (beforetext): the assigned variable n
#        stands at the start of its line, after a line that ends in "else".
#        Joined, the variable reads as "elsen" and no check of it is found.
cat > "$c5fix/d.c" <<'EOF'
int f_d(char *b, int other) {
    int n;
    n = jc_snprintf(b, 8, "x");
    if (n < 0) return 1;
    if (other > 3) return 2;
    return 0;
}
EOF
cat > "$c5fix/e.c" <<'EOF'
int f_e(char *b, int len_ok, int a, int c) {
    if (len_ok &&
        a > c && jc_snprintf(b, 8, "x") < 0) {
        return 1;
    }
    return 0;
}
EOF
cat > "$c5fix/g.c" <<'EOF'
int f_g(char *b) {
    int n = 0;
    if (b == 0) return 0; else
n = jc_snprintf(b, 8, "x");
    if (n < 0) return 1;
    return 0;
}
EOF
run_classify "$c5fix" "$tmp/c5tot" "$c5fix/a.c" "$c5fix/b.c" "$c5fix/c.c" \
    "$c5fix/d.c" "$c5fix/e.c" "$c5fix/g.c" > "$tmp/c5cand"
_c5_bad=""
for _f in a b c d e g; do
    _n_f=$(awk -F '\t' -v pre="${_f}.c:" '$1 == "F" && index($2, pre) == 1' "$tmp/c5cand" | $G -c .)
    _n_e=$(awk -F '\t' -v pre="${_f}.c:" '$1 == "E" && index($2, pre) == 1' "$tmp/c5cand" | $G -c .)
    case "$_f" in
        a) [ "$_n_f" -eq 1 ] && [ "$_n_e" -eq 0 ] || _c5_bad="$_c5_bad a.c (a bare < 0 must be a finding; got F=$_n_f E=$_n_e)" ;;
        b) [ "$_n_f" -eq 0 ] && [ "$_n_e" -eq 1 ] || _c5_bad="$_c5_bad b.c (the :560 shape must exclude the second call and keep the first out by the NEG_PLUS branch; got F=$_n_f E=$_n_e)" ;;
        c) [ "$_n_f" -eq 0 ] && [ "$_n_e" -eq 0 ] || _c5_bad="$_c5_bad c.c (n < 0 || n >= sizeof must NOT be a finding; got F=$_n_f E=$_n_e)" ;;
        d) [ "$_n_f" -eq 1 ] && [ "$_n_e" -eq 0 ] || _c5_bad="$_c5_bad d.c (n < 0 alone, a > on the NEXT line: must be a finding; got F=$_n_f E=$_n_e -- is the newline in the tail?)" ;;
        e) [ "$_n_f" -eq 1 ] && [ "$_n_e" -eq 0 ] || _c5_bad="$_c5_bad e.c (a size-like name on the line ABOVE a > is no bound: must be a finding; got F=$_n_f E=$_n_e -- is the newline in the condition?)" ;;
        g) [ "$_n_f" -eq 1 ] && [ "$_n_e" -eq 0 ] || _c5_bad="$_c5_bad g.c (n assigned at a line start after else: must be a finding; got F=$_n_f E=$_n_e -- is the newline before the call?)" ;;
    esac
done
if [ -z "$_c5_bad" ]; then
    t_ok "C5: the classifier fires on all six planted fixtures (bare < 0 a finding; :560 shape excluded; assigned with a truncation check not a finding; three where the newline between two lines decides)"
else
    t_fail "C5: fixture misclassified:${_c5_bad}"
fi

t_done
