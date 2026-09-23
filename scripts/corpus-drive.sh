#!/bin/sh
# corpus-drive.sh -- drive jichi over a list of tasks, headless, to MAKE a corpus.
#
# WHY THIS EXISTS. Four decisions in docs/DEFERRED.md wait on journals and
# telemetry that ordinary use was not producing (one active day in 48 on the
# development bench, 2026-09-23), and plan D1's thresholds must be fitted to a
# corpus from builds that already have M432. A register row that waits for "more
# journals" is a plan only if something produces journals. This is the something:
# plan D7 ("a corpus on purpose") in the smallest form that can be repeated on
# another machine. First used 2026-09-23 against zigodot, docs/plans/2026-09-after-m712.md.
#
# WHAT IT DOES. For each line `TASK KIND` of the list, in order: reset the
# workspace to the base commit, run ONE headless `jichi --auto` turn with the
# prompt in TASKS/TASK.md, and keep everything the run leaves -- the jsonl stream,
# stderr, the journal, the diff it made, and one line of runs.tsv. Telemetry goes
# to one file per arm at the `full` tier, so tests/measure/success_repeats.py can
# key on the whole arguments and a hash of each result. KIND is `read` or `edit`;
# an `edit` task also gets --verify CMD --verify-baseline.
#
# THE RULES, and the incident behind each:
#   - fences ON: --edit-scope (default src/** and tests/**) with
#     --revert-out-of-scope, --lease fail (one run per workspace), the path fence,
#     and whatever tool-iteration cap the config sets.
#   - caps OFF: no --budget-tokens, no --deadline -- a cap that fires does not
#     hide the answer, it manufactures a different one (CLAUDE.md, "Caps versus
#     fences"). Put `"timeouts": {"stall": 0, "request": 0}` in the config too.
#   - the outer timeout is HOUSEKEEPING, not a bound: SIGINT, so jichi stops the
#     way Ctrl-C stops it and writes its journal, then SIGKILL 60 s later. It
#     prefers GNU's timeout, because uutils coreutils 0.10.0 `timeout -s INT -k N`
#     SIGKILLs at once and returns 137 where GNU returns 124 (measured
#     2026-09-23). Every kill is recorded as rc=124/137 in runs.tsv, never hidden.
#   - the workspace must be a CLONE the operator can lose: every task starts with
#     `git reset --hard BASE && git clean -fd`. The script refuses a workspace
#     with no .git, and records BASE in the manifest.
#   - NO PRICED MODEL, refused before the first request: every model in the
#     config whose apiBase is not loopback must have an id beginning with the
#     free prefix (default `jlu/`, the institutional free namespace; CLAUDE.md,
#     "Models: local only"). A loopback server is the operator's own machine.
#   - a key, when an arm needs one, comes from --env-file, sourced with `set -a`
#     in the run's own subshell, the way the setup wizard's launcher loads
#     ~/.jichi.env. The script never reads, prints or copies the key.
#
# Usage:
#   scripts/corpus-drive.sh --dir DIR --arm NAME --config FILE --workspace CLONE \
#       --list FILE [--tasks DIR] [--base COMMIT] [--verify CMD] \
#       [--edit-scope GLOB ...] [--env-file FILE] [--bin JICHI] \
#       [--timeout SECS] [--free-prefix PREFIX] [--dry-run]
#
#   --dry-run prints, one per line, the arguments each task's jichi would get,
#   and runs nothing -- the way to see a fence before a model does.
#
#   DIR is the drive's own directory, OUTSIDE every repository: output lands in
#   DIR/out/NAME. --bin defaults to DIR/driver/jichi -- pin one first with
#   scripts/pin-driver.sh --prefix DIR/driver, so a build in the jichi tree
#   cannot delete the binary a run is using.
#
#   scripts/corpus-drive.sh --self-test
#
#   --self-test proves the priced-model refusal two-sided, offline, with planted
#   configs: a vendor id on a remote endpoint (refused), the same id as a SECOND
#   entry (refused -- every model is checked, not only the active one), a
#   free-namespace id (passes), a loopback server with any id (passes), and a
#   loopback look-alike host (refused). It is here rather than in the smoke tier
#   because the check needs python3 and the smoke tier runs none (M209) -- the
#   precedent is tests/bench/version_probe.py's self-test mode.
#
# Exit: 0 when the list ran (whatever each task's own rc was -- those are data),
# 2 on a refusal or a usage error, before any request.
set -u

die() { echo "corpus-drive: $*" >&2; exit 2; }

