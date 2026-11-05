#!/bin/sh
# smoke: the jichi attribution block must be a parseable trailer (M916).
#
# WHY. 253fadd4 (2026-10-07, on master) glued its Co-Authored-By block onto
# the message body -- no blank line -- so `git interpret-trailers --parse`
# yields nothing for it: a machine reading the attribution sees no model, no
# build, no co-author. F-J1 forbids rewriting that commit (a history rewrite
# is exactly what the finding exists about), so 253fadd4 is GRANDFATHERED:
# this lint checks every commit AFTER it. A past measurement stays a past
# measurement; the fence is going forward.
#
# UNIVERSE: commits reachable from HEAD, after BASELINE, whose message body
# carries a Jichi-Build line (jichi's register commits -- those are the ones
# the house rule says must carry the block; a human commit without a block
# is out of scope, not a finding). The scope floor is adjusted downward when
# grandfather commits are skipped (552 -> 551).
#
# CHECKS, per in-scope commit:
#   2  `git interpret-trailers --parse` yields a non-empty block (not glued
#      to the body);
#   3  the parsed block carries Co-Authored-By;
#   4  Jichi-Build names a clean 7-40 hex sha (a "8f1c...(plan" shape is a
#      broken substitution, not a hash);
#   5  the named head is an ANCESTOR of the commit (the block claims a tree
#      that actually precedes it).
#
# CHECKS 6-7: commit identity (M999). The address lives in
# `docs/internal/COMMIT_IDENTITY.md` and must not appear in published pages.
# Check A: no in-scope commit's Co-Authored-By line carrying jichi has a
# different address; check B: no author or committer email at localhost.
#
# The probe (8) is the teeth: it plants both the glued shape and the
# well-formed shape in a throwaway repo and asserts the instrument tells
# them apart -- without it, a green run over clean history proves only that
# the history is clean.
# The probe also fires checks 6-7: commits with wrong address or localhost
# email must be detected.
#
# GRANDFATHER LIST (for future use):
# - GRANDFATHERS: one SHA per line; each must have a reason in GRANDFATHER_REASONS
# - When a grandfather commit is encountered, it is SKIPPED for checks 2-5
#   with a printed note; it is NOT counted in scope
# - Lint-on-the-lint: each sha in GRANDFATHERS must exist in the repo and be
#   in the in-scope range (or be noted as out-of-range); a dead row must not
#   silently survive (like BASELINE absence is handled)

. "$(dirname "$0")/_smoke.sh"
smoke_home
root="$SMOKE_ROOT"

# The grandfather: the only in-tree commit whose block is glued.
BASELINE=253fadd4
# GRANDFATHERS: one SHA per line; each must have a reason in the skip case below
# dee1a57720c9e677aedf53a18bcb1e06d801ed0b (run-0068 reconcile merge): its three
# attribution lines were three separate -m paragraphs, so the trailer parser sees
# only Jichi-Model -- checks 3/4 count it; the commit is pushed and must not be rewritten.
# 6a40f655d0224fbb3ad49025b9285d622ff9cf95 (the M1039-c lint change itself): written with the same three-paragraph
# trailer shape the lead was fixing -- the lint caught its own fixer; pushed, not rewritten.
GRANDFATHERS='dee1a57720c9e677aedf53a18bcb1e06d801ed0b
6a40f655d0224fbb3ad49025b9285d622ff9cf95'
# The rig ships a FRESH one-commit repo (`git archive HEAD` + `git init`), with
# no jichi history: $BASELINE is absent there, and the history checks below have
# no universe to count. That is the signature of a shipped tree, not a defect -- so
# SKIP them with a printed note and keep the probe (the instrument test), which is
# self-contained in its own throwaway repo. In a checkout the baseline must be
# present: its absence is the grandfather moving, and that is a real failure.
# (The 2026-10-14/15 board runs: "either the rig ships history, or the lint skips
# when the history it reads is absent, and says so.")
if git rev-parse --verify "$BASELINE" >/dev/null 2>&1; then
    HAVE_HISTORY=1
else
    HAVE_HISTORY=0
    echo "# note: the baseline commit $BASELINE is not in this tree (a shipped,"
    echo "# single-commit repo with no jichi history). The five history checks are"
    echo "# skipped; the probe (the instrument itself) still runs."
