#!/bin/sh
# smoke: a COLD first call whose DNS RESOLVE times out is a transport failure
# that honours timeouts.connect (or fails the resolve fast) -- NEVER a
# reused-connection stream death (M1062-c, SV:1141, card 91).
#
# THE FINDING. Run 0079 measured the -b (M1062) misreading this exact call:
# a model call whose apiBase name could not be resolved STUCK in the resolver
# and was recorded fail_class=timeout-stall with the dead-stream message -- a
# *stall* where nothing connected, when timeouts.connect is the bound that
# fired. The -b's reused discriminator (jc_http_reused_conn) read the call as
# REUSED. M1062-c corrects it with a THIRD input, bytes sent
# (CURLINFO_REQUEST_SIZE > 0): 0 on a resolve timeout (nothing sent), 158 on a
# reused-then-dead transfer (measured, libcurl 8.18.0). With it the resolve
# timeout is NOT reused -> kept (M326v) -> reads a CONNECT failure, not a stall.
#
# TWO SHAPES, ONE ENVIRONMENT-DEPENDENT. The same netns (below) behaves
# differently depending on whether the resolver's UDP syscalls are FENCED:
#
#   * CONTAINER (the lead's sandbox, this cell's home) -- the resolver is
#     KILLED FAST: the netns syscall fence makes the resolver's getaddrinfo
#     fail immediately, so libcurl returns rc=6 "Could not resolve host" in ~10
#     ms and NO UDP is ever sent to the blackhole (measured run 0080: getent 8
#     ms; libcurl rc=6 ~10 ms; the blackhole's rx_queue stays 0). jichi records
#     fail_class `dns` (jc_http_fail_class's rc==6 row) and the transport
#     "Could not resolve hostname (before connecting)".
#
#   * HOST (the supervisor's drive image, SV:1141) -- the resolver HANGS: it
#     sends UDP to the blackhole and waits for the glibc default, so curl's
#     connect bound fires INSIDE the resolve -> rc=28 "Timeout was reached".
#     Measured SV:1141: 2000.88 ms on master (timeouts.connect 2), 30030 ms
#     on the -b. jichi records fail_class `timeout-connect` and the transport
#     names timeouts.connect. THIS is the shape the supervisor verifies on the
#     host; the container cannot produce it.
#
#   Both are honest GREEN on the FIXED (-c) tree. The regression (the -b reading
#   a resolve failure as a reused stream death) is RED where it actually lives:
#   on the -b, the rc=28 HANG shape reads fail_class `timeout-stall` with the
#   dead-stream message (the -b's empty-watch + connect_time-0 reading). It is
#   the hang shape's red, verified on the host (the rc==28 gate that consults
#   jc_http_reused_conn is what admits it; see the report).
#
# THE REGRESSION GUARD (checks 3-4, the load-bearing assertions, valid in BOTH
# environments): the failed row's fail_class is NOT timeout-stall, and the
# transport message does NOT name the dead-stream fence (timeouts.deadStream /
# "went dead" / "stalled"). On the unfixed -b's hang shape BOTH fail: the class
# is timeout-stall and the message names the fence. The guard is what keeps the
# -b red -- no assertion here may be loosened to make a wrong class pass.
#
# THE SHAPE-SPECIFIC CHECK (check 5, one TAP check that BRANCHES on the recorded
# fail_class and never silently passes):
#   * fail_class == timeout-connect (host hang)  -> the message names
#     timeouts.connect AND latency_ms < 15000 (the connect bound fired inside
#     the resolve; SV:1141 measured 2000.88 ms on master with the 2 s bound).
#   * fail_class == dns (container fast-fail)    -> the message is the resolve
#     failure (libcurl's "Could not resolve hostname (before connecting)" --
#     verified verbatim in the container) AND latency_ms < 15000 (nothing
#     hung; the fence killed the resolver in ~10 ms).
#   * any OTHER fail_class -> t_fail naming it (an unknown shape is a red,
#     never a pass).
# The plan count stays 5: one check now branches.
#
# THE ROUTE. The smoke tier cannot bind :53 unprivileged, so the cell runs in
# unshare -Urmn (root: bring up loopback via lo_up, SIOCSIFFLAGS; udpblack reads
# 127.0.0.1:53 and never answers; resolv.conf -> nameserver 127.0.0.1;
# timeouts.connect 8, stall 0, request 0, deadStream 5, maxRetries 0). t_skips
# ONLY when unshare -Urmn is unavailable -- a real skip, not a pass
# (SV:1141). On a host where unshare -Urmn works (the lead's / CI environment)
# it RUNS.
#
# with_deadline 40 wraps the whole unshare in THIS shell (_smoke.sh function).
. "$(dirname "$0")/_smoke.sh"

