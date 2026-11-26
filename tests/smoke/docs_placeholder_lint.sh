#!/bin/sh
# smoke lint: no page carries an unfilled template placeholder (M872).
#
# WHY THIS EXISTS. M869 and M870 were committed, gated by a green `make ci` and
# pushed to master with "**Held by.** @HELD@" in both ROADMAP entries. The script
# that wrote the entries left a placeholder for the evidence, to be filled once the
# red-first runs were in, and nothing filled it. The runs had happened -- their logs
# named every red check -- but the record of them had not, and no docs lint reads a
# placeholder as a defect. Found two commits later, by reading the ROADMAP.
#
# CHECKED      -- every Markdown page in the tree, .git excluded, for a template
#                 token: @WORD@ or {{WORD}}, an upper-case word of three
#                 characters or more. None held one once the two were filled.
# THE FLOOR    -- counts the pages that SHIP: those outside docs/internal/, the one
#                 directory .gitattributes marks export-ignore. M872 set it to the
#                 checkout's count, 925, and every tree a rig ships with `git
#                 archive` failed it: 856 pages, on the first board it met (the Pi
#                 400, M879). A checkout, a shipped tree and a release tarball hold
#                 the same pages outside docs/internal/, so one floor fits all three,
#                 and none of them needs git (M451: on-target trees often have none).
# NOT CHECKED  -- a placeholder in any other form ("TBD", "XXX", "<fill in>"), which
#                 prose also uses for real; and whether a filled section is true,
#                 which only a reader can tell.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
root=$(cd "$(dirname "$0")/../.." && pwd)

# The floor is the shipped page count when it was set (M879): a find over the wrong
# directory would find nothing, and nothing would hold no placeholder.
JC_PH_FLOOR=856
PAT='@[A-Z][A-Z0-9_]{2,}@|\{\{ ?[A-Z][A-Z0-9_]* ?\}\}'

# --- 1: the universe is the tree's pages (floor) -----------------------------------
n=$(find "$root" \( -name .git -o -path "$root/docs/internal" \) -prune -o \
        -type f -name '*.md' -print | wc -l | tr -d ' ')
if [ "$n" -ge "$JC_PH_FLOOR" ]; then
    t_ok "the pages read are the tree's ($n shipped Markdown pages, floor $JC_PH_FLOOR)"
else
    t_fail "only $n shipped Markdown pages found under $root (floor $JC_PH_FLOOR)"
fi

