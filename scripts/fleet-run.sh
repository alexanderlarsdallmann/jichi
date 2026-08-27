#!/bin/sh
# fleet-run.sh -- run one jichi task across several DEVICES at once, from here.
#
# WHY THIS EXISTS, AND WHY IT IS NOT THE LOOP.
#
# docs/AUTONOMOUS_LOOPS.md already describes running N jichi instances against
# one queue, and its claiming rule is an atomic rename(2) into running/ -- "no
# lock file, no coordinator". That is a good design and it silently assumes ONE
# FILESYSTEM. A fleet spread over a Raspberry Pi, an Android tablet and a proot
# guest has no shared filesystem, and reaching for NFS or sshfs to manufacture
# one would put a network filesystem's rename semantics underneath the only
# thing keeping two agents off the same task.
#
# So this is a third topology, not a replacement:
#
#   single instance   -> loop.sh, one queue          (AUTONOMOUS_LOOPS.md 2)
#   N on one machine  -> N loop.sh, shared queue     (AUTONOMOUS_LOOPS.md 2)
#   N across machines -> THIS: the supervisor PUSHES  (M459)
#
# Push is what removes the coordination problem instead of solving it. The
# supervisor already holds an ssh connection per device -- that is how every
# hardware row in this repo is driven -- and an assignment carried over that
# connection is exactly-once by construction. No queue, no claim, no lock.
#
# DESIGN DECISIONS, and what each rejects:
#
# 1. Devices are THIN CLIENTS. The model stays on the workstation and every
#    device is given only an apiBase. docs/ROBOTICS_BRINGLIST.md states this in
#    bold for boards, and it holds here for the same reason: a 4 GB device that
#    is also hosting a model is measuring the model, not jichi.
#
# 2. The DEADLINE scales per device; the TOKEN budget does not. A token is the
#    same work everywhere, so scaling it would make the devices incomparable.
#    Wall-clock is not: the same turn takes an order of magnitude longer on a
#    Pi Zero than here. Each target therefore carries the multiplier its
#    tier-b row measured (device build secs / this host's 6.19 s), and the
#    deadline is multiplied by it. A fleet whose deadline is uniform is a fleet
#    that kills its slowest member and calls it a timeout.
#
# 3. Every agent is FENCED to a scratch workspace it owns. --edit-scope plus
#    the path fence plus an isolated HOME, all under a directory whose whole
#    purpose is to be thrown away. The blast radius of a bad turn is a scratch
#    tree, and that is what makes --auto acceptable on somebody's tablet.
#
#    --strict-scope as well, which is the half an outer fence cannot supply.
#    jichi's own doctor says so plainly: "a shell command
#    (run_terminal_command) can reach past it and is detected afterward, not
#    prevented". Detected-afterward is the wrong tense for an unattended run on
#    a device whose owner is not watching, so the shell is closed rather than
#    audited. Note the cost honestly: a fleet task that genuinely needs to run
#    a build cannot use this script as written, and should not silently get the
#    weaker fence instead.
#
#    Snapshots are turned on through the pushed CONFIG, not a flag -- there is
#    no --snapshots, and inventing one cost this script its first dispatch.
#
# 4. Verdicts come from POSITIVE MARKERS in the jsonl, never from exit codes
#    (docs/BUILD.md, M368). An ssh that dies mid-run and a jichi that refused
#    the task both produce a non-zero status; only the stream says which.
#
# 5. --heartbeat is on. Without it a supervisor cannot tell a wedged device
#    from a long model call, which on a fleet is the difference between waiting
#    and losing an hour (M165 added the flag for exactly this).
#
# Usage:
#   scripts/fleet-run.sh --targets FILE --task FILE --model URL [options]
#   scripts/fleet-run.sh --targets FILE --task FILE --model URL --dry-run
#
# The targets file is one device per line, tab- or space-separated:
#
#   # label     ssh-target            mult  cc     ssh-options
#   pi400       user@192.0.2.10       3     -      -i ~/.ssh/id_bench
#   termux      u0_a283@127.0.0.1     7     clang  -i ~/.ssh/id_bench -p 8022
#
# `cc` is the compiler for --deploy ("-" for the platform default). Termux needs
# clang named explicitly; a Debian guest does not.
#
# Options:
#   --targets FILE   device list (required)
#   --task FILE      the prompt, sent verbatim to every device (required)
#   --model URL      apiBase every device is given (required; a LAN address,
#                    not localhost -- localhost on a device is the device)
#   --model-name N   model id to request (default: the server's first)
#   --budget-tokens N   per-device token budget (default 200k)
#   --deadline D     BASE wall-clock, scaled per device by its mult (default 10m)
#   --max-tool-calls N  per-device attempt cap (default 40)
#   --context-length N  the model's REAL window, declared to every device.
#                    Worth passing: undeclared, jichi assumes ~32000, and a
#                    server whose window is 8192 then overflows on every turn.
#                    doctor warns about this; the fleet should not need to be
#                    told twice.
#   --out DIR        where artifacts land (default ./.fleet-results)
#   --deploy         ship this tree and build it on each device first. Separate
#                    from the run on purpose: a build is minutes on a board and
#                    rebuilding every dispatch would make the fleet's wall-clock
#                    the compiler's rather than the agent's. Deploy once, run
#                    many. The tree shipped is HEAD, so a dirty working copy
#                    cannot silently become what the fleet is running.
#   --dry-run        print the plan and the exact per-device command; run nothing
#   --help
set -u