# check_models CONFIG FREE_PREFIX -- exit 0 if every model is loopback or in the
# free namespace; exit 2 with the offending entries named otherwise. One function
# for the real run and for --self-test, so the test cannot drift from the check.
check_models() {
    python3 - "$1" "$2" <<'EOF'
import json, sys
cfg, free = sys.argv[1], sys.argv[2]
try:
    c = json.load(open(cfg))
except ValueError as e:
    print("corpus-drive: %s is not valid JSON: %s" % (cfg, e), file=sys.stderr); sys.exit(2)
ms = c.get("models") or ([c["model"]] if isinstance(c.get("model"), dict) else [])
if not ms:
    print("corpus-drive: %s names no model" % cfg, file=sys.stderr); sys.exit(2)
bad = []
for m in ms:
    base = str(m.get("apiBase", ""))
    host = base.split("://", 1)[-1].split("/", 1)[0]
    if host.startswith("["):
        host = host[1:].split("]", 1)[0]
    else:
        host = host.rsplit(":", 1)[0]
    loopback = host in ("localhost", "::1") or host.split(".")[0] == "127" and host.count(".") == 3
    if not loopback and not str(m.get("model", "")).startswith(free):
        bad.append("%s (%s at %s)" % (m.get("name"), m.get("model"), base or "no apiBase"))
if bad:
    print("corpus-drive: refusing -- not in the free namespace '%s' and not loopback: %s"
          % (free, "; ".join(bad)), file=sys.stderr)
    sys.exit(2)
EOF
}

self_test() {
    SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
    command -v python3 >/dev/null 2>&1 || { echo "corpus-drive: self-test needs python3" >&2; exit 2; }
    t=$(mktemp -d) || exit 2
    fails=0
    one() {  # one NAME WANT(0|2) JSON
        printf '%s\n' "$3" > "$t/$1.json"
        check_models "$t/$1.json" "jlu/" 2> "$t/$1.err"
        got=$?
        if [ "$got" = "$2" ]; then echo "ok - $1 -> $got"; else echo "not ok - $1 -> $got, wanted $2"; fails=$((fails + 1)); fi
    }
    one priced-remote 2 '{"models":[{"name":"a","model":"vendor/model-x","apiBase":"https://gw.example/v1"}]}'
    one priced-second-entry 2 '{"models":[{"name":"a","model":"jlu/free","apiBase":"https://gw.example/v1"},{"name":"b","model":"vendor/model-x","apiBase":"https://gw.example/v1"}]}'
    one free-remote 0 '{"models":[{"name":"a","model":"jlu/free","apiBase":"https://gw.example/v1"}]}'
    one loopback-any 0 '{"models":[{"name":"a","model":"anything","apiBase":"http://127.0.0.1:1234/v1"}]}'
    one loopback-lookalike 2 '{"models":[{"name":"a","model":"anything","apiBase":"http://127.0.0.1.gw.example/v1"}]}'
    one no-model 2 '{"models":[]}'
    # The edit-scope globs must reach jichi LITERALLY. The first version of this
    # script expanded them in its own shell, against the launch directory, and
    # handed jichi hundreds of that directory's file paths instead of `src/**`
    # (2026-09-23: every zigodot edit would have been reverted as out of scope).
    # So run a dry run from a directory where `src/**` WOULD expand.
    mkdir -p "$t/cwd/src" "$t/cwd/tests" "$t/ws" "$t/tasks"
    : > "$t/cwd/src/a.c"
    : > "$t/cwd/tests/b.c"
    git -C "$t/ws" init -q
    git -C "$t/ws" -c user.name=t -c user.email=t@example.org commit -q --allow-empty -m init
    printf '{"models":[{"name":"a","model":"m","apiBase":"http://127.0.0.1:9/v1"}]}\n' > "$t/cfg.json"
    printf 'go\n' > "$t/tasks/t.md"
    printf 't edit\n' > "$t/list"
    printf '#!/bin/sh\necho "build: selftest"\n' > "$t/bin"
    chmod +x "$t/bin"
    ( cd "$t/cwd" && sh "$SELF" --dir "$t" --arm st --config "$t/cfg.json" \
        --workspace "$t/ws" --list "$t/list" --tasks "$t/tasks" --bin "$t/bin" \
        --verify true --dry-run ) > "$t/dry.out" 2>&1
    if grep -qx 'src/\*\*' "$t/dry.out" && grep -qx 'tests/\*\*' "$t/dry.out" &&
       ! grep -q 'src/a\.c' "$t/dry.out"; then
        echo "ok - edit-scope globs reach jichi literally, not expanded by this shell"
    else
        echo "not ok - edit-scope globs were expanded before jichi saw them: $(grep -c -e 'a\.c' -e 'b\.c' "$t/dry.out") expanded path(s)"
        fails=$((fails + 1))
    fi
    rm -rf "$t"
    [ "$fails" = 0 ] && { echo "self-test: ok (7 cases)"; exit 0; }
    echo "self-test: $fails case(s) failed" >&2
    exit 1
}
[ "${1:-}" = "--self-test" ] && self_test

DIR="" ARM="" CFG="" WS="" LIST="" TASKS="" BASE="" VERIFY="" ENVF="" BIN=""
TMO_SECS=3600 FREE="jlu/" SCOPES="" DRY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --dir) DIR=$2; shift 2 ;;
        --arm) ARM=$2; shift 2 ;;
        --config) CFG=$2; shift 2 ;;
        --workspace) WS=$2; shift 2 ;;
        --list) LIST=$2; shift 2 ;;
        --tasks) TASKS=$2; shift 2 ;;
        --base) BASE=$2; shift 2 ;;
        --verify) VERIFY=$2; shift 2 ;;
        --edit-scope) SCOPES="$SCOPES
