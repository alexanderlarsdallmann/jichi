#!/bin/sh
# smoke: the `assignment` verb group over the daemon socket (M529).
#
# WHAT THIS EXISTS FOR. jichi's teaching features were reachable only by running
# the CLI and parsing output written for a human, so a course platform, a marking
# service or an editor plugin had to scrape. Three verbs -- `assignment.list`,
# `.get`, `.grade` -- hand back the same data the CLI renders, from the SAME
# collector and the SAME grading core, because a grade that differs between the
# terminal and the wire is not a grade.
#
# Two properties matter more than the happy path:
#
#   1. A NAME IS NOT A PATH. Grading runs the spec's own `verify` command, so a
#      caller who could name a location could name a file they wrote. The wire
#      takes a plain `<file>.md` and the server resolves it inside
#      docs/assignments/; anything expressing a location is refused.
#   2. A REFUSAL IS NOT A FAILING GRADE. M502's guard ("this is NOT a grade")
#      has to survive onto the wire: a spec whose verify script cannot run must
#      come back as an ERROR, never as passed:false -- otherwise a marking
#      service records a fail for a harness problem, which is the exact defect
#      M502 fixed for the CLI.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- the handshake advertises the group; list/get/grade over
#                   purpose-built fixtures; a passing AND a failing grade; the
#                   no-verify and not-gradeable refusals; and three shapes of
#                   name that must be refused.
#   NOT checked  -- `assignment.attempt`, which does not exist. Submitting an
#                   attempt means writing caller-supplied files into a workspace
#                   the daemon did not choose and grading them; it is left to a
#                   milestone that can fence it (see the proposal's §5.2 and P5).
#   NOT checked  -- progress recording, which the CLI's `--record` owns; the verb
#                   grades and reports, it does not write the learner's file.
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)
SOCKQ="$SMOKE_TOOLS/sockq"

# A workspace with its own assignment set, so nothing depends on the shipped
# curriculum and a passing grade is possible without touching the real tree.
ws="$tmp/ws"
mkdir -p "$ws/docs/assignments"
cat > "$ws/docs/assignments/10-pass.md" <<'EOF'
---
title: Always passes
phase: testing
points: 3
verify: "true"
---
Do the thing.
EOF
cat > "$ws/docs/assignments/20-fail.md" <<'EOF'
---
title: Always fails
points: 2
verify: "false"
---
Do the other thing.
EOF
cat > "$ws/docs/assignments/30-noverify.md" <<'EOF'
---
title: Nothing defines success
points: 1
---
Ungradeable by construction.
EOF
cat > "$ws/docs/assignments/40-unreachable.md" <<'EOF'
---
title: Harness is missing
points: 5
verify: "sh ./no/such/grader.sh"
---
The verify script is not here, which is not the learner's fault.
EOF

# A real, well-formed, GRADEABLE spec that is NOT in the assignment set, placed
# exactly where `../outside.md` resolves to: docs/assignments/.. is docs/, so it
# goes in docs/, one level up and no further. Getting that wrong once already
# made this check pass for the wrong reason -- the escape "failed" because the
# target was somewhere else, not because it was refused, which is a vacuous
# check of precisely the kind docs/TEST_INTEGRITY.md is about. Its verify would
# visibly succeed, so a successful escape is unmistakable.
cat > "$ws/docs/outside.md" <<'EOF'
---
title: Outside the set
points: 99
verify: "true"
---
Reaching this means a name escaped the assignment directory.
EOF

cat > "$ws/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:9/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

# AF_UNIX paths cap near 107 bytes and smoke_tmp can be long (M528).
short="/tmp/jcasg.$$"
mkdir -p "$short" && chmod 0700 "$short"
sock="$short/d.sock"

(cd "$ws" && exec "$BIN" --config "$ws/config.json" daemon --socket "$sock" \
    < /dev/null > "$tmp/d.log" 2>&1) &
i=0
while [ ! -S "$sock" ]; do
    i=$((i + 1)); [ $i -gt 10 ] && break
    sleep 1
done
if [ ! -S "$sock" ]; then
    t_fail "daemon never started: $(tail -c 200 "$tmp/d.log")"
    for _ in 1 2 3 4 5 6 7 8 9 10; do t_fail -; done
    rm -rf "$short"; t_done
fi
ask() { printf '%s\n' "$1" | "$SOCKQ" --deadline 25 "$sock"; }

# --- 1: the handshake names the group ---------------------------------------
out=$(ask '{"v":1,"type":"hello"}')
case "$out" in
    *'"assignment"'*) t_ok "hello advertises the assignment group" ;;
    *) t_fail "hello does not advertise the group: $out" ;;
esac

# --- 2-3: list ---------------------------------------------------------------
out=$(ask '{"v":1,"type":"assignment.list"}')
case "$out" in
    *'"type":"assignment.list.ok"'*) t_ok "assignment.list answers" ;;
    *) t_fail "list got: $out" ;;