# Decide it can run BEFORE the plan (smoke_lint check 20): a t_skip after t_plan
# emits a second TAP plan line and the runner reads the driver as failed.
unshare -Urmn true 2>/dev/null \
    || t_skip "unshare -Urmn unavailable (no private netns, or the sandbox's shell fence blocks the uid_map write) -- the resolve-timeout repro needs a private netns; the unit row in test_http.c is the load-bearing tooth"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# apiBase names a host that will NOT resolve: the query goes to the black hole
# (127.0.0.1:53) and is never answered (host) or is killed fast (container).
# timeouts.connect 8 is the bound that must fire on the hang shape; stall 0 and
# request 0 turn those caps OFF; deadStream 5 is the FENCE that must NOT fire --
# so ONLY the connect bound bounds a hang-shape call (the -c's whole point).
# maxRetries 0 and --no-session keep it a single cold call.
cat > "$tmp/config.json" <<'CFG'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://resolve-blackhole.invalid/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0,
"timeouts":{"connect":8,"stall":0,"request":0,"deadStream":5}}
CFG

# The netns: bring up loopback, blackhole :53, point resolv.conf at it.
# BIN/config/ws are passed as positional args to sh -c (with_deadline is a
# function, so it must wrap the unshare in THIS shell).
with_deadline 40 unshare -Urmn sh -c '
    "$1"/lo_up || exit 1
    # The blackhole is a DAEMON inside the netns. Give it its own closed
    # stdin/stdout/stderr so it never inherits this sh stdout -- which is the
    # driver stdout. An inherited pipe write end is what kept the driver stdout
    # open after the driver exited and made a foreground run block until the
    # orphan died (SV:1557, the 85-minute hang).
    "$1"/udpblack 53 < /dev/null > /dev/null 2>&1 &
    blackpid=$!
    # Reap it on EVERY exit of this sh: a normal return (jichi below exits, we
    # exit $rc), a mid-setup failure (an exit 1 above), or an interrupt or
    # deadline (TERM/INT). blackpid is assigned before the trap, so the
    # double-quoted argument already holds the right pid. Set the trap BEFORE
    # running jichi: after an exec the shell is replaced and the trap would be
    # lost, so jichi is a plain foreground command and we capture its rc and
    # exit on it. (The netns dies with unshare regardless; the trap is the
    # belt-and-braces that closes the pipe and leaves no orphan in the brief
    # window before the netns is torn down.)
    trap "kill $blackpid 2>/dev/null" EXIT TERM INT
    mount -t tmpfs tmpfs /etc || exit 1
    echo "nameserver 127.0.0.1" > /etc/resolv.conf
    cd "$2" && "$3" --config "$4" --no-session \
        --log "$5" --log-level metrics \
        -q --auto -p hi < /dev/null > "$6" 2> "$7"
    rc=$?
    exit "$rc"
' _ "$SMOKE_TOOLS" "$ws" "$BIN" "$tmp/config.json" "$tmp/telemetry.jsonl" "$tmp/out.txt" "$tmp/err"
rc=$?

# The blackhole is reaped inside the netns sh by its EXIT/TERM/INT trap (above)
# on every exit path -- jichi returning, a mid-setup failure, the deadline, or an
# interrupt -- and its stdout/stderr are closed to /dev/null, so it holds no
# driver pipe and leaves no orphan when the netns dies with unshare.

# 1: THE PRECONDITION -- the call actually failed (rc != 0). A resolve timeout
# is a transport failure; if rc is 0 the cell did not reproduce the shape.
if [ "$rc" -ne 0 ]; then
    t_ok "the cold resolve-timeout call failed (rc=$rc)"
else
    t_fail "the call did NOT fail (rc=0) -- the resolve was answered; the black hole did not take effect; check udpblack/resolv.conf, never this assertion"
fi

# The failed model_call row (ok=false). grep isolates it; jsonq -l reads the
# one document per line (reuse_dead_stream.sh's shape).
$G '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
    | "$JQ" -l '.ok' 2>/dev/null > "$tmp/oks" || true
$G '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
    > "$tmp/calls" || true
nfail=0
while read -r o; do
    [ "$o" = "false" ] && nfail=$((nfail + 1))
done < "$tmp/oks"

# 2: the PRECONDITION -- a failed model_call row exists. If the resolve timeout
# was not recorded as a failure, the cell did not reproduce and checks 3-5 have
# nothing to read: fail them all (never read silence as green).
if [ "$nfail" -lt 1 ]; then
    t_fail "no failed model_call in telemetry (nfail=$nfail) -- the resolve timeout was not recorded as a failure; the cell is not reproduced"
    t_fail "no failed row to read fail_class (nfail=$nfail)"
    t_fail "no failed row to read the message (nfail=$nfail)"
    t_fail "no failed row to read the latency (nfail=$nfail)"
    t_done
fi
t_ok "a failed model_call is in telemetry (ok=false, $nfail)"

