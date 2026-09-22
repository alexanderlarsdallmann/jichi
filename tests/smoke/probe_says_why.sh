#!/bin/sh
# smoke: when something cannot be checked, the report says WHICH thing and WHY
# (M692). Two seams, both found by driving jichi on another project and both
# the same shape -- jichi holding the answer and printing something vaguer.
#
# 1. `doctor --live` DISCARDED THE SERVER'S OWN DIAGNOSIS. It printed
#
#        the probe request did not complete (http error, HTTP error)
#
#    -- the status string, then the literal words "HTTP error" -- for a gateway
#    that had answered HTTP 400 with `"There are no healthy deployments for this
#    model"`. The status code and the message were both in hand. From the old
#    line the next move is to check the network; from the server's, it is to fix
#    one string, and telling those apart is the whole value of the row.
#
# 2. THE HOLLOW-GATE CHECK WAS SILENT WHEN IT COULD NOT RUN. M86 warns when a
#    green verify ran zero tests, or fewer than an earlier green. All three
#    checks need a parseable test count, and `zig build test` -- a real
#    project's real verifier -- prints NOTHING on success. With no count the
#    machinery said nothing at all, so "the gate is fine" and "the gate could
#    not be inspected" printed identically.
#
#    Reported in the reach footer's not-checked half and NOT as a warning:
#    a verifier that is quiet on success is a normal setup, and a per-run
#    warning would fire on every run of every such project. That is how a
#    warning stops being read -- the same argument M689 used for `ls`.
. "$(dirname "$0")/_smoke.sh"

command -v git > /dev/null 2>&1 || t_skip "git is required for the verify baseline"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

mkdir -p "$ws/src"
printf 'x\n' > "$ws/src/a.c"
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) > /dev/null 2>&1

# ---- the live probe, against a server that refuses WITH a reason -----------
cat > "$tmp/refuse.mm" <<'EOF'
wire openai
rule
  status 400
  body {"error":{"message":"no healthy deployments for MODEL_XYZ"}}
EOF
mm_start "$tmp/refuse.mm" "$tmp/capA"
cat > "$tmp/a.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false}
EOF
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/a.json" doctor --live \
    < /dev/null > "$tmp/a.out" 2> "$tmp/a.err")
mm_stop

# ---- 1: the denominator -- the probe really did fail -----------------------
# Without this, checks 2-3 assert about a line that was never printed, and a
# fixture whose server answered 200 would look identical to a passing check.
if $G -q 'tool-calling probe failed' "$tmp/a.out"; then
    t_ok "the fixture makes the live probe fail (the case is live)"
else
    t_fail "the probe did not fail, so checks 2-3 have nothing to read: \
$($G -i 'live' "$tmp/a.out" | head -2 | tr '\n' ' ' | head_bytes 220)"
fi

# ---- 2: the STATUS is a number, not the words "HTTP error" -----------------
# ANCHORED TO THE PROBE'S OWN LINE. The first version grepped the whole doctor
# output for "HTTP 400" and passed under a perturbation that removed the status
# from this row entirely -- because the model-LIMITS check prints the same
# status a few rows above ("the window was NOT checked"). A check whose universe
# is the whole page tests whichever row happens to mention the string.
if $G 'probe request did not complete' "$tmp/a.out" | $G -q 'HTTP 400'; then
    t_ok "the probe failure names the HTTP status"
else
    t_fail "the status code was in hand and not printed -- it is what separates \
'the server refused this' from 'the network is down': \
$($G -A2 'probe request did not complete' "$tmp/a.out" | head_bytes 240)"
fi

# ---- 3: and the SERVER'S OWN WORDS are passed through ----------------------
if $G 'probe request did not complete' "$tmp/a.out" | \
   $G -q 'no healthy deployments for MODEL_XYZ'; then
    t_ok "the server's own diagnosis reaches the reader"
else
    t_fail "the server said exactly what was wrong and jichi did not repeat it: \
$($G -A2 'probe request did not complete' "$tmp/a.out" | head_bytes 240)"
fi

# ---- the verifier half -----------------------------------------------------
cat > "$tmp/write.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"src/a.c","content":"y\n"}
rule
  text DONE
EOF
vrun() {   # vrun <label> <verify-command>
    mm_start "$tmp/write.mm" "$tmp/cap.$1"
    cat > "$tmp/$1.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"snapshots":true,"maxRetries":0,"verify":"$2"}
EOF
    (cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/$1.json" --no-session \
        --edit-scope 'src/**' -p "go" < /dev/null > "$tmp/$1.out" 2> "$tmp/$1.err")
    mm_stop
}
vrun silent  "true"
vrun counted "printf '5 tests passed\\n'"

# ---- 4: a verifier that prints nothing says so -----------------------------
if $G -q 'printed no test count' "$tmp/silent.err"; then
    t_ok "a silent verifier is reported as uninspectable, not as fine"
else
    t_fail "a green verify that printed no test count said nothing -- so 'the \
gate is fine' and 'the gate could not be inspected' read identically, which is \
the defect: $($G 'not checked' "$tmp/silent.err" | head_bytes 240)"
fi

# ---- 5: and one that DOES print a count does not (the two-sided half) ------
# A notice printed whenever a verifier is armed passes check 4 and tells every
# project with a counting gate that its gate cannot be inspected.
if ! $G -q 'printed no test count' "$tmp/counted.err" &&
   $G '^\[jichi\] checked:' "$tmp/counted.err" | $G -q 'verify green'; then
    t_ok "a verifier that reports a count is left alone"
else
    t_fail "a gate that printed '5 tests passed' was still called \
uninspectable: $($G -A1 'checked:' "$tmp/counted.err" | head -2 | tr '\n' ' ' \
| head_bytes 240)"
fi

t_done