esac
# The bare name is present because it is what .get and .grade take -- a caller
# should not have to strip a path prefix to turn a listing into a request.
if printf '%s' "$out" | grep -q '"name":"10-pass.md"' &&
   printf '%s' "$out" | grep -q '"name":"40-unreachable.md"'; then
    t_ok "the listing carries the bare name each verb accepts"
else
    t_fail "listing lacks usable names: $out"
fi

# --- 4-5: get ----------------------------------------------------------------
out=$(ask '{"v":1,"type":"assignment.get","name":"10-pass.md"}')
case "$out" in
    *'"title":"Always passes"'*) t_ok "assignment.get returns the spec" ;;
    *) t_fail "get got: $out" ;;
esac
# The verify command comes back deliberately: a grade whose basis a caller
# cannot inspect is an opinion.
case "$out" in
    *'"verify":"true"'*) t_ok "and returns the verify command it will run" ;;
    *) t_fail "get hides the verify command: $out" ;;
esac

# --- 6-7: a real grade, both ways -------------------------------------------
out=$(ask '{"v":1,"type":"assignment.grade","name":"10-pass.md"}')
if printf '%s' "$out" | grep -q '"passed":true' &&
   printf '%s' "$out" | grep -q '"points":3' &&
   printf '%s' "$out" | grep -q '"of":3' &&
   printf '%s' "$out" | grep -q '"exitCode":0'; then
    t_ok "a passing spec grades passed with its points and the verify exit code"
else
    t_fail "passing grade wrong: $out"
fi
out=$(ask '{"v":1,"type":"assignment.grade","name":"20-fail.md"}')
if printf '%s' "$out" | grep -q '"passed":false' &&
   printf '%s' "$out" | grep -q '"points":0' &&
   printf '%s' "$out" | grep -q '"of":2'; then
    t_ok "a failing spec grades failed, awarding 0 of its 2 points"
else
    t_fail "failing grade wrong: $out"
fi

# --- 8: no verify is a refusal, not a fail ----------------------------------
out=$(ask '{"v":1,"type":"assignment.grade","name":"30-noverify.md"}')
case "$out" in
    *'"code":"assignment.no_verify"'*) t_ok "a spec with no verify is refused, not graded" ;;
    *'"passed":false'*) t_fail "a spec with NO verify was reported as a failing
 grade -- nothing defined success, so there was nothing to fail: $out" ;;
    *) t_fail "no-verify got: $out" ;;
esac

# --- 9: THE ONE THAT MATTERS -- M502 on the wire -----------------------------
# A verify whose script is not reachable is a broken harness, not wrong work.
out=$(ask '{"v":1,"type":"assignment.grade","name":"40-unreachable.md"}')
case "$out" in
    *'"code":"assignment.not_gradeable"'*)
        t_ok "an unreachable verify script is not a grade (M502 survives the wire)" ;;
    *'"passed":false'*)
        t_fail "an unreachable verify script was reported as passed:false -- a
 marking service would record a FAIL for a harness problem, which is exactly the
 defect M502 fixed for the CLI: $out" ;;
    *) t_fail "unreachable-verify got: $out" ;;
esac

# --- 10-11: a name may not express a location -------------------------------
bad=0
for n in '../../etc/passwd' 'sub/dir.md' '..' '.hidden.md'; do
    out=$(ask "{\"v\":1,\"type\":\"assignment.grade\",\"name\":\"$n\"}")
    case "$out" in
        *'"code":"assignment.name"'*) ;;
        *) bad=$((bad + 1)); echo "# unrefused name: $n -> $out" ;;
    esac
done
if [ "$bad" -eq 0 ]; then
    t_ok "every name expressing a location is refused"
else
    t_fail "$bad name(s) expressing a location were not refused"
fi
# A well-formed name that simply does not exist is a DIFFERENT answer, so a
# caller can tell "you may not ask that" from "there is no such assignment".
out=$(ask '{"v":1,"type":"assignment.grade","name":"99-absent.md"}')
case "$out" in
    *'"code":"assignment.not_found"'*) t_ok "an absent assignment is not_found, distinctly" ;;
    *) t_fail "absent assignment got: $out" ;;
esac

# --- 12: containment, not just refusal --------------------------------------
# ../outside.md IS a real, gradeable spec one level up. Without the name rule it
# resolves and grades (passed:true, 99 points); with it, nothing is even opened.
out=$(ask '{"v":1,"type":"assignment.grade","name":"../outside.md"}')
case "$out" in
    *'"code":"assignment.name"'*)
        t_ok "a name cannot reach a real gradeable spec outside the set" ;;
    *'"passed":true'*)
        t_fail "a name escaped docs/assignments/ and graded a spec outside it --
 the wire could grade any file a caller can point at, and grading RUNS the
 spec's own verify command: $out" ;;
    *) t_fail "escape attempt got: $out" ;;
esac

printf '{"type":"shutdown"}\n' | "$SOCKQ" --deadline 10 "$sock" >/dev/null 2>&1
rm -rf "$short"
t_done
