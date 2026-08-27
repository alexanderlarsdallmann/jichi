# _smoke.sh - shared lib for the Python-free smoke tier (M209).
#
# Sourced (not executed) by every tests/smoke/*.sh driver. Strict POSIX sh:
# no `local`, no `[[`, no bashisms -- enforced by smoke_lint.sh. The tier's
# _e2e.py analog: TAP emission, temp/HOME isolation, mockmodel lifecycle,
# config heredocs, and a timeout fallback for boxes without timeout(1).
#
# Drivers: call t_plan FIRST, one t_ok/t_fail per check, t_done LAST.
# Run jichi as "$BIN" with stdin closed (< /dev/null) -- an open stdin on a
# non-TTY is read as prompt context and blocks a headless run forever.

# --- paths / env -------------------------------------------------------------
# $0 is the sourcing driver's path; drivers live beside this file.
SMOKE_DIR=$(cd "$(dirname "$0")" && pwd)
SMOKE_ROOT=$(cd "$SMOKE_DIR/../.." && pwd)
BIN="${JC_SMOKE_BIN:-$SMOKE_ROOT/jichi}"
SMOKE_TOOLS="$SMOKE_ROOT/tests/tools"

# Deterministic output; the caller's terminal must not leak into asserts.
LANG=C; LC_ALL=C; NO_COLOR=1
export LANG LC_ALL NO_COLOR

# --- TAP ---------------------------------------------------------------------
T_PLAN=0; T_N=0; T_FAILED=0

t_plan() {
    T_PLAN=$1
    echo "1..$1"
}

# smoke_plain FILE -- the file with ANSI escape sequences removed, portably.
#
# WHY THIS EXISTS (M471). Five drivers stripped escapes with
# `sed 's/\x1b[\[][0-9;?]*[a-zA-Z]//g'`, and **`\xNN` is a GNU sed extension**.
# POSIX sed has no such escape, so on OpenBSD the pattern matches nothing and every
# escape survives into the text the driver then searches.
#
# MEASURED OUTCOME, because the first guess was too tidy. This was predicted to be
# all three of OpenBSD's remaining smoke failures and it was ONE of them:
#
#   setup_keyfile       FIXED -- 28/28 after this change. Its width check counted the
#                       surviving escapes as columns and reported "3 line(s) over 76
#                       columns: [J  test command ... [31C[?2004h" -- a quoted line
#                       made mostly OF escapes, which is the tell that the stripper
#                       had silently done nothing.
#   sessions_footprint  STILL FAILS ("could not read the /context arena gauge")
#   turn_scratch        STILL FAILS ("could not read the turn-scratch gauge")
#
# So those two have a DIFFERENT cause, still undiagnosed. Both greps look POSIX-clean
# (`grep -o 'Arenas: session [0-9]* KB'`), so the text is likely absent or wrapped in
# the PTY transcript rather than mis-parsed. Recorded rather than guessed at again.
#
# The escape itself is nonetheless the same family as the `\b` of M466 and the GNU
# `\|` before it: a construct that is correct on every machine this project is
# developed on and matches nothing on a BSD.
#
# A literal ESC byte from printf is POSIX and works everywhere. One
# implementation, so the next driver that needs it cannot reintroduce the bug.
smoke_plain() {
    _sp_esc=$(printf '\033')
    sed "s/${_sp_esc}\[[0-9;?]*[a-zA-Z]//g" "$1" 2>/dev/null
}

# printf, never echo, for anything carrying a message. `echo` expands backslash
# escapes in dash (Linux /bin/sh) and in ksh (OpenBSD /bin/sh), so a diagnostic
# quoting the code it just rejected was CORRUPTED BY that code: a finding about a
# stray \b printed as a backspace and vanished from its own report, which cost a
# few minutes of believing a lint had matched a line that did not contain the
# pattern (2026-08-17). The message is an argument to %s here, so backslashes and
# percent signs both survive verbatim.
t_ok() {
    T_N=$((T_N + 1))
    printf 'ok %s - %s\n' "$T_N" "$1"
}

t_fail() {
    T_N=$((T_N + 1))
    T_FAILED=$((T_FAILED + 1))
    printf 'not ok %s - %s\n' "$T_N" "$1"
}

# Skip the whole driver (exit 0, empty TAP plan): for a check that this
# build legitimately cannot run (e.g. faults.sh without FAULT=1). Assert
# nothing rather than weaken the assertion -- mirrors _e2e.skip.
t_skip() {
    echo "1..0"
    printf '# skip: %s\n' "$1"
    exit 0
}

