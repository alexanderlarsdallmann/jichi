#!/bin/sh
# smoke lint: the build's portability floor stays real and stays documented
# (M326u).
#
# THE DEFECT THIS EXISTS FOR. jc_now_millis guarded its clock_gettime call with
# `#if defined(CLOCK_MONOTONIC)` alone. <time.h> defines that macro on every
# glibc, INCLUDING the ones where the function lives in librt (glibc < 2.17,
# Dec 2012). So the code compiled and the LINK failed -- a compile-time guard
# cannot see a linker's symbol table -- and nothing ever put -lrt in LDLIBS.
# That was an undocumented build floor of glibc 2.17 with no diagnostic, found
# only by someone asking "what is the oldest Linux this runs on?".
#
# The fix has THREE parts that are individually inert: a Makefile probe, an
# LDLIBS reference to what the probe sets, and a source guard for the case where
# the symbol is absent entirely. Remove any one and the other two keep looking
# correct -- which is exactly the shape that needs a lint rather than a comment.
#
# It also pins the doc: INSTALL.md's minimum-version table must exist, and the
# libcurl versions it names must be the ones the source actually guards on.
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 13

mk="$SMOKE_ROOT/Makefile"
plat="$SMOKE_ROOT/src/platform/jc_platform_posix.c"
http="$SMOKE_ROOT/src/net/jc_http.c"
inst="$SMOKE_ROOT/docs/INSTALL.md"

# --- 1: the probe and its consumer, together --------------------------------
# Either half alone is a no-op: a probe whose result nothing reads, or an
# LDLIBS slot nothing ever fills.
probe=$(grep -c 'clock_gettime' "$mk" || true)
consumed=$(grep -c '^LDLIBS = .*\$(RT_LIBS)' "$mk" || true)
if [ "$probe" -ge 1 ] && [ "$consumed" -ge 1 ]; then
    t_ok "the Makefile probes clock_gettime and LDLIBS consumes \$(RT_LIBS)"
else
    t_fail "clock_gettime probe=$probe, LDLIBS reads RT_LIBS=$consumed -- half the fix is inert"
fi

# --- 2: the source guard the probe switches ---------------------------------
# Anchored on the #if LINE, not on the file. A plain file-wide grep passed with
# the guard reverted to `#if defined(CLOCK_MONOTONIC)`, because the COMMENT
# above it still said JC_NO_CLOCK_GETTIME -- the exact failure mode in
# docs/TEST_INTEGRITY.md fm. 9: the assertion matched, but not the thing it
# named. Every conditional that gates on CLOCK_MONOTONIC must also consult the
# probe's verdict; a bare one is the original defect restored.
bare=$(grep '^#if' "$plat" | grep 'CLOCK_MONOTONIC' | grep -c -v 'JC_NO_CLOCK_GETTIME' || true)
guards=$(grep '^#if' "$plat" | grep -c 'CLOCK_MONOTONIC' || true)
if [ "$guards" -ge 1 ] && [ "$bare" -eq 0 ] && grep -q 'JC_NO_CLOCK_GETTIME' "$mk"; then
    t_ok "every CLOCK_MONOTONIC guard also honours JC_NO_CLOCK_GETTIME ($guards)"
else
    t_fail "$bare of $guards CLOCK_MONOTONIC guards ignore the probe's verdict (Makefile sets it: $(grep -c 'JC_NO_CLOCK_GETTIME' "$mk"))"
fi

# --- 3: the verdict is visible to whoever is porting ------------------------
# A probe nobody can see the result of is a probe nobody trusts on a strange
# box, which is the only kind of box it matters on.
if sed -n '/^info:/,/^$/p' "$mk" | grep -q 'CLOCK_GETTIME'; then
    t_ok "make info reports the clock_gettime verdict"
else
    t_fail "make info does not report the clock probe -- invisible on the systems that need it"
fi

