#!/bin/sh
# smoke: a GREEN baseline makes the run-start checkpoint known-good (M473).
#
# THE DEFECT THIS EXISTS FOR, found by dogfooding jichi on another project
# (docs/analysis/2026-08-18-dogfooding-on-chrtext.md §2).
#
# `green_verified` exists because, in jc_envelope.h's own words, "the first
# pre-edit checkpoint is recorded as green ON THE PREMISE that the tree started
# green; nothing checks that premise" -- so M207 made the budget stop keep work
# rather than discard it in favour of a possibly-equally-red baseline.
#
# M343 then added the baseline probe, which checks exactly that premise. The two
# were never connected: the probe journaled its verdict and warned on the two bad
# ones, and dropped JC_BASELINE_OK -- the good news -- on the floor.
#
# The cost, measured on a real run: it stopped on its tool-call budget having
# edited three files, and reported
#
#   "no verify passed during this run, so there is no known-good checkpoint --
#    keeping the work rather than reverting ... a rollback could not have helped"
#
# while its own journal carried `baseline {"exit":0,"kind":"invariant"}` two lines
# above. The message was false: the start state had been verified green, so a
# rollback was precisely what would have helped.
#
# WHAT THIS ASSERTS. The end-to-end behaviour, not the flag: a run that starts
# green, breaks the gate, and hits its budget must ROLL BACK. And -- the half that
# stops this from becoming a work-destroying regression -- a run whose gate was
# already red at the start must still KEEP its work, which is the M207 behaviour
# that must not be undone.
. "$(dirname "$0")/_smoke.sh"


# The config is written here rather than via write_config: that helper emits
# "snapshots":false in its base template, so passing an override appends a DUPLICATE
# key and the parser takes the first one. Snapshots stayed off, green_commit was
# never recorded, and check 2 failed for a reason that had nothing to do with the
# fix. Snapshots are the whole mechanism under test, so they are set once, here.
mk_cfg() {  # $1 = path, $2 = port
    cat > "$1" <<EOC
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOC
}

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

command -v git >/dev/null 2>&1 || t_skip "needs git (snapshots back the checkpoint)"

# A workspace whose gate is a script we control, so "green" and "red" are exact.
( cd "$ws" && git init -q . && git config user.email t@t && git config user.name t )
printf 'ok\n' > "$ws/state.txt"
cat > "$ws/gate.sh" <<'EOF'
#!/bin/sh
# green iff state.txt says ok
grep -q '^ok$' state.txt
EOF
chmod +x "$ws/gate.sh"
( cd "$ws" && git add -A && git commit -qm init )

# The model breaks the gate with a FILE TOOL, then burns a second call so the
# budget stops it. --max-tool-calls 2 makes the stop deterministic.
#
# write_file rather than a shell redirect, deliberately: the pre-edit checkpoint
# that green_commit names is taken on the file-tool path. A first cut of this
# driver broke the gate with `printf > state.txt` and the budget stop then had no
# checkpoint at all to roll back to -- so it proved nothing about this fix, and
# would have passed for the wrong reason once the fix was reverted.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"state.txt","content":"broken\n"}
rule
  count 2
  tool run_terminal_command {"command":"echo still going"}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 8
mk_cfg "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session --auto \
    --verify './gate.sh' --verify-kind invariant \
    --max-tool-calls 2 \
    --journal "$tmp/journal.jsonl" \
    -p 'break it' < /dev/null > "$tmp/out.txt" 2>&1)
mm_stop

# 1. The baseline probe ran and passed -- without that the rest proves nothing.
if grep -q '"event":"baseline"' "$tmp/journal.jsonl" && \
   grep '"event":"baseline"' "$tmp/journal.jsonl" | grep -q '"exit":0'; then
    t_ok "the invariant baseline probe ran and was green"
else
    t_fail "no green baseline in the journal: $(grep baseline "$tmp/journal.jsonl" | head_bytes 200)"
fi

# 2. The budget stop rolled back to it, instead of keeping a broken tree.
if grep -q '"rolled_back":true' "$tmp/journal.jsonl"; then
    t_ok "the budget stop rolled back to the verified-green start"
else
    t_fail "kept a tree that fails its own gate: $(grep '\"event\":\"end\"' "$tmp/journal.jsonl" | head_bytes 220)"
fi

# 3. THE HALF THAT MUST NOT REGRESS. A gate that was ALREADY RED at the start has
#    no green baseline, so M207's rule still applies: keep the work rather than
#    revert to an equally red tree. Same script, gate red before the run.
ws2=$(smoke_tmp)
( cd "$ws2" && git init -q . && git config user.email t@t && git config user.name t )
printf 'already-broken\n' > "$ws2/state.txt"
cp "$ws/gate.sh" "$ws2/gate.sh"
( cd "$ws2" && git add -A && git commit -qm init )

mm_start "$tmp/replies.mm" "$tmp/cap2" 8
mk_cfg "$tmp/config2.json" "$MM_PORT"
(cd "$ws2" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    --no-session --auto \
    --verify './gate.sh' --verify-kind invariant \
    --max-tool-calls 2 \
    --journal "$tmp/journal2.jsonl" \
    -p 'break it' < /dev/null > "$tmp/out2.txt" 2>&1)
mm_stop

if grep -q '"rolled_back":false' "$tmp/journal2.jsonl"; then
    t_ok "a run that started RED still keeps its work (M207 preserved)"
else
    t_fail "discarded work in favour of an equally red baseline -- the exact \
regression M207 exists to prevent: $(grep '\"event\":\"end\"' "$tmp/journal2.jsonl" | head_bytes 220)"
fi

t_done