$2"; shift 2 ;;
        --env-file) ENVF=$2; shift 2 ;;
        --bin) BIN=$2; shift 2 ;;
        --timeout) TMO_SECS=$2; shift 2 ;;
        --free-prefix) FREE=$2; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
        *) die "unknown argument: $1 (see --help)" ;;
    esac
done
[ -n "$DIR" ] && [ -n "$ARM" ] && [ -n "$CFG" ] && [ -n "$WS" ] && [ -n "$LIST" ] ||
    die "--dir, --arm, --config, --workspace and --list are required"
[ -n "$TASKS" ] || TASKS="$DIR/tasks"
[ -n "$BIN" ] || BIN="$DIR/driver/jichi"
[ -n "$SCOPES" ] || SCOPES="
src/**
tests/**"
[ -x "$BIN" ] || die "no jichi binary at $BIN (pin one: scripts/pin-driver.sh --prefix $DIR/driver)"
[ -f "$CFG" ] || die "no config at $CFG"
[ -f "$LIST" ] || die "no task list at $LIST"
[ -d "$WS/.git" ] || die "$WS is not a git clone -- the workspace must be a clone you can lose"
if [ -n "$ENVF" ] && [ ! -r "$ENVF" ]; then die "--env-file $ENVF is not readable"; fi

# The priced-model refusal, before anything is sent. python3 reads the JSON; the
# check is on the config the run will actually use, not on a separate list.
command -v python3 >/dev/null 2>&1 || die "python3 is needed to read the config"
check_models "$CFG" "$FREE" || exit 2

TMO=timeout
command -v gnutimeout >/dev/null 2>&1 && TMO=gnutimeout
OUT="$DIR/out/$ARM"
mkdir -p "$OUT" || die "cannot create $OUT"
[ -n "$BASE" ] || BASE=$(git -C "$WS" rev-parse HEAD) || die "cannot read the workspace HEAD"
{
    echo "arm:       $ARM"
    echo "started:   $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "base:      $BASE"
    echo "binary:    $("$BIN" --version 2>/dev/null | sed -n 's/^build: //p')"
    echo "config:    $CFG"
    echo "timeout:   $TMO $TMO_SECS s (SIGINT, then SIGKILL after 60 s)"
    echo "verify:    ${VERIFY:-(none)}"
} >> "$OUT/MANIFEST"

n=$(ls "$OUT"/run-*.jsonl 2>/dev/null | wc -l)
while read -r task kind; do
    [ -n "$task" ] || continue
    [ -f "$TASKS/$task.md" ] || { echo "corpus-drive: no task file $TASKS/$task.md -- skipped" >&2; continue; }
    n=$((n + 1))
    id=$(printf '%02d-%s' "$n" "$task")
    (cd "$WS" && git reset -q --hard "$BASE" && git clean -qfd) || die "cannot reset $WS"
    set -- --config "$CFG" --auto --no-route --no-session --quiet --lease fail \
        --log "$OUT/telemetry.jsonl" --log-level full \
        --journal "$OUT/journal-$id.jsonl" --revert-out-of-scope --output jsonl
    # set -f: the globs are jichi's to match, inside the workspace -- never this
    # shell's to expand, against whatever directory it was launched from. Without
    # it the first real drive handed jichi hundreds of the jichi tree's own file
    # paths instead of `src/**` (the --self-test case above catches it).
    old_ifs=$IFS
    IFS='
'
    set -f
    for g in $SCOPES; do
        [ -n "$g" ] && set -- "$@" --edit-scope "$g"
    done
    set +f
    IFS=$old_ifs
    if [ "$kind" = edit ] && [ -n "$VERIFY" ]; then
        set -- "$@" --verify "$VERIFY" --verify-baseline
    fi
    if [ -n "$DRY" ]; then
        printf '== %s\n' "$id"
        printf '%s\n' "$@" -p "(the prompt in $TASKS/$task.md)"
        continue
    fi
    s=$(date +%s)
    (
        cd "$WS" || exit 99
        if [ -n "$ENVF" ]; then set -a; . "$ENVF"; set +a; fi
        exec "$TMO" -s INT -k 60 "$TMO_SECS" "$BIN" "$@" -p "$(cat "$TASKS/$task.md")" \
            < /dev/null > "$OUT/run-$id.jsonl" 2> "$OUT/run-$id.err"
    )
    rc=$?
    e=$(date +%s)
    (cd "$WS" && git diff > "$OUT/diff-$id.patch" && git status --porcelain > "$OUT/status-$id.txt")
    printf '%s\t%s\t%s\t%s\trc=%s\t%ss\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$ARM" "$id" "$kind" \
        "$rc" "$((e - s))" >> "$OUT/runs.tsv"
done < "$LIST"
exit 0