# Skip ONE check and keep going: emits the TAP SKIP directive and COUNTS toward
# the plan, so the checks after it still run and t_done's denominator stays
# honest. Use where a single check needs something this host lacks (a locale, a
# device) while the rest of the driver remains meaningful.
#
# This exists because t_skip above is a WHOLE-DRIVER verb and was being called
# mid-driver (M450). Found on Guix, whose glibc 2.33 predates C.UTF-8 (glibc
# 2.35): accessible.sh planned 8, passed 5, hit `t_skip` at check 6 and
# exited 0 -- silently abandoning checks 7 and 8, then failing the tier on
# "plan said 8 but 6 ran". Both halves are bad: coverage vanished without a
# word, and the red named a count rather than the missing locale. Any glibc
# older than 2.35 does this, which includes CentOS 7 (2.17) and Debian 9
# (2.24) -- two of this project's own Tier V rows.
t_skip_one() {
    T_N=$((T_N + 1))
    echo "ok $T_N - $1 # SKIP"
}

# The per-driver denominator: emitted checks must equal the plan, and none
# may have failed. run.sh re-checks the same sums suite-wide.
t_done() {
    if [ "$T_N" -ne "$T_PLAN" ]; then
        echo "not ok - plan said $T_PLAN checks but $T_N ran" >&2
        exit 1
    fi
    [ "$T_FAILED" -eq 0 ] || exit 1
    exit 0
}

# --- temp dirs + cleanup -------------------------------------------------------
SMOKE_TMPDIRS=""

# smoke_md_corpus OUT DIR... -- concatenate every *.md under DIR(s) into OUT.
#
# `grep --include=` / `--exclude=` / `--exclude-dir=` are GNU extensions. BSD
# grep does not reject them: it treats the argument as a FILENAME, reports
# "No such file or directory", and carries on searching WITHOUT the filter.
# That is two failure modes in one. Where the grep is wrapped in `!` (the
# "is this documented anywhere?" shape) its error status inverts every answer
# -- docs_flags reported 146 of 153 flags "documented nowhere" on OpenBSD with
# the docs untouched. Where it is not, the filter is silently dropped and the
# lint quietly searches more than it claims to.
#
# Building the corpus once is also what the loop shape wants: 153 flags across
# 611 files is ~93,000 greps, against one pass and 153 cheap ones. (M461)
smoke_md_corpus() {
    _out=$1; shift
    : > "$_out"
    find "$@" -type f -name '*.md' -exec cat {} + >> "$_out" 2>/dev/null
}