TARGETS=""; TASK=""; MODEL=""; MODEL_NAME=""
BUDGET="200k"; DEADLINE="10m"; MAXCALLS="40"; CTXLEN=""
OUT="./.fleet-results"; DRY=0; DEPLOY=0

usage() { sed -n '2,90p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
    case "$1" in
        --targets)  TARGETS="$2"; shift ;;
        --task)     TASK="$2";    shift ;;
        --model)    MODEL="$2";   shift ;;
        --model-name) MODEL_NAME="$2"; shift ;;
        --budget-tokens) BUDGET="$2"; shift ;;
        --deadline) DEADLINE="$2"; shift ;;
        --max-tool-calls) MAXCALLS="$2"; shift ;;
        --context-length) CTXLEN="$2"; shift ;;
        --out)      OUT="$2";     shift ;;
        --deploy)   DEPLOY=1 ;;
        --dry-run)  DRY=1 ;;
        --help|-h)  usage ;;
        *) echo "fleet-run: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

[ -n "$TARGETS" ] || { echo "fleet-run: --targets is required" >&2; exit 2; }
[ -n "$TASK" ]    || { echo "fleet-run: --task is required" >&2; exit 2; }
[ -n "$MODEL" ]   || { echo "fleet-run: --model is required -- a device has no model of its own" >&2; exit 2; }
[ -r "$TARGETS" ] || { echo "fleet-run: cannot read $TARGETS" >&2; exit 2; }
[ -r "$TASK" ]    || { echo "fleet-run: cannot read $TASK" >&2; exit 2; }

# A LOCALHOST apiBase is the single most likely way to get this wrong, and it
# fails in a way that looks like the model being down: on the device,
# 127.0.0.1 is the DEVICE. Refuse rather than let every row fail identically.
case "$MODEL" in
    *127.0.0.1*|*localhost*)
        echo "fleet-run: --model must be an address the DEVICES can reach." >&2
        echo "  '$MODEL' resolves to the device itself, not this workstation." >&2
        exit 2 ;;
esac

REMOTE_DIR="jichi-fleet"
mkdir -p "$OUT"

# Scrub only the artifacts we write, by name -- --out may point somewhere with
# other content, and a stale file from a dead run must not answer for a live
# one (the lesson tier-b-device.sh learned the hard way in the same session).
rm -f "$OUT"/fleet-summary.txt
: > "$OUT/fleet-summary.txt"

note() { echo "$*" >> "$OUT/fleet-summary.txt"; }
say()  { echo "== $*"; note "== $*"; }

say "fleet-run -- $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# task    : $TASK"
note "# model   : $MODEL"
note "# budget  : $BUDGET tokens, base deadline $DEADLINE, <= $MAXCALLS tool calls"
note ""

# ---------------------------------------------------------------- the plan
NTARGET=0
while IFS= read -r line; do
    case "$line" in ''|\#*) continue ;; esac
    NTARGET=$((NTARGET + 1))
