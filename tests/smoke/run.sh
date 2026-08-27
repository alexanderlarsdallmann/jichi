#!/bin/sh
# Python-free smoke tier runner (M209). Validates a BUILD on any POSIX box
# -- old systems, low-resource targets (docs/LOW_MEMORY.md) -- with no
# python3 anywhere: POSIX-sh drivers + the three C89 helpers in
# tests/tools (mockmodel/ptydrive/jsonq, `make smoke-tools`).
#
# This tier complements, and does not replace, the full Python e2e suite
# (tests/e2e/run.sh): smoke validates a build, e2e validates the product.
# Idioms are inherited from the e2e runner deliberately (M198 isolated
# HOME, M201 failure-classification retry) -- each was paid for by a
# documented incident.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
BIN="${JC_SMOKE_BIN:-$root/jichi}"

if [ ! -x "$BIN" ]; then
    echo "smoke: build the agent first (make) -- missing $BIN" >&2
    exit 1
fi
for t in mockmodel ptydrive jsonq sockq; do
    if [ ! -x "$root/tests/tools/$t" ]; then
        echo "smoke: missing tests/tools/$t -- run 'make smoke-tools' first" >&2
        exit 1
    fi
done

# M198 idiom: a suite-wide private $HOME (hygiene + determinism). Drivers
# additionally take per-driver HOMEs inside it (smoke_home). Set
# JC_SMOKE_KEEP_HOME=1 to run against the real HOME for debugging.
if [ -z "${JC_SMOKE_KEEP_HOME:-}" ]; then
    SMOKE_HOME=$(mktemp -d "${TMPDIR:-/tmp}/jichi_smoke_home.XXXXXX")
    trap 'rm -rf "$SMOKE_HOME"' EXIT INT TERM
    HOME="$SMOKE_HOME"
    export HOME
    echo "smoke: isolated HOME=$SMOKE_HOME"
fi

# Reserved for a future --lite sweep, mirroring e2e's JC_E2E_EXTRA.
export JC_SMOKE_BIN="$BIN" JC_SMOKE_EXTRA="${JC_SMOKE_EXTRA:-}"

# Deadline wrapper for the drivers themselves: timeout(1) when present,
# else a pure-sh watchdog. (Duplicated small from _smoke.sh -- the runner
# does not source the driver lib; its EXIT trap must stay its own.)
wd() {
    _secs="$1"; shift
    if command -v timeout >/dev/null 2>&1; then
        # M623: -k, when supported. Plain timeout(1) TERMs its direct child --
        # the driver SHELL -- and a shell defers traps until its foreground
        # child exits, so a driver blocked on a hung jichi ABSORBS the TERM and
        # the "deadline" waits forever (one gate spent an hour inside a 60s
        # limit exactly this way). KILL cannot be deferred; killing the shell
        # and timeout also closes their end of the harness stdin socket, which
        # un-wedges a child blocked reading it.
        if [ -z "$WD_KILL_PROBED" ]; then
            WD_KILL_PROBED=1
            if timeout -k 1 1 true >/dev/null 2>&1; then WD_KILL="-k 5"; else WD_KILL=""; fi
        fi
        # shellcheck disable=SC2086 -- WD_KILL is deliberately word-split
        timeout $WD_KILL "$_secs" "$@"
        return $?
    fi
    "$@" &
    _cpid=$!
    ( sleep "$_secs"; kill -TERM "$_cpid" 2>/dev/null; \
      sleep 2; kill -KILL "$_cpid" 2>/dev/null ) &
    _wpid=$!
    wait "$_cpid"
    _rc=$?
    kill "$_wpid" 2>/dev/null
    wait "$_wpid" 2>/dev/null
    return $_rc
}

total_plan=0
total_ok=0
ndrivers=0
nfail=0
failed_drivers=""

# JC_SMOKE_KEEP_GOING=1 -- run every driver and report ALL failures, like `make -k`.
#
# WHY (M466). The tier is fail-fast: `run_driver ... || exit 1`. On this workstation
# that is right -- the fix loop is seconds long and the first failure is the one you
# want. On a REMOTE PLATFORM ROW it is close to useless: an OpenBSD or FreeBSD row
# costs a ten-minute unattended install plus a build, and reports exactly ONE failing
# driver, so enumerating a platform's defects costs one full boot per defect. FreeBSD's
# "seven defects" took that many sessions for this reason, and the OpenBSD row hid its
# real stop for three runs behind an unrelated lint failure that happened to come
# first: the row said "did not print its OK marker" while the driver everybody wanted
# to see had never been reached.
#
# Default stays 0, so local runs and `make ci` are unchanged -- a fast fail is a
# feature where iteration is cheap. The rigs pass 1.
KEEP_GOING="${JC_SMOKE_KEEP_GOING:-0}"

