#!/bin/sh
# smoke: every surface tells the same story about why the run stopped (M688).
#
# WHY THIS EXISTS. Eight stop reasons are reported on five surfaces -- stdout,
# the exit code, the reach footer, the `[envelope]` verdict and `--output json`.
# Forty cells, each wired by hand, and the session that prompted this found two
# of them empty:
#
#   M687  `max_iters` x reach footer. Empty for fifteen milestones AFTER M322
#         wrote down exactly what was wrong with it.
#   M688  `budget` x reach footer. Found by audit the day after M687, in a run
#         whose block said, four words apart, "verify did not conclude" (in the
#         CHECKED half) and "not checked: (nothing -- a verifier and an edit
#         scope were armed)" -- while stdout was empty.
#
# The fix is one classifier every surface reads (jc_outcome.h), guarded by
# `-Wswitch`: a ninth stop reason will not compile until each renderer handles
# it. This driver guards the other half -- that the renderers, having compiled,
# AGREE. A `switch` can be exhaustive and still say contradictory things.
#
# WHAT IT DOES NOT COVER, said plainly rather than implied: `scope_tainted`,
# `timeout` and `interrupted` are not reachable with a mock model here. Trying
# them produced fixtures that measured something else -- the edit-scope fence
# refused the write before it could taint anything, and 200 mock iterations
# finish well inside any deadline worth setting. They are named in
# docs/plans/2026-09-run-outcome.md as unreached rather than left to look
# covered.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws/out"
printf 'hello\n' > "$ws/a.txt"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  tool read_file {"path":"a.txt"}
EOF
cat > "$tmp/done.mm" <<'EOF'
wire openai
rule
  text FINISHED
EOF
cat > "$tmp/write.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"out/x.txt","content":"x\n"}
rule
  text WROTE
EOF