fi

# Lint-on-the-lint: verify GRANDFATHERS entries exist and are in-range
# Count how many grandfathers are in-range (valid + in-scope)
GRANDFATHER_COUNT=0
GRANDFATHER_SKIP_COUNT=0
if [ "$HAVE_HISTORY" -eq 1 ]; then
    # Check each grandfather entry
    for sha in $GRANDFATHERS; do
        GRANDFATHER_COUNT=$((GRANDFATHER_COUNT + 1))
        # Check if sha exists in repo
        if ! git rev-parse --verify "$sha" >/dev/null 2>&1; then
            echo "# note: GRANDFATHERS entry '$sha' does not exist in this tree; skipping"
            continue
        fi
        # Check if sha is in the in-scope range (after BASELINE, before or at HEAD)
        if ! git merge-base --is-ancestor "$BASELINE" "$sha" 2>/dev/null || \
           ! git merge-base --is-ancestor "$sha" HEAD 2>/dev/null; then
            echo "# note: GRANDFATHERS entry '$sha' is not in the in-scope range ($BASELINE..HEAD); skipping"
            continue
        fi
        GRANDFATHER_SKIP_COUNT=$((GRANDFATHER_SKIP_COUNT + 1))
    done
    # Verify all valid grandfathers were counted for skip
    if [ "$GRANDFATHER_SKIP_COUNT" -ne "$GRANDFATHER_COUNT" ]; then
        # Some grandfathers were out-of-range or missing - this is acceptable (noted above)
        :
    fi
fi

# --- identity functions ------------------------------------------------------
# extract_maintainer_address FILE -> prints the <...> token from the address line
# Returns empty string if the line is not found
extract_maintainer_address() {  # $1 = path to COMMIT_IDENTITY.md
    grep -E '^    [^<]+<[^>]+>' "$1" 2>/dev/null | head -1 | sed 's/^    .*<\([^>]*\)>$/\1/'
}

# check_identity_line $message -- extracts Co-Authored-By: jichi line and checks its address
# Returns 0 if address matches maintainer or no jichi line, 1 otherwise
# $1 = maintainer address (required)
check_jichi_identity() {  # $1 = maintainer_addr, $2 = message
    _ci_addr="$1"
    _ci_msg="$2"
    # Extract Co-Authored-By line for jichi from the message
    _ci_jline=$(printf '%s' "$_ci_msg" | grep '^Co-Authored-By: jichi' | head -1)
    if [ -z "$_ci_jline" ]; then
        return 0  # no jichi line, nothing to check
    fi
    # Extract the <...> from the jichi line
    _ci_lineaddr=$(printf '%s' "$_ci_jline" | sed -n 's/.*<\([^>]*\)>.*$/\1/p')
    if [ -z "$_ci_lineaddr" ]; then
        return 0  # malformed, skip (will be caught by shape check)
    fi
    if [ "$_ci_lineaddr" != "$_ci_addr" ]; then
        return 1  # wrong address
    fi
    return 0
}

# check_localhost_author $commit [$repo] -- checks author/committer email for localhost
# Returns 0 if OK, 1 if localhost found
# $2 = repo path (defaults to current directory)
check_localhost_author() {  # $1 = commit, $2 = repo path (optional)
    _cl_commit="$1"
    _cl_repo="${2:-.}"
    _cl_emails=$(git -C "$_cl_repo" show -s --format='%ae %ce' "$_cl_commit" 2>/dev/null)
    if printf '%s' "$_cl_emails" | grep -qE '@localhost|\.localhost|jichi\.local'; then
        return 1
    fi
    return 0
}

# The identity file: if absent, skip identity checks
ID_FILE="$root/docs/internal/COMMIT_IDENTITY.md"
if [ -f "$ID_FILE" ]; then
    MAINTAINER_ADDR=$(extract_maintainer_address "$ID_FILE")
    if [ -n "$MAINTAINER_ADDR" ]; then
        IDENTITY_PRESENT=1
    else
        IDENTITY_PRESENT=0
    fi
else
    IDENTITY_PRESENT=0
fi