# head_bytes N [FILE] -- the first N bytes, of FILE or of stdin.
#
# `head -c` is a GNU/FreeBSD extension. OpenBSD's head has ONLY -n
# ("head: unknown option -- c"), which POSIX is the whole of what it requires.
# 139 uses of `head -c` had accumulated here; 137 were inside t_fail messages,
# so on OpenBSD a failing test would have printed head's usage string INSTEAD
# of the diagnostic that says what went wrong -- the instrument breaking
# exactly when it is needed. Found by the OpenBSD row (M461).
head_bytes() {
    if [ $# -ge 2 ]; then dd if="$2" bs="$1" count=1 2>/dev/null
    else dd bs="$1" count=1 2>/dev/null; fi
}

smoke_tmp() {
    _st_d=$(mktemp -d "${TMPDIR:-/tmp}/jichi_smoke.XXXXXX") || {
        echo "not ok - mktemp failed" >&2
        exit 1
    }
    SMOKE_TMPDIRS="$SMOKE_TMPDIRS $_st_d"
    printf '%s\n' "$_st_d"
}

# Can this host make a directory unreadable TO ITS OWNER?
#
# A driver that builds a negative fixture with `chmod 000` -- an unreadable
# session store, an unlistable index root -- has to ASK this rather than assume
# it, because two entirely different hosts answer no:
#
#   * root ignores permission bits, so a 000 directory stays listable; and
#   * on WINDOWS the OWNER keeps access whatever the mode records. Measured on
#     both Cygwin and MSYS2, and still true with MSYS2 mounted `acl`, where
#     chmod 600/644/700 ARE honoured -- so "modes work here" and "modes fence
#     the owner here" are different questions, and only the second one matters
#     for these fixtures.
#
# `sessions.sh` used to gate on `id -u` = 0 while asserting in a comment that
# "every platform can check it". MSYS2 disproved that
# (docs/analysis/2026-08-19-msys2-first-row.md). It is the same shape as M477's
# pid-1 and VmHWM assumptions: a fact about the hosts the author happened to run
# on, written down as a fact about POSIX. "Am I root" was one instance of the
# general question, so the general question is what this asks.
#
# Echoes "yes" or "no", and never fails, so it is safe inside a gate.
smoke_can_fence_owner() {
    _cfo_d=$(mktemp -d "${TMPDIR:-/tmp}/jichi_fence.XXXXXX" 2>/dev/null) || {
        printf 'no\n'
        return 0
    }
    : > "$_cfo_d/probe" 2>/dev/null
    chmod 000 "$_cfo_d" 2>/dev/null
    if ls "$_cfo_d" >/dev/null 2>&1; then
        _cfo_ans=no
    else
        _cfo_ans=yes
    fi
    # Restore before removing: where the fence DOES work, rm needs the bits back.
    chmod 755 "$_cfo_d" 2>/dev/null
    rm -rf "$_cfo_d" 2>/dev/null
    printf '%s\n' "$_cfo_ans"
}

smoke_cleanup() {
    # EVERY mock this driver started, not just the last one (M459).
    #
    # MM_PID holds only the most recent, so a driver calling mm_start twice
    # orphaned the first, and five drivers call mm_start and never mm_stop at
    # all. On Linux that is invisible: GNU timeout returns when its direct
    # child exits and the orphan dies on its own --deadline 120. On FreeBSD,
    # timeout waits for the whole group, so a 2.19-second driver returned 124
    # -- a "timeout" 118 seconds before any deadline, reported as a driver
    # failure though all five of its checks had passed.
    #
    # Fixed in the trap rather than in the five drivers: a cleanup that depends
    # on every author remembering is the kind of discipline that has already
    # failed five times here.
    for _sc_p in ${MM_PIDS:-} ${MM_PID:-}; do
        kill "$_sc_p" 2>/dev/null
        wait "$_sc_p" 2>/dev/null
    done
    MM_PIDS=""
    for _sc_d in $SMOKE_TMPDIRS; do
        rm -rf "$_sc_d"
    done
}
trap smoke_cleanup EXIT INT TERM

# Give this driver a private HOME (inside the runner's suite-wide one).
smoke_home() {
    HOME=$(smoke_tmp)
    export HOME
    # M376: neutralize the AMBIENT project config too. HOME isolation covers
    # ~/.jichi, but ./local/config.json outranks it (jc_config.c) and holds
    # real endpoints on a dev box -- M375's born-red run made live model
    # calls through exactly that channel from this tier. $JC_CONFIG outranks
    # the local file, and a driver's explicit --config outranks $JC_CONFIG,
    # so pinning it here changes nothing for config-passing drivers.
    printf '%s' '{}' > "$HOME/smoke-null-config.json"
    JC_CONFIG="$HOME/smoke-null-config.json"
    export JC_CONFIG
}

# --- mockmodel lifecycle -------------------------------------------------------
# mm_start SCRIPT CAPDIR [MAX_REQUESTS] -> exports MM_PORT, MM_PID.
# The port arrives via an atomically renamed port file; poll it coarsely
# (plain `sleep 1` -- fractional sleep is not POSIX).
mm_start() {
    _mm_script="$1"; _mm_cap="$2"; _mm_max="${3:-}"
    mkdir -p "$_mm_cap"
    _mm_pf="$_mm_cap/.port"
    rm -f "$_mm_pf"
    if [ -n "$_mm_max" ]; then
        "$SMOKE_TOOLS/mockmodel" --script "$_mm_script" \
            --capture "$_mm_cap" --port-file "$_mm_pf" \
            --deadline "${MM_DEADLINE:-120}" \
            --max-requests "$_mm_max" >/dev/null &
    else
        "$SMOKE_TOOLS/mockmodel" --script "$_mm_script" \
            --capture "$_mm_cap" --port-file "$_mm_pf" \
            --deadline "${MM_DEADLINE:-120}" >/dev/null &
    fi
    MM_PID=$!
    MM_PIDS="${MM_PIDS:-} $MM_PID"
    _mm_i=0
    while [ ! -s "$_mm_pf" ]; do
        kill -0 "$MM_PID" 2>/dev/null || {
            echo "not ok - mockmodel exited before announcing a port" >&2
            exit 1
        }
        _mm_i=$((_mm_i + 1))
        if [ "$_mm_i" -gt 10 ]; then
            echo "not ok - mockmodel did not announce a port" >&2
            exit 1
        fi
        sleep 1
    done
    MM_PORT=$(cat "$_mm_pf")
}

mm_stop() {
    if [ -n "${MM_PID:-}" ]; then
        kill "$MM_PID" 2>/dev/null
        wait "$MM_PID" 2>/dev/null
    fi
    MM_PID=""
}

# mm_start_unbounded SCRIPT CAPDIR -- like mm_start but with no
# --max-requests, for the spawn_parallel drivers that make an
# indeterminate number of calls (parent + N children + follow-ups). The
# sequential accept loop is faithful even here: a stalled child's
# connection is unblocked in bounded time by the watchdog/abort closing
# it, and the merge drivers' calls are quick (verified 9/9; M216 found
# D4's "needs concurrent accepts" assumption to be false). Runs until
# mm_stop / --deadline.
mm_start_unbounded() {
    MM_DEADLINE="${MM_DEADLINE:-120}" mm_start "$1" "$2"
}

# --- config ---------------------------------------------------------------------
# write_config PATH PORT [TOP_EXTRA] [MODEL_EXTRA]
# The throwaway config every mock driver uses (the shape the Python drivers
# built): one openai-wire model at the mock's port, resource features off,
# no retries. TOP_EXTRA / MODEL_EXTRA are raw JSON fragments (no leading
# comma), e.g.:  write_config c.json 8080 '' '"toolCalling":"none"'
write_config() {
    _wc_path="$1"; _wc_port="$2"; _wc_top="${3:-}"; _wc_model="${4:-}"
    cat > "$_wc_path" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$_wc_port/v1","apiKey":"x",
"roles":["chat"]${_wc_model:+,$_wc_model}}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,
"maxRetries":0${_wc_top:+,$_wc_top}}
EOF
}

