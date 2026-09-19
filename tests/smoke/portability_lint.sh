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

t_plan 17

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

# --- 7d: README's never-compiled prose matches PLATFORMS.md ------------------
# THE DEFECT, found by the operator reading the page on 2026-09-18. README.md
# said "**macOS and illumos have never been compiled**, and the page says so in
# those words" -- while PLATFORMS.md said, in those words, "Never compiled:
# macOS", illumos having been partly verified since M658 the previous night
# (13,273 unit checks, 284 of 303 smoke drivers). The README's OWN summary five
# paragraphs earlier already carried the new verdict; only the sentence
# asserting what the other page says was stale.
#
# This is M486 in a second costume. There the BINARY told a FreeBSD user "jichi
# has never been compiled on this platform" for months after FreeBSD passed
# 1,068 smoke checks, and check 7c now pins the C's list against this page both
# ways. The prose in README.md was never pinned to anything -- and a
# never-compiled claim is the one kind of platform statement a reader acts on
# immediately, by not trying.
#
# THE RULE is narrow and two-sided. From PLATFORMS.md's "### Never compiled"
# table, take the platform names. Then every README line that makes a
# never-compiled CLAIM may name only those. The extraction is floored at both
# ends -- an empty never-set or zero claim lines means the check stopped
# reading, not that the tree is clean, which is the failure mode that hid the
# `grep -r` lint's own vacuity earlier the same day.
_readme="$SMOKE_ROOT/README.md"
_plat_md="$SMOKE_ROOT/docs/PLATFORMS.md"
_names='macOS Darwin illumos Solaris FreeBSD OpenBSD NetBSD WSL Android Guix Haiku'
_never=$(awk '/^### Never compiled/{f=1; next} /^### /{f=0} f' "$_plat_md" \
         | sed -n 's/^| *\*\*\([^*]*\)\*\*.*/\1/p')
_nnever=$(printf '%s' "$_never" | grep -c . | tr -d '[:space:]')
[ -n "$_never" ] || _nnever=0
# SENTENCE granularity, not line. A README line legitimately carries BOTH a
# never-compiled claim and a verdict for another platform -- line 28 says
# "illumos is partly verified ... Never compiled: macOS" and is correct. A
# line-wide rule called that a contradiction on its first run. Split on
# sentence and clause boundaries and keep only the fragments that make the
# claim; the platform must be named inside the claim itself.
# PARAGRAPH mode, then clause. Markdown prose is hard-wrapped, so the claim
# this check exists for -- "**macOS and illumos have never been\ncompiled**" --
# had "never" on one line and "compiled" on the next, and a line-based grep
# could not see it. The perturbation caught that; the count could not, because
# zero contradictions is also what a clean tree looks like. Second time in one
# session an extraction was narrower than the thing it extracted (see
# posix_utils_lint check 20), which is why the floors below are not optional.
#
# So: join each paragraph onto one line, split into clauses, keep the ones that
# make a never-compiled claim. Line numbers are lost and the clause text is
# printed instead, which is what a reader needs to fix it anyway.
#
# The em dash in the sed below is a LITERAL character, not \xe2\x80\x94.
# The escape is what I wrote first, and check 12 of posix_utils_lint --
# this tier's own ban on GNU hex escapes, which match nothing on a BSD --
# failed on FreeBSD and told me so. Do not 'fix' it back.
# THE SPLIT IS awk, NOT sed, AND THAT IS THE WHOLE POINT (2026-09-19).
# BSD sed does not interpret `\n` in a REPLACEMENT -- it emits a literal `n`.
# On OpenBSD this check therefore never split the paragraph at all: the text came
# back as `**19 rows verified**ncompiled ...`, one enormous clause, and every
# platform named anywhere in it was reported as contradicted. A check about
# portability, failing on the second platform that read it, for a GNU-ism in its
# own implementation. awk's gsub inserts a real newline everywhere.
_claims=$(awk 'BEGIN{RS=""} {gsub(/\n/, " "); print}' "$_readme" \
          | awk '{ gsub(/\. /, "\n"); gsub(/; /, "\n"); gsub(/[()]/, "\n");
                   gsub(/ — /, "\n"); print }' \
          | grep -i 'never' | grep -i 'compiled' \
          | grep -vi 'never compiled from source before')
_nclaims=$(printf '%s' "$_claims" | grep -c . | tr -d '[:space:]')
[ -n "$_claims" ] || _nclaims=0
_bad=''
for _n in $_names; do
    if printf '%s\n' "$_never" | grep -qi "$_n"; then continue; fi
    _hit=$(printf '%s\n' "$_claims" | grep -i "$_n" || true)
    [ -n "$_hit" ] && _bad="$_bad
