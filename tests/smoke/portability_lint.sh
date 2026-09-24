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

t_plan 36

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
min_glibc=$(sed -n 's/.*\*\*\([0-9][0-9]*\.[0-9][0-9]*\)\*\* (2007).*/\1/p' "$inst" | head -1)  # the floor measured by the userland ladder, 2026-09-24
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

# The two tiers SEPARATELY (M695). verified_tables() above deliberately spans
# both, because checks 7a/7b ask "does the page claim this platform works at
# all", for which Partly counts. Check 7c asks which VERDICT the page gives,
# and for that the tiers must not be merged -- merging them is what let the C
# hold a name the page only partly verifies and still pass.
verified_only_table() {
    awk '/^### Verified/{f=1;next} f&&/^### /{exit} f' "$plats"
}
partly_table() {
    awk '/^### Partly verified/{f=1;next} f&&/^### /{exit} f' "$plats"
}

# The SAME relation the product uses (jc_sys_listed): an entry ending in '-' is
# a PREFIX, anything else is a whole sysname. Written twice, once per direction,
# because a single helper would have to guess which argument is the pattern.
_entry_matches_token() {        # $1 = C entry, $2 = uname token
    case "$1" in
        *-) case "$2" in "$1"*) return 0 ;; esac ;;
        *)  [ "$2" = "$1" ] && return 0 ;;
    esac
    return 1
}
_any_line_matches() {           # $1 = C entry, $2 = file of uname tokens
    while IFS= read -r _t; do
        _entry_matches_token "$1" "$_t" && return 0
    done < "$2"
    return 1
}
_any_entry_matches() {          # $1 = uname token, $2 = file of C entries
    while IFS= read -r _e; do
        _entry_matches_token "$_e" "$1" && return 0
    done < "$2"
    return 1
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

# --- 7f: the phrase doctor.sh greps for is the phrase doctor prints ---------
# THE DEFECT THIS EXISTS FOR, and it happened twice in one hour.
#
# doctor's partly-verified sentence has TWO consumers that never see each other:
# tests/smoke/doctor.sh greps the verdict wording, and tests/smoke/setup_keyfile.sh
# check 27 counts the lowercase word "platform" in doctor's whole output. Neither
# exercises that branch on this bench, because the bench is Verified.
#
#   M695 wrote "jichi is PARTLY verified here (docs/PLATFORMS.md)"
#        -> setup_keyfile 27 went from 2 matches to 0. Shipped.
#   The fix wrote "PARTLY verified on this platform"
#        -> doctor.sh, grepping 'partly verified here', went red. Shipped.
#
# One sentence, two consumers, opposite directions, and no gate that sees either.
# So the COUPLING is pinned here rather than each side alone: whatever phrase
# doctor.sh greps for must actually appear in main.c. That is a property this
# bench CAN check, about a branch it cannot run.
_dsh="$SMOKE_ROOT/tests/smoke/doctor.sh"
_dmain="$SMOKE_ROOT/src/main.c"
if [ ! -f "$_dsh" ] || [ ! -f "$_dmain" ]; then
    t_fail "doctor.sh or main.c is missing -- cannot check the verdict-phrase coupling"
else
    _phrase=$(grep -oE "grep -ci '[^']*partly[^']*'" "$_dsh" | head -1 \
              | sed "s/.*'\(.*\)'/\1/")
    if [ -z "$_phrase" ]; then
        t_fail "could not extract the partly-verified phrase doctor.sh greps for -- \
the extraction broke, so this check compares nothing. It expects a line shaped \
like: grep -ci '<phrase containing partly>'"
    elif grep -qi "$_phrase" "$_dmain"; then
        t_ok "doctor.sh greps '$_phrase', and main.c prints it"
    else
        t_fail "doctor.sh greps for '$_phrase' and main.c does not contain it. That \
sentence has two consumers -- this driver and setup_keyfile check 27 -- and neither \
runs on this bench in the branch that prints it, so a reword ships green and fails \
on Cygwin, MSYS2 and illumos at once. It has already done so twice."
    fi
fi

# --- 7e: entering raw mode flushes EXPLICITLY, not by side effect -----------
# THE DEFECT THIS EXISTS FOR. jichi discards type-ahead by entering raw mode with
# TCSAFLUSH and relying on its specified side effect. POSIX does specify it;
# Cygwin does not implement it. Measured 2026-09-21 on one machine, same code and
# flags: 18 bytes pending, tcsetattr(TCSAFLUSH), and Linux reports 0 pending while
# Cygwin reports 18. tcflush(TCIFLUSH) clears it on both.
#
# The consequence was not cosmetic: jc_term announced "discarded -- retype" and
# the stray line went on to become the user's first prompt, so
# preprompt_discard check 3 -- the SAFETY half, the reason the flush exists --
# failed on Cygwin while the two announcement checks passed.
#
# This is pinned in the SOURCE because the bench cannot see it: TCSAFLUSH works
# here, so every behavioural test passes on Linux whether or not the explicit
# flush is present. A check that can only go red on a platform no gate runs is
# not a gate, which is the same lesson as 7d one file over.
_tsrc="$SMOKE_ROOT/src/tui/jc_term.c"
if [ ! -f "$_tsrc" ]; then
    t_fail "src/tui/jc_term.c is missing -- cannot check the raw-mode flush"
else
    _rawset=$(grep -c 'tcsetattr(fd, TCSAFLUSH, &raw)' "$_tsrc")
    _explicit=$(grep -c 'tcflush(fd, TCIFLUSH)' "$_tsrc")
    if [ "$_rawset" -ge 1 ] && [ "$_explicit" -ge 1 ]; then
        t_ok "entering raw mode flushes input explicitly (tcflush), not by relying on TCSAFLUSH alone"
    else
        t_fail "jc_term enters raw mode $_rawset time(s) and calls tcflush(TCIFLUSH) \
$_explicit time(s). TCSAFLUSH alone does not discard input on Cygwin (measured: 18 \
bytes in, 18 still pending), so the type-ahead jichi announces as discarded becomes \
the user's next prompt -- and no test on this bench can see it, because TCSAFLUSH \
works here."
    fi
fi

# --- 7d: every doctor platform verdict names the platform -------------------
# THE DEFECT THIS EXISTS FOR (M695, found the same day by measuring on Cygwin).
# doctor's platform verdict has three branches and any given host executes
# exactly ONE of them. The bench is Verified, so `make ci` here can only ever
# see that branch -- the partly-verified and never-compiled wordings are, from
# this machine, unreachable code that no gate reads.
#
# M695 rewrote the partly branch and dropped the lowercase word "platform" from
# it: "jichi is PARTLY verified here (docs/PLATFORMS.md)" contains PLATFORMS in
# capitals and nothing else. tests/smoke/setup_keyfile.sh check 27 counts
# `doctor | grep -c "platform"`, case-sensitively, and went from 2 matches to 0
# on Cygwin -- a regression that shipped because the only check covering it runs
# only on the platforms the gate never reaches. It was found by running the tier
# on Cygwin, which is not a gate and cannot be relied on to be run.
#
# So the property is checked HERE, in the source, where all three branches are
# visible at once regardless of which one this host would execute. It is
# deliberately a weak property -- the word must appear -- because that is
# exactly what the downstream driver greps for, and a check that asserts
# something stronger than its consumer needs would fail for the wrong reasons.
_dsrc="$SMOKE_ROOT/src/main.c"
if [ ! -f "$_dsrc" ]; then
    t_fail "src/main.c is missing -- cannot check doctor's platform verdicts"
else
    # The three verdict wordings, by the distinctive phrase each one owns.
    _v_ok=$(grep -c 'JC_DOC_OK, "platform"' "$_dsrc")
    _v_partly=$(grep -c 'PARTLY verified on this platform' "$_dsrc")
    _v_never=$(grep -c 'never been compiled on this platform' "$_dsrc")
    _v_total=$((_v_ok + _v_partly + _v_never))
    if [ "$_v_total" -lt 3 ]; then
        t_fail "doctor's platform verdicts: verified=$_v_ok partly=$_v_partly \
never=$_v_never (want one each). A branch that does not name the platform in \
lowercase is invisible to setup_keyfile check 27, and that check only runs on \
the platforms this gate never reaches -- which is how M695 shipped a regression \
to Cygwin, MSYS2 and illumos at once."
    else
        t_ok "all three of doctor's platform verdicts name the platform in lowercase (verified/partly/never)"
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

# --- 7c: the product's own verdict TABLE matches PLATFORMS.md ----------------
# THE DEFECT THIS EXISTS FOR (M486). `jichi doctor` told a FreeBSD user "jichi has
# never been compiled on this platform" for months after FreeBSD started passing
# 1,068 smoke checks there -- the binary asserting a verdict its own documentation
# had retired, in the first place a support conversation looks. tests/smoke/doctor.sh
# had even written the staleness down and deferred the decision to PLATFORMS.md;
# PLATFORMS.md made it, and nothing carried the answer back into the C.
#
# WIDENED AT M695, because the same defect was live one verdict tier down for the
# whole of that time and this check could not see it. Its universe was the
# **Verified** table, so the three rows under **Partly verified** -- illumos/Solaris,
# Cygwin and MSYS2 -- were outside it BY CONSTRUCTION. All three were told by
# `doctor`, and by the setup wizard, that jichi had never been compiled there; on
# Cygwin that sentence was printed by a binary Cygwin had just compiled, and illumos
# has run a live agentic task. Audit the universe, not the result: a check that
# covers one tier of a three-tier verdict reports on one tier of a three-tier verdict.
#
# The page now carries the machine-readable half itself. Every measured row states
# `uname -s = <token>`, so this check reads the page's own declaration instead of
# holding a second copy of the answer -- the previous version hardcoded
# "FreeBSD NetBSD OpenBSD" as its page-side universe, which is the same staleness
# this check exists to catch, one level up. Both tiers are pinned BOTH WAYS, each
# with its own floor: an empty extraction on either side makes both directions
# vacuously agree, which is how this family of check fails.
_vsrc="$SMOKE_ROOT/src/platform/jc_platform_posix.c"
if [ ! -f "$_vsrc" ]; then
    t_fail "src/platform/jc_platform_posix.c is missing -- cannot check the product's verdict table"
else
    # The C side: two arrays, one literal per line, extracted by name.
    sed -n '/^static const char \*const jc_sys_verified\[\]/,/^};/p' "$_vsrc" \
      | sed -n 's/^ *"\([A-Za-z0-9_.-]*\)",$/\1/p' | sort -u > "$tmp/csys_v"
    sed -n '/^static const char \*const jc_sys_partly\[\]/,/^};/p' "$_vsrc" \
      | sed -n 's/^ *"\([A-Za-z0-9_.-]*\)",$/\1/p' | sort -u > "$tmp/csys_p"
    # The page side: the uname token each measured row declares about itself.
    verified_only_table > "$tmp/vonly"
    partly_table        > "$tmp/ptab"
    grep -o 'uname -s = [A-Za-z0-9_.-]*' "$tmp/vonly" | sed 's/^uname -s = //' \
      | sort -u > "$tmp/psys_v"
    grep -o 'uname -s = [A-Za-z0-9_.-]*' "$tmp/ptab"  | sed 's/^uname -s = //' \
      | sort -u > "$tmp/psys_p"
    _ncv=$(grep -c . < "$tmp/csys_v"); _ncp=$(grep -c . < "$tmp/csys_p")
    _npv=$(grep -c . < "$tmp/psys_v"); _npp=$(grep -c . < "$tmp/psys_p")
    # Floors at today's exact counts: 4 verified kernels, 3 partly-verified rows.
    if [ "$_ncv" -lt 4 ] || [ "$_ncp" -lt 3 ] \
       || [ "$_npv" -lt 4 ] || [ "$_npp" -lt 3 ]; then
        t_fail "verdict-table extraction came up short (C: $_ncv verified, $_ncp partly; \
PLATFORMS.md: $_npv verified, $_npp partly; floors 4/3/4/3) -- one side is empty or a \
format moved, and this check compares nothing until both sides read"
    else
        _mismatch=""
        # A token may not sit in both tiers: the verdict would be ambiguous and
        # the product would answer whichever array it happened to scan first.
        _both=$(comm -12 "$tmp/psys_v" "$tmp/psys_p")
        [ -n "$_both" ] && _mismatch="$_mismatch
  PLATFORMS.md declares the same uname -s in BOTH tiers: $_both"
        # page -> C, per tier. This is the direction M486 and M695 both failed.
        while IFS= read -r _t; do
            _any_entry_matches "$_t" "$tmp/csys_v" || _mismatch="$_mismatch
  PLATFORMS.md VERIFIES a row reporting uname -s '$_t'; jc_sys_verified has no entry \
matching it, so doctor does not call that platform verified"
        done < "$tmp/psys_v"
        while IFS= read -r _t; do
            _any_entry_matches "$_t" "$tmp/csys_p" || _mismatch="$_mismatch
  PLATFORMS.md PARTLY verifies a row reporting uname -s '$_t'; jc_sys_partly has no \
entry matching it, so doctor and the setup wizard call that platform never-compiled"
        done < "$tmp/psys_p"
        # C -> page, per tier: a name compiled in that the page does not carry.
        while IFS= read -r _e; do
            _any_line_matches "$_e" "$tmp/psys_v" || _mismatch="$_mismatch
  jc_sys_verified holds '$_e', which no Verified row on PLATFORMS.md declares"
        done < "$tmp/csys_v"
        while IFS= read -r _e; do
            _any_line_matches "$_e" "$tmp/psys_p" || _mismatch="$_mismatch
  jc_sys_partly holds '$_e', which no Partly verified row on PLATFORMS.md declares"
        done < "$tmp/csys_p"
        if [ -z "$_mismatch" ]; then
            t_ok "the product's verdict table matches PLATFORMS.md both ways \
($_ncv verified + $_ncp partly in the C, $_npv + $_npp declared on the page)"
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

# --- 16b: ...and no row whose TAIL became prose ------------------------------
# THE OTHER DIRECTION, and check 16 is green throughout it. Its rule is "a row
# whose PREVIOUS line is not a row", which catches a row stranded AFTER prose.
# This is the mirror: a row that OPENS with `|` and never closes, so everything
# from its last `|` onward renders as a paragraph below the table. The cell is
# cut off mid-word and a paragraph begins mid-sentence.
#
# It was live on this page for six milestones. The illumos row was whole at
# M661 and split at M661b, which appended to the cell and let the text run past
# the line; M678, M681 and M683 then appended MORE prose to the orphan, so the
# page grew a 28-line paragraph that a reader could only understand as part of
# a table cell they could not see. A second orphan dated to M479, where an
# OpenBSD row in the Partly-verified table was written the same way.
#
# Mechanically detectable, which is why it is a check and not a convention: in
# a table region, a line that starts with `|` must end with `|`. Trailing
# whitespace is tolerated because an editor adds it and it changes nothing.
_sev=$(awk '
    { line[NR] = $0 }
    END {
        n = 0
        intable = 0
        for (i = 1; i <= NR; i++) {
            l = line[i]
            sub(/[ \t]+$/, "", l)
            if (l ~ /^\|[ :|-]+\|?[ :|-]*$/) { intable = 1; continue }  # separator
            if (l == "") { intable = 0; continue }
            if (substr(l,1,1) != "|") continue
            if (!intable) continue
            if (substr(l, length(l), 1) == "|") continue
            n++
            printf "  line %d opens a row and never closes it: %.55s...\n", i, l
        }
        exit (n > 0 ? 1 : 0)
    }' "$SMOKE_ROOT/docs/PLATFORMS.md") || true
if [ -z "$_sev" ]; then
    t_ok "no severed table cells in PLATFORMS.md (every row that opens, closes)"
else
    t_fail "table row(s) in PLATFORMS.md that open with | and never close:
$_sev
Everything after the last | renders as a paragraph BELOW the table, and the
cell itself is cut off mid-word. Either keep the cell on one line, or move the
long material into a \`###\` sub-section the way the Cygwin, WSL2 and FreeBSD
rows already do -- and do not guess which row a stray paragraph belongs to:
\`git log -S\` on a distinctive phrase names the commit that wrote it."
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

# --- 18: BUILD.md documents every NON-LINUX userland the matrix has built on --
# THE DEFECT, reported by a reader on 2026-09-19 and not by any check here:
# "BUILD.md doesn't mention FreeBSD, OpenBSD, NetBSD, nor illumos."  It did not.
# Four platforms carried a Verified or Partly-verified verdict on PLATFORMS.md,
# three of them Driven, and the build page named none of them -- its headings
# were Linux, macOS, Windows, and its at-a-glance table had three rows. Someone
# arriving on a BSD was told nothing, including the one thing that actually
# stops them: that `make` there is not GNU make.
#
# Checks 15-17 all passed throughout, because every one of them compares
# PLATFORMS.md against the README or against itself. Nothing held the BUILD page
# to the matrix, so the matrix grew four rows and the build page did not move.
#
# WHY THE UNIVERSE IS AN EXCLUSION AND NOT A LIST. The Driven register is the
# one flat, short-labelled enumeration of every row on the page (check 17 keeps
# it complete). From it we drop the LINUX family -- distributions, boards,
# architectures, libcs -- and what remains is the set of foreign userlands, each
# of which needs its own package manager and its own make. Written the other way
# round, as a list of the kernels we know about, a NEW kernel would be skipped
# silently; written as an exclusion, a new kernel is included by default and
# this check fails until the build page mentions it. A check should fail towards
# the work, not away from it.
#
# WHAT IT DOES NOT CHECK, stated so nobody reads more into a green line than is
# there: it asks only that the name APPEAR on the page. A passing mention in
# prose satisfies it -- "OpenBSD" did, before this revision, while the page
# still had no OpenBSD instructions. The floor is "a reader can find the word",
# which is the most a text lint can hold; whether the section is any good is a
# reviewer's job (docs/DOC_REVIEW.md), not this one's.
_uni=$(awk '
    { line[NR] = $0 }
    END {
        for (i = 1; i <= NR; i++) {
            if (line[i] ~ /^\| Row \| Driven\?/ && line[i+1] ~ /^\|[ :|-]+\|?[ :|-]*$/) {
                j = i + 2
                while (j <= NR && substr(line[j],1,1) == "|" && line[j] ~ /[^ \t|]/) {
                    cell = line[j]
                    sub(/^\| */, "", cell)
                    sub(/ *\|.*$/, "", cell)
                    print cell
                    j++
                }
                exit
            }
        }
    }' "$SMOKE_ROOT/docs/PLATFORMS.md" |
    sed 's/\*\*//g; s/([^)]*)//g; s/  */ /g; s/ *$//' |
    tr 'A-Z' 'a-z' |
    grep -E -v 'linux|debian|raspberry|arduino|musl|s390x|aarch64|x86-64|architecture')
# The distinctive token is the LAST word: "windows + cygwin" and "windows +
# msys2" both begin with the word the build page already has, and both were
# missing from it. "macos / darwin" -> darwin, "illumos / solaris" -> solaris.
_missing=''
_seen=0
for _row in $(echo "$_uni" | sed 's/.* //' | sort -u); do
    [ -n "$_row" ] || continue
    _seen=$((_seen + 1))
    if ! grep -i -q "$_row" "$SMOKE_ROOT/docs/BUILD.md"; then
        _missing="$_missing $_row"
    fi
done
if [ "$_seen" -ge 6 ] && [ -z "$_missing" ]; then
    t_ok "BUILD.md names all $_seen non-Linux userlands the matrix records"
else
    t_fail "BUILD.md is missing:$_missing (of $_seen non-Linux userlands in PLATFORMS.md).
A platform the matrix says jichi BUILDS on, with no build instructions on the
build page, is a reader on that platform being told nothing -- including that
their \`make\` is not GNU make. Add a section, or say plainly why there is none.
(A universe under 6 means the register parse broke; fix that before the prose.)"
fi

# --- 19: curl's own header is read under a relaxation of EXACTLY one TU -------
# THE DEFECT THIS EXISTS FOR, measured 2026-09-21 while driving the emulated
# architecture rows. libcurl's public header types `curl_off_t` as `long long`
# on every non-LP64 target -- the STOCK header says so, in a block predicated on
# __i386__ / __arm__ / __mips__ / __powerpc__ / __ILP32__ / __SIZEOF_LONG__ == 4
# -- and this tree compiles -std=c89 -pedantic, where `long long` is an
# extension. So `make WERROR=1` with libcurl fails INSIDE A THIRD-PARTY HEADER,
# on a line no first-party source can reach, on EVERY 32-bit platform. Measured
# by cross-compiling src/net/jc_http.c alone: x86, riscv32 and powerpc all fail,
# x86_64 is clean -- which is why no 64-bit row here has ever seen it, and why a
# 32-bit board with libcurl-dev cannot run the project's own gate today.
#
# Two fixes that do NOT work, both measured rather than assumed, so nobody
# retries them: the gnu89 fallback warns identically (-pedantic objects in
# either dialect), and libcurl 8.18 ignores --disable-largefile for the typedef.
#
# WHY THIS IS A LINT AND NOT A COMMENT. The fix has the M326u shape exactly --
# parts that are individually inert. A probe with no consumer relaxes nothing; a
# consumer with no probe relaxes it everywhere, unconditionally; and BOTH stay
# correct-looking while a second translation unit starts including <curl/curl.h>
# and silently falls outside the relaxation. That third part is the one no
# compiler on this bench can fail on, because the header is clean at 64 bits.
#
# The relaxation is scoped to ONE object for a reason worth stating: check 6
# forbids `long long` in first-party code by grep, and -Wno-long-long applied
# tree-wide would leave that rule enforced by nothing but that grep. Scoped, the
# compiler still refuses the type in all ~180 other units.
# The pattern spells the dot as [.] rather than \. on purpose: smoke_lint's
# forbidden-tool rule matches (curl)([ \t]|$), and inside a POSIX bracket
# expression \t is the two characters backslash and t -- so a BACKSLASH after
# `curl` reads to that rule as an invocation. Caught by the tier, on this file.
_curl_includers=$(grep -rl 'curl/curl[.]h' "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" 2>/dev/null \
                  | sed "s#^$SMOKE_ROOT/##" | sort)
_ncurl=$(printf '%s\n' "$_curl_includers" | grep -c . | tr -d ' ')
[ -n "$_curl_includers" ] || _ncurl=0
_mk_probe=0; _mk_consumer=0
grep -q 'CURL_HDR_CLEAN' "$mk" && _mk_probe=1
grep -q '^src/net/jc_http\.o: CFLAGS' "$mk" && _mk_consumer=1
if [ "$_ncurl" -eq 1 ] && [ "$_curl_includers" = "src/net/jc_http.c" ] \
   && [ "$_mk_probe" -eq 1 ] && [ "$_mk_consumer" -eq 1 ]; then
    t_ok "curl's header is included by src/net/jc_http.c alone, and the Makefile carries both the probe and its one consumer"
else
    t_fail "the scoped curl-header relaxation is not intact.
  first-party files including <curl/curl.h>: $_ncurl [$(printf '%s' "$_curl_includers" | tr '\n' ' ')]
  Makefile has the probe (CURL_HDR_CLEAN): $_mk_probe
  Makefile has the consumer (src/net/jc_http.o: CFLAGS): $_mk_consumer
All three must hold together. The probe decides whether curl's header is clean
under this build's dialect and warning set; the consumer applies -Wno-long-long
to the ONE object that reads that header; and the count is what keeps the
scoping true -- a second includer compiles outside the relaxation and breaks
\`make WERROR=1\` on every 32-bit platform, where nothing on this bench compiles.
(A count of 0 means the extraction broke, not that the problem is solved.)"
fi

# --- 20: uname() SUCCEEDS WITH ANY NON-NEGATIVE VALUE, so it is never tested
#         against zero ------------------------------------------------------
#
# THE DEFECT, measured 2026-09-22 on OmniOS r151058 and found only by running
# there. POSIX: "upon successful completion, a non-negative value shall be
# returned" -- it does not say zero, and illumos returns a POSITIVE value.
# Linux, FreeBSD, NetBSD, OpenBSD, Cygwin and MSYS2 all return 0, so four call
# sites written as `uname(&u) == 0` were right on every row this project had
# ever run and wrong on the first SysV kernel it met. The visible cost: doctor
# said "host platform not recognised" on illumos, which made M695's whole
# platform-verdict feature dead there -- `jc_sys_partly[]` names "SunOS" and
# that entry could not be reached. `tests/smoke/doctor.sh` failed correctly.
#
# WHY A LINT AND NOT A FIX ALONE. The four sites were written months apart by
# the same reflex, and `== 0` for a syscall is the correct idiom for most of
# them -- so the next `uname()` will be written the same way, on a bench where
# it passes. This is the M449 shape: a probe that answers correctly for the
# wrong reason on every platform you own.
#
# THE UNIVERSE is first-party C under src/ and include/. The floor is today's
# exact call-site count: a pattern that reads nothing would otherwise pass with
# an empty set, which is the failure mode this tier has a rule against.
_un_files=$(grep -rl 'uname(&' "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" 2>/dev/null | sort)
_un_sites=$(grep -rhn 'uname(&' "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" 2>/dev/null | grep -c .)
# The offenders: a uname() call compared against zero, either direction.
_un_bad=$(grep -rn 'uname(&[A-Za-z_]*)[ ]*[!=]=[ ]*0' "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" 2>/dev/null \
          | sed "s#^$SMOKE_ROOT/##")
_un_nbad=$(printf '%s\n' "$_un_bad" | grep -c . | tr -d ' ')
[ -n "$_un_bad" ] || _un_nbad=0
if [ "$_un_sites" -lt 4 ]; then
    t_fail "uname() call-site extraction found $_un_sites sites (floor 4) -- the \
pattern reads nothing, so this check would pass on an empty set. Fix the \
extraction, not the floor. Files seen: $(printf '%s' "$_un_files" | tr '\n' ' ')"
elif [ "$_un_nbad" -eq 0 ]; then
    t_ok "all $_un_sites uname() call sites test the result as < 0 / >= 0, never against zero"
else
    t_fail "$_un_nbad of $_un_sites uname() call site(s) test the result against ZERO:
$(printf '%s' "$_un_bad" | sed 's/^/    /')
POSIX returns a NON-NEGATIVE value on success and illumos returns a positive
one, so '== 0' reads a successful call as a failure there. Use '< 0' for the
error test and '>= 0' for the success test. Measured on OmniOS r151058: this
made doctor report \"host platform not recognised\" on a platform PLATFORMS.md
partly-verifies, and silently disabled the M695 verdict table."
fi

# --- 25: a verdict that asks for a report says where the procedure is (M716) -
# doctor and the setup wizard are where a person on an unmeasured platform finds
# out -- "from the program, not from a page they did not open" (PLATFORMS.md).
# Both used to end in "please report it" and name no procedure. The five
# non-verified messages (doctor: never / partly / not recognised; setup: partly /
# never) now name docs/VERIFY_A_PLATFORM.md. This pins the pointer AND its
# target: a renamed page would otherwise leave five messages sending people to a
# file that does not exist, which no other gate would notice.
#
# NOT a general "every docs path in src exists" lint, deliberately: measured at
# M716, 9 of the 36 docs/*.md paths quoted in src/ are paths in the USER's
# project that jichi scaffolds (docs/DESIGN.md, docs/REQUIREMENTS.md ...), so a
# universe-wide gate would be wrong by construction.
_vpage="$SMOKE_ROOT/docs/VERIFY_A_PLATFORM.md"
_vptr=$(grep -c 'docs/VERIFY_A_PLATFORM\.md' "$_dsrc" 2>/dev/null)
if [ -f "$_vpage" ] && [ "${_vptr:-0}" -ge 5 ]; then
    t_ok "the five non-verified platform messages name docs/VERIFY_A_PLATFORM.md, and it exists"
else
    t_fail "platform messages naming docs/VERIFY_A_PLATFORM.md: ${_vptr:-0} (want 5: doctor \
never/partly/not-recognised, setup partly/never); page present: \
$([ -f "$_vpage" ] && echo yes || echo NO). A person on an unmeasured platform is \
told to report what they find; this is where they learn how."
fi

# --- 26: every MANDATORY warning flag is one the oldest measured gcc accepts --
# THE DEFECT (M722). gcc rejects an unknown -W option as an ERROR, with or
# without -Werror -- the same way it rejects a made-up flag. M472 put -Walloca on
# the mandatory WARN list, measured against gcc 13 and clang 18, which both know
# it; it arrived in GCC 7. From then on no gcc before 7 could compile one file of
# this tree, and nothing in the gate could notice, because every compiler the gate
# runs knows the flag. INSTALL.md promised CentOS 6 and Debian 7 throughout; two
# recorded rows (CentOS 7's gcc 4.8.5, Debian 9's gcc 6) predate M472. Found by the
# FreeMiNT step 0 cross-compile, gcc 4.6.4: 314 compiles, all dead in 1.1 s.
#
# The baseline is MEASURED, not recalled: exactly the mandatory flags gcc 4.6.4
# compiled 299 files with in that step (docs/plans/2026-09-freemint-aranym.md §8).
# NARROWED by the userland ladder (2026-09-24): Debian 4's gcc 4.1.2 rejected all
# 171 compiles on -Wvla, which gcc gained in 4.3, so -Wvla left this list for
# WARN_OPTIONAL and the baseline is now what gcc 4.1.2 accepts -- the oldest gcc
# measured compiling this tree.
# A flag outside it goes through cc_warn_ok into WARN_OPTIONAL, so a compiler that
# lacks it builds without it -- or, if an old gcc is shown to accept it, it joins
# this list with that evidence. The universe is the first `WARN =` block and its
# continuation lines; `WARN += -Werror` (WERROR=1) is outside it on purpose, and
# every gcc has -Werror. Floor: today's exact count, so an extraction that stops
# reading fails instead of passing an empty set.
_warn_base='-Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wpointer-arith -Wredundant-decls -Wundef'
_warn_mand=$(awk '
    /^WARN[ \t]*=/ { on = 1 }
    on {
        line = $0
        gsub(/\$\([^)]*\)/, "", line)
        n = split(line, w, /[ \t\\]+/)
        for (i = 1; i <= n; i++) if (w[i] ~ /^-W/) print w[i]
        if ($0 !~ /\\[ \t]*$/) exit
    }' "$mk")
_warn_n=$(printf '%s\n' "$_warn_mand" | grep -c -- '^-W')
_warn_bad=''
for _f in $_warn_mand; do
    case " $_warn_base " in
        *" $_f "*) ;;
        *) _warn_bad="$_warn_bad $_f" ;;
    esac
done
if [ "$_warn_n" -ge 8 ] && [ -z "$_warn_bad" ]; then
    t_ok "all $_warn_n mandatory warning flags are ones gcc 4.1.2 compiled this tree with"
else
    t_fail "mandatory warning flag(s) outside the measured gcc 4.1.2 baseline:${_warn_bad:- none} \
(extracted $_warn_n, floor 8: $(printf '%s ' $_warn_mand)) -- an old gcc rejects an \
unknown -W option as an ERROR, so probe it into WARN_OPTIONAL with cc_warn_ok (M722)"
fi

# --- 27: ...and -Walloca and -Wvla are still PROBED, so the compilers that have
# them keep them --
# Check 26 alone passes if the flag is deleted outright. That would trade one
# defect for another: M472 added -Walloca as a tripwire (alloca is not C89 at
# all), and on gcc >= 7 and clang it should keep firing. So the fix is pinned in
# the form it took -- a cc_warn_ok call inside WARN_OPTIONAL.
_walloca=$(awk '
    /^WARN_OPTIONAL[ \t]*:?=/ { on = 1 }
    on {
        if ($0 ~ /cc_warn_ok,-Walloca\)/) a = 1
        if ($0 ~ /cc_warn_ok,-Wvla\)/) v = 1
        if ($0 !~ /\\[ \t]*$/) exit
    }
    END { if (a && v) print "probed" }' "$mk")
if [ "$_walloca" = "probed" ]; then
    t_ok "-Walloca and -Wvla are probed into WARN_OPTIONAL, so the compilers that know them keep the tripwires"
else
    t_fail "-Walloca or -Wvla is not probed in WARN_OPTIONAL: either it went back on the \
mandatory list (check 26) or it was dropped, and the compilers that know it lost a \
tripwire (M722; -Wvla since the userland ladder)"
fi

# --- 28-31 (M723): what FreeMiNT's MiNTLib showed the tree assumed ------------
# MiNTLib is an older glibc derivative, and the FreeMiNT step 0 cross-compile
# (docs/plans/2026-09-freemint-aranym.md §8) found four places where the tree
# leaned on glibc's headers rather than on POSIX's promises. Each check below
# holds one of them for every file, not for the files that happened to fail.
_c_universe=$(find "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" "$SMOKE_ROOT/tests" \
    -name '*.[ch]' -type f 2>/dev/null | sort)

# 28: a file that declares a struct timeval includes <sys/time.h>. MiNTLib's
# <sys/select.h> only forward-declares it, so select()'s own header is not
# enough there; <sys/time.h> defines it on every libc. Ten files failed on MiNT;
# the four that already included it compiled.
_tv_n=0; _tv_bad=''
for _f in $(grep -l 'struct timeval' $_c_universe 2>/dev/null); do
    _tv_n=$((_tv_n + 1))
    grep -q '^#include <sys/time\.h>' "$_f" || _tv_bad="$_tv_bad ${_f#"$SMOKE_ROOT"/}"
done
if [ "$_tv_n" -ge 14 ] && [ -z "$_tv_bad" ]; then
    t_ok "all $_tv_n files that declare a struct timeval include <sys/time.h>"
else
    t_fail "struct timeval without <sys/time.h>:${_tv_bad:- none} ($_tv_n files, floor 14) -- \
MiNTLib's <sys/select.h> only forward-declares it (M723)"
fi

# 29: a file that uses pid_t includes <sys/types.h>. POSIX says <unistd.h>
# defines it too; MiNTLib's does so only under an X/Open feature level, and
# tests/test_proc.c, which had <unistd.h> and <signal.h> and nothing else, was
# the file that failed. <sys/types.h> is pid_t's home on every libc.
_pid_n=0; _pid_bad=''
for _f in $(grep -lw 'pid_t' $_c_universe 2>/dev/null); do
    _pid_n=$((_pid_n + 1))
    grep -q '^#include <sys/types\.h>' "$_f" || _pid_bad="$_pid_bad ${_f#"$SMOKE_ROOT"/}"
done
if [ "$_pid_n" -ge 16 ] && [ -z "$_pid_bad" ]; then
    t_ok "all $_pid_n files that use pid_t include <sys/types.h>"
else
    t_fail "pid_t without <sys/types.h>:${_pid_bad:- none} ($_pid_n files, floor 16) -- \
MiNTLib's <unistd.h> declares it only under an X/Open level (M723)"
fi

# 30: <sys/mman.h> is included only where JC_NO_MMAP can switch it off, and the
# probe that sets JC_NO_MMAP exists. FreeMiNT has neither the header nor mmap;
# jc_index already reads a copy when a mapping fails (M141), so the guard is the
# whole of the port -- and without the probe the guard is never switched.
_mm_n=0; _mm_bad=''
for _f in $(grep -l '^#include <sys/mman\.h>' $_c_universe 2>/dev/null); do
    _mm_n=$((_mm_n + 1))
    awk '/^#include <sys\/mman\.h>/ { exit (prev ~ /^#ifndef JC_NO_MMAP/) ? 0 : 1 } { prev = $0 }' \
        "$_f" || _mm_bad="$_mm_bad ${_f#"$SMOKE_ROOT"/}"
done
_mm_probe=$(grep -c 'STD += -DJC_NO_MMAP' "$mk")
if [ "$_mm_n" -ge 1 ] && [ -z "$_mm_bad" ] && [ "$_mm_probe" -eq 1 ]; then
    t_ok "every <sys/mman.h> include ($_mm_n) sits under #ifndef JC_NO_MMAP, and the Makefile's probe sets it"
else
    t_fail "mmap is not switchable: unguarded <sys/mman.h> in:${_mm_bad:- none} ($_mm_n \
files); Makefile lines setting -DJC_NO_MMAP: $_mm_probe (want 1) (M723)"
fi

# 31: the X/Open probe asks whether lstat is DECLARED, not whether it links.
# The symbols ARE in MiNT's libc and only the declarations are hidden, so a probe
# without -Werror=implicit-function-declaration says yes there and the build
# then fails -- measured with gcc 4.6.4, and M449's lesson (uClibc-ng's
# malloc_trim) in a new place.
_lst_lines=$(awk '/printf .\$\(LSTAT_PROBE\)/ { getline nxt; print nxt }' "$mk")
_lst_n=$(printf '%s\n' "$_lst_lines" | grep -c -- '-xc -')
_lst_nowerr=$(printf '%s\n' "$_lst_lines" | grep -- '-xc -' | grep -vc -- '-Werror=implicit-function-declaration')
if [ "$_lst_n" -ge 2 ] && [ "$_lst_nowerr" -eq 0 ] && grep -q 'STD += -D_XOPEN_SOURCE=600' "$mk"; then
    t_ok "both lstat probes ($_lst_n) carry -Werror=implicit-function-declaration, and a hidden lstat adds -D_XOPEN_SOURCE=600"
else
    t_fail "the lstat probe is missing, links without -Werror=implicit-function-declaration \
($_lst_nowerr of $_lst_n), or has no consumer -- on MiNT it would answer yes and the build fail (M723, M449)"
fi

# --- 32: a MiNT build carries the stack the suite was measured to need (M726) ---
# FreeMiNT gives a program a FIXED stack of `_stksize` bytes, 64 KB by MiNTLib's
# default, and it never grows. The unit suite overran it in the guest and wrote
# over the environment beside it (bus errors in getenv and setenv, reading
# 0x78787878). The fix is one definition in the platform layer, and the way it
# fails is silently: my first placement sat inside `#if defined(__APPLE__)`,
# compiled to nothing on MiNT, and the build stayed green. So this holds the
# block at CONDITIONAL DEPTH ZERO, and holds its size to the measured floor.
_plat="$SMOKE_ROOT/src/platform/jc_platform_posix.c"
_stk=$(awk '
    /^[ \t]*#[ \t]*if/ { depth++; if ($0 ~ /^[ \t]*#[ \t]*ifdef[ \t]+__MINT__/) { at = depth; mint_open = (depth == 1) } }
    /^[ \t]*#[ \t]*endif/ { depth-- }
    /^long _stksize = / && at > 0 {
        v = $0; sub(/^long _stksize = /, "", v); sub(/;.*$/, "", v); gsub(/L/, "", v)
        n = split(v, f, /[ \t]*\*[ \t]*/); prod = 1; for (i = 1; i <= n; i++) prod *= f[i]
        print (mint_open ? "top" : "nested"), prod; exit
    }' "$_plat")
_stk_where=${_stk%% *}
_stk_bytes=${_stk##* }
if [ "$_stk_where" = "top" ] && [ "${_stk_bytes:-0}" -ge 524288 ] 2>/dev/null; then
    t_ok "MiNT builds define _stksize = $_stk_bytes bytes at the top level of the platform layer"
else
    t_fail "MiNT stack: ${_stk:-no _stksize definition under #ifdef __MINT__} -- it must sit at \
conditional depth zero (not inside another #if) and be at least 512 KB, the stack \`make ci\` \
runs the curl-free suite under (M726, M727)"
fi

# --- 33: the gate runs the suite under the stack a MiNT build gets (M727) -----
# Check 32 holds the size; this holds the gate to it. A host with an 8 MB stack
# cannot see a call chain that MiNT's fixed stack cannot hold: the path resolver
# took ~700 KB for a symlink cycle, and only the guest crashed. So `make ci` runs
# the curl-free suite -- the build FreeMiNT runs -- under `ulimit -s`. A limit
# above _stksize would pass what MiNT cannot run, and no limit passes anything.
# The ci recipe's continuation lines are joined, so the limit must be on the
# same logical line as the HAVE_CURL= build it runs.
_ul=$(awk '
    /^ci:/ { in_ci = 1; next }
    in_ci && /^[^\t#]/ { in_ci = 0 }
    in_ci && /^\t/ {
        line = line $0
        if (line ~ /\\$/) { sub(/\\$/, "", line); next }
        if (line ~ /HAVE_CURL= / && line ~ /ulimit -s [0-9]/) {
            v = line; sub(/.*ulimit -s /, "", v); sub(/[^0-9].*$/, "", v); print v; n++
        }
        line = ""
    }
    END { if (n != 1) print "found=" n + 0 }' "$mk")
case $_ul in
    *found=*|"") _ul_ok=0 ;;
    *) if [ "$((_ul * 1024))" -le "${_stk_bytes:-0}" ] 2>/dev/null; then _ul_ok=1; else _ul_ok=0; fi ;;
esac
if [ "$_ul_ok" -eq 1 ]; then
    t_ok "make ci runs the curl-free suite under ulimit -s $_ul (KB), within MiNT's _stksize of $_stk_bytes bytes"
else
    t_fail "the ci recipe's curl-free suite must run under exactly one \`ulimit -s N\` with N KB \
at most MiNT's _stksize (${_stk_bytes:-unknown} bytes); read: $(printf '%s' "$_ul" | tr '\n' ' ') (M727)"
fi

# --- 34: every libcurl identifier newer than the documented floor is guarded ---
# THE DEFECT (the userland ladder, 2026-09-24). INSTALL.md promised libcurl
# 7.19.4 and CentOS 6 / Debian 7 as the floor, and nothing had built there. The
# first build on each failed in src/net/jc_http.c: CURL_SSLVERSION_TLSv1_2
# (7.34.0), CURL_SOCKOPT_OK (7.21.5) and CURL_SEEKFUNC_CANTSEEK (7.19.5) were used
# bare. Every libcurl the gate meets is 8.x, so nothing in it could see them.
# THE UNIVERSE: every CURL* identifier in src/ and include/, comments removed. The
# table below says when libcurl introduced each one, generated from libcurl 8.18.0's
# docs/libcurl/symbols-in-versions for exactly the identifiers the tree uses
# (CURLcode, a type the table does not list, is entered as 7.1); an identifier
# missing from it fails, so a new one is a decision rather than an accident. THE RULE, for one newer than 7.19.4:
# the file defines a fallback for it (#ifndef ID / #define ID), or every use sits
# inside an #if that names LIBCURL_VERSION_NUM. Floor: today's 53 identifiers.
cat > "$tmp/curlv" <<'CURLV'
CURLE_ABORTED_BY_CALLBACK 7.1
CURLE_OK 7.1
CURLE_OPERATION_TIMEDOUT 7.10.2
CURLE_WRITE_ERROR 7.1
CURLINFO_CONNECT_TIME 7.4.1
CURLINFO_REDIRECT_URL 7.18.2
CURLINFO_RESPONSE_CODE 7.10.8
CURLOPT_CONNECTTIMEOUT 7.7
CURLOPT_EXPECT_100_TIMEOUT_MS 7.36.0
CURLOPT_FOLLOWLOCATION 7.1
CURLOPT_HEADERDATA 7.10
CURLOPT_HEADERFUNCTION 7.7.2
CURLOPT_HTTPHEADER 7.1
CURLOPT_LOW_SPEED_LIMIT 7.1
CURLOPT_LOW_SPEED_TIME 7.1
CURLOPT_MAXREDIRS 7.5
CURLOPT_NOPROGRESS 7.1
CURLOPT_OPENSOCKETDATA 7.17.1
CURLOPT_OPENSOCKETFUNCTION 7.17.1
CURLOPT_POST 7.1
CURLOPT_POSTFIELDS 7.1
CURLOPT_POSTFIELDSIZE 7.2
CURLOPT_PROGRESSDATA 7.1
CURLOPT_PROGRESSFUNCTION 7.1
CURLOPT_PROTOCOLS 7.19.4
CURLOPT_PROTOCOLS_STR 7.85.0
CURLOPT_READDATA 7.9.7
CURLOPT_READFUNCTION 7.1
CURLOPT_REDIR_PROTOCOLS 7.19.4
CURLOPT_REDIR_PROTOCOLS_STR 7.85.0
CURLOPT_SEEKDATA 7.18.0
CURLOPT_SEEKFUNCTION 7.18.0
CURLOPT_SOCKOPTDATA 7.16.0
CURLOPT_SOCKOPTFUNCTION 7.16.0
CURLOPT_SSLVERSION 7.1
CURLOPT_SSL_VERIFYHOST 7.8.1
CURLOPT_SSL_VERIFYPEER 7.4.2
CURLOPT_TIMEOUT 7.1
CURLOPT_URL 7.1
CURLOPT_USERAGENT 7.1
CURLOPT_WRITEDATA 7.9.7
CURLOPT_WRITEFUNCTION 7.1
CURLOPT_XFERINFODATA 7.32.0
CURLOPT_XFERINFOFUNCTION 7.32.0
CURLPROTO_HTTP 7.19.4
CURLPROTO_HTTPS 7.19.4
CURLSOCKTYPE_IPCXN 7.16.0
CURL_GLOBAL_ALL 7.8
CURL_SEEKFUNC_CANTSEEK 7.19.5
CURL_SOCKET_BAD 7.14.0
CURL_SOCKOPT_OK 7.21.5
CURL_SSLVERSION_TLSv1 7.9.2
CURL_SSLVERSION_TLSv1_2 7.34.0
CURLcode 7.1
CURLV
_cv=$(cd "$ROOT" && find src include -name '*.[ch]' | sort | while read -r f; do
    awk -v F="$f" -v T="$tmp/curlv" '
        BEGIN {
            while ((getline l < T) > 0) {
                split(l, a, " "); split(a[2], v, ".")
                ver[a[1]] = v[1] * 10000 + v[2] * 100 + v[3]
            }
        }
        { src[NR] = $0 }
        END {
            inc = 0; depth = 0
            for (i = 1; i <= NR; i++) {
                line = src[i]; out = ""
                # strip comments, which may span lines
                while (length(line) > 0) {
                    if (inc) {
                        p = index(line, "*/")
                        if (p == 0) { line = ""; break }
                        line = substr(line, p + 2); inc = 0
                    } else {
                        p = index(line, "/*")
                        if (p == 0) { out = out line; line = ""; break }
                        out = out substr(line, 1, p - 1); line = substr(line, p + 2); inc = 1
                    }
                }
                if (out ~ /^[ \t]*#[ \t]*if/) { depth++; cond[depth] = out }
                else if (out ~ /^[ \t]*#[ \t]*elif/) { cond[depth] = out }
                else if (out ~ /^[ \t]*#[ \t]*endif/) { if (depth > 0) depth-- }
                if (out ~ /^[ \t]*#[ \t]*ifndef[ \t]+CURL/) {
                    split(out, w, /[ \t]+/); fb[w[length(w)]] = 1
                }
                code[i] = out; d[i] = depth
                for (k = 1; k <= depth; k++) g[i, k] = cond[k]
            }
            for (i = 1; i <= NR; i++) {
                rest = code[i]
                while (match(rest, /CURL[A-Za-z0-9_]+/)) {
                    id = substr(rest, RSTART, RLENGTH)
                    before = (RSTART > 1) ? substr(rest, RSTART - 1, 1) : ""
                    rest = substr(rest, RSTART + RLENGTH)
                    # a word of its own: LIBCURL_VERSION_NUM is not CURL_VERSION_NUM
                    if (before ~ /[A-Za-z0-9_]/) continue
                    if (!(id in ver)) { print "UNKNOWN " id " " F ":" i; continue }
                    if (ver[id] <= 71904) continue
                    ok = (id in fb)
                    for (k = 1; k <= d[i] && !ok; k++) if (g[i, k] ~ /LIBCURL_VERSION_NUM/) ok = 1
                    if (!ok) print "BARE " id " " F ":" i
                    else print "GUARDED " id
                }
            }
            for (id in ver) seen = seen
        }' "$f"
done)
_cv_bad=$(printf '%s\n' "$_cv" | grep -e '^UNKNOWN' -e '^BARE' | sort -u)
_cv_guarded=$(printf '%s\n' "$_cv" | grep -c '^GUARDED' || true)
_cv_ids=$(cd "$ROOT" && find src include -name '*.[ch]' -exec cat {} + 2>/dev/null \
    | grep -o -E '(^|[^A-Za-z0-9_])CURL[A-Za-z0-9_]*' | sed 's/^[^C]*//' | sort -u | grep -c .)
if [ -z "$_cv_bad" ] && [ "$_cv_ids" -ge 53 ] && [ "$_cv_guarded" -ge 5 ]; then
    t_ok "every libcurl identifier newer than 7.19.4 is guarded or has a fallback ($_cv_guarded uses; $_cv_ids identifiers in the table's universe)"
else
    t_fail "libcurl identifier(s) the documented floor lacks, used bare or missing from the table: \
$(printf '%s ' $_cv_bad) (identifiers $_cv_ids, floor 53; guarded uses $_cv_guarded, floor 5) -- \
guard with LIBCURL_VERSION_NUM or define a fallback, and add a new identifier to the table \
with its version from curl's symbols-in-versions (the userland ladder, 2026-09-24)"
fi

# --- 35: no git subcommand newer than the oldest git the tests are run on ------
# THE DEFECT (V2f, 2026-09-24). undo_across_branch.sh made its fixture with
# `git switch`, which is git 2.23 (2019); on Debian 9's git 2.11 the branch was
# never made, check 2 passed with nothing to test, and checks 3 and 5 failed for
# the fixture's reason, not the product's. jichi itself calls neither. THE
# UNIVERSE: the smoke drivers and scripts/, comments excluded; `git restore` is
# the same release's other new verb. `git checkout` does both jobs everywhere.
_gnew=$(cd "$ROOT" && grep -n -E '(^|[^#[:alnum:]_-])git +(switch|restore)( |$)' \
    tests/smoke/*.sh scripts/*.sh 2>/dev/null | grep -v -E '^[^:]*:[0-9]+:[[:space:]]*#' | head -n 5)
if [ -z "$_gnew" ]; then
    t_ok "no smoke driver or script uses git switch/restore (git 2.23; Debian 9 has 2.11)"
else
    t_fail "git 2.23 verbs where git 2.11 must work -- use git checkout: $_gnew"
fi

# --- 36: /usr/bin/grep only with its fallback ----------------------------------
# THE DEFECT (V2f, 2026-09-24). i18n_tracks_lint.sh called /usr/bin/grep by its
# absolute path, and Debian 9 predates the merged /usr: grep is /bin/grep there,
# and the lint died of "not found". 25 drivers already use the absolute path the
# guarded way -- `G=/usr/bin/grep` then `[ -x "$G" ] || G=grep` -- because in an
# agent session bare `grep` can be a shell function (CLAUDE.md). THE UNIVERSE:
# every /usr/bin/grep in a smoke driver, comments excluded; the only line allowed
# to name it is the assignment, and the next line must be the fallback. This file
# is outside it: it names the path in its own patterns.
_ubg=$(cd "$ROOT" && for f in tests/smoke/*.sh; do
    [ "$f" = tests/smoke/portability_lint.sh ] && continue
    awk -v F="$f" '
        /^[[:space:]]*#/ { prev = $0; next }
        index($0, "/usr/bin/grep") {
            if ($0 ~ /^[[:space:]]*G=\/usr\/bin\/grep[[:space:]]*$/) { want = NR + 1; next }
            print F ":" NR
        }
        NR == want && $0 !~ /\[ -x "\$G" \] [|][|] G=grep/ { print F ":" NR " (no fallback after G=)" }
    ' "$f"
done)
_ubg_n=$(cd "$ROOT" && grep -l '^[[:space:]]*G=/usr/bin/grep' tests/smoke/*.sh | grep -c .)
if [ -z "$_ubg" ] && [ "$_ubg_n" -ge 26 ]; then
    t_ok "every /usr/bin/grep in the smoke tier is the guarded G= form ($_ubg_n drivers)"
else
    t_fail "/usr/bin/grep without its fallback (a system without the merged /usr has no such file): \
$(printf '%s ' $_ubg) (guarded drivers $_ubg_n, floor 26)"
fi

t_done