# --- fixtures --------------------------------------------------------------------
# smoke_make_pdf PATH TEXT -- a minimal one-page PDF carrying TEXT. All
# content is ASCII and LC_ALL=C, so ${#var} counts bytes and the xref
# offsets are exact by construction (M212; shared by pdf.sh/docs_pdf.sh).
smoke_make_pdf() {
    _mp_stream="BT /F1 24 Tf 72 700 Td ($2) Tj ET"
    _mp_o1='<</Type/Catalog/Pages 2 0 R>>'
    _mp_o2='<</Type/Pages/Kids[3 0 R]/Count 1>>'
    _mp_o3='<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>'
    _mp_o4="<</Length ${#_mp_stream}>>stream
$_mp_stream
endstream"
    _mp_o5='<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>'
    _mp_body="%PDF-1.4
"
    _mp_off1=${#_mp_body}; _mp_body="${_mp_body}1 0 obj${_mp_o1}endobj
"
    _mp_off2=${#_mp_body}; _mp_body="${_mp_body}2 0 obj${_mp_o2}endobj
"
    _mp_off3=${#_mp_body}; _mp_body="${_mp_body}3 0 obj${_mp_o3}endobj
"
    _mp_off4=${#_mp_body}; _mp_body="${_mp_body}4 0 obj${_mp_o4}endobj
"
    _mp_off5=${#_mp_body}; _mp_body="${_mp_body}5 0 obj${_mp_o5}endobj
"
    _mp_xref=${#_mp_body}
    {
        printf '%s' "$_mp_body"
        printf 'xref\n0 6\n0000000000 65535 f \n'
        for _mp_off in $_mp_off1 $_mp_off2 $_mp_off3 $_mp_off4 $_mp_off5; do
            printf '%010d 00000 n \n' "$_mp_off"
        done
        printf 'trailer<</Size 6/Root 1 0 R>>\nstartxref\n%d\n%%%%EOF' \
            "$_mp_xref"
    } > "$1"
}

# --- mock MCP stdio server -------------------------------------------------------
# smoke_write_mock_mcp PATH -- emit a Python-free mock MCP server (the sh
# twin of the Python trio's shared mcp.py MOCK). jichi spawns it as an MCP
# stdio server; it speaks newline-framed JSON-RPC 2.0, parsing each request
# with jsonq and replying with canned results: initialize, tools/list
# (empty), resources/list+read (mem://notes -> NOTES_BODY), prompts/list+get
# (greet, echoing the `who` argument). Notifications (no id) get no reply.
# The jsonq path is baked in so the child needs no special environment.
smoke_write_mock_mcp() {
    cat > "$1" <<EOF
#!/bin/sh
JQ="$SMOKE_TOOLS/jsonq"
EOF
    cat >> "$1" <<'EOF'
while IFS= read -r line; do
    [ -n "$line" ] || continue
    id=$(printf '%s' "$line" | "$JQ" .id 2>/dev/null)
    method=$(printf '%s' "$line" | "$JQ" .method 2>/dev/null)
    # notifications carry no id -> no reply
    [ -n "$id" ] && [ "$id" != "null" ] || continue
    case "$method" in
    initialize)
        printf '{"jsonrpc":"2.0","id":%s,"result":{"protocolVersion":"2025-06-18","capabilities":{},"serverInfo":{"name":"mock"}}}\n' "$id" ;;
    tools/list)
        printf '{"jsonrpc":"2.0","id":%s,"result":{"tools":[]}}\n' "$id" ;;
    resources/list)
        printf '{"jsonrpc":"2.0","id":%s,"result":{"resources":[{"uri":"mem://notes","name":"Notes","description":"team notes","mimeType":"text/plain"}]}}\n' "$id" ;;
    resources/read)
        uri=$(printf '%s' "$line" | "$JQ" .params.uri 2>/dev/null)
        printf '{"jsonrpc":"2.0","id":%s,"result":{"contents":[{"uri":"%s","text":"NOTES_BODY for %s"}]}}\n' "$id" "$uri" "$uri" ;;
    prompts/list)
        printf '{"jsonrpc":"2.0","id":%s,"result":{"prompts":[{"name":"greet","description":"a greeting","arguments":[{"name":"who","required":true}]}]}}\n' "$id" ;;
    prompts/get)
        name=$(printf '%s' "$line" | "$JQ" .params.name 2>/dev/null)
        who=$(printf '%s' "$line" | "$JQ" .params.arguments.who 2>/dev/null)
        txt="GREETING from $name"
        [ -n "$who" ] && [ "$who" != "null" ] && txt="$txt who=$who"
        printf '{"jsonrpc":"2.0","id":%s,"result":{"messages":[{"role":"user","content":{"type":"text","text":"%s"}}]}}\n' "$id" "$txt" ;;
    *)
        printf '{"jsonrpc":"2.0","id":%s,"result":{}}\n' "$id" ;;
    esac