$_n: $_hit"
done
if [ "${_nnever:-0}" -ge 1 ] && [ "${_nclaims:-0}" -ge 1 ] && [ -z "$_bad" ]; then
    t_ok "README's $_nclaims never-compiled claim(s) name only what PLATFORMS.md lists ($(printf '%s' "$_never" | tr '\n' ' '))"
else
    t_fail "README.md asserts a platform is never compiled that PLATFORMS.md verifies.
  PLATFORMS.md '### Never compiled' names ($_nnever): ${_never:-NOTHING EXTRACTED}
  README never-compiled claim lines: $_nclaims
  contradicted:${_bad:- none}
A never-compiled claim is the one platform statement a reader acts on by not
trying. Fix README.md, or move the row on PLATFORMS.md -- but they must agree.
If either count above is 0 the extraction broke and this check read nothing."
fi

# --- 15: README's platform COUNTS are the ones PLATFORMS.md actually has -----
# THE DEFECT, found by trying to count before a public release (2026-09-19).
# README.md's headline said "**19 rows verified** -- compiled *and* gate-run on
# each", and a second paragraph said "Nineteen of them". The page had **20**
# Verified rows. Worse, the first count I took was 14/5/2, because I counted
# BOLD VERDICT WORDS anywhere in the file -- and those words appear in prose as
# often as in a verdict cell. Three different numbers for one question is what an
# unpinned public claim looks like.
#
# THE UNIVERSE IS DEFINED, not guessed: rows of a table whose first column header
# is "Platform", inside one of the three `### ` verdict sections. That excludes
# the front-end table (gcc/clang/zig), which sits inside `### Verified` and is
# about compilers rather than platforms -- it is six rows, and counting it is how
# a plausible wrong answer is produced.
#
# FLOORED ABOVE ZERO on purpose. A floor of zero cannot tell a clean tree from a
# broken extraction, which this tier learned twice in one week; if the parse
# breaks, the counts collapse and the check says so instead of passing.
_pcounts=$(awk '
    { line[NR] = $0 }
    END {
        sec = ""
        for (i = 1; i <= NR; i++) {
            l = line[i]
            if (l ~ /^### Verified[ \t]*$/)             { sec = "V"; continue }
            if (l ~ /^### Partly verified[ \t]*$/)      { sec = "P"; continue }
            if (l ~ /^### Never compiled[ \t]*$/)       { sec = "N"; continue }
            if (l ~ /^### / || l ~ /^## /)              { sec = "";  continue }
            # a table header is a | line whose NEXT line is the separator
            if (sec != "" && substr(l,1,1) == "|" && line[i+1] ~ /^\|[ :|-]+\|?[ :|-]*$/) {
                split(l, c, "|")
                hdr = c[2]; gsub(/^[ \t]+|[ \t]+$/, "", hdr)
                j = i + 2; n = 0
                while (j <= NR && substr(line[j],1,1) == "|" && line[j] ~ /[^ \t|]/) {
                    n++; j++
                }
                if (hdr ~ /^Platform/) { cnt[sec] += n }
                i = j - 1
            }
        }
        printf "%d %d %d\n", cnt["V"] + 0, cnt["P"] + 0, cnt["N"] + 0
    }' "$SMOKE_ROOT/docs/PLATFORMS.md")
_pv=$(echo "$_pcounts" | awk '{print $1}')
_pp=$(echo "$_pcounts" | awk '{print $2}')
_pn=$(echo "$_pcounts" | awk '{print $3}')
# The README states them as "<n> rows Verified", "<n> Partly verified",
# "<n> Never compiled". Read as words too, because the prose paragraph spells it.
_rv=$(sed -n 's/.*\*\*\([0-9][0-9]*\) rows Verified\*\*.*/\1/p' "$SMOKE_ROOT/README.md" | head -1)
_rp=$(sed -n 's/.*\*\*\([0-9][0-9]*\) Partly verified\*\*.*/\1/p' "$SMOKE_ROOT/README.md" | head -1)
_rn=$(sed -n 's/.*\*\*\([0-9][0-9]*\) Never compiled\*\*.*/\1/p' "$SMOKE_ROOT/README.md" | head -1)
if [ "${_pv:-0}" -ge 15 ] && [ "${_pn:-0}" -ge 1 ] && \
   [ "$_pv" = "${_rv:-}" ] && [ "$_pp" = "${_rp:-}" ] && [ "$_pn" = "${_rn:-}" ]; then
    t_ok "README's platform counts match the page: $_pv Verified, $_pp Partly verified, $_pn Never compiled"
else
    t_fail "README and PLATFORMS.md disagree about how many platforms there are.
  PLATFORMS.md 'Platform' table rows : Verified=$_pv  Partly=$_pp  Never=$_pn
  README.md says                     : Verified=${_rv:-<none found>}  Partly=${_rp:-<none found>}  Never=${_rn:-<none found>}
The README's is the number a reader meets first and the one a release is judged
on. If the extraction reads 0 the parse broke -- fix that before the prose."
fi

# --- 16: no ORPHANED table rows in PLATFORMS.md ------------------------------
# THE DEFECT, and it was invisible until something tried to count (2026-09-19).
# The NetBSD and OpenBSD rows sat AFTER a prose paragraph with no header and no
# separator above them -- one of them on the line immediately following the
# prose, which markdown treats as a lazy continuation of that paragraph. So two
# of the most substantive rows on the page, both freshly Driven, were rendering
# as literal pipe-delimited text rather than as table rows. Windows + Cygwin and
# Windows + MSYS2 were stranded the same way in the section below.
#
# Nothing rendered an error; the page simply showed rows as prose. That is the
# shape of defect a public snapshot carries to readers who never see the source.
_orph=$(awk '
    { line[NR] = $0 }
    END {
        n = 0
        for (i = 1; i <= NR; i++) {
            l = line[i]
            if (substr(l,1,2) != "| ") continue
            if (l ~ /^\|[ :|-]+\|?[ :|-]*$/) continue          # a separator
            if (substr(line[i-1],1,1) == "|") continue          # part of a table
            if (line[i+1] ~ /^\|[ :|-]+\|?[ :|-]*$/) continue   # it is a header
            n++
            printf "  line %d: %.60s\n", i, l
        }
        exit (n > 0 ? 1 : 0)
    }' "$SMOKE_ROOT/docs/PLATFORMS.md") || true
if [ -z "$_orph" ]; then
    t_ok "no orphaned table rows in PLATFORMS.md (a row after prose renders as prose)"
else
    t_fail "table row(s) in PLATFORMS.md with no header or separator above them.
Markdown renders these as ordinary text, so the row is invisible as a row:
$_orph
Move them into the table they belong to, and keep a blank line before a heading."
fi

# --- 17: the Driven register covers EVERY platform row ----------------------
# WHY THIS IS A CHECK AND NOT A CONVENTION. The `Driven` verdict was added
# because evidence that a platform had run the agent loop existed in analysis
# pages and **not on the matrix**, so no reader could answer "has it ever run
# here?" without a search -- and the first count published with the word was
# wrong for exactly that reason. The register fixes it only while it stays
# complete: a register listing the eight rows that ARE driven reintroduces the
# same defect one level down, because a row's absence then means either "not
# driven" or "nobody updated this", and a reader cannot tell which.
#
# So the property is completeness, checked by count. Names are not compared:
# the register's labels are short by design and the table cells are long, and a
# name-matching check would fail on formatting rather than on substance. A count
# mismatch says precisely the thing worth saying -- a row was added or removed
# somewhere and the other table did not move.
_reg=$(awk '
    { line[NR] = $0 }
    END {
        for (i = 1; i <= NR; i++) {
            if (line[i] ~ /^\| Row \| Driven\?/ && line[i+1] ~ /^\|[ :|-]+\|?[ :|-]*$/) {
                j = i + 2; n = 0
                while (j <= NR && substr(line[j],1,1) == "|" && line[j] ~ /[^ \t|]/) { n++; j++ }
                printf "%d\n", n; exit
            }
        }
        print 0
    }' "$SMOKE_ROOT/docs/PLATFORMS.md")
_plat=$((${_pv:-0} + ${_pp:-0} + ${_pn:-0}))
if [ "${_reg:-0}" -ge 15 ] && [ "${_reg:-0}" -eq "$_plat" ]; then
    t_ok "the Driven register covers all $_plat platform rows"
else
    t_fail "the Driven register has ${_reg:-0} row(s) for $_plat platform row(s).
Every row on this page must appear there, driven or not: a register that lists
only the driven ones makes absence ambiguous, which is the defect the Driven
verdict was introduced to remove. (A register of 0 means the parse broke.)"
fi

t_done

