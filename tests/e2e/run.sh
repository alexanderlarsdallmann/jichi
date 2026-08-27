#!/bin/sh
# Offline E2E driver. Runs the network-free interactive/CLI checks against the
# built binary; the model-gated check runs only when JC_E2E_MODEL is set.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
BIN="${JC_E2E_BIN:-$root/jichi}"
CFG="${JC_E2E_CONFIG:-$here/fixtures/config.json}"

if [ ! -x "$BIN" ]; then
    echo "e2e: build the agent first (make) -- missing $BIN" >&2
    exit 1
fi
# M217: python3 is now OPTIONAL. The Python-free smoke tier (make smoke)
# carries the ported coverage; only a permanently-Python residual is left
# here -- redraw's VT emulator, the stress/web_bridge Python *products*,
# curriculum_graders (needs a C compiler at test time), rig_lint (it lints
# the Python rig), and the two model-gated live checks. So a missing
# python3 is a LOUD SKIP (exit 0), mirroring elisp-compile/slides, not a
# hard failure -- a python-free box gets its full gate from
# `make check-target` (unit + smoke).
if ! command -v python3 >/dev/null 2>&1; then
    echo "e2e: python3 not found -- skipping the residual Python drivers." >&2
    echo "     The ported coverage runs in 'make smoke' (no python3); this" >&2
    echo "     tier now holds only redraw/stress/web_bridge/curriculum_graders" >&2
    echo "     + the model-gated live checks. See docs/plans/" >&2
    echo "     2026-07-python-free-testing.md." >&2
    exit 0
fi

# M198 #5: `run.sh --lite` re-runs the whole suite with the resource-lean
# defaults. Development sits at one corner of the config cube; --lite is the
# other one that matters, and it was previously covered only at the level of
# config resolution (tests/test_config.c), never end to end.
JC_E2E_EXTRA="${JC_E2E_EXTRA:-}"
for arg in "$@"; do
    case "$arg" in
        --lite) JC_E2E_EXTRA="$JC_E2E_EXTRA --lite" ;;
        *) echo "e2e: unknown option $arg" >&2; exit 2 ;;
    esac
done

# M198: a suite-wide private $HOME. Two reasons, both load-bearing:
#   1. Hygiene -- every driver that completes a turn (headless as well as TUI)
#      persists a session, so a full run used to deposit ~23 files in the
#      developer's real ~/.jichi.d/sessions.
#   2. Determinism -- without it the drivers read the developer's real global
#      config, glossary and memory from ~, so a test could pass or fail
#      depending on whose machine it ran on.
# Set JC_E2E_KEEP_HOME=1 to run against the real HOME (for debugging a driver
# that genuinely depends on global asset discovery).
if [ -z "${JC_E2E_KEEP_HOME:-}" ]; then
    E2E_HOME=$(mktemp -d "${TMPDIR:-/tmp}/jichi_e2e_home.XXXXXX")
    trap 'rm -rf "$E2E_HOME"' EXIT INT TERM
    HOME="$E2E_HOME"
    export HOME
    echo "e2e: isolated HOME=$E2E_HOME"
fi

export JC_E2E_BIN="$BIN" JC_E2E_CONFIG="$CFG" JC_E2E_EXTRA="$JC_E2E_EXTRA"
if [ -n "$JC_E2E_EXTRA" ]; then
    echo "e2e: extra flags:$JC_E2E_EXTRA"
fi