# The failed row (the last one; there is exactly one cold call, maxRetries 0).
failed_row=$("$G" '"ok":false' "$tmp/calls" | tail -n 1)
[ -n "$failed_row" ] || failed_row=""
fc=$(printf '%s' "$failed_row" | "$JQ" .fail_class 2>/dev/null)
tr=$(printf '%s' "$failed_row" | "$JQ" -l .transport 2>/dev/null)
lat=$(printf '%s' "$failed_row" | "$JQ" .latency_ms 2>/dev/null)

# 3: THE REGRESSION GUARD -- the failed row's fail_class is NOT timeout-stall.
# On the unfixed -b's HANG shape (rc=28, the host) the row IS timeout-stall: the
# -b read the empty per-transfer watch + connect_time 0 as a reused connection
# and reported a stream death where nothing connected. The fixed (-c) tree
# records timeout-connect (hang) or dns (container fast-fail) -- neither is
# timeout-stall, so this is GREEN in both environments. (The -b's FAST-FAIL
# shape, rc=6, also reads dns and passes here: the -b's rc==28 gate short-
# circuits it, so the -b regression this guard pins lives on the hang shape.)
case "$fc" in
    timeout-stall)
        t_fail "the cold resolve-timeout call is timeout-stall (RED: the -b read the empty watch + connect_time 0 as a reused stream death; M1062-c not fixed)" ;;
    timeout-connect|dns)
        t_ok "the cold resolve-timeout call is $fc (a connect/resolve failure, NOT a reused stream death)" ;;
    *)
        t_fail "the cold resolve-timeout call has an unexpected fail_class=$fc (expected timeout-connect on the hang shape or dns on the container fast-fail; never timeout-stall)" ;;
esac

# 4: THE REGRESSION GUARD -- the transport message does NOT name the dead-stream
# fence (timeouts.deadStream / "went dead" / "stalled"). The fence (deadStream 5)
# must NOT fire: with stall 0 and request 0, only the connect bound bounds a
# hang-shape call, and the container fast-fails before any connect. On the
# unfixed -b's hang shape the message reads "connected, then the stream went
# dead ... (timeouts.deadStream)" -- RED. The fixed tree's messages name
# timeouts.connect (hang) or the resolve failure (container) -- GREEN in both.
case "$tr" in
    *deadStream*|*went*dead*|*stall*)
        t_fail "the message names the dead-stream fence (RED: the fence fired / a stream death was reported, not the connect bound; got: $tr)" ;;
    *)
        t_ok "the message does not name the dead-stream fence (got: $tr)" ;;
esac

# 5: THE SHAPE-SPECIFIC CHECK -- branch on the recorded fail_class; one TAP
# check, never silently passing, with the latency bound folded in (count stays
# 5). The lat_int math mirrors the old check 5 (15 s floor = "nothing stuck in
# the resolver's ~30 s glibc default").
lat_int=${lat%%.*}
case "$lat_int" in
    ''|*[!0-9]*) lat_int=0 ;;
esac
case "$fc" in
    timeout-connect)
        # Hang shape (host, SV:1141): the connect bound fired INSIDE the
        # resolve. The message names timeouts.connect; latency is the ~8 s
        # bound, well under 15 s (SV measured 2000.88 ms on master at 2 s).
        case "$tr" in
            *timeouts.connect*)
                if [ -n "$lat" ] && [ "$lat" != "null" ] && [ "$lat_int" -lt 15000 ]; then
                    t_ok "hang shape: names timeouts.connect and the connect bound fired inside the resolve (latency ${lat_int} ms < 15000)"
                else
                    t_fail "hang shape: the connect bound did NOT fire inside the resolve (latency ${lat:-none} ms; want < 15000)"
                fi ;;
            *)
                t_fail "hang shape: the message does not name timeouts.connect (got: $tr)" ;;
        esac ;;
    dns)
        # Fast-fail shape (container): the netns fence killed the resolver, so
        # libcurl read rc=6 "Could not resolve host" in ~10 ms (measured run 0080,
        # blackhole rx_queue 0). The message is the resolve failure; latency is
        # tiny, well under 15 s. The string is verified verbatim in the
        # container (the report).
        case "$tr" in
            *"Could not resolve"*|*"before connecting"*)
                if [ -n "$lat" ] && [ "$lat" != "null" ] && [ "$lat_int" -lt 15000 ]; then
                    t_ok "fast-fail shape: the resolve failed fast (got: $tr; latency ${lat_int} ms < 15000, nothing hung)"
                else
                    t_fail "fast-fail shape: the resolve did NOT fail fast (latency ${lat:-none} ms; want < 15000)"
                fi ;;
            *)
                t_fail "fast-fail shape: the message is not a resolve failure (got: $tr)" ;;
        esac ;;
    *)
        t_fail "unknown shape: fail_class=$fc (expected timeout-connect [host hang] or dns [container fast-fail]) -- never a silent pass" ;;
esac

t_done