# --- 2: none carries a template token (the whole tree, with named exemptions) ------
ph_check() {
    _pc_root="$1"
    _pc_mode="$2"

    _pc_unexempt=0
    _pc_exempt=0
    # 14890 (SV:0224): the run-0100 rejection entry names the driver's dead token
    # bare in prose ("the @SHIM@ and pins/current bugs ..."); the LOG is append-only,
    # so it is exempted by line number and every FUTURE token goes in backticks.
    # 14940 (SV:0824): run 0104's answer to SV:0824, the @SHIM@ in the LOG quoted bare;
    # the LOG is append-only so it is exempted by line number.
    _pc_log_bare="13949 13987 14031 14046 14351 14353 14358 14890 14940"

    _pc_hits=$(mktemp)
    find "$_pc_root" -name .git -prune -o -type f -name '*.md' -print | xargs grep -nHE "$PAT" 2>/dev/null | grep -v ':$' > "$_pc_hits"

    _pc_hits_unexempt=$(mktemp)

    while IFS= read -r _pc_line; do
        [ -n "$_pc_line" ] || continue
        _pc_file=$(printf '%s' "$_pc_line" | cut -d: -f1)
        _pc_linenum=$(printf '%s' "$_pc_line" | cut -d: -f2)
        _pc_content=$(printf '%s' "$_pc_line" | cut -d: -f3-)
        [ -z "$_pc_file" ] && continue

        _pc_rel="${_pc_file#$_pc_root/}"

        # Rule 1: LOG.md bare lines by number
        # Teeth check: the exemption only holds if the expected token is on the expected line.
        # If the file exists but the line has a different token (line number drifted), fail.
        case "$_pc_rel" in
            docs/internal/drive/2026-11-05-harness-port/LOG.md)
                _pc_is_bare=0
                _pc_expected_token=""
                for _pc_bn in $_pc_log_bare; do
                    if [ "$_pc_linenum" = "$_pc_bn" ]; then
                        _pc_is_bare=1
                        # Determine the expected token for this line
                        case "$_pc_bn" in
                            13949) _pc_expected_token="@TMP@" ;;
                            13987) _pc_expected_token="@TMP@" ;;
                            14031) _pc_expected_token="@TMP@" ;;
                            14046) _pc_expected_token="@HELD@" ;;
                            14351) _pc_expected_token="@HELD@" ;;
                            14353) _pc_expected_token="@HELD@" ;;
                            14358) _pc_expected_token="@TMP@" ;;
                            14890) _pc_expected_token="@SHIM@" ;;
                            14940) _pc_expected_token="@SHIM@" ;;
                        esac
                        break
                    fi
                done
                if [ "$_pc_is_bare" -eq 1 ]; then
                    # Verify the token on this line matches what's expected
                    if [ -n "$_pc_expected_token" ]; then
                        if printf '%s' "$_pc_content" | grep -qF "$_pc_expected_token"; then
                            _pc_exempt=$(( _pc_exempt + 1 ))
                            continue
                        else
                            # Teeth check failure: the expected token is not on this line
                            t_fail "exemption entry LOG.md:$_pc_linenum no longer holds $_pc_expected_token; re-pin the number or drop the exemption"
                            # Continue processing, don't exempt this line
                        fi
                    fi
                fi
                ;;
        esac

        # Rule 2: LOG.md code spans (odd backticks before token)
        case "$_pc_rel" in
            docs/internal/drive/2026-11-05-harness-port/LOG.md)
                _pc_line_content=$(sed -n "${_pc_linenum}p" "$_pc_file")
                _pc_temp=$(printf '%s' "$_pc_line_content" | sed 's/@[A-Z][A-Z0-9_]*@.*//')
                if [ "$_pc_temp" = "$_pc_line_content" ]; then
                    _pc_temp=$(printf '%s' "$_pc_line_content" | sed 's/{{ ?[A-Z][A-Z0-9_]* ?}}.*//')
                fi
                _pc_backticks=$(printf '%s' "$_pc_temp" | tr -cd '`' | wc -c)
                if [ $(( _pc_backticks % 2 )) -eq 1 ]; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
        esac

        # Rule 3: Worker reports per file+token
        case "$_pc_rel" in
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1209.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1203-i5-f.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-a.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-b.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-c.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-d.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
            docs/internal/drive/2026-11-05-harness-port/reports/2026-11-23-M1213-S1-e.md)
                if printf '%s' "$_pc_content" | grep -q '@SHIM@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
        esac

        # Rule 4: This note per file+token
        case "$_pc_rel" in
            docs/internal/drive/2026-11-05-harness-port/notes/M1212.md)
                if printf '%s' "$_pc_content" | grep -q '@TMP@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                if printf '%s' "$_pc_content" | grep -q '@HELD@'; then
                    _pc_exempt=$(( _pc_exempt + 1 ))
                    continue
                fi
                ;;
        esac

        _pc_unexempt=$(( _pc_unexempt + 1 ))
        printf '%s\n' "$_pc_file:$_pc_linenum:$_pc_content" >> "$_pc_hits_unexempt"
    done < "$_pc_hits"

    rm -f "$_pc_hits"

    # Anti-vacuous check for MODE=tree
    if [ "$_pc_mode" = "tree" ]; then
        _pc_vacuum_hits=$(mktemp)
        find "$_pc_root" -name .git -prune -o -type f -name '*.md' -print | xargs grep -nHE "$PAT" 2>/dev/null | grep -v ':$' > "$_pc_vacuum_hits"

        # Teeth check: each exemption must be verifiable wherever it is read.
        # An exemption holds only if (1) the file exists in this tree AND (2) the expected token is found.
        # In a pruned tree where the exempted file is absent, the exemption is not applicable (skip).
        # If the file exists but the token is not at the expected location, the lint FAILS.

        _pc_match_log_13949=0
        _pc_match_log_13987=0
        _pc_match_log_14031=0
        _pc_match_log_14046=0

        _pc_match_log_14351=0
        _pc_match_log_14353=0
        _pc_match_log_14358=0
        _pc_match_log_14890=0
        _pc_match_log_14940=0
        _pc_match_m1209=0
        _pc_match_m1203=0
        _pc_match_m1212_a_tmp=0
        _pc_match_m1212_a_held=0
        _pc_match_m1212_b_tmp=0
        _pc_match_m1212_b_held=0
        _pc_match_m1212_c_tmp=0
        _pc_match_m1212_c_held=0
        _pc_match_m1212_d_tmp=0
        _pc_match_m1212_d_held=0
        _pc_match_m1212_tmp=0
        _pc_match_m1212_held=0
        _pc_match_s1e_shim=0
        _pc_match_s1e_held=0
        _pc_match_s1e_tmp=0

        while IFS= read -r _pc_line; do
            [ -n "$_pc_line" ] || continue
            _pc_file=$(printf '%s' "$_pc_line" | cut -d: -f1)
            _pc_linenum=$(printf '%s' "$_pc_line" | cut -d: -f2)
            _pc_content=$(printf '%s' "$_pc_line" | cut -d: -f3-)

            _pc_rel="${_pc_file#$_pc_root/}"

            # Rule 1: LOG.md bare lines by number - teeth check: verify token on expected line
            case "$_pc_rel" in
                docs/internal/drive/2026-11-05-harness-port/LOG.md)
                    case "$_pc_linenum" in
                        13949)
                            # Only count if line 13949 contains @TMP@
                            if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_log_13949=1; fi
                            ;;
                        13987)
                            if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_log_13987=1; fi
                            ;;
                        14031)
                            if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_log_14031=1; fi
                            ;;
                        14046)
                            if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_log_14046=1; fi
                            ;;

                        14351)
                            if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_log_14351=1; fi
                            ;;
                        14353)
                            if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_log_14353=1; fi
                            ;;
                        14358)
                            if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_log_14358=1; fi
                            ;;
                        14890)
                            if printf '%s' "$_pc_content" | grep -q '@SHIM@'; then _pc_match_log_14890=1; fi
                            ;;
                        14940)
                            if printf '%s' "$_pc_content" | grep -q '@SHIM@'; then _pc_match_log_14940=1; fi
                            ;;
                    esac
                    ;;
                # Rule 3: Worker reports per file+token - teeth check: verify token exists in file
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1209.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1209=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1203-i5-f.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1203=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-a.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1212_a_tmp=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_m1212_a_held=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-b.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1212_b_tmp=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_m1212_b_held=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-c.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1212_c_tmp=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_m1212_c_held=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-d.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1212_d_tmp=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_m1212_d_held=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/reports/2026-11-23-M1213-S1-e.md)
                    if printf '%s' "$_pc_content" | grep -q '@SHIM@'; then _pc_match_s1e_shim=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_s1e_held=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_s1e_tmp=1; fi
                    ;;
                docs/internal/drive/2026-11-05-harness-port/notes/M1212.md)
                    if printf '%s' "$_pc_content" | grep -q '@TMP@'; then _pc_match_m1212_tmp=1; fi
                    if printf '%s' "$_pc_content" | grep -q '@HELD@'; then _pc_match_m1212_held=1; fi
                    ;;
            esac
        done < "$_pc_vacuum_hits"
        rm -f "$_pc_vacuum_hits"

        # Teeth check: exemptions must have valid teeth (file exists + token matches).
        # In a pruned tree where the file doesn't exist, the exemption is not applicable (no failure).
        # If the file exists but the token is not at the expected location, the lint FAILS.

        _pc_file_exists_check() {
            [ -f "$_pc_root/$1" ]
        }

        # Rule 1 LOG.md line exemptions - fail if file exists but token not on expected line
        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/LOG.md"; then
            [ "$_pc_match_log_13949" -eq 0 ] && t_fail "exemption entry LOG.md:13949 matches 0 lines"
            [ "$_pc_match_log_13987" -eq 0 ] && t_fail "exemption entry LOG.md:13987 matches 0 lines"
            [ "$_pc_match_log_14031" -eq 0 ] && t_fail "exemption entry LOG.md:14031 matches 0 lines"
            [ "$_pc_match_log_14046" -eq 0 ] && t_fail "exemption entry LOG.md:14046 matches 0 lines"
            [ "$_pc_match_log_14351" -eq 0 ] && t_fail "exemption entry LOG.md:14351 matches 0 lines"
            [ "$_pc_match_log_14353" -eq 0 ] && t_fail "exemption entry LOG.md:14353 matches 0 lines"
            [ "$_pc_match_log_14358" -eq 0 ] && t_fail "exemption entry LOG.md:14358 matches 0 lines"
            [ "$_pc_match_log_14890" -eq 0 ] && t_fail "exemption entry LOG.md:14890 matches 0 lines"
            [ "$_pc_match_log_14940" -eq 0 ] && t_fail "exemption entry LOG.md:14940 matches 0 lines"
        fi

        # Rule 3 worker report exemptions - fail if file exists but token not found
        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1209.md"; then
            [ "$_pc_match_m1209" -eq 0 ] && t_fail "exemption entry M1209.md @TMP@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1203-i5-f.md"; then
            [ "$_pc_match_m1203" -eq 0 ] && t_fail "exemption entry M1203-i5-f.md @TMP@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-a.md"; then
            [ "$_pc_match_m1212_a_tmp" -eq 0 ] && t_fail "exemption entry M1212-a.md @TMP@ matches 0 lines"
            [ "$_pc_match_m1212_a_held" -eq 0 ] && t_fail "exemption entry M1212-a.md @HELD@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-b.md"; then
            [ "$_pc_match_m1212_b_tmp" -eq 0 ] && t_fail "exemption entry M1212-b.md @TMP@ matches 0 lines"
            [ "$_pc_match_m1212_b_held" -eq 0 ] && t_fail "exemption entry M1212-b.md @HELD@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-c.md"; then
            [ "$_pc_match_m1212_c_tmp" -eq 0 ] && t_fail "exemption entry M1212-c.md @TMP@ matches 0 lines"
            [ "$_pc_match_m1212_c_held" -eq 0 ] && t_fail "exemption entry M1212-c.md @HELD@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M1212-d.md"; then
            [ "$_pc_match_m1212_d_tmp" -eq 0 ] && t_fail "exemption entry M1212-d.md @TMP@ matches 0 lines"
            [ "$_pc_match_m1212_d_held" -eq 0 ] && t_fail "exemption entry M1212-d.md @HELD@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/reports/2026-11-23-M1213-S1-e.md"; then
            [ "$_pc_match_s1e_shim" -eq 0 ] && t_fail "exemption entry M1213-S1-e.md @SHIM@ matches 0 lines"
            [ "$_pc_match_s1e_held" -eq 0 ] && t_fail "exemption entry M1213-S1-e.md @HELD@ matches 0 lines"
            [ "$_pc_match_s1e_tmp" -eq 0 ] && t_fail "exemption entry M1213-S1-e.md @TMP@ matches 0 lines"
        fi

        if _pc_file_exists_check "docs/internal/drive/2026-11-05-harness-port/notes/M1212.md"; then
            [ "$_pc_match_m1212_tmp" -eq 0 ] && t_fail "exemption entry M1212.md @TMP@ matches 0 lines"
            [ "$_pc_match_m1212_held" -eq 0 ] && t_fail "exemption entry M1212.md @HELD@ matches 0 lines"
        fi
    fi

    # Print unexempted hits in BOTH modes (for arms 3-5 to grep from)
    if [ -s "$_pc_hits_unexempt" ]; then
        while IFS= read -r _pc_hit; do
            [ -n "$_pc_hit" ] || continue
            printf '%s\n' "${_pc_hit#$_pc_root/}"
        done < "$_pc_hits_unexempt"
    fi

    # Print summary in tree mode only
    if [ "$_pc_mode" = "tree" ]; then
        printf 'the tree check found %s unexempted placeholder(s), %s exempted\n' \
            "$_pc_unexempt" "$_pc_exempt"
    fi

    rm -f "$_pc_hits_unexempt"
}

