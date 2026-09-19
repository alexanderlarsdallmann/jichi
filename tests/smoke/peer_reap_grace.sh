#!/bin/sh
# smoke: a peer that ignores SIGTERM cannot hang jichi's exit (M661b).
#
# THE DEFECT, and it is the third and last row of the M609 family. MCP and LSP
# shutdown both did:
#
#     kill(pid, SIGTERM);
#     waitpid(pid, &status, 0);      /* blocking, no timeout */
#
# A server that traps or ignores SIGTERM therefore hung jichi's exit FOREVER --
# no journal finalisation, no lease release, and nothing on screen saying why.
# The row sat from M609 because its test was called "deterministic but fiddly in
# POSIX sh", which was fair: it needs a child that really survives a TERM.
#
# THE FIX IS THE HELPER THE PARALLEL POOL AND THE DAEMON ALREADY USE.
# jc_worker_reap_grace polls waitpid(WNOHANG) for JC_WORKER_TERM_GRACE_MS, then
# SIGKILLs and block-reaps -- so the parent cannot wait on a child that will not
# die. Nothing new was invented for this; the row named the drop-in and it was
# right.
#
# WHAT IS AND IS NOT COVERED:
#   checked -- the mock really survives SIGTERM (so check 2 cannot pass
#              vacuously); jichi's exit is BOUNDED against such a server, which
#              is the whole property; and a well-behaved server is unaffected,
#              so the grace window did not become a delay everyone pays.
#   not checked -- the LSP site. The same two lines and the same helper, but
#              driving an LSP server that traps TERM needs a second mock
#              speaking a second protocol for one shared call; the MCP path is
#              the one with a fixture already in this tier. Stated rather than
#              implied, and the LSP change is one line of the same kind.
#   not checked -- that SIGKILL specifically is what ends it. The property is
#              "jichi exits"; asserting the signal would pin an implementation
#              detail the helper is free to change.
. "$(dirname "$0")/_smoke.sh"

t_plan 3

smoke_home
tmp=$(smoke_tmp)

# THE DEAF SERVER IS DERIVED FROM THE TIER'S OWN MOCK, not hand-rolled.
# The first version of this driver wrote its own JSON-RPC responses and
# deadlocked the REQUEST path -- jichi sat in select() waiting for a reply while
# the mock sat in read() waiting for a request, and the driver reported 45s as
# though it had measured the shutdown path. It had measured nothing of the kind.
# `smoke_write_mock_mcp` is already proven by three other drivers; the only
# changes here are the two that make it deaf:
#
#   trap '' TERM          -- survive the shutdown signal
#   exec >&- 2>&-       -- close jichi's pipe, so EOF is not what is measured
#   read from a fifo    -- LINGER, which is the condition a blocking waitpid
#                          cannot survive.
#
# THE LINGER MUST NOT FORK, and this cost a second FreeBSD diagnosis. It was
# `sleep 120`, a CHILD of the mock. When jichi reaped the mock that sleep was
# orphaned -- and FreeBSD's timeout(1) acquires reaper status, so the orphan
# reparented to the per-driver `timeout` the smoke runner wraps every driver in
# (tests/smoke/_smoke.sh, the deadline wrapper). The runner then declared this
# driver FAILED while its own captured output showed 1..3 and three `ok` lines:
# a green driver reported red because of a grandchild it left behind. Blocking
# on a read from a fifo lingers inside the mock's OWN shell, so there is no
# second process to orphan, and jichi's SIGKILL ends it.
smoke_write_mock_mcp "$tmp/deaf_mcp.sh"
{
    head -1 "$tmp/deaf_mcp.sh"
    printf "trap '' TERM\n"
    tail -n +2 "$tmp/deaf_mcp.sh"
    printf 'exec >&- 2>&-\n_f="$0.wait"; mkfifo "$_f" 2>/dev/null\nread _x < "$_f"\n'
} > "$tmp/deaf2.sh"
mv "$tmp/deaf2.sh" "$tmp/deaf_mcp.sh"
chmod +x "$tmp/deaf_mcp.sh"

cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1"}],
 "mcpServers":[{"name":"deaf","command":"/bin/sh","args":["$tmp/deaf_mcp.sh"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"lowResource":false}
EOF

# --- 1: the mock really survives SIGTERM --------------------------------------
# Without this the driver passes on a server that simply exited when asked.
/bin/sh "$tmp/deaf_mcp.sh" < /dev/null > /dev/null 2>&1 &
deaf=$!
sleep 1
kill -TERM "$deaf" 2>/dev/null
sleep 1
if kill -0 "$deaf" 2>/dev/null; then
    t_ok "the mock survives SIGTERM (pid $deaf still alive), as a trapping server does"
else
    t_fail "the mock DIED on SIGTERM -- it is not standing in for a server that \
traps it, and check 2 would pass without exercising the reap at all"
fi
kill -KILL "$deaf" 2>/dev/null

# --- 2: jichi's exit is bounded -----------------------------------------------
# The property. Without the grace reap the parent blocks in waitpid forever and
# only the deadline ends it.
# MEASURE THE SUBJECT'S EXIT, NOT THE WRAPPER'S RETURN (2026-09-18, FreeBSD).
# This check used `with_deadline 45` and timed how long IT took. On Linux the two
# are the same number. On FreeBSD they are not, and the difference is 50 seconds:
#
#     jichi own exit took 0s
#     with_deadline returned after 50s      <- same command, same fixture
#
# FreeBSD's timeout(1) acquires REAPER status (procctl PROC_REAP_ACQUIRE) so it
# can kill a whole tree, so when jichi reaps the deaf server the server's own
# child -- the `sleep 120` that makes it linger -- is reparented to TIMEOUT and
# timeout waits for it. The wrapper was measuring the fixture's grandchild.
#
# The driver then printed "that is what a blocking waitpid on a TERM-trapping
# child looks like" -- a CAUSE inferred from a duration and never measured. It
# accused the very fix it exists to defend, on a platform where that fix worked
# perfectly. Asserting a diagnosis a check cannot see is the error; the duration
# was real and the explanation was invented.
#
# So: run jichi as OUR OWN child and `wait` for it. A shell waits for its own
# children only, so an orphaned grandchild cannot inflate the number on any
# platform. The 45-second bound stays, as a watchdog that kills rather than as
# the thing being timed.
# A POLL, not a backgrounded watchdog subshell. The first version wrote
#     ( sleep 45; kill -9 $_jp ) &
# and smoke_lint check 15 failed it: on OpenBSD ksh `$!` after a backgrounded
# SUBSHELL is the subshell, not the command inside it, so the later `kill`
# reaches the wrong process. Polling needs no second process at all.
t0=$(date +%s)
"$BIN" --config "$tmp/config.json" mcp resources < /dev/null > /dev/null 2>&1 &
_jp=$!
_n=0
while kill -0 "$_jp" 2>/dev/null && [ "$_n" -lt 45 ]; do
    sleep 1
    _n=$((_n + 1))
done
kill -9 "$_jp" 2>/dev/null
wait "$_jp" 2>/dev/null
t1=$(date +%s)
if [ $((t1 - t0)) -lt 25 ]; then
    t_ok "jichi exits in $((t1 - t0))s against a server that ignores SIGTERM"
else
    t_fail "jichi took $((t1 - t0))s to exit against a server that ignores \
SIGTERM (want < 25). This times jichi ITSELF, not a wrapper, so a slow fixture \
cannot cause it -- but it does NOT tell you WHERE it waited. Read the stack \
before naming a cause: a blocking waitpid after SIGTERM is the defect this \
driver was built for, and posix_utils_lint check 19 finds that shape statically."
fi

# --- 3: a well-behaved server is unaffected -----------------------------------
# The grace window must not become a delay every shutdown pays.
smoke_write_mock_mcp "$tmp/good_mcp.sh"
sed "s|$tmp/deaf_mcp.sh|$tmp/good_mcp.sh|; s|\"deaf\"|\"good\"|" \
    "$tmp/config.json" > "$tmp/config-good.json"
g0=$(date +%s)
out=$(with_deadline 30 "$BIN" --config "$tmp/config-good.json" mcp resources \
      < /dev/null 2>&1)
g1=$(date +%s)
if printf '%s' "$out" | grep -q 'mem://notes' && [ $((g1 - g0)) -lt 15 ]; then
    t_ok "a well-behaved server still answers, and shuts down without the grace delay"
else
    t_fail "good server: $((g1 - g0))s, output: $(printf '%s' "$out" | head_bytes 150)"
fi

t_done