# M201: on failure, retry the driver ONCE standalone and label the outcome.
# NOT retry-to-green: the suite fails either way; the label ("in-suite
# only" vs "also alone") is diagnostic evidence, captured with the output.
run_driver() {
    _t="$1"; _limit="$2"
    # M220: runtime timeout multiplier for slow silicon (single-board ARM,
    # Pi-Zero class). JC_SMOKE_TIMEOUT_MULT, falling back to the e2e knob so
    # an operator on a slow box sets ONE variable for both tiers. The x86
    # constants stay load-bearing on fast machines (a tight timeout is what
    # turns a hang into a failure), hence multiply-at-runtime.
    _limit=$((_limit * ${JC_SMOKE_TIMEOUT_MULT:-${JC_E2E_TIMEOUT_MULT:-1}}))
    _log=$(mktemp "${TMPDIR:-/tmp}/jichi_smoke_$_t.XXXXXX")
    if wd "$_limit" sh "$here/$_t.sh" >"$_log" 2>&1; then
        # TAP accounting: the plan and the emitted ok-count must agree --
        # a green with no denominator is not evidence.
        _plan=$(awk -F'\\.\\.' '/^1\.\.[0-9]+$/ { print $2; exit }' "$_log")
        _oks=$(grep -c '^ok ' "$_log") || true
        if [ -z "$_plan" ] || [ "$_plan" -ne "$_oks" ]; then
            echo "smoke: $_t emitted a bad TAP count (plan=${_plan:-none}," \
                 "ok=$_oks)" >&2
            sed 's/^/    | /' "$_log" >&2
            rm -f "$_log"
            return 1
        fi
        cat "$_log"
        total_plan=$((total_plan + _plan))
        total_ok=$((total_ok + _oks))
        ndrivers=$((ndrivers + 1))
        rm -f "$_log"
        return 0
    fi
    echo "smoke: $_t FAILED (in suite)" >&2
    sed 's/^/    | /' "$_log" >&2
    rm -f "$_log"
    echo "smoke: retrying $_t standalone to classify the failure..." >&2
    _log2=$(mktemp "${TMPDIR:-/tmp}/jichi_smoke_$_t.XXXXXX")
    if wd "$_limit" sh "$here/$_t.sh" >"$_log2" 2>&1; then
        echo "smoke: $_t PASSES standalone -> IN-SUITE-ONLY failure;" >&2
        echo "       suspect cross-driver load/resource effects." >&2
    else
        echo "smoke: $_t ALSO fails standalone -> a real defect:" >&2
        sed 's/^/    | /' "$_log2" >&2
    fi
    rm -f "$_log2"
    return 1
}

# One place decides what a failure means, so the two loops cannot drift.
driver_failed() {
    nfail=$((nfail + 1))
    failed_drivers="$failed_drivers $1"
    [ "$KEEP_GOING" = 1 ] || exit 1
}

# One summary for every exit path, so a subset run and a full run cannot report
# success in two different shapes.
summarise() {
    if [ "$nfail" -gt 0 ]; then
        # Reached only under KEEP_GOING; the fail-fast path has already exited.
        echo "smoke: FAILED -- $nfail driver(s):$failed_drivers" >&2
        echo "smoke: ($ndrivers of $((ndrivers + nfail)) drivers passed," \
             "$total_ok checks)" >&2
        exit 1
    fi
    echo "smoke: OK ($ndrivers drivers, $total_ok checks)"
    exit 0
}