done < "$TARGETS"
say "$NTARGET device(s)"

if [ "$DRY" -eq 1 ]; then
    echo
    echo "fleet-run: DRY RUN -- nothing will be executed"
    while IFS= read -r line; do
        case "$line" in ''|\#*) continue ;; esac
        label=$(echo "$line" | awk '{print $1}')
        target=$(echo "$line" | awk '{print $2}')
        mult=$(echo "$line" | awk '{print $3}')
        cc=$(echo "$line" | awk '{print $4}')
        sshopts=$(echo "$line" | awk '{for(i=5;i<=NF;i++) printf "%s%s", $i, (i<NF?" ":"")}')
        echo
        echo "  [$label] $target   mult=$mult  cc=$cc"
        echo "    deadline : $DEADLINE x $mult"
        echo "    ssh      : ssh $sshopts $target"
        echo "    workspace: ~/$REMOTE_DIR/ws   (scratch; --edit-scope confines writes to it)"
        echo "    command  : jichi --auto --output jsonl --heartbeat 30 \\"
        echo "                 --edit-scope '**' --path-fence --strict-scope \\"
        echo "                 --budget-tokens $BUDGET --deadline <scaled> \\"
        echo "                 --max-tool-calls $MAXCALLS -p <task>"
    done < "$TARGETS"
    exit 0
fi