# --- 4: the documented floor exists -----------------------------------------
min_curl=$(sed -n 's/.*\*\*\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)\*\* (Feb 2009).*/\1/p' "$inst" | head -1)
min_glibc=$(sed -n 's/.*\*\*\([0-9][0-9]*\.[0-9][0-9]*\)\*\* (2010).*/\1/p' "$inst" | head -1)
if [ -n "$min_curl" ] && [ -n "$min_glibc" ]; then
    t_ok "INSTALL.md states minimum libcurl $min_curl and glibc $min_glibc"
else
    t_fail "INSTALL.md states no minimum versions (curl='$min_curl' glibc='$min_glibc')"
fi

# --- 5: the doc's libcurl versions are the ones the source guards on --------
# LIBCURL_VERSION_NUM is 0xMMmmpp. Decoding it here rather than trusting a
# hand-written dotted form is the point: the guards are the ground truth, and a
# doc that names a different version is worse than one naming none.
tmp=$(smoke_tmp)
: > "$tmp/want"
for hex in $(grep -o 'LIBCURL_VERSION_NUM >= 0x[0-9a-f]*' "$http" | sed 's/.*0x//'); do
    printf '%d.%d.%d\n' \
        "0x$(echo "$hex" | cut -c1-2)" \
        "0x$(echo "$hex" | cut -c3-4)" \
        "0x$(echo "$hex" | cut -c5-6)" >> "$tmp/want"
done
nwant=$(grep -c . "$tmp/want" || true)
missing=""
for v in $(cat "$tmp/want"); do
    grep -q "$v" "$inst" || missing="$missing $v"
done
if [ "$nwant" -ge 2 ] && [ -z "$missing" ]; then
    t_ok "INSTALL.md names every guarded libcurl version ($(tr '\n' ' ' < "$tmp/want"))"
else
    t_fail "guarded but undocumented libcurl versions:$missing (found $nwant guards)"
fi

# --- 6: no `long long` anywhere in first-party C (M400) ----------------------
# THE DEFECT THIS EXISTS FOR. CLAUDE.md: first-party code must compile with zero
# warnings under `-std=c89 -pedantic -Wall -Wextra`, EVERY translation unit, no
# exemptions. gcc's -Wlong-long fires on both the type and a `ULL` constant, so
# `long long` breaks that rule outright -- and WERROR=1 turns it into a failed
# build. The tree held exactly one instance, in `jc_mem_total_mb`'s
# `#if defined(__APPLE__)` branch, since the day it was written. It survived
# every WERROR build, every compiler in the M368 matrix and four claims-audit
# passes for one reason: NO MACHINE HERE COMPILES IT. A platform guard for a
# platform you cannot build is a hole in the compiler's coverage, and the only
# instrument that reaches inside it is grep.
#
# Scope is deliberately the whole first-party tree, not just the Darwin branch:
# the rule is tree-wide, and a lint that only watched one `#if` would miss the
# next never-compiled branch (a BSD, an illumos) -- which is the entire class.
#
# IT MUST SEE CODE, NOT PROSE. The first cut flagged three hits in the very
# comment explaining the fix -- the same blindness `asset_keys_lint` had (a table
# extraction that read the comment quoting the key it was looking for). The fix
# is NOT an exception list and NOT rewording the comment: the reason for a rule
# has to be writable next to the code it governs. Instead the extraction drops
# any line whose first non-blank character is `*` or `/`, i.e. a block-comment
# body or a whole-line comment -- narrowing on a fact about C rather than a guess
# about English. A code line with a trailing comment is still checked.
ll_hits() {
    grep -rn 'long long' "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" 2>/dev/null |
    awk '{ code = $0
           sub(/^[^:]*:[^:]*:/, "", code)
           sub(/^[ \t]+/, "", code)
           if (code !~ /^[*\/]/) print }'
}
ll=$(ll_hits | wc -l | tr -d ' ')
if [ "$ll" -eq 0 ]; then
    t_ok "no 'long long' in first-party src/ or include/ code (C89 has no such type)"