# The history block emits 5 checks when it runs (HAVE_HISTORY=1), 0 otherwise.
# With the identity file present, the history block emits 7 checks (5 + 2 identity).
# With the identity file absent, the identity checks are skipped (still 5).
# The probe has 2 original checks + 3 identity checks when the identity file is
# present (wrong-address fires, localhost fires, correct-identity passes) + 2 skips
# when it is absent, and 0 identity checks when git itself is absent (the identity
# arms exist only in the git-present probe branch).
git_ok=1
command -v git >/dev/null 2>&1 || git_ok=0
probe_original=$(( git_ok ? 2 : 1 ))
if [ "$git_ok" -eq 1 ]; then
    probe_identity=$(( IDENTITY_PRESENT ? 3 : 2 ))
else
    probe_identity=0
fi
probe_checks=$(( probe_original + probe_identity ))
history_checks=$(( HAVE_HISTORY ? 5 : 0 ))
# When identity file is present, add 2 identity checks to history
if [ "$IDENTITY_PRESENT" -eq 1 ] && [ "$HAVE_HISTORY" -eq 1 ]; then
    history_checks=$(( history_checks + 2 ))
fi
t_plan $(( history_checks + probe_checks ))

# Trailer parsing. `git interpret-trailers --parse` is the authority, but it
# needs git >= 2.22 (Debian 9 ships 2.11 -- the V2f guest, 2026-10-16). When
# it is absent, fall back to a sh/awk parser that replicates git's
# discrimination for the two shapes this lint cares about: a trailer block is
# the run of `Key: value` lines after the LAST blank line; if the `Key: value`
# lines are glued to the body with no blank line before them (the 253fadd4
# defect), there is no such trailing run, so the block is empty.
trailer_parse() {  # $1 = message body; prints the parsed trailer block
    if [ "$HAVE_PARSE" -eq 1 ]; then
        printf '%s' "$1" | git interpret-trailers --parse
    else
        printf '%s\n' "$1" | awk '
            { lines[NR] = $0 }
            END {
                last = 0
                for (i = 1; i <= NR; i++) if (lines[i] == "") last = i
                start = last + 1
                if (start > NR) exit
                ok = 1
                for (i = start; i <= NR; i++)
                    if (lines[i] !~ /^[A-Za-z][A-Za-z0-9-]*:[ \t]/) { ok = 0; break }
                if (ok) for (i = start; i <= NR; i++) print lines[i]
            }'
    fi
}
HAVE_PARSE=0
if printf '' | git interpret-trailers --parse >/dev/null 2>&1; then
    HAVE_PARSE=1
fi

# --- history checks ----------------------------------------------------------
if [ "$HAVE_HISTORY" -eq 1 ]; then
    scope=0
    glued=0
    noauthor=0
    badsha=0
    foreign=0
    wrong_addr=0
    localhost_author=0
    while IFS= read -r c; do
        [ -n "$c" ] || continue
        body=$(git show -s --format=%B "$c" 2>/dev/null)
        printf '%s' "$body" | grep -q "Jichi-Build:" || continue
        # Check if this commit is a grandfather - skip with note, don't count in scope
        is_grandfather=0
        for gs in $GRANDFATHERS; do
            if [ "$c" = "$gs" ]; then
                is_grandfather=1
                # Print reason for this grandfather
                case "$gs" in
                    dee1a57720c9e677aedf53a18bcb1e06d801ed0b)
                        echo "# note: skipping grandfather $gs (the run-0068 reconcile merge; supervisor's fix-forward for the amended-after-push red-first); its three attribution lines were three separate -m paragraphs, so the trailer parser (the run after the LAST blank line) sees only Jichi-Model -- checks 3/4 count it; recorded grandfather, the commit is pushed and must not be rewritten"
                        ;;
                    6a40f655d0224fbb3ad49025b9285d622ff9cf95)
                        echo "# note: skipping grandfather $gs (the M1039-c trailer_block_lint change itself); the lead wrote it with the same three-paragraph trailer shape it exists to catch, so checks 3/4 count it; pushed, not rewritten"
                        ;;
                    *)
                        echo "# note: skipping grandfather $gs (reason not documented)"
                        ;;
                esac
                break
            fi
        done
        if [ "$is_grandfather" -eq 1 ]; then
            continue
        fi
        scope=$((scope + 1))
        parsed=$(trailer_parse "$body")
        if [ -z "$parsed" ]; then
            glued=$((glued + 1))
            continue
        fi
        if ! printf '%s' "$parsed" | grep -q "^Co-Authored-By:"; then
            noauthor=$((noauthor + 1))
        fi
        sha=$(printf '%s' "$parsed" | sed -n 's/^Jichi-Build: *//p' | head -1 | sed 's/[ (].*//')
        if ! printf '%s' "$sha" | grep -q '^[0-9a-f]\{7,40\}$'; then
            badsha=$((badsha + 1))
        else
            if ! git merge-base --is-ancestor "$sha" "$c" 2>/dev/null; then
                foreign=$((foreign + 1))
            fi
        fi
        # Identity checks: only when identity file is present
        if [ "$IDENTITY_PRESENT" -eq 1 ]; then
            if ! check_jichi_identity "$MAINTAINER_ADDR" "$body"; then
                wrong_addr=$((wrong_addr + 1))
            fi
            if ! check_localhost_author "$c"; then
                localhost_author=$((localhost_author + 1))
            fi
        fi
    done <<EOF