# Named drivers only: `sh tests/smoke/run.sh accessible parallel_abort`.
#
# WHY (M466): re-checking one failure on a remote platform meant re-running all 201
# drivers, because the runner took no arguments. An OpenBSD row is a ten-minute boot;
# spending it re-proving 200 passing drivers to look at the 201st is why the same
# stop was re-measured three times in one afternoon. A wrong name is exit 2 (a usage
# error, matching the rigs' contract) and NOT a silent skip -- a typo that quietly
# ran nothing would report OK over zero drivers.
if [ $# -gt 0 ]; then
    for t in "$@"; do
        if [ ! -f "$here/$t.sh" ]; then
            echo "smoke: no such driver: $t" >&2
            exit 2
        fi
    done
    echo "smoke: subset -- $# driver(s) named on the command line"
    for t in "$@"; do
        echo "--- smoke: $t"
        # The wider of the two bounds, deliberately: a subset run cannot know which
        # list a driver came from, and a too-generous deadline turns a hang into a
        # slow failure rather than a wrong pass.
        run_driver "$t" 120 || driver_failed "$t"
    done
    summarise
fi

for t in smoke_lint snapshot_lint license_lint self_learner_lint revert_provenance hint_record preprompt_discard verify_source model_defaulted budget_stop_verdict docs_index_lint harden_flags_lint sub_prompt_lint docs_flags arena_lint reading_refs_lint reading_quotes_lint reading_trace self_hosting_pack undo_across_branch undo_scope rules_budget_lint config_bool_lint daemon_auth assignment_verbs fence_alias_bypass writer_reads_reader bool_dialect fence_write_tools ctx_estimate_lint stream_unterminated reasoning_budget_hint docs_locators_lint hint_ladder attempt_tainted attempt_guard improve_tainted route_pin grade_expect_fail grade_wrongdir curriculum_universe_lint telemetry_join outcome_join verify_stuck_periodic verify_consistency_periodic tool_loop goalpost_note subagent_tool_ad delegate_report liveness jsonl_utf8 fetch_ssrf acp_result_utf8 jsonl_tool_id degraded sysmsg_env cost_model learn_draft_clobber blocked_repeat sprintf_lint \
         subcommands_lint describe_names_lint man_page_lint config_keys_lint changelog_coverage_lint milestone_currency_lint telemetry_events_lint telem_alias_rows telem_cache_per_session read_truncated_total session_fields_lint secret_env_lint notice_tags_lint keys_lint priced_model_lint prompt_keys_lint approval_keys headless_accessible doctor_language queue_notice_glyph deny_stops view_key status_wisdom doc_claims_lint ghost_announce group_sep_lint \
         project_records_lint org_mode_lint design_multi portability_lint posix_utils_lint mincurl_recipe_lint migration_paths telemetry_failure tool_caps_lint \
         slash_commands_lint assignment_i18n_lint i18n_tracks_lint tool_nameless install_no_build tool_profile context_assets context_tools context_tools_use context_tools_live context_history \
         docs_counts_lint config_defaults_lint lite_context_cap builtin_cmds_lint completions_lint config_verbs config_dir setup_key_env flags doctor doctor_stall_latency doctor_selectors doctor_fences doctor_styles doctor_tooluse doctor_cache tool_names_lint refs_lint examples_lint asset_keys_lint unit_orphans_lint doc_commands_lint describe describe_fields_lint init \
         dream prune_dreams prune_index prune_worktrees workflow grade improve export output_style learn faults \
         faults_net faults_net_midstream provider_redirect state_root child_fds secret_env_subcommands output_escapes transport_posture \
         acp_load headless_basic headless_tool run_kill_note glob_pattern toolcalling_none \
         compact_pressed compact_latch accessible slash_leading_space paste_special history_check prefix_churn context_gauge ask_unattended fence_refusal state_reach headless_progress output_json stop_reason_capped sessions prose_nudge empty_answer notify command_fm \
         slash_unknown expect_header advice \
         ask websearch subagent_itercap subagent_budget learn_on_stop learn_on_stop_cost subtask_persona subtask_language telemetry_default learn_retract learn_checks bg \
         constraints_scope constraint_vs_scope blocked_calls_count context_underdeclared config_jsonc constraints_scan brief_check learn_on_stop_outcome hooks \
         verify_kind budget_notice budget_panel elide_ticket resume_drift todo_resume pathfence_dangling hollow_green_note \
         sysmsg_date repair_note superseded_marker flight_plan \
         observability pdf \
         transcribe audiogen \
         imagegen vision compaction compact_short privileged docs docs_pdf \
         index_coverage ls_pattern_coverage build_rev no_changes_field \
         example_data_analysis example_game_design example_project_management example_personal_finance example_blender_python example_krita_python example_academic_writing example_research_notes example_game_dev example_scheduling example_business_plan example_web_basics \
         daemon mcp mcp_prompt mcp_ref tab editor ghost; do
    echo "--- smoke: $t"
    run_driver "$t" 60 || driver_failed "$t"
done

# The wall-clock contracts and the multi-scenario drivers get wider
# wrappers (a stalled stream must be given room to time out on its own
# terms; degenerate_store runs eleven bounded ls invocations;
# pathfence/rewind run two full mock turns each, rewind through git;
# kinetic runs six scenarios, sound four; enablers waits out a slow
# model for the heartbeat; route_stall waits out a 6s stall; typeahead
# waits out a deliberately slow model call to have a window to type in;
# typeahead_live runs three PTY sessions, one of them over a 4s tool).
for t in setup degenerate_store pathfence rewind baseline_checkpoint kinetic sound lease \
         enablers route_stall autocontext control acp_cancel \
         parallel_hang parallel_abort parallel_merge parallel_timeout_msg supervisor \
         sessions_footprint turn_scratch learner_flow \
         setup_keyfile format_command paste typed tui_tool_escapes typeahead typeahead_live stall signals tui_basic tui_context_views tui_learn tui_learn_apply tui_model_name undo_note; do
    echo "--- smoke: $t"
    run_driver "$t" 120 || driver_failed "$t"
done

summarise