else
    t_fail "$ll use(s) of 'long long' -- illegal C89, a warning under the mandatory flags, a failed build under WERROR=1: $(ll_hits | head -3 | tr '\n' ' ')"
fi

# --- 7: no routed page contradicts PLATFORMS.md's verdict -------------------
# THE DEFECT THIS EXISTS FOR, and it was this check's own (M486). The first
# version could not fail. Its precondition was four INDEPENDENT greps over
# PLATFORMS.md -- 'never compiled', 'macOS', 'WSL', 'uClibc' -- and `WSL` went on
# matching after WSL2 was promoted to Verified (M475), because the word is right
# there in the Verified row. Then the body only checked that six pages CONTAIN the
# string "PLATFORMS.md". It never checked that they refrain from stating a verdict.
#
# So all six were free to assert the opposite of the page they linked, and did:
# nine sites still called WSL never-compiled while the check reported t_ok, and
# docs/BUILD.md contradicted ITSELF 26 lines apart. The project's strongest
# portability result -- three BSD kernels, WSL2, 14 architectures, five libcs --
# was described on its own front page as "Linux only".
#
# THE GROUND TRUTH IS PLATFORMS.md, READ RATHER THAN RESTATED. The candidate
# tokens below are a list; which of them COUNT is decided by whether they appear
# in that page's Verified / Partly verified tables. So promoting a platform
# automatically starts policing claims about it, and this check cannot again be
# left asserting a verdict the owning page has retired.
plats="$SMOKE_ROOT/docs/PLATFORMS.md"
routed="$SMOKE_ROOT/README.md $SMOKE_ROOT/TUTORIAL.md $SMOKE_ROOT/docs/INSTALL.md
        $SMOKE_ROOT/docs/BUILD.md $SMOKE_ROOT/docs/PREPARE_AND_BUILD.md
        $SMOKE_ROOT/docs/LOW_MEMORY.md"

# The Verified + Partly verified tables, and nothing after them.
verified_tables() {
    awk '/^### Verified/{f=1} /^### /{ if (f && $0 !~ /^### (Verified|Partly verified)/) exit } f' "$plats"
}

# A verdict phrase: the closed set of ways this project says "we have not run it".
# Matched case-INSENSITIVELY throughout, because docs/BUILD.md shouts it inside a
# mermaid node ("NEVER COMPILED") and the first draft of this check, which used
# [Nn]ever, walked straight past the one page that contradicted itself.
VERDICT='never (been )?compiled|never been executed|never compiled by us'

# An exclusivity claim: false by construction once a non-Linux row is Verified.
# Defined HERE and not inline, because an unset variable in `grep -qiE "$X"` is an
# EMPTY PATTERN, which matches every file -- the inverse of a hollow check, and it
# happened while this very block was being edited: all six pages "failed" at once,
# which is the tell.
EXCLUSIVE='only the .{0,3}Linux.{0,3} path|only Linux is verified|verified on Linux only'

