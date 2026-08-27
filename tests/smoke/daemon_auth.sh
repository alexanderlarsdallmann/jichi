#!/bin/sh
# smoke: the daemon's access control, as an EFFECT and as a stated posture
# (M528).
#
# WHAT THIS EXISTS FOR. docs/DAEMON.md said it without hedging: "The socket's
# file mode is the entire access-control list. There is no token, no
# authentication and no peer check." Two things followed from taking that
# seriously:
#
#   1. The mode was REQUESTED (a umask around bind, then jc_make_private) and
#      never VERIFIED -- and the code's own comment names the case neither call
#      can rule out: "there are platforms that ignore the umask for sockets".
#      There, the kernel makes a 0755 socket, this daemon (which runs tools and
#      shell commands as its own user) becomes a shell prompt for every local
#      account, and nothing says so. It now reads the mode back and refuses to
#      serve if it is wrong.
#   2. There was no request-size limit AT ALL: daemon_read_line read one byte at
#      a time into an unbounded buffer until a newline, so a client that never
#      sent one made the server allocate until it died.
#
# And `hello` reports the posture rather than implying it -- including
# `"peercred":false`, which exists in order to say that no peer-credential check
# is performed (SO_PEERCRED and struct ucred are hidden under _POSIX_C_SOURCE on
# glibc, and getpeereid is BSD-only). Claiming an authentication you do not
# perform is worse than having none.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- hello's reply shape and its two honesty fields; that a
#                   LEGACY request still works with no handshake (docs/EMBEDDING.md
#                   declares those shapes Stable); the line cap's named error; and
#                   the holding-directory warning WITH its absence pair.
#   NOT checked  -- the refusal path for a too-open socket. It cannot be provoked
#                   on a platform that honours the umask, which is the only kind
#                   this tier runs on; its logic is unit-tested pure in
#                   tests/test_platform.c (test_priv_verdict, 15 cases).
#   NOT checked  -- peer identity, because none is established. See above.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
SOCKQ="$SMOKE_TOOLS/sockq"

cat > "$tmp/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:9/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "timeouts":{"connect":2,"request":5,"stall":5}}
EOF

# An AF_UNIX path is capped near 107 bytes, and smoke_tmp can be long, so the
# socket goes in a short private dir of our own. Measured: a path one byte over
# makes the daemon refuse with a clear message, which is correct and is not what
# this driver is testing.
short="/tmp/jcauth.$$"
mkdir -p "$short" && chmod 0700 "$short"
sock="$short/d.sock"

start_daemon() {  # start_daemon <socket> <logfile>
    (cd "$tmp" && exec "$BIN" --config "$tmp/config.json" daemon --socket "$1" \
        < /dev/null > "$2" 2>&1) &
    _dpid=$!
    _i=0
    while [ ! -S "$1" ]; do
        kill -0 "$_dpid" 2>/dev/null || return 1
        _i=$((_i + 1)); [ $_i -gt 10 ] && return 1
        sleep 1
    done
    return 0
}
stop_daemon() { printf '{"type":"shutdown"}\n' | "$SOCKQ" --deadline 10 "$1" \
                >/dev/null 2>&1; }

if start_daemon "$sock" "$tmp/d.log"; then
    t_ok "the daemon started and created its socket"
else
    t_fail "daemon never created its socket: $(tail -c 200 "$tmp/d.log" 2>/dev/null)"
    t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -
    rm -rf "$short"; t_done
fi

# --- 1: the mode it actually created -----------------------------------------
# The daemon refuses to serve a socket that is not owner-only, so a running
# daemon is itself evidence -- but assert the mode directly too, because "it
# started" and "the mode is 0600" are different claims.
mode=$(ls -l "$sock" | cut -c1-10)
case "$mode" in
    srw-------) t_ok "the socket is owner-only ($mode)" ;;
    *) t_fail "the socket is $mode -- the daemon served a socket it should have refused" ;;
esac

# --- 2-4: hello reports the posture, honestly --------------------------------
out=$(printf '{"v":1,"type":"hello","client":"smoke"}\n' | "$SOCKQ" --deadline 15 "$sock")
case "$out" in
    *'"type":"hello.ok"'*) t_ok "hello returns hello.ok" ;;
    *) t_fail "hello got: $out" ;;