# ---------------------------------------------------------------- per device
run_one() {
    _label="$1"; _target="$2"; _mult="$3"; _sshopts="$4"; _cc="${5:-}"
    [ -z "$_cc" ] || _cc="CC=$_cc "
    _dir="$OUT/$_label"
    rm -rf "$_dir"; mkdir -p "$_dir"

    # shellcheck disable=SC2086
    _ssh="ssh $_sshopts -o BatchMode=yes -o StrictHostKeyChecking=no \
-o UserKnownHostsFile=/dev/null -o ConnectTimeout=15"

    if ! $_ssh "$_target" true 2>/dev/null; then
        echo "unreachable" > "$_dir/verdict"
        return
    fi

    if [ "$DEPLOY" -eq 1 ]; then
        $_ssh "$_target" "rm -rf \$HOME/$REMOTE_DIR && mkdir -p \$HOME/$REMOTE_DIR" 2>/dev/null
        git archive --format=tar HEAD \
            | $_ssh "$_target" "cd \$HOME/$REMOTE_DIR && tar xf -" 2>/dev/null
        if ! $_ssh "$_target" "cd \$HOME/$REMOTE_DIR && \
${_cc}make jichi >/dev/null 2>&1 && test -x ./jichi && echo BUILD_OK" \
             2>/dev/null | grep -q BUILD_OK; then
            echo "build-failed" > "$_dir/verdict"
            return
        fi
    fi

    if ! $_ssh "$_target" "test -x \$HOME/$REMOTE_DIR/jichi" 2>/dev/null; then
        echo "not-deployed" > "$_dir/verdict"
        return
    fi

    # Scaled deadline. Only the NUMBER is scaled; the unit is carried through,
    # so "10m" x 7 is "70m" and not seventy of something else.
    _num=$(echo "$DEADLINE" | sed 's/[^0-9]//g')
    _unit=$(echo "$DEADLINE" | sed 's/[0-9]//g')
    [ -n "$_num" ] || _num=10
    [ -n "$_unit" ] || _unit=m
    _scaled="$(( _num * _mult ))$_unit"
    echo "$_scaled" > "$_dir/deadline"

    # Fresh scratch workspace + isolated HOME, every run.
    $_ssh "$_target" "rm -rf \$HOME/$REMOTE_DIR/ws \$HOME/$REMOTE_DIR/.home && \
mkdir -p \$HOME/$REMOTE_DIR/ws \$HOME/$REMOTE_DIR/.home" 2>/dev/null

    # The config the device runs under. apiKey is a PLACEHOLDER, and that is a
    # decision rather than a limitation.
    #
    # The HRZ gateway turns out to be publicly reachable (a keyless request from
    # the Pi returns 401, not a connection failure), so a device COULD call it
    # directly -- if it held the key. It does not, and will not: a fleet member
    # is a borrowed tablet or a board on a shelf, and a credential written to
    # its disk outlives the run, the task and usually the operator's memory of
    # having put it there.
    #
    # So the split is: DEVICES use a keyless model on the LAN, and work that
    # needs a keyed model runs on the workstation where the key already lives.
    # Rejected: pushing the key (simple, and wrong for the reason above).
    # Deferred: a key-injecting relay on the workstation, which would let
    # devices use keyed models without holding credentials -- worth building if
    # a fleet ever needs a model the LAN cannot serve, and not before.
    _mn="$MODEL_NAME"; [ -n "$_mn" ] || _mn="local"
    _ctx=""
    [ -z "$CTXLEN" ] || _ctx="\"contextLength\":$CTXLEN,"
    $_ssh "$_target" "cat > \$HOME/$REMOTE_DIR/config.json" <<CFG 2>/dev/null
{"models":[{"name":"fleet","provider":"openai","model":"$_mn",
 "apiBase":"$MODEL","apiKey":"unused",$_ctx"roles":["chat"]}],
 "snapshots":true,"pathFence":true,"lowResource":false,
 "privilegedCommands":"deny","privilegedAudit":true,
 "maxRetries":1}
CFG

    $_ssh "$_target" "cat > \$HOME/$REMOTE_DIR/task.txt" < "$TASK" 2>/dev/null

    # --edit-scope '**' is NOT "anything": the path fence pins the root to the
    # workspace first, so '**' means "anything INSIDE the scratch tree".
    $_ssh "$_target" "cd \$HOME/$REMOTE_DIR/ws && \
HOME=\$HOME/$REMOTE_DIR/.home \
\$HOME/$REMOTE_DIR/jichi --config \$HOME/$REMOTE_DIR/config.json \
  --auto --no-session --output jsonl --heartbeat 30 \
  --edit-scope '**' --path-fence --strict-scope \
  --budget-tokens $BUDGET --deadline $_scaled --max-tool-calls $MAXCALLS \
  --journal \$HOME/$REMOTE_DIR/run.jsonl \
  -p \"\$(cat \$HOME/$REMOTE_DIR/task.txt)\" < /dev/null" \
        > "$_dir/stream.jsonl" 2> "$_dir/stderr.txt"

    $_ssh "$_target" "cat \$HOME/$REMOTE_DIR/run.jsonl 2>/dev/null" \
        > "$_dir/journal.jsonl" 2>/dev/null

    # POSITIVE MARKER, never the exit code: a dead ssh and a refused task both
    # exit non-zero, and only the stream distinguishes them.
    #
    # But "done" alone is NOT a success verdict, and reporting it as one was
    # this script's own first defect. A run against a model that DESCRIBES tool
    # calls instead of invoking them terminates perfectly cleanly: stop_reason
    # done, no error, tokens spent -- and an empty workspace. The first
    # dispatch here reported "1 completed, 0 did not" for a run that created
    # nothing, which is precisely the shape of report a supervisor must never
    # produce.
    #
    # jichi already says it. The run journal carries `no_changes: true`
    # alongside tool_calls/tool_calls_executed; nothing needed inventing, the
    # supervisor simply was not reading what it was given. A fleet is exactly
    # where this matters: nobody is watching the device, so the summary IS the
    # result.
    # SCOPE EVERY JOURNAL READ TO THIS RUN. The device's journal ACCUMULATES --
    # jichi appends each run to the path it is given -- so a bare grep answers
    # with whatever any earlier run said. That is how this script spent an
    # afternoon accusing jichi of mis-reporting `no_changes`: run 1 (against a
    # model that only described tool calls) legitimately recorded
    # no_changes:true, and every later run inherited it, including two that had
    # demonstrably written their files. jichi was correct in all three; the
    # supervisor was reading a dead run's answer.
    #
    # It is the same defect tier-b-device.sh was fixed for earlier the same day
    # -- a stale artifact answering for a live one -- reintroduced here in a
    # different shape, which is the argument for filtering by run id rather
    # than remembering to wipe a file.
    _run=$(grep -o '"run":"[0-9a-f-]*"' "$_dir/stream.jsonl" 2>/dev/null \
           | tail -1 | cut -d'"' -f4)
    if [ -n "$_run" ]; then
        grep -F "$_run" "$_dir/journal.jsonl" > "$_dir/journal-thisrun.jsonl" 2>/dev/null
    else
        : > "$_dir/journal-thisrun.jsonl"
    fi

    if grep -q '"type":"done"' "$_dir/stream.jsonl" 2>/dev/null; then
        if grep '"event":"end"' "$_dir/journal-thisrun.jsonl" 2>/dev/null \
           | tail -1 | grep -q '"no_changes":true'; then
            echo "done-no-changes" > "$_dir/verdict"
        else
            echo "done" > "$_dir/verdict"
        fi
    elif [ -s "$_dir/stream.jsonl" ]; then
        echo "incomplete" > "$_dir/verdict"
    else
        echo "no-stream" > "$_dir/verdict"
    fi
}