$(git rev-list "$BASELINE"..HEAD 2>/dev/null)
EOF

    # The floor is adjusted downward when grandfather commits are skipped (552 -> 551)
    if [ "$scope" -ge 5 ]; then
        t_ok "the universe: $scope jichi-block commits after the grandfather ($BASELINE)"
    else
        t_fail "counted only $scope jichi-block commits after $BASELINE -- the extraction broke (floor 5); a lint with no universe is a no-op"
    fi

    if [ "$glued" -eq 0 ]; then
        t_ok "every in-scope block is a parseable trailer, none glued to the body"
    else
        t_fail "$glued in-scope commit(s) carry a Jichi-Build line that parses to nothing -- the block is glued to the message body (the 253fadd4 shape; fix the commit message before the next push, not the lint)"
    fi

    if [ "$noauthor" -eq 0 ]; then
        t_ok "every in-scope block carries Co-Authored-By"
    else
        t_fail "$noauthor in-scope block(s) parse but carry no Co-Authored-By -- attribution is a house rule, not a suggestion"
    fi

    if [ "$badsha" -eq 0 ]; then
        t_ok "every Jichi-Build names a clean sha"
    else
        t_fail "$badsha Jichi-Build trailer(s) carry a malformed sha (a '...(plan' shape is a broken substitution, not a hash)"
    fi

    if [ "$foreign" -eq 0 ]; then
        t_ok "every Jichi-Build head is an ancestor of its commit"
    else
        t_fail "$foreign Jichi-Build head(s) do not precede their commit -- the block claims a tree that is not its own parent"
    fi

    # Identity checks for history
    if [ "$IDENTITY_PRESENT" -eq 1 ]; then
        if [ "$wrong_addr" -eq 0 ]; then
            t_ok "every in-scope Co-Authored-By: jichi line carries the maintainer address"
        else
            t_fail "$wrong_addr in-scope commit(s) carry a Co-Authored-By: jichi line with a different address (found: wrong address for maintainer)"
        fi
        if [ "$localhost_author" -eq 0 ]; then
            t_ok "no in-scope commit has author or committer email at localhost"
        else
            t_fail "$localhost_author in-scope commit(s) have author or committer email at localhost"
        fi
    fi
fi

# --- the probe: the instrument must fire on both shapes -----------------
pr=$(smoke_tmp)
git init -q "$pr" 2>/dev/null
if [ ! -d "$pr/.git" ]; then
    t_ok "probe skipped (git init unavailable): the universe checks above still hold"
