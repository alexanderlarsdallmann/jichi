# shellcheck shell=sh
# _rig_mult.sh -- the ONE implementation of JC_SMOKE_TIMEOUT_MULT. Source it.
#
# WHY THIS FILE EXISTS (M464). The multiplier is a RATIO -- device build seconds
# divided by *this bench's* build seconds -- and four rigs computed it four ways:
#
#   tier-b-device.sh  correct: --ref-secs REQUIRED, awk, numeric-validated, ceil
#   tier-v-vm.sh      correct for its shape: measures BOTH terms in-run, floor 2
#   tier-v-openbsd.sh WRONG: `_mult=$(( (_secs + 6) / 6 ))` -- one bench's 6.19 s
#                     baked in as a literal 6, unoverridable, and silently wrong
#                     on any other machine (this bench measures 4.38 s)
#   tier-v-bsd.sh     WRONG: no multiplier at all. It ran bare `gmake smoke` with
#                     JC_SMOKE_TIMEOUT_MULT unset, i.e. 1 -- the tightest possible
#                     deadlines -- which is a candidate cause of that row's
#                     undiagnosed stop at 185 of 198 drivers
#
# docs/SESSION_RUNBOOK.md §5 states the rule the two broken ones violate: **copy
# the formula, never a row's number.** A hardcoded denominator is that mistake
# compiled in, and it fails in the direction that hurts -- a multiplier too small
# makes healthy runs fail, and the failure reads exactly like a product defect.
#
# TWO PROPERTIES THAT ARE LOAD-BEARING, both learned by losing a row to them
# (the comments in tier-b-device.sh record the incident; this is that logic, moved
# rather than reinvented):
#
#   1. awk, never bc. `bc` is absent from a stock Raspberry Pi OS image, and its
#      absence was SILENT: a failed timing produced `secs=?`, awk compared the
#      STRING "?" against "0", "?" sorts higher so the `<= 0` guard passed, "?"/ref
#      evaluated to 0, and the clamp turned that into a multiplier of 1 -- on the
#      slowest board in the fleet. A missing tool produced the tightest deadlines
#      and a plausible-looking number instead of an error.
#   2. Both operands are validated as NUMERIC first, in the shell, before awk sees
#      them -- because that is exactly the check whose absence caused (1).
#
# It REFUSES rather than guesses: a multiplier is the denominator of every timing
# in a row, so a wrong one does not degrade the row, it invalidates it.
#
# Usage:
#   . "$(dirname "$0")/_rig_mult.sh"
#   mult=$(jc_rig_mult "$dev_secs" "$ref_secs") || { ...refuse, do not guess... }
#
# jc_rig_mult DEV_SECS REF_SECS
#   stdout: ceil(DEV/REF), never below 1
#   exit 0 on success; exit 1 (and no output) if either operand is not a positive
#   decimal number. Callers must treat a non-zero exit as "no row", not as 1.
jc_rig_mult() {
    _jrm_dev=${1:-}
    _jrm_ref=${2:-}

    # Positive decimal only: digits with at most one dot, and not a bare dot.
    # `*.*.*` rejects "1.2.3"; the bare-dot case rejects "." on its own.
    for _jrm_v in "$_jrm_dev" "$_jrm_ref"; do
        case "$_jrm_v" in
            ''|*[!0-9.]*|.|*.*.*) return 1 ;;
        esac
    done

    awk -v d="$_jrm_dev" -v r="$_jrm_ref" 'BEGIN{
        d += 0; r += 0;
        if (r <= 0 || d <= 0) exit 1;
        x = d / r; m = int(x); if (x > m) m++;
        if (m < 1) m = 1;
        print m;
    }'
}

# jc_rig_ref_or_die SCRIPTNAME REF_SECS
#   The guard every rig that takes a reference must run before it does any work.
#   Refuses an absent or non-numeric reference with the reason, because a row
#   whose denominator is unknown cannot be compared to any other row -- which is
#   the whole purpose of measuring it.
jc_rig_ref_or_die() {
    _jrm_who=${1:-rig}
    _jrm_r=${2:-}
    case "$_jrm_r" in
        ''|*[!0-9.]*|.|*.*.*)
            echo "$_jrm_who: --ref-secs is required and must be a number (got '${_jrm_r}')." >&2
            echo "  It is THIS bench's serial \`make WERROR=1\` seconds, the denominator of" >&2
            echo "  JC_SMOKE_TIMEOUT_MULT. Measure it, do not copy one from a published row:" >&2
            echo "    make clean && time make WERROR=1     # three runs, take the median" >&2
            echo "  (docs/SESSION_RUNBOOK.md §5: copy the formula, never the number.)" >&2
            return 1 ;;
    esac
    return 0
}