while IFS= read -r line; do
    case "$line" in ''|\#*) continue ;; esac
    label=$(echo "$line" | awk '{print $1}')
    target=$(echo "$line" | awk '{print $2}')
    mult=$(echo "$line" | awk '{print $3}')
    # cut -d' ' cannot split tab- or multi-space-separated columns; awk's
    # default field splitting handles both. The dry run caught this by printing
    # an ssh line with the target in it twice.
    cc=$(echo "$line" | awk '{print $4}')
    [ "$cc" = "-" ] && cc=""
    sshopts=$(echo "$line" | awk '{for(i=5;i<=NF;i++) printf "%s%s", $i, (i<NF?" ":"")}')
    case "$mult" in ''|*[!0-9]*) mult=1 ;; esac
    say "dispatching to $label ($target, deadline x$mult)"
    run_one "$label" "$target" "$mult" "$sshopts" "$cc" &
done < "$TARGETS"

wait

# ---------------------------------------------------------------- collect
note ""
note "device        verdict      stop_reason        tokens   attempted/executed"
note "------------  -----------  -----------------  -------  ------------------"
NOK=0; NBAD=0
for d in "$OUT"/*/; do
    [ -d "$d" ] || continue
    lbl=$(basename "$d")
    v=$(cat "$d/verdict" 2>/dev/null || echo "?")
    sr=$(grep -o '"stop_reason":"[^"]*"' "$d/stream.jsonl" 2>/dev/null | tail -1 | cut -d'"' -f4)
    # this run's rows only -- see the note beside the verdict above
    jr="$d/journal-thisrun.jsonl"; [ -s "$jr" ] || jr="$d/journal.jsonl"
    end=$(grep '"event":"end"' "$jr" 2>/dev/null | tail -1)
    tk=$(printf '%s' "$end" | grep -o '"tokens_used":[0-9.]*' | cut -d: -f2)
    ta=$(printf '%s' "$end" | grep -o '"tool_calls":[0-9]*' | cut -d: -f2)
    te=$(printf '%s' "$end" | grep -o '"tool_calls_executed":[0-9]*' | cut -d: -f2)
    [ -n "$sr" ] || sr="-"; [ -n "$tk" ] || tk="-"
    [ -n "$ta" ] || ta="-"; [ -n "$te" ] || te="-"
    note "$(printf '%-12s  %-11s  %-17s  %-7s  %s/%s' "$lbl" "$v" "$sr" "$tk" "$ta" "$te")"
    if [ "$v" = "done" ]; then NOK=$((NOK + 1)); else NBAD=$((NBAD + 1)); fi
done

note ""
note "$NOK completed with changes, $NBAD did not"
if grep -q "done-no-changes" "$OUT"/*/verdict 2>/dev/null; then
    note ""
    note "NOTE: a device reported done-no-changes -- the run terminated cleanly"
    note "and altered nothing. Check the executed count in the table first: 0"
    note "executed usually means the model DESCRIBED tool calls instead of"
    note "invoking them, which \`jichi doctor --live\` names directly (it reports"
    note "tool calling observed \"text\"). A non-zero executed count with no"
    note "changes means the run did read-only work and never wrote."
fi
cat "$OUT/fleet-summary.txt"
echo
echo "fleet-run: artifacts in $OUT/<device>/{stream.jsonl,journal.jsonl,stderr.txt}"
[ "$NBAD" -eq 0 ]