# Run check 2 on real tree
_ph_result=$(ph_check "$root" "tree")
if printf '%s' "$_ph_result" | grep -qE '@[A-Z][A-Z0-9_]{2,}@'; then
    t_fail "unfilled placeholders: $(printf '%s' "$_ph_result" | head -5 | while IFS= read -r _pc_line; do printf '%s\n' "${_pc_line#$root/}"; done | head -5)"
else
    t_ok "no page carries an unfilled template placeholder"
fi

# --- 3: PERTURB a (the M872 class, the tooth) --------------------------------------
_ph_scratch_a=$(smoke_tmp)
mkdir -p "$_ph_scratch_a/docs/internal/drive/2026-11-05-harness-port"
cp "$root/docs/internal/drive/2026-11-05-harness-port/LOG.md" "$_ph_scratch_a/docs/internal/drive/2026-11-05-harness-port/LOG.md"
echo '  holding @HELD@ must red check 2 -- that is the class (f) reopened.' >> "$_ph_scratch_a/docs/internal/drive/2026-11-05-harness-port/LOG.md"
_ph_result_a=$(ph_check "$_ph_scratch_a" "count")
# N3 (LOG run 0096): count the printed hit lines and t_fail when the count
# is not exactly 1. In count mode ph_check prints exactly one line per
# unexempted hit (the summary line is tree-mode only), so the capture's
# non-empty line count IS the hit count -- the presence grep proved >=1,
# not exactly 1, so a scratch LOG with two bare-token lines passed it.
_ph_a_n=$(printf '%s\n' "$_ph_result_a" | grep -c .)
if [ "$_ph_a_n" -eq 1 ]; then
    t_ok "perturb a: scratch LOG with bare @HELD@ reports 1 unexempted hit"
elif [ "$_ph_a_n" -eq 0 ]; then
    t_fail "perturb a: scratch LOG with bare @HELD@ reports 0 unexempted hits -- the exemption swallowed the log, want 1"
else
    t_fail "perturb a: scratch LOG with bare @HELD@ reports $_ph_a_n unexempted hits, want 1 (the scratch copy is wrong)"
fi

# --- 4: PERTURB b (the idiom) ------------------------------------------------------
_ph_scratch_b=$(smoke_tmp)
mkdir -p "$_ph_scratch_b/docs/internal/drive/2026-11-05-harness-port"
cp "$root/docs/internal/drive/2026-11-05-harness-port/LOG.md" "$_ph_scratch_b/docs/internal/drive/2026-11-05-harness-port/LOG.md"
echo '  `@HELD@` in a code span' >> "$_ph_scratch_b/docs/internal/drive/2026-11-05-harness-port/LOG.md"
_ph_result_b=$(ph_check "$_ph_scratch_b" "count")
if printf '%s' "$_ph_result_b" | grep -qE '@HELD@'; then
    t_fail "perturb b: scratch LOG with @HELD@ in code span reports unexempted hits, want 0"
else
    t_ok "perturb b: scratch LOG with @HELD@ in code span reports 0 unexempted hits"
fi

# --- 5: PERTURB c (a new report) ---------------------------------------------------
_ph_scratch_c=$(smoke_tmp)
mkdir -p "$_ph_scratch_c/docs/internal/drive/2026-11-05-harness-port/reports"
cat > "$_ph_scratch_c/docs/internal/drive/2026-11-05-harness-port/reports/2026-11-22-M0000-probe.md" <<'EOF'
# M0000-probe -- a fresh report for perturb c

This report quotes @TMP@ as prose.
EOF
_ph_result_c=$(ph_check "$_ph_scratch_c" "count")
if printf '%s' "$_ph_result_c" | grep -qE '@TMP@'; then
    t_ok "perturb c: fresh report file with bare @TMP@ reports 1 unexempted hit"
else
    t_fail "perturb c: fresh report file with bare @TMP@ reports no unexempted hits (0), want 1"
fi

t_done