done
EOF
    chmod +x "$1"
}

# --- deadline fallback -----------------------------------------------------------
# with_deadline SECS CMD [ARGS...] -- coreutils timeout(1) when present,
# else a pure-sh watchdog (TERM, then KILL after a grace window). The
# helpers self-watchdog via --deadline; this guards direct jichi runs on
# timeout(1)-less boxes.
with_deadline() {
    _wd_secs="$1"; shift
    # M465: scale by the same knob as every other deadline in this tier.
    # tt_mult.c states the invariant -- "Every deadline in this tier must scale by
    # the same knob, or the knob is a lie" -- and records three layers found one at
    # a time: run.sh's outer per-driver limit (M220), ptydrive's expect/waitexit
    # (M272), and mockmodel's self-watchdog (M273, found the hard way because the
    # layer that died was not the one being raised). THIS was the fourth and the
    # largest: 210 call sites across 124 drivers, each a fixed wall-clock bound,
    # guarding exactly the slowest calls in the tier (see the note above -- it
    # guards direct jichi runs, i.e. a setup wizard or a `doctor` that makes
    # network probes). On a slow target those fail a HEALTHY run and then blame
    # whatever the driver was asserting.
    #
    # Clamped the way tt_mult_parse clamps: a non-numeric or < 1 value means 1, so
    # a deadline is never SHORTENED by a malformed knob.
    _wd_mult="${JC_SMOKE_TIMEOUT_MULT:-${JC_E2E_TIMEOUT_MULT:-1}}"
    case "$_wd_mult" in
        ''|*[!0-9]*) _wd_mult=1 ;;
    esac
    [ "$_wd_mult" -lt 1 ] 2>/dev/null && _wd_mult=1
    case "$_wd_secs" in
        ''|*[!0-9]*) ;;                                  # leave non-integers alone
        *) _wd_secs=$((_wd_secs * _wd_mult)) ;;
    esac
    if command -v timeout >/dev/null 2>&1; then
        timeout "$_wd_secs" "$@"
        return $?
    fi
    "$@" &
    _wd_cpid=$!
    (
        sleep "$_wd_secs"
        kill -TERM "$_wd_cpid" 2>/dev/null
        sleep 2
        kill -KILL "$_wd_cpid" 2>/dev/null
    ) &
    _wd_wpid=$!
    wait "$_wd_cpid"
    _wd_rc=$?
    kill "$_wd_wpid" 2>/dev/null
    wait "$_wd_wpid" 2>/dev/null
    return $_wd_rc
}