# A page contradicts the owning page about $2 if any of three shapes holds. They
# are separate because a verdict attaches to a platform in three different ways,
# and every attempt to cover them with one rule produced either a false positive
# or a false negative -- each of the boundaries below was put there by a measured
# case, and all six are pinned by the self-test in 7b.
#
#   MATRIX ROW    `| **Windows + WSL2** | Never compiled | ... |` -- the platform IS
#                 the row's subject, so the scan must cross a `|`. Restricted to a
#                 SHORT first cell, because a prose row that merely mentions several
#                 platforms ("WSL2 verified; macOS never executed") is not a verdict
#                 about any of them and was flagged by the first draft.
#   CLAUSE        "never compiled on macOS or under WSL" -- one verdict governing a
#                 conjoined list. Bounded by `.`, `;` and `|`, and list markers are
#                 turned into sentence breaks first: without that, a bullet saying
#                 macOS is never compiled and the NEXT bullet naming WSL2 as verified
#                 were joined by the flattening and read as one false claim.
#   LABELLED ITEM "- **Never compiled:** macOS (...long parenthetical...), WSL2, ..."
#                 -- the platform sits 150 characters from the phrase governing it,
#                 past any window that stays honest for prose. The label governs the
#                 whole item, so the item is the scope.
contradicts() {
    # matrix row: first cell short and naming the platform, verdict anywhere after
    awk -F'|' -v tok="$2" -v v="$VERDICT" '
        /^\|/ {
            c = $2; gsub(/^[ \t*]+|[ \t*]+$/, "", c)
            if (length(c) <= 40 && index(c, tok) && tolower($0) ~ v) { hit = 1 }
        }
        END { exit hit ? 0 : 1 }
    ' "$1" && return 0
    # clause: same sentence, list markers demoted to sentence breaks
    sed 's/^[[:space:]]*[-*] /. /' "$1" | tr '\n' ' ' | tr -s ' ' \
      | grep -qiE "($VERDICT)[^.;|]{0,60}$2|$2[^.;|]{0,60}($VERDICT)" && return 0
    # list item whose LABEL is the verdict
    awk -v tok="$2" -v v="$VERDICT" '
        /^[-*] / { inblk = (tolower($0) ~ v) ? 1 : 0 }
        /^[[:space:]]*$/ { inblk = 0 }
        inblk && index($0, tok) { hit = 1 }
        END { exit hit ? 0 : 1 }
    ' "$1" && return 0
    return 1
}

if [ ! -f "$plats" ]; then
    t_fail "docs/PLATFORMS.md is missing -- the page that owns every verdict"