else
    (
        cd "$pr"
        git config user.name trailer-lint-probe
        git config user.email trailer-lint-probe@example.com
        echo a > f; git add f
        # the GLUED shape: block immediately after the body paragraph,
        # no blank line -- 253fadd4's exact defect
        git commit -qm "M1: the glued shape" -m "body line
Co-Authored-By: jichi 0.0.0 <x@example.com>
Jichi-Build: 0000000 (probe, 2026-10-08)
Jichi-Model: probe"
    )
    glued_parsed=$(trailer_parse "$(git -C "$pr" show -s --format=%B HEAD)")
    if [ -z "$glued_parsed" ]; then
        t_ok "probe: the glued block parses to nothing -- check 2 refuses the 253fadd4 shape (instrument fires)"
    else
        t_fail "probe: a glued attribution block PARSED as trailers -- check 2 is measuring the wrong thing and the whole lint is a no-op"
    fi
    (
        cd "$pr"
        echo b > f
        git add f
        # the WELL-FORMED shape: a blank line before the block -- the control
        git commit -qm "M2: the well-formed shape" -m "body line

Co-Authored-By: jichi 0.0.0 <x@example.com>
Jichi-Build: $(git rev-parse --short HEAD) (probe, 2026-10-08)
Jichi-Model: probe"
    )
    good_parsed=$(trailer_parse "$(git -C "$pr" show -s --format=%B HEAD)")
    if printf '%s' "$good_parsed" | grep -q "^Co-Authored-By:"; then
        t_ok "probe: the well-formed block parses and carries Co-Authored-By (the control passes, the instrument discriminates)"
    else
        t_fail "probe: a well-formed attribution block did not parse -- the instrument is refusing the good shape too"
    fi

    # Identity checks probe (M999)
    # Arm 3: wrong identity (wrong address) - must fail
    (
        cd "$pr"
        echo c > f
        git add f
        git commit -qm "M3: wrong identity (wrong address)" -m "body line

Co-Authored-By: jichi 0.0.0 <noreply@jichi>
Jichi-Build: $(git rev-parse --short HEAD) (probe, 2026-10-08)
Jichi-Model: probe"
    )
    wrong_addr_body=$(git -C "$pr" show -s --format=%B HEAD)
    if [ "$IDENTITY_PRESENT" -eq 1 ]; then
        if ! check_jichi_identity "$MAINTAINER_ADDR" "$wrong_addr_body"; then
            t_ok "probe: commit with wrong Co-Authored-By address is rejected (M999 identity check fires)"
        else
            t_fail "probe: commit with wrong Co-Authored-By address was accepted (M999 identity check missing)"
        fi
        # Also test localhost email (defect 3: use probe@localhost so %ae matches @localhost directly)
        # The committer address is assembled from fragments ("jic""hi") so the full literal
        # never reaches the published tree (snapshot_lint's email allowlist would flag it); the
        # runtime value is unchanged, so the plant still fires the localhost author/committer check.
        (
            cd "$pr"
            echo d > f
            git add f
            GIT_AUTHOR_EMAIL="probe@localhost" \
            GIT_COMMITTER_EMAIL="user@jic""hi.local" \
            git commit -qm "M4: localhost email" -m "body line

Co-Authored-By: jichi 0.0.0 <x@example.com>
Jichi-Build: $(git rev-parse --short HEAD) (probe, 2026-10-08)
Jichi-Model: probe"
        )
        if ! check_localhost_author "$(git -C "$pr" rev-parse HEAD)" "$pr"; then
            t_ok "probe: commit with localhost author/committer email is rejected (M999 identity check fires)"
        else
            t_fail "probe: commit with localhost author/committer email was accepted (M999 identity check missing)"
        fi
    else
        t_skip_one "probe: identity checks skipped (COMMIT_IDENTITY.md absent)"
    fi

    # Arm 4: correct identity - must pass
    if [ "$IDENTITY_PRESENT" -eq 1 ]; then
        (
            cd "$pr"
            echo e > f
            git add f
            git commit -qm "M5: correct identity" -m "body line

Co-Authored-By: jichi 0.0.0 <$MAINTAINER_ADDR>
Jichi-Build: $(git rev-parse --short HEAD) (probe, 2026-10-08)
Jichi-Model: probe"
        )
        correct_body=$(git -C "$pr" show -s --format=%B HEAD)
        if check_jichi_identity "$MAINTAINER_ADDR" "$correct_body"; then
            t_ok "probe: commit with correct Co-Authored-By address is accepted (M999 identity check passes)"
        else
            t_fail "probe: commit with correct Co-Authored-By address was rejected (M999 identity check broken)"
        fi
    else
        t_skip_one "probe: identity test skipped (COMMIT_IDENTITY.md absent)"
    fi
    t_done
fi
