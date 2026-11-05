#!/bin/sh
# smoke lint: every req.fail_class write goes through jc_http_fail_class (M1042, card 76).
#
# WHAT THIS EXISTS FOR. M1039 added the fail_class telemetry token and a pure
# classifier (jc_http_fail_class, src/net/jc_http.c) for it. But the vocabulary
# had SEVERAL writers: the classifier ran only on libcurl failures, while the
# 4xx/5xx value was a literal in the stream OK path (jc_http.c:1234) with an
# agent fallback (jc_agent.c:385), and "aborted"/"provider-error" were agent
# literals. The consequence (measured by the supervisor at 2026-10-22 13:41):
# the classifier's http-status and provider-error branches were dead at runtime,
# and the telemetry_fail_class driver's LEG 3 could not go red for any single
# writer -- you had to break the classifier AND an agent literal together. This
# lint pins the fix: one function (jc_http_fail_class) is the sole writer of
# every fail_class token, and its returned set is the documented vocabulary.
#
# WHAT IS AND IS NOT CHECKED.
#   checked  -- ZERO direct `fail_class = "<literal>"` writes remain in src/
#              (the four M1039 sites); that the classifier is DEFINED in
#              exactly one file; that every documented vocabulary token is
#              RETURNED by the classifier; and that the two stream-path call
#              sites still exist (floor).
#   NOT checked -- the classifier's decision ORDER or branch logic (that is
#              the unit test's job, test_http.c; the classifier is untouched
#              here) and the runtime effect on a real failure (that is the
#              telemetry_fail_class driver's job).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home

# --- 1: ZERO direct literal writes -------------------------------------------
# grep -c prints 0 AND exits 1 with no matches, so use find|grep + test -z
# (config_bool_lint check 15). On the unfixed tree this prints exactly four
# lines: jc_http.c:1234 and jc_agent.c:353/380/385.
# Exclude comment lines: lines starting with whitespace followed by * (C-style
# comment continuation) or after /* are comments.
hand=$(find "$SMOKE_ROOT/src" -name '*.c' -exec grep -Hn 'fail_class *= *"' {} + \
       2>/dev/null | grep -v ':[0-9]*: *\*.*fail_class *= *"' | sed "s|^$SMOKE_ROOT/||")
if [ -z "$hand" ]; then
    t_ok "no direct fail_class literal write remains in src/ (all go through the classifier)"
else
    t_fail "direct fail_class literal write(s) remain -- the writer must be
 jc_http_fail_class, not a hardcoded token (M1042):
$hand"
fi

# --- 2: the classifier is defined in exactly one file ------------------------
impls=$(find "$SMOKE_ROOT/src" -name '*.c' -exec grep -l 'char \*jc_http_fail_class(' {} + \
        2>/dev/null | sed "s|^$SMOKE_ROOT/||" | sort | tr '\n' ' ')
if [ "$impls" = "src/net/jc_http.c " ]; then
    t_ok "jc_http_fail_class is defined in exactly one file (src/net/jc_http.c)"
else
    t_fail "the fail_class classifier is defined in: $impls -- it must be ONE
 function that computes every class (card 76). If it moved on purpose, update
 this expectation and say why."
fi

# --- 3: every documented vocabulary token is returned by the classifier ------
# Pins the nine-token vocabulary (include/jc_http.h) against a future edit that
# drops a token from BOTH the classifier and the doc.
missing=""
for tok in connect timeout-connect timeout-tls timeout-stall dns refused http-status provider-error aborted; do
    n=$(grep -c "return \"$tok\"" "$SMOKE_ROOT/src/net/jc_http.c" 2>/dev/null)
    [ -n "$n" ] || n=missing
    if [ "$n" = missing ] || [ "$n" -lt 1 ]; then
        missing="$missing $tok"
    fi
done
if [ -z "$missing" ]; then
    t_ok "all nine documented fail_class tokens are returned by the classifier"
else
    t_fail "the classifier no longer returns token(s):$missing -- the vocabulary
 (include/jc_http.h) and the classifier have drifted (M1042)"
fi

# --- 4: the three stream-path call sites still exist (floor 3) ---------------
# grep -c exits 1 at 0; floor-on-extraction-broken as in config_bool_lint.
# After M1042 jc_http.c has exactly three writer call sites (the perform path
# ~1131, the stream curl-error path ~1245, and the stream OK path ~1233 added
# by M1042); at floor 2, deleting the OK-path site still passed (the probe
# measured it), so the new site was pinned by the driver alone; at floor 3,
# deleting ANY one goes red. See probe report:
# docs/internal/drive/2026-10-17-hardening/reports/2026-10-22-m1042-probe.md
calls=$(grep -c 'req->fail_class = jc_http_fail_class(' "$SMOKE_ROOT/src/net/jc_http.c" 2>/dev/null)
[ -n "$calls" ] || calls=missing
if [ "$calls" = missing ]; then
    t_fail "src/net/jc_http.c is unreadable from this lint -- the extraction is
 broken, and a broken extraction reports a clean universe"
elif [ "$calls" -lt 3 ]; then
    t_fail "src/net/jc_http.c has $calls fail_class classifier call(s), floor is 3 --
 a call disappeared (perform, stream curl-error, stream OK path). If the writer
 moved on purpose, lower the floor here and say why."
else
    t_ok "src/net/jc_http.c: $calls classifier call sites (floor 3: perform + stream x2)"
fi

t_done