else
    verified_tables > "$tmp/vtab"
    _nrows=$(grep -c '^| \*\*' "$tmp/vtab")
    if [ "$_nrows" -lt 10 ]; then
        t_fail "only $_nrows rows read from PLATFORMS.md's Verified tables -- the extraction broke, so this check is measuring nothing"
    else
        _bad=""
        # (a) a verdict phrase applied to a platform PLATFORMS.md now verifies.
        # Windowed on a whitespace-flattened page, because the claims wrap and sit
        # in table cells. 60 chars keeps "never compiled on macOS" next to macOS and
        # away from a separate, correct sentence about WSL2 -- proven both ways by
        # the self-test below.
        for tok in WSL Cygwin MSYS2 FreeBSD NetBSD OpenBSD Termux; do
            grep -q "$tok" "$tmp/vtab" || continue      # not verified: silent
            for f in $routed; do
                [ -f "$f" ] || continue
                if contradicts "$f" "$tok"; then
                    _bad="$_bad
  ${f#"$SMOKE_ROOT"/}: calls $tok never-compiled, but PLATFORMS.md verifies it"
                fi
            done
        done
        # (b) any "Linux only" claim, once a non-Linux row is Verified.
        if [ -z "$EXCLUSIVE" ]; then
            _bad="$_bad
  the exclusivity pattern is empty -- grep would match every page"
        elif grep -qE '^\| \*\*(FreeBSD|NetBSD|OpenBSD|Windows)' "$tmp/vtab"; then
            for f in $routed; do
                [ -f "$f" ] || continue
                if tr '\n' ' ' < "$f" | tr -s ' ' | grep -qiE "$EXCLUSIVE"; then
                    _bad="$_bad
  ${f#"$SMOKE_ROOT"/}: claims Linux is the only verified platform"
                fi
            done
        fi
        # Every routed page must still point at the owning page.
        _unrouted=""
        for f in $routed; do
            [ -f "$f" ] || continue
            grep -q 'PLATFORMS\.md' "$f" || _unrouted="$_unrouted ${f#"$SMOKE_ROOT"/}"
        done
        [ -n "$_unrouted" ] && _bad="$_bad
  not linking PLATFORMS.md:$_unrouted"

        if [ -z "$_bad" ]; then
            t_ok "no routed page contradicts PLATFORMS.md ($_nrows verified rows, 6 pages)"
        else
            t_fail "a routed page states a verdict PLATFORMS.md has retired:$_bad"
        fi
    fi
fi

# --- 7b: the matcher for check 7 flags a planted contradiction --------------
# A clean result from a broken matcher is what check 7 spent three milestones
# producing. Two-sided on purpose: the planted page must fail AND the corrected
# wording -- the same two facts stated separately, which is what the fix looks
# like -- must pass, or the window is too wide and would forbid saying them.
_pf="$tmp/plant7"
# Four cases, because the matcher has two shapes and each must be two-sided. The
# "spared" halves are the fix's actual wording -- the same two facts stated
# separately -- so a window wide enough to forbid saying them fails here first.
_p1=0; _p2=0; _c1=0; _c2=0
printf 'See PLATFORMS.md. jichi has never been compiled on macOS or under WSL.\n' > "$_pf"
contradicts "$_pf" WSL && _p1=1
printf '%s\n' 'subgraph win["Windows - WSL only, NEVER COMPILED"]' > "$_pf"
contradicts "$_pf" WSL && _p2=1
printf 'See PLATFORMS.md. Never compiled on macOS. WSL2 is verified (M475).\n' > "$_pf"
contradicts "$_pf" WSL || _c1=1
printf '%s\n%s\n' '| macOS | Never compiled | no Mac here |' '| WSL2 | Verified | full gate |' > "$_pf"
contradicts "$_pf" WSL || _c2=1
_p3=0; _c3=0
printf '%s\n' '- **Never compiled:** **macOS** (a long parenthetical that pushes the platform well past any honest prose window), **WSL2**, and the BSDs.' > "$_pf"
contradicts "$_pf" WSL && _p3=1
printf '%s\n\n%s\n' '- **Never compiled:** **macOS**, and illumos.' '- **Verified:** **WSL2**, the full gate.' > "$_pf"
contradicts "$_pf" WSL || _c3=1
if [ "$_p1" -eq 1 ] && [ "$_p2" -eq 1 ] && [ "$_p3" -eq 1 ] \
   && [ "$_c1" -eq 1 ] && [ "$_c2" -eq 1 ] && [ "$_c3" -eq 1 ]; then
    t_ok "check 7's matcher flags planted prose, a table row and a labelled list item, and spares all three corrected forms"
else
    t_fail "check 7's matcher is broken (prose=$_p1 mermaid=$_p2 bullet=$_p3 spared=$_c1/$_c2/$_c3) -- check 7 above is meaningless"
fi

# --- 7c: the product's own verdict list matches PLATFORMS.md -----------------
# THE DEFECT THIS EXISTS FOR (M486). `jichi doctor` told a FreeBSD user "jichi has
# never been compiled on this platform" for months after FreeBSD started passing
# 1,068 smoke checks there -- the binary asserting a verdict its own documentation
# had retired, in the first place a support conversation looks. tests/smoke/doctor.sh
# had even written the staleness down and deferred the decision to PLATFORMS.md;
# PLATFORMS.md made it, and nothing carried the answer back into the C.
#
# So the kernel list in jc_platform_verified_row() is pinned here, BOTH WAYS: a name
# in the C that the page does not verify, and a kernel the page verifies that the C
# does not know. The floor guards the extraction itself -- an empty list would make
# both directions vacuously agree, which is how this family of check fails.
_vsrc="$SMOKE_ROOT/src/platform/jc_platform_posix.c"
if [ ! -f "$_vsrc" ]; then
    t_fail "src/platform/jc_platform_posix.c is missing -- cannot check the product's verdict list"
else
    sed -n '/jc_platform_verified_row(void)/,/^}/p' "$_vsrc" \
      | sed -n 's/^ *"\([A-Za-z]*\)",$/\1/p' | sort -u > "$tmp/csys"
    _nc=$(grep -c . < "$tmp/csys")
    if [ "$_nc" -lt 2 ]; then
        t_fail "extracted only $_nc kernel(s) from jc_platform_verified_row -- the extraction broke, so this check compares nothing"
    else
        _mismatch=""
        # C says verified -> the page must carry a Verified row for it.
        while IFS= read -r k; do
            [ "$k" = "Linux" ] && continue          # the development platform, row 1
            grep -qE "^\| \*\*$k\*\*" "$tmp/vtab" \
              || _mismatch="$_mismatch
  the C claims $k is verified; PLATFORMS.md has no Verified row for it"
        done < "$tmp/csys"
        # The page verifies a BSD -> the C must know it, or doctor lies there.
        for k in FreeBSD NetBSD OpenBSD; do
            grep -qE "^\| \*\*$k\*\*" "$tmp/vtab" || continue
            grep -qx "$k" "$tmp/csys" \
              || _mismatch="$_mismatch
  PLATFORMS.md verifies $k; jc_platform_verified_row does not, so doctor calls it never-compiled there"
        done
        if [ -z "$_mismatch" ]; then
            t_ok "the product's verdict list ($_nc kernels) matches PLATFORMS.md"
        else
            t_fail "doctor's platform verdict has drifted from PLATFORMS.md:$_mismatch"
        fi
    fi
fi

# --- 8: the RAM tiers state which grade of evidence they have (M403) --------
# THE DEFECT THIS EXISTS FOR. LOW_MEMORY.md's tiers read as a support matrix:
# "Comfortable / Constrained / Tight / Very tight", one config per row, no hint
# that the top two were measured on real machines and the bottom two had never
# been run at all. A cgroup ceiling on a 4.9 GB box is not a 64 MB machine -- the
# host page cache, the kernel's footprint outside the ceiling, and the absence of
# competing pressure all flatter the number -- so the page now grades every tier
# and says which grade it has. This pins that the distinction survives: the words
# stay, and the two untested tiers keep their tags.
# M430 re-pointed this check. It used to require EXACTLY FOUR literal verdicts
# ('cgroup ceiling', 'grade B', 'lower bound', 'never compiled') to appear
# somewhere in the page. That pinned the ANSWERS of 2026-08-12 rather than the
# property worth defending, and it fails the moment the answers improve: M430
# measured two tiers on whole machines, so 'grade B' was on its way out of the
# rows it described, and the check would have gone red for the page getting
# BETTER -- or, worse, stayed green while asserting words that no longer matched
# any row. Pin the INVARIANT instead: the grade vocabulary is still defined, and
# every tier row still carries a grade. A row may say A, B or C, or that
# something was never run -- what it may not do is go back to having no grade
# at all, which is the support-matrix defect this check exists for.
lm="$SMOKE_ROOT/docs/LOW_MEMORY.md"

# (a) the vocabulary is still DEFINED -- these are the definitions in the grade
# table, not any row's verdict, so improving a row cannot invalidate them.
vocab=0
grep -qi 'cgroup ceiling' "$lm" && vocab=$((vocab + 1))
grep -qi 'lower bound' "$lm" && vocab=$((vocab + 1))

# (b) every row of the tier table carries a grade. Extract the table by its
# header and stop at the blank line after it; a row's grade is field 3.
# The >=5 floor is deliberate: if the extraction ever stops matching the table,
# this check must fail loudly rather than pass over an empty set -- the lesson
# M390/M393 paid for twice.
rows=0
ungraded=0
while IFS= read -r line; do
    rows=$((rows + 1))
    g=$(printf '%s\n' "$line" | awk -F'|' '{print $3}')
    case "$g" in
        *A*|*B*|*C*|*never*|*not\ run*) ;;
        *) ungraded=$((ungraded + 1))
           echo "#   ungraded tier row: $(printf '%s' "$line" | cut -c1-60)" ;;
    esac