# run <label> <script> <extra-config> <flags...>  -- text and json, same config
run() {
    _l="$1"; _s="$2"; _x="$3"; shift 3
    mm_start "$tmp/$_s" "$tmp/cap.$_l"
    # Written here rather than via write_config, which pins "snapshots":false --
    # and the verifier's rollback needs them, so the vfail fixture reported
    # `done` with a green verify. A fixture that cannot reach the state it is
    # named for is the thing check 1 exists to catch, and it caught this.
    # lowResource is pinned because smoke_lint requires it of every inline
    # driver config: on a low-RAM host auto-lite reshapes the configuration and
    # a reshaped config could change which surface says what.
    cat > "$tmp/$_l.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"snapshots":true,"maxRetries":0,$_x}
EOF
    (cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/$_l.json" --no-session \
        "$@" -p "do the thing" < /dev/null > "$tmp/$_l.out" 2> "$tmp/$_l.err")
    echo $? > "$tmp/$_l.rc"
    (cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/$_l.json" --no-session \
        --journal "$tmp/$_l.jrnl" \
        --output json "$@" -p "do the thing" < /dev/null > "$tmp/$_l.js" 2>/dev/null)
    mm_stop
}

run done   done.mm  '"verify":"true"'                      --edit-scope '**'
run iters  loop.mm  '"verify":"true","maxToolIters":3'     --edit-scope '**'
run budget loop.mm  '"verify":"true"'                      --edit-scope '**' --max-tool-calls 4
run vfail  write.mm '"verify":"false"'                     --edit-scope '**'

sr() { sed -n 's/.*"stop_reason"[ ]*:[ ]*"\([a-z_]*\)".*/\1/p' "$tmp/$1.js" | head -1; }
inc() { $G -q 'ANSWER IS INCOMPLETE' "$tmp/$1.err" && echo yes || echo no; }

# ---- 1: the denominator -- each fixture produced the stop reason it claims --
# Without this the rest is a test of four runs that all ended "done": every
# "no" below would be correct for the wrong reason, which is exactly how a
# matrix check passes while covering one cell.
got="$(sr done)/$(sr iters)/$(sr budget)/$(sr vfail)"
if [ "$got" = "done/max_iters/budget/verify_failed" ]; then
    t_ok "the four fixtures produce four distinct stop reasons ($got)"
else
    t_fail "the fixtures did not reach the intended states: got '$got', wanted \
'done/max_iters/budget/verify_failed'. Every assertion below would be vacuous."
fi

# ---- 2: a truncated run says so in the footer ------------------------------
if [ "$(inc iters)" = yes ] && [ "$(inc budget)" = yes ]; then
    t_ok "both truncating reasons report an incomplete answer"
else
    t_fail "a run that was cut off did not say so: iters=$(inc iters) \
budget=$(inc budget). budget was the M688 cell -- it printed \
'not checked: (nothing)' beside an empty stdout."
fi

# ---- 3: and a COMPLETE run does not (the two-sided half) -------------------
# verify_failed is the discriminating case: it FAILED, but the model finished
# saying what it had to say, so calling its answer truncated would be false.
# A notice keyed on "something went wrong" rather than "the answer is cut off"
# passes check 2 and fails here.
if [ "$(inc done)" = no ] && [ "$(inc vfail)" = no ]; then
    t_ok "a finished answer is not called incomplete, even when it failed verify"
else
    t_fail "a complete answer was reported as truncated: done=$(inc done) \
vfail=$(inc vfail). verify_failed means the verifier refused a finished answer, \
not that the turn was cut off."
fi

# ---- 4: an armed-but-inconclusive verifier is NOT-checked -------------------
if $G -q 'verifier never reached a verdict' "$tmp/budget.err" &&
   ! $G '^\[jichi\] checked:' "$tmp/budget.err" | $G -q 'did not conclude'; then
    t_ok "a verifier that never concluded is reported as not-checked"
else
    t_fail "'verify did not conclude' is on the checked side of the colon -- a \
verifier that never reached a verdict is the definition of something that was \
NOT checked: $($G '^\[jichi\] checked:' "$tmp/budget.err" | head_bytes 200)"
fi

# ---- 5: a verifier that DID conclude stays on the checked side -------------
# The other half of 4. Moving every verifier mention to not-checked would pass
# check 4 and lose the verdict that a green run is entitled to report.
if $G '^\[jichi\] checked:' "$tmp/vfail.err" | $G -q 'verify RED' &&
   ! $G -q 'verifier never reached a verdict' "$tmp/vfail.err"; then
    t_ok "a concluded verifier keeps its verdict in the checked half"
else
    t_fail "a verifier that returned RED is no longer reported as a check that \
ran: $($G '^\[jichi\] checked:' "$tmp/vfail.err" | head_bytes 200)"
fi

# ---- 6: the json and the text surfaces never disagree ----------------------
# The point of the whole milestone. Before it, the stop reason was computed
# INSIDE the json branch, so the text path had no idea -- which is how one
# surface said budget_exhausted while the other said nothing was unchecked.
bad=""
for l in done iters budget vfail; do
    case "$(sr $l)" in
        max_iters|budget|timeout|interrupted|error) want=yes ;;
        *)                                          want=no  ;;
    esac
    [ "$(inc $l)" = "$want" ] || bad="$bad $l(json=$(sr $l),footer-incomplete=$(inc $l))"
done
if [ -z "$bad" ]; then
    t_ok "json stop_reason and the text footer agree on all four conditions"
else
    t_fail "the two surfaces disagree about whether the answer is complete:$bad \
-- they are supposed to read the same classifier now (jc_outcome.h)"
fi

# ---- 7: the RUN JOURNAL is a sixth surface, and it agrees too (M690) -------
# It used to carry only the envelope's `outcome`, which cannot express
# max_iters: a capped run recorded `"outcome":"ok"` and was indistinguishable
# from a clean one. Measured over 114 journals / 101 completed runs before the
# field existed -- the values were ok, running, budget_exhausted and
# verify_failed, and nothing else. DEFERRED item 7 had claimed the journal
# already carried stop_reason; it did not, and the claim had been read rather
# than measured.
jsr() { sed -n 's/.*"event":"end".*"stop_reason":"\([a-z_]*\)".*/\1/p' "$tmp/$1.jrnl" | head -1; }
jbad=""
for l in done iters budget vfail; do
    [ "$(jsr $l)" = "$(sr $l)" ] || jbad="$jbad $l(journal=$(jsr $l),json=$(sr $l))"
done
if [ -z "$jbad" ]; then
    t_ok "the run journal records the same stop reason as --output json"
else
    t_fail "the journal and the json disagree:$jbad -- a supervisor reading the \
journal cannot tell a capped run from a clean one, which is the state that made \
DEFERRED item 7 unanswerable from its own corpus"
fi

t_done