esac
case "$out" in
    *'"modeVerified":true'*) t_ok "and states that the mode was verified, not merely requested" ;;
    *) t_fail "hello.ok does not report modeVerified: $out" ;;
esac
# The honesty field. If this ever says true, a peer check must actually exist.
case "$out" in
    *'"peercred":false'*) t_ok "and admits that peer credentials are NOT checked" ;;
    *'"peercred":true'*) t_fail "hello.ok claims peercred is checked -- no such check
 exists in this build; claiming an authentication you do not perform is worse
 than having none" ;;
    *) t_fail "hello.ok omits the peercred field entirely: $out" ;;
esac

# --- 5: the Stable legacy shape still works with NO handshake ----------------
# docs/EMBEDDING.md lists `prompt`/`ping`/`shutdown` as Stable. Requiring a
# handshake would break that promise, so this must keep working forever.
out=$(printf '{"type":"ping"}\n' | "$SOCKQ" --deadline 15 "$sock")
case "$out" in
    *pong*) t_ok "a legacy ping with no hello still returns pong" ;;
    *) t_fail "the legacy shape broke: $out" ;;
esac

# --- 6: the line cap is an error, not a truncation ---------------------------
# Deliberately no trailing newline: the point is a sender that never frames.
big=$( (printf '{"type":"ping","pad":"'; awk 'BEGIN{while(i++<70000)printf "0123456789012345"}') \
       | "$SOCKQ" --deadline 20 "$sock" )
case "$big" in
    *'"code":"limit.line"'*) t_ok "an over-long request line gets a named limit error" ;;
    *pong*) t_fail "an over-long line was TRUNCATED and served as a valid ping --
 a request cut mid-flight means something its sender did not ask for" ;;
    *) t_fail "over-long line got: $(printf '%s' "$big" | cut -c1-120)" ;;
esac
# --- 7b: a long-but-legal line, spanning many read chunks (M530) ------------
# The reader takes 4 KiB at a time now, so a request whose newline lands in a
# later chunk exercises the accumulate path the byte-at-a-time version never
# had. Under the cap, so it must SUCCEED -- the cap check must not fire early,
# and the reassembled line must still parse.
long=$( (printf '{"type":"ping","pad":"'
         awk 'BEGIN{while(i++<600)printf "0123456789012345678901234567890123456789012345678901234567890123"}'
         printf '"}\n') | "$SOCKQ" --deadline 20 "$sock" )
case "$long" in
    *pong*) t_ok "a ~38 KB request spanning many read chunks is reassembled" ;;
    *) t_fail "a legal multi-chunk request failed: $(printf '%s' "$long" | cut -c1-120)" ;;
esac
stop_daemon "$sock"

# --- 8-9: the holding directory, and the absence pair ------------------------
# A 0600 socket is no defence if another user can unlink it and bind their own.
# Writable-by-others is acceptable only with the sticky bit -- which is what
# makes /tmp legitimate and 0777 not.
opendir="/tmp/jcauth-open.$$"
mkdir -p "$opendir" && chmod 0777 "$opendir"
if start_daemon "$opendir/d.sock" "$tmp/open.log"; then
    if grep -q 'writable by others' "$tmp/open.log"; then
        t_ok "a world-writable, non-sticky holding directory is reported"
    else
        t_fail "no warning for a 0777 holding directory: $(tail -c 200 "$tmp/open.log")"
    fi
    stop_daemon "$opendir/d.sock"
else
    t_fail "the daemon would not start in a 0777 directory: $(tail -c 200 "$tmp/open.log")"
fi

# The absence pair (M310): /tmp is 1777 -- writable by all, sticky -- and must
# NOT warn, or the check is just noise about every legitimate location.
stickysock="/tmp/jcauth-sticky.$$.sock"
if start_daemon "$stickysock" "$tmp/sticky.log"; then
    if grep -q 'writable by others' "$tmp/sticky.log"; then
        t_fail "a STICKY directory (/tmp) was reported as unsafe -- the check
 fires on every legitimate location and would be trained away"
    else
        t_ok "a sticky directory (/tmp) is not reported (the absence pair)"
    fi
    stop_daemon "$stickysock"
else
    t_fail "the daemon would not start with a socket in /tmp: $(tail -c 200 "$tmp/sticky.log")"
fi

rm -rf "$short" "$opendir" "$stickysock"
t_done