done <<EOF
$(awk '/^\| Tier \| Grade \| Evidence \|/{f=1; next} f && /^\|---/{next} f && /^\|/{print} f && !/^\|/{exit}' "$lm")
EOF

if [ "$vocab" -eq 2 ] && [ "$rows" -ge 5 ] && [ "$ungraded" -eq 0 ] &&
   grep -q 'ram_floor\.sh' "$lm" &&
   [ -x "$SMOKE_ROOT/tests/measure/ram_floor.sh" ]; then
    t_ok "every RAM tier row carries a grade ($rows rows), the grade vocabulary is defined, and the harness is named"
else
    t_fail "LOW_MEMORY.md tier grades broken: vocab $vocab/2, $rows rows found (need >=5), $ungraded ungraded, or ram_floor.sh is gone"
fi

# ---- 9-11 (M479): a FLAG probe must ask the question the BUILD asks ----------
#
# THE DEFECT THIS EXISTS FOR. M472's HARDENFLAGS probe asked "does the compiler
# ACCEPT this flag":
#
#   harden_ok = $(shell printf ... | $(CC) $(1) -xc - -o ... 2>/dev/null && echo $(1))
#
# OpenBSD's clang 19 accepts -fstack-clash-protection and then ignores it. So the
# probe said yes, the flag went into CFLAGS, and EVERY translation unit failed
# under the build's own -Werror:
#
#   cc: error: argument unused during compilation: '-fstack-clash-protection'
#       [-Werror,-Wunused-command-line-argument]
#
# The OpenBSD row could not build for six milestones and nothing noticed, because
# `make ci`'s clang stage runs on Linux where that flag is genuinely supported --
# the defect needs a target where clang accepts-but-ignores, so the local gate was
# structurally blind to it.
#
# This is M449's lesson repeated ("the question a capability probe must ask is 'is
# it DECLARED under the flags I build with'"), and M476's cc_warn_ok -- written one
# milestone AFTER the broken probe, in the same file, forty lines above it --
# already asked it correctly. Two probes for one question is how they drift.
#
# 9  the flag probe carries -Werror
# 10 there is only ONE flag probe, so a second cannot drift from it
# 11 the floor: this lint found a flag probe at all
# A flag probe is recognised by its piped $(CC) line carrying $(1) -- the flag
# under test. That line, not the `name = $(shell` line above it, is where the
# compiler invocation lives; a first cut of this lint matched only the first line,
# found nothing, and reported "no flag probe" while check 10 was simultaneously
# finding one. Both halves were then vacuous.
_flag_probes=$(grep -cE '\| \$\(CC\).*\$\(1\)' "$mk" || true)
_flag_no_werror=$(grep -E '\| \$\(CC\).*\$\(1\)' "$mk" | grep -vc -- '-Werror' || true)

if [ "$_flag_probes" -ge 1 ]; then
    t_ok "found $_flag_probes flag probe(s) in the Makefile to check"
else
    t_fail "no flag probe found -- this lint is checking nothing; did the probe get \
renamed away from the '<name> = \$(shell ... \$(1) ...)' shape?"
fi

if [ "$_flag_no_werror" -eq 0 ] && [ "$_flag_probes" -ge 1 ]; then
    t_ok "the flag probe compiles with -Werror, as the build does"
else
    t_fail "a flag probe does not pass -Werror: a flag the compiler accepts and \
then ignores will be selected and then fail every TU under the build's own -Werror \
(M479: OpenBSD, -fstack-clash-protection)"
fi

if [ "$_flag_probes" -le 1 ]; then
    t_ok "exactly one flag probe, so there is nothing for a second to drift from"
else
    t_fail "$_flag_probes flag probes: one question, one probe -- the M479 defect \
was a second probe forty lines below a correct one"
fi

t_done