# M201: on failure, retry the driver ONCE standalone and label the outcome.
#
# NOT retry-to-green: the suite still fails either way. The point is that three
# different drivers (prose_nudge, constraints_scope, pathfence) have now failed
# only INSIDE a full run and passed 4/4 alone, and each occurrence arrived with no
# evidence -- so the next one should arrive labelled. "in-suite only" says look at
# what 55 sequential drivers do to the machine; "also alone" says the driver or
# the product is genuinely broken. It also captures the failing output, which is
# what distinguishes a truncation-style WRONG ANSWER from a timeout.
run_driver() {
    _t="$1"; _limit="$2"
    # M220 (hardware plan prep): the per-driver limits are calibrated on x86
    # and LOAD-BEARING there (a tight timeout is what turns a hang into a
    # failure) -- on Pi-Zero-class silicon they false-fail, so multiply at
    # runtime instead of raising the constants. Integer factor.
    _limit=$((_limit * ${JC_E2E_TIMEOUT_MULT:-1}))
    _log=$(mktemp "${TMPDIR:-/tmp}/jichi_e2e_$_t.XXXXXX")
    # -u (M256): python block-buffers stdout when it is a file, so a driver
    # KILLED by the timeout above flushes nothing and the log below is EMPTY --
    # which the classifier then reported as "a real defect" with no evidence at
    # all. Unbuffered, a timeout shows exactly how far the driver got.
    if timeout "$_limit" python3 -u "$here/$_t.py" >"$_log" 2>&1; then
        cat "$_log"; rm -f "$_log"; return 0
    fi
    echo "e2e: $_t FAILED (in suite)" >&2
    sed 's/^/    | /' "$_log" >&2
    rm -f "$_log"
    echo "e2e: retrying $_t standalone to classify the failure..." >&2
    _log2=$(mktemp "${TMPDIR:-/tmp}/jichi_e2e_$_t.XXXXXX")
    if timeout "$_limit" python3 -u "$here/$_t.py" >"$_log2" 2>&1; then
        echo "e2e: $_t PASSES standalone -> IN-SUITE-ONLY failure. The driver is" >&2
        echo "     not obviously broken; suspect cross-driver load/resource" >&2
        echo "     effects. See docs/analysis/2026-07-29-tool-arena.md." >&2
    else
        echo "e2e: $_t ALSO fails standalone -> a real defect, not a suite effect:" >&2
        sed 's/^/    | /' "$_log2" >&2
    fi
    rm -f "$_log2"
    return 1
}

# M210: the offline subprocess drivers + the docs_flags/arena_lint lints
# moved to the Python-free smoke tier (tests/smoke/, same names, .sh) --
# one driver, one tier, enforced by tests/smoke/smoke_lint.sh.
for t in rig_lint redraw probes; do
    echo "--- e2e: $t"
    run_driver "$t" 60 || exit 1
done

# curriculum_graders is the tier's outlier and needs its own budget (M256): it
# grades every assignment TWICE (untouched fixture must fail, reference solution
# must pass) plus every trap case -- and each of those spawns jichi and a real
# toolchain (cc, c++, rustc, zig, ghc, clojure, elixir, guile, racket), under
# e2e's isolated cold $HOME where every compiler cache starts empty. It shared
# the 60s limit above from when the curriculum was a fraction of its current
# size; measured 119s cold / 81s warm on the reference box once the nine
# language courses landed, so it had become a GUARANTEED timeout whose empty
# output (see -u above) hid the cause. 600s is deliberate headroom, not a
# hang-catcher: a broken grader fails in seconds, so a tight limit here buys
# nothing and only re-breaks the gate on the next course.
echo "--- e2e: curriculum_graders"
run_driver curriculum_graders 600 || exit 1

echo "--- e2e: stress"
run_driver stress 60 || exit 1

# M165b web bridge: the single-file supervisor drives real jichi children over
# --output jsonl; token gate, SSE run->done, heartbeat over SSE, cancel. Needs
# a longer wrapper (it restarts the bridge against a slow model).
echo "--- e2e: web_bridge"
timeout 180 python3 "$here/web_bridge.py" \
    || { echo "e2e: web_bridge FAILED" >&2; exit 1; }

if [ -n "$JC_E2E_MODEL" ]; then
    # The live-model check needs a config that can actually reach a model, not
    # the offline fixture; default to the repo's dev config if present.
    export JC_E2E_REAL_CONFIG="${JC_E2E_REAL_CONFIG:-$root/local/config.json}"
    echo "--- e2e: headless_model ($JC_E2E_MODEL)"
    timeout 150 python3 "$here/headless_model.py" \
        || { echo "e2e: headless_model FAILED" >&2; exit 1; }
    echo "--- e2e: acp_terminal ($JC_E2E_MODEL)"
    timeout 200 python3 "$here/acp_terminal.py" \
        || { echo "e2e: acp_terminal FAILED" >&2; exit 1; }
fi

echo "e2e: OK"
