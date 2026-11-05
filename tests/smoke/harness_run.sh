#!/bin/sh
# smoke: `jichi-harness run` -- the C89 port of the drive's run-lead.sh (the
# jichi-harness drive of 2026-10-23, batch H4, M1057). Mode A (the plan 7.1):
# the harness and the agent run as the same user, so the port drops the
# container half and keeps the record and key-delivery half, re-expressed in C.
# The driver runs a REAL jichi against the REAL mockmodel (no fake pinned
# binary here -- the point is the records land OUTSIDE the workspace and the
# key never reaches argv, a URL, a log or the child's children).
#
# M1057 (H4). Design note:
#   docs/internal/drive/2026-10-23-harness/notes/M1057.md
# Ported reference (read line by line; its comments name the incidents):
#   docs/internal/drive/2026-10-23-harness/harness/run-lead.sh
# Output-shape precedent: tests/smoke/harness_init.sh (the $HBIN use, the
# _smoke.sh sourcing, the t_plan/t_ok/t_fail/t_done discipline).
#
# THE RED THIS IS HELD BY. M1058 (the handoff body, H4 batch) re-arms the
# driver: the red is now check 12, which on the CURRENT tree (M1057) finds
# that run 0016's prompt.md lacks `Its todo list` (and the files, the notes
# and the rc/commit bullets) -- the M1057 handoff is the fixed stub
# `(none -- the first run)`, not a section built from run 0015's own records.
# Checks 13/14/16/17 are red the same way (no `Its batch` lines, no
# handoff.md cap, no fail-open `handoff=none`, and run 0001's prompt still
# carries the stub header). The not-built red that check 1 R0 originally held
# (a BARE `run` -- no --tenant, no --run -- refused with the H0 not-built
# line, `jichi-harness: run: not built yet (see docs/HARNESS.md)`, exit 2 --
# the dispatch's own guard, init's precedent) shipped WITH the M1057 code and
# now passes on the current tree; it is kept as a regression cell.
#
# WHAT IS CHECKED:
#   1. R0 the not-built red, held by the driver itself: a BARE `run` (no
#      flags) exits 2 with 'run: not built yet' on stderr and prints nothing
#      on stdout (init's precedent; the parser acceptance ships with the
#      code, the M1053 precedent).
#   2. R1 the happy run: `run --config <f> --tenant lead --run 0001 < /dev/null`
#      exits the CHILD's rc (0 with a mockmodel that answers one text reply);
#      runs/0001/stream.jsonl is non-empty; meta.txt has run=0001, rc=0 and a
#      build: stamp; prompt.md has "## This run" and "Run 0001"; a config.json
#      copy is present.
#   3. R2 the key never appears on argv, in meta, in prompt, or in a record:
#      the RANDOM canary is ABSENT from the argv dump, meta.txt, prompt.md,
#      stream.jsonl and the harness's own stdout/stderr -- and PRESENT in the
#      mockmodel's request (the model got the key, so the env delivery worked).
#   4. R3 a missing model-key file is a NAMED refusal: exit 2, and runs/N/ is
#      NOT created (the refusal precedes the layout).
#   5. R4 caps off, fences on: the argv carries NO --deadline and NO
#      --budget-tokens, and DOES carry --max-tool-calls, --verify-timeout
#      and --prompt-b64 (the prompt travels base64, the key never does).
#   6. R5 the run did not read the harness's stdin (SV:0337.1, non-vacuous): a
#      DEDICATED happy run 0021 (same tenant, happy.mm) is fed through a bounded
#      pipe carrying the marker STDIN-MARK-42 (printf closes the pipe at the
#      newline); the marker is NOT in any of the run's mock-captured request
#      bodies and NOT in runs/0021/prompt.md -- the harness's fd_stdin =
#      JC_SPAWN_DEVNULL kills it at the spawn. The separate teeth probe (flip
#      fd_stdin to JC_SPAWN_INHERIT, rebuild, witness the marker in the capture;
#      merge:false, a separate task -- NOT this commit) is named in the cell's
#      comment, not run here.
#   7. R6 a bad tenant is a usage refusal: `--tenant nobody` exits 2 with
#      "no tenant named nobody" and creates nothing; `--run` without
#      `--tenant` exits 2.
#   8. R7 the child's HOME is the tenant's home, not the operator's.
#   9. R8 the journal and telemetry are OUTSIDE the workspace (the H4 done-when
#      line): runs/0001/journal.jsonl and runs/0001/telemetry.jsonl exist and
#      are non-empty; no *.jsonl the driver did not put there is created under
#      the workdir.
#   10. R9 the supervisor's notes are moved into the run's records ONLY after
#      the first model call succeeds: a happy run moves inbox/note-a.md into
#      runs/0009/notes/; a run whose mockmodel answers 500 (maxRetries 0)
#      keeps inbox/note-b.md (the read happened, the move did not).
#   11. R10 the lead-model guard (SV:1818.1): a drive.leadModel the tenant's
#      jichi config does NOT name (a) is WARNED on the harness's stderr and
#      OMITTED from the spawn argv -- the run still exits the child's rc (0)
#      on the config's routing; a leadModel the config DOES name (b) IS
#      passed as --model <name> (the argv dump carries it, no warn).
#   12. H1 the handoff's todo/files/notes/last-message section (M1058): run
#      0015 (two read_files on real fixture files, one 15-item todowrite, a
#      >=601-char reply) then FIVE notes filed into runs/0015/notes/ -- a.md,
#      d.gz (plain file with a .gz NAME), e-dash.md (600 em-dashes, 1800
#      bytes), long.md (a 500+-char body with embedded newlines), z.md -- so
#      the byte-sorted FIRST FOUR render and z.md is the "and 1 more". Run
#      0016's prompt.md then carries `## What the previous run left you`, the
#      `*Written by the harness from run 0015's own records` guard, `rc 0`+`it
#      ended itself`+`It ended at commit`, `Its last message:` capped, `Its
#      todo list, as it last wrote it:` with 15 `  - [` items, `The files it
#      read or changed` naming BOTH paths `(read 1)`, and `The manager's notes
#      it was given` with the records path + EACH of the four rendered note
#      lines asserted BY NAME + `and 1 more` + the .gz note's `(unreadable)` +
#      the long note's 500-char ellipsis. 0016's meta.txt says
#      `handoff=from run 0015`.
#   13. H2 the handoff's batches (M1058): run 0013 (SIX spawn_parallel
#      calls, 8 no-such-agent tasks each, one task 110+ chars) then run 0014's
#      prompt carries `Its batch 1:` through `Its batch 6:`, each `8 task(s);
#      the result began:`; PLUS the SV:0959.3 pairing tooth (M1058 leg 5) on
#      a NEW pair: a real text-only run 0022 whose stream is then fabricated
#      with two spawn_parallel calls (DISTINCT ids c13a/c13b) and their two
#      tool_results in REVERSE stream order (preview PREV-B first, PREV-A
#      second) -- run 0023's prompt must pair previews BY id, not by
#      position: `Its batch 1:` quotes PREV-A and `Its batch 2:` quotes
#      PREV-B (a position-pairing handoff would swap them).
#   14. H3 the handoff's byte cap (M1058): run 0014's handoff.md carries
#      `- *(cut at 6000 bytes)*` AND is <= 6040 bytes.
#   15. H4 the two behaviours with no cell yet (M1058, SV:0417.1/SV:0518.2):
#      (a) CHANGED 6 -- run 0019's stream is fabricated with an apply_patch
#      tool_call (paths in edits[].path: p19a.txt, p19b.txt) + its result
#      (parse-equal, the H1 todowrite idiom), and run 0020's prompt carries
#      BOTH paths, each `(changed 1)`; (b) the 1800-byte e-dash.md note (600
#      em-dashes) renders cut at 500 CODE POINTS: run 0016's handoff.md
#      strict-decodes as UTF-8 (iconv) and carries a complete em-dash
#      immediately followed by the ellipsis.
#   16. H5 the handoff's fail-open (M1058): run 0017, then DELETE
#      runs/0017/stream.jsonl, then run 0018 exits 0 (the child's rc, not an
#      error), its prompt has NO handoff header, runs/0018/handoff.md is
#      EMPTY, handoff.err is NON-EMPTY, and meta.txt says `handoff=none`.
#   17. H6 the first run has no header (M1058): run 0001's prompt has NO
#      `## What the previous run left you` line and its meta says
#      `handoff=none` (the M1057 stub is gone).
#   18. M1063 (SV:0904.2) the run consumes ONLY the notes it actually read
#      into the prompt (name-sorted at assembly): a note dropped into the
#      inbox at t+3s of run 0024 (a `delay 9000` mock) is STILL in inbox/
#      after 0024, NOT in runs/0024/notes/, and its body is NOT in 0024's
#      prompt (the pre-placed note's body IS, and it is filed in
#      0024/notes/); run 0025 then reads the still-pending note (its body IS
#      in 0025's prompt) and its two ordering notes appear in 0025's prompt
#      in NAME-SORTED order (### a-z-order before ### z-a-order), all three
#      filed in 0025/notes/ (moved == read into the prompt).
#
# THE ARGV/HOME DUMP. A wrapper replaces the pinned bin/jichi: it dumps $* and
# HOME to a file, then execs the real jichi, so R2/R4/R7 measure the ACTUAL
# spawn the harness builds (the M1055 git-wrapper idiom). The tenant's jichi
# config uses "toolProfile":"none", so the model's one text reply ends the
# turn with no tool work: the driver measures the spawn, not an agent.

. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"
# /usr/bin/grep only with its fallback (portability_lint check 36, V2f): in an
# agent session bare `grep` can be a shell function (CLAUDE.md), and a system
# without the merged /usr has no /usr/bin/grep. The guard keeps both.
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# The loader resolves each top-level reference from its JICHI_HARNESS_* env
# setting FIRST (env wins over the config's own value). An ambient setting
# would defeat the fixtures below, so clear all three.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

t_plan 18
smoke_home
tmp=$(smoke_tmp)

# Neutralise the operator's ambient git config for the FIXTURES: the workdir
# repo run reads team_head from must not see a machine-level gitconfig.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0

# --- fixtures ----------------------------------------------------------------
# The drive dir the harness owns: runs/, inbox/, home/, bin/ (the pinned jichi
# is a WRAPPER around the real $BIN so the argv and HOME are measurable).
drive="$tmp/drive"
mkdir -p "$drive/runs" "$drive/inbox" "$drive/home/.cache" "$drive/home/.jichi.d/tool-output" "$drive/bin"

# A REAL git workdir (the team's clone stand-in): one commit so team_head
# resolves.
work="$tmp/work"
git init -q "$work"
git -C "$work" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "work: the run's base commit"

# The RANDOM model-key canary (the od idiom, M1053): its ABSENCE from a record
# is a real assertion only because it is not a constant the driver wrote.
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
keyfile="$tmp/keyfile"
printf '%s\n' "$_kc" > "$keyfile"
chmod 600 "$keyfile"
_missing_key="$tmp/keyfile-missing"   # never created (R3)

# A DISTINCT ambient key in the driver's OWN environment: behaviour 2's
# "the file is the ONLY source". The harness unsets JICHI_API_KEY from its
# process env before spawning, so the child must receive the FILE canary, never
# this ambient one. R2 asserts the mock's request carries _kc and NOT this.
export JICHI_API_KEY="ambient-canary-should-never-reach-the-model-$$_"
export JLU_API_KEY="$JICHI_API_KEY"

# The prompt base text the tenant's config names.
promptbase="$tmp/lead-prompt.md"
printf 'You are the lead of the test drive. Read the plan and work it.\n' > "$promptbase"

# The argv/HOME dump the wrapper writes.
dump="$tmp/jichi-argv.txt"

# The tenant's jichi config is written once the mock's port is known
# (write_config PATH PORT TOP_EXTRA MODEL_EXTRA, _smoke.sh). The key is by
# ENV NAME (not a literal), and toolProfile is none, so the one text reply
# ends the turn with no tool work.
jicfg="$tmp/jichi.json"

# The harness config: one tenant named lead, the drive dir, the key reference.
hcfg="$tmp/harness.json"

# The mockmodel scripts: one that answers a single text reply (the happy run),
# one that 500s every request (the first-call-failed run).
cat > "$tmp/happy.mm" <<'MMEOF'
wire openai
rule
  text Done: the lead run finished its one turn.
MMEOF
cat > "$tmp/dead.mm" <<'MMEOF'
wire openai
rule
  status 500
MMEOF

# M1063 (check 18): a happy reply HELD for 9 s (`delay 9000`: the mockmodel
# sleeps before the response, mockmodel.c's pre-dispatch select) so that the
# note drop (t+3s, the driver's background `sleep 3 && cp`) lands in the inbox
# AFTER the harness has assembled the prompt (assembly is instant) but BEFORE
# the child exits and the move runs.
cat > "$tmp/delay9.mm" <<'MMEOF'
wire openai
rule
  delay 9000
  text Done: the lead run finished its one turn.
MMEOF

# --- the M1058 handoff cells' mocks ------------------------------------------
# The tenant's jichi config keeps "toolProfile":"none": measured, a scripted
# read_file/todowrite/spawn_parallel EXECUTE under none (file tools are not
# fenced off), so these cells need no extra profile. The agent runs ONE
# scripted tool per model round, and the mock answers by REQUEST INDEX (the
# `count N` predicate, the jsonl_tool_id idiom): the first round is the
# model's initial request (no tool result yet), and each round after a tool
# call is one more request carrying that call's result. A final text rule with
# no predicate is the catch-all that ends the run once the scripted tools are
# spent. H1's 15 todo contents are each >=141 chars so one_line's 140 cap
# fires (mixed pending/in_progress); the text reply is >=601 chars so the
# 600 cap fires. H2's six spawn_parallel calls are each 8 no-such-agent tasks
# (one 110+ chars so the 110 cap fires): every task fails IN PLACE with a
# tool_result preview the handoff quotes -- measured to cost one model round
# each, no sub-agent, no model call for a task.
cat > "$tmp/h1.mm" <<'MMEOF'
wire openai
rule
  count 1
  tool read_file {"path":"alpha15.txt"}
rule
  count 2
  tool read_file {"path":"beta15.txt"}
rule
  text Done reading the fixture files and writing the list, the run now answers a long reply: the previous run left a stream of read and todo events that the handoff must quote back in full, capping the last message at six hundred characters and the todo contents at one hundred and forty, naming both files it read with a read count, and stating how it ended at the commit the meta records so the next run can verify before it relies on anything the mechanical section cannot invent. fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword fillerword H1LASTMSG-CUT-PROOF
MMEOF

cat > "$tmp/h2.mm" <<'MMEOF'
wire openai
rule
  count 1
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B01 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B01 short task t8 reading nothing in place"}]}
rule
  count 2
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B02 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B02 short task t8 reading nothing in place"}]}
rule
  count 3
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B03 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B03 short task t8 reading nothing in place"}]}
rule
  count 4
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B04 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B04 short task t8 reading nothing in place"}]}
rule
  count 5
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B05 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B05 short task t8 reading nothing in place"}]}
rule
  count 6
  tool spawn_parallel {"tasks":[{"agent":"no-such-agent","model":"m","task":"H2B06 long task text over one hundred and ten characters so the one_line cap at 110 fires on this batch: xxxxxxxxxxxxxxxxxxxx"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t2 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t3 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t4 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t5 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t6 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t7 reading nothing in place"},{"agent":"no-such-agent","model":"m","task":"H2B06 short task t8 reading nothing in place"}]}
rule
  text The batches ran in place with no such agent; the run now finishes its turn so the next one can read the six batch events from its own stream record.
MMEOF

# smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once
"$BIN" --version > "$tmp/binv" 2>/dev/null

# Lay the harness config against a given jichi config path (LEAD is the
# drive.leadModel name; the happy fixtures pass "m", the config's own model).
lay_hcfg() {
    cat > "$hcfg" <<HCEOF
{"dir":"$drive",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com","leadModel":"$2"},
"modelKeyFile":"$keyfile",
"tenants":[{"name":"lead","home":"$drive/home","workdir":"$work",
"jichiConfig":"$1","promptFile":"$promptbase","bin":"$drive/bin/jichi","branchPrefix":"harness-plan"}]}
HCEOF
}

# Start a mock for $1 (a script) under capture dir $2, lay the jichi + harness
# configs against its port (the harness config's leadModel is $3), and install
# the argv-dump wrapper. The tenant's jichi config is written BY HAND, not via
# write_config: write_config hardcodes a literal "apiKey":"x", and jichi's
# resolve_key (jc_config.c) PREFERS a literal apiKey over apiKeyEnv, so the key
# would be the constant "x" and the canary never reaches the model -- R2's
# control (the model got the key) would be vacuous. No literal apiKey here. The
# key travels ONLY as the JICHI_API_KEY env var the harness delivers from the
# file (behaviour 2's fixed convention: the harness does not parse the jichi
# config for the key NAME, so it cannot read its apiKeyEnv -- it delivers under
# the reference's JICHI_API_KEY, the M242 setup-wizard default; jichi registers
# that name via apiKeyEnv and scrubs it from its children). R10's guard DOES
# parse the jichi config -- for the models table, not the key.
start_mock() {
    mm_start "$1" "$2" 8
    cat > "$jicfg" <<JCEOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKeyEnv":"JICHI_API_KEY","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"none","lowResource":false,"maxRetries":0}
JCEOF
    lay_hcfg "$jicfg" "$3"
    printf '%s\n' '#!/bin/sh' \
        "printf '%s\n' \"\$*\" > \"$dump\"" \
        "printf 'HOME=%s\n' \"\$HOME\" >> \"$dump\"" \
        "exec \"$BIN\" \"\$@\"" > "$drive/bin/jichi"
    # No `< /dev/null` on the exec (SV:0720): the wrapper must not close
    # stdin itself, or the R5 marker dies in the WRAPPER and check 6 could
    # not witness the teeth at the harness's fd_stdin (the supervisor's
    # INHERIT run would be ok, not red -- the assertion would hold for the
    # wrong reason).
    chmod 755 "$drive/bin/jichi"
}

# --- 1: R0 the not-built red, held by the driver itself ------------------------
# A BARE `run` (no --tenant, no --run) keeps the H0 not-built refusal -- init's
# precedent (a bare init keeps its H0 not-built refusal; the sibling driver
# harness_third_program.sh runs bare init and asserts that line). The parser
# acceptance for `run --config --tenant --run` ships WITH the code (the M1053
# precedent: on the tree before the code commit's parser+dispatch hunks the
# canonical form dies at the parser, so the not-built assertion lives here, on
# a shape the parser accepts -- bare `run` -- and the teeth probe reverting
# those hunks re-runs this driver and check 1 prints `not ok 1` with the
# not-built line).
"$HBIN" run > "$tmp/r0.out" 2> "$tmp/r0.err"
rc0=$?
if [ "$rc0" -ne 2 ] || [ -s "$tmp/r0.out" ] || \
   ! $G -qF "run: not built yet" "$tmp/r0.err"; then
    t_fail "1: a bare \`run\` did not keep the not-built refusal (rc=$rc0; stdout \
$([ -s "$tmp/r0.out" ] && echo NONEMPTY || echo empty); stderr:
$(head_bytes 160 "$tmp/r0.err"))"
else
    t_ok "1: a bare \`run\` (no --tenant, no --run) is the not-built refusal: exit 2, 'run: not built yet' on stderr, nothing on stdout (init's precedent)"
fi

# --- 2: R1 the happy run ------------------------------------------------------
start_mock "$tmp/happy.mm" "$tmp/mm_happy" m
rm -f "$dump"
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0001 < /dev/null \
    > "$tmp/r1.out" 2> "$tmp/r1.err"
rc1=$?
mm_stop
if [ "$rc1" -ne 0 ] || [ ! -s "$drive/runs/0001/stream.jsonl" ] || \
   ! $G -q "^run=0001" "$drive/runs/0001/meta.txt" 2>/dev/null || \
   ! $G -q "^rc=0" "$drive/runs/0001/meta.txt" 2>/dev/null || \
   ! $G -qE "^jichi=.* build [0-9a-f]{7,8}(-dirty)?" "$drive/runs/0001/meta.txt" 2>/dev/null || \
   ! $G -q "## This run" "$drive/runs/0001/prompt.md" 2>/dev/null || \
   ! $G -q "Run 0001" "$drive/runs/0001/prompt.md" 2>/dev/null || \
   [ ! -f "$drive/runs/0001/config.json" ]; then
    t_fail "2: the happy run did not land (rc=$rc1; harness err: $(cat "$tmp/r1.err" 2>/dev/null | head_bytes 300))"
else
    t_ok "2: the happy run exits the child's rc; stream.jsonl non-empty; meta.txt has run=, rc=0, a build: stamp; prompt.md has the run facts; config.json kept"
fi

# --- 3: R2 the key is on no argv, in no record, in the mock's request ---------
_leak=0
for f in "$dump" "$drive/runs/0001/meta.txt" "$drive/runs/0001/prompt.md" \
         "$drive/runs/0001/stream.jsonl" "$tmp/r1.out" "$tmp/r1.err"; do
    if [ -f "$f" ] && $G -qF "$_kc" "$f"; then
        _leak=1
    fi
done
_mmbody=$(cat "$tmp/mm_happy"/req.* 2>/dev/null)
if [ "$_leak" -ne 0 ]; then
    t_fail "3: the model key canary appears in the argv dump or a record (the secret rule)"
elif ! printf '%s' "$_mmbody" | $G -qF "$_kc"; then
    t_fail "3: the model did NOT receive the key from the FILE (the env delivery did not reach it: control failed)"
elif printf '%s' "$_mmbody" | $G -qF "$JICHI_API_KEY"; then
    t_fail "3: the AMBient key reached the model (the harness did not unset JICHI_API_KEY from its own env: the file is not the only source)"
else
    t_ok "3: the key is ABSENT from argv, meta, prompt, stream and the harness's own output; the model got the FILE canary, not the ambient key (env delivery worked, the file is the only source, nothing leaked)"
fi

# --- 4: R3 a missing model-key file is a named refusal ------------------------
cat > "$tmp/hcfg_miss.json" <<HCEOF
{"dir":"$drive","modelKeyFile":"$_missing_key",
"tenants":[{"name":"lead","home":"$drive/home","workdir":"$work","jichiConfig":"$jicfg","promptFile":"$promptbase","bin":"$drive/bin/jichi"}]}
HCEOF
rm -rf "$drive/runs/0003"
"$HBIN" run --config "$tmp/hcfg_miss.json" --tenant lead --run 0003 < /dev/null \
    > "$tmp/r3.out" 2> "$tmp/r3.err"
rc3=$?
if [ "$rc3" -ne 2 ] || [ -e "$drive/runs/0003" ]; then
    t_fail "4: a missing model-key file was not a named refusal (rc=$rc3, runs/0003 exists=$([ -e "$drive/runs/0003" ] && echo yes || echo no))"
else
    t_ok "4: a missing model-key file exits 2 and creates no runs/N/ (the refusal precedes the layout)"
fi

# --- 5: R4 caps off, fences on ------------------------------------------------
if [ -f "$dump" ] && { $G -qF -- "--deadline" "$dump" || $G -qF -- "--budget-tokens" "$dump"; }; then
    t_fail "5: the spawn argv carries a cap (--deadline or --budget-tokens)"
elif [ ! -f "$dump" ] || ! $G -qF -- "--max-tool-calls" "$dump" || ! $G -qF -- "--verify-timeout" "$dump"; then
    t_fail "5: the spawn argv is missing a fence (--max-tool-calls or --verify-timeout)"
else
    t_ok "5: caps off (no --deadline/--budget-tokens), fences on (--max-tool-calls + --verify-timeout present)"
fi

# --- 6: R5 the run did not read the harness's stdin (SV:0337.1, non-vacuous) -
# NOT a re-run of check 2's run 0001 (re-running 0001 risks the check 2/3/9/
# 17 assertions on that run's records -- the previous attempt's loss). A
# DEDICATED happy run 0021 (same tenant, happy.mm) is fed through a bounded
# pipe carrying a marker: printf closes the pipe at the newline, so it is a
# bounded pipe, not a held-open one (caps off, fences on). The marker is
# MEASURED to reach the model's REQUEST BODY when a child inherits stdin
# (jichi appends piped stdin as context even under --prompt-b64, main.c's
# no---no-stdin path, 2026-10-26 measurement), so the assertion is real:
# the marker is NOT in any of this run's mock-captured request bodies (the
# start_mock capture dir) AND NOT in runs/0021/prompt.md. The harness's
# fd_stdin = JC_SPAWN_DEVNULL (jc_harness_run.c:1404) keeps the child's stdin
# at /dev/null, so the marker dies there. THE TEETH ARE SUPERVISOR-WITNESSED
# (SV:0720), not run here: run B (that line flipped to JC_SPAWN_INHERIT, the
# wrapper's exec carrying NO `< /dev/null` of its own -- see start_mock) put
# the marker IN the capture, so this check goes RED there; run C (this
# commit's JC_SPAWN_DEVNULL line) leaves it absent, so ok. That contrast is
# the assertion's own teeth. The flip probe stays merge:false, a separate
# task, not this commit.
start_mock "$tmp/happy.mm" "$tmp/mm_stdin21" m
# "$0" inside the sh -c is "$HBIN" (passed as its first argument): the driver's
# OWN $0 is the script path, so the marker-pipe shell must not reference it.
with_deadline 60 sh -c 'printf "STDIN-MARK-42\n" | "$0" run --config "$1" --tenant lead --run 0021' "$HBIN" "$hcfg" \
    > "$tmp/r21.out" 2> "$tmp/r21.err"
rc21=$?
mm_stop
_mark21=0
for _f21 in "$tmp/mm_stdin21"/req.* "$drive/runs/0021/prompt.md"; do
    if [ -f "$_f21" ] && $G -qF "STDIN-MARK-42" "$_f21" 2>/dev/null; then
        _mark21=1
    fi
done
if [ "$_mark21" -ne 0 ]; then
    t_fail "6: the run read the harness's stdin -- STDIN-MARK-42 is in a captured \
request body or in runs/0021/prompt.md (rc=$rc21) -- the child must keep stdin \
at /dev/null (fd_stdin = JC_SPAWN_DEVNULL); the teeth probe (flip to \
JC_SPAWN_INHERIT, rebuild, witness the marker in the capture) is merge:false, a \
separate task, not this commit"
elif [ "$rc21" -ne 0 ]; then
    t_fail "6: run 0021 (the dedicated stdin-marker run) did not complete (rc=$rc21; \
harness err: $(head_bytes 200 "$tmp/r21.err" 2>/dev/null)) -- a spawn blocked on the \
pipe would show here, not only in the marker assert"
else
    t_ok "6: the run did not read the harness's stdin -- a dedicated happy run 0021 on a bounded pipe carrying STDIN-MARK-42; the marker is in NONE of the mock's captured request bodies and NOT in runs/0021/prompt.md (fd_stdin = JC_SPAWN_DEVNULL kills it at the spawn)"
fi

# --- 7: R6 a bad tenant is a usage refusal ------------------------------------
"$HBIN" run --config "$hcfg" --tenant nobody --run 0006 < /dev/null \
    > "$tmp/r6.out" 2> "$tmp/r6.err"
rc6a=$?
"$HBIN" run --config "$hcfg" --run 0006 < /dev/null \
    > "$tmp/r6b.out" 2> "$tmp/r6b.err"
rc6b=$?
if [ "$rc6a" -ne 2 ] || ! $G -qF "no tenant named" "$tmp/r6.err" || \
   [ -e "$drive/runs/0006" ] || [ "$rc6b" -ne 2 ]; then
    t_fail "7: a bad tenant / missing tenant was not a usage refusal (rc=$rc6a/$rc6b, runs/0006 exists)"
else
    t_ok "7: a bad tenant exits 2 naming the tenant and creates nothing; a missing tenant also exits 2"
fi

# --- 8: R7 the child's HOME is the tenant's home ------------------------------
_home_line=$($G '^HOME=' "$dump" 2>/dev/null | head -1)
if [ "$_home_line" != "HOME=$drive/home" ]; then
    t_fail "8: the child's HOME is not the tenant's home (got: ${_home_line:-<no HOME line in the dump>})"
else
    t_ok "8: the child's HOME is the tenant's home (the isolated state root, not the operator's)"
fi

# --- 9: R8 the journal and telemetry are outside the workspace ----------------
if [ ! -s "$drive/runs/0001/journal.jsonl" ] || [ ! -s "$drive/runs/0001/telemetry.jsonl" ]; then
    t_fail "9: the journal or telemetry did not land in runs/0001/ (outside the workspace)"
else
    # No *.jsonl the driver did not create may sit under the workdir.
    _stray=""
    for f in "$work"/*.jsonl "$work"/.jichi/*.jsonl; do
        [ -f "$f" ] && _stray="$_stray $f"
    done
    if [ -n "$_stray" ]; then
        t_fail "9: record files leaked into the workspace:$_stray"
    else
        t_ok "9: journal.jsonl and telemetry.jsonl are in runs/0001/ and NO record file is under the workdir (the H4 done-when line)"
    fi
fi

# --- 10: R9 the notes are kept until the first model call succeeds ------------
# (a) a happy run MOVES the note into the run's records.
printf 'a supervisor note for run 0009\n' > "$drive/inbox/note-a.md"
start_mock "$tmp/happy.mm" "$tmp/mm_ok9" m
rm -f "$dump"
"$HBIN" run --config "$hcfg" --tenant lead --run 0009 < /dev/null > "$tmp/r9a.out" 2> "$tmp/r9a.err"
rc9a=$?
mm_stop
# (b) a first-call-failed run KEEPS the note in the inbox.
printf 'a supervisor note for run 0009b\n' > "$drive/inbox/note-b.md"
start_mock "$tmp/dead.mm" "$tmp/mm_bad9" m
"$HBIN" run --config "$hcfg" --tenant lead --run 0010 < /dev/null > "$tmp/r9b.out" 2> "$tmp/r9b.err"
rc9b=$?
mm_stop
if [ "$rc9a" -ne 0 ] || [ -e "$drive/inbox/note-a.md" ] || [ ! -f "$drive/runs/0009/notes/note-a.md" ]; then
    t_fail "10: a happy run did not move the note into runs/0009/notes/ (rc=$rc9a; inbox still holds note-a=$([ -e "$drive/inbox/note-a.md" ] && echo yes || echo no))"
elif [ ! -e "$drive/inbox/note-b.md" ] || [ -f "$drive/runs/0010/notes/note-b.md" ]; then
    t_fail "10: a first-call-failed run (rc=$rc9b) still moved the note (it should have kept inbox/note-b.md)"
else
    t_ok "10: the note is moved into the run's records after a successful first call, and KEPT in the inbox when the first call failed"
fi

# --- 11: R10 the lead-model guard (SV:1818.1) ---------------------------------
# (a) a leadModel the tenant's jichi config does NOT name (its models array
# holds only "m"): the harness WARNS on its own stderr (the model named, never
# a key or a URL) and OMITS --model from the spawn argv -- the run still
# exits the child's rc (0), on the config's routing. The behaviour is the
# reference's (run-lead.sh 49-52), WARN + OMIT, not a refusal.
rm -f "$dump"
start_mock "$tmp/happy.mm" "$tmp/mm_r10a" no-such-model
"$HBIN" run --config "$hcfg" --tenant lead --run 0011 < /dev/null \
    > "$tmp/r10a.out" 2> "$tmp/r10a.err"
rc10a=$?
mm_stop
# The happy fixtures' harness config (leadModel "m", the config's own model):
lay_hcfg "$jicfg" m
dump_r10a="$tmp/jichi-argv-r10a.txt"
cp -f "$dump" "$dump_r10a" 2>/dev/null
# (b) a leadModel the config DOES name: passed as --model <name>, no warn.
rm -f "$dump"
start_mock "$tmp/happy.mm" "$tmp/mm_r10b" m
"$HBIN" run --config "$hcfg" --tenant lead --run 0012 < /dev/null \
    > "$tmp/r10b.out" 2> "$tmp/r10b.err"
rc10b=$?
mm_stop
dump_r10b="$tmp/jichi-argv-r10b.txt"
cp -f "$dump" "$dump_r10b" 2>/dev/null
if [ "$rc10a" -ne 0 ] || ! $G -qF "lead-model no-such-model is not a model" "$tmp/r10a.err" \
   || [ ! -f "$dump_r10a" ] || $G -qF -- "--model" "$dump_r10a"; then
    t_fail "11: the lead-model guard (a): an un-named leadModel was not WARNED+OMITTED \
(rc=$rc10a; stderr: $(head_bytes 200 "$tmp/r10a.err"); argv has --model: \
$([ -f "$dump_r10a" ] && { $G -qF -- "--model" "$dump_r10a" && echo yes || echo no; } || echo no-dump))"
elif [ "$rc10b" -ne 0 ] || [ ! -f "$dump_r10b" ] || ! $G -qF -- "--model m" "$dump_r10b" \
   || $G -qF "lead-model m is not a model" "$tmp/r10b.err"; then
    t_fail "11: the lead-model guard (b): a NAMED leadModel was not passed \
(rc=$rc10b; argv has '--model m': \
$([ -f "$dump_r10b" ] && { $G -qF -- "--model m" "$dump_r10b" && echo yes || echo no; } || echo no-dump); \
warn: $(head_bytes 120 "$tmp/r10b.err"))"
else
    t_ok "11: the lead-model guard -- an un-named leadModel is WARNED on the harness's stderr and OMITTED from the argv (the run still exits 0, on the config's routing); a named leadModel IS passed as --model"
fi

# --- M1058 (H4) the handoff cells: runs 0013-0020, checks 12-17 -------------
# The NEXT run's handoff is built from the PREVIOUS run's records, so the pairs
# execute 0015, 0016, 0013, 0014, 0022, 0023, 0019, 0020, 0017, 0018
# (assertions grouped by check after both runs of their pair; 0022/0023 are
# check 13's SV:0959.3 pairing tooth). Every harness invocation is deadline-
# bounded the way checks 2/9/10/11 bound theirs (a blocked harness would hang
# the whole tier).
# The mocks stay small: one scripted tool per model round, and a batch's 8
# no-such-agent tasks all fail IN PLACE in one round (no sub-agent, no model
# call per task), so each run is ~1-2 s.
# H1's two read_file fixtures: small files under the workdir (jichi chdirs the
# child there, so the relative paths resolve against $work).
printf 'alpha fixture body one\n' > "$work/alpha15.txt"
printf 'beta fixture body two\n' > "$work/beta15.txt"

# --- 12: H1 the todo/files/notes/last-message section ------------------------
# Run 0015 with the h1.mm tool mock (two read_file on the fixture files, one
# 15-item todowrite, a >=601-char reply), then FILE FIVE notes into
# runs/0015/notes/ (a fabricated record -- the inbox MOVE that would file them
# is already proven by check 10/R9). SV:0557.1's set, byte-sorted: a.md (short,
# rendered), d.gz (PLAIN file with a .gz NAME, not real gzip -- the port
# renders it (unreadable) by name, the def-358 negative, rendered), e-dash.md
# (600 em-dashes, U+2014, 3 bytes each = 1800 bytes -- check 15's code-point
# cut, rendered), long.md (the 500+-char body with embedded newlines and
# em-dash/arrow around position 499, rendered), z.md (short, the "and 1 more",
# never rendered). The reference renders the FIRST FOUR of a byte-sorted
# listing (handoff.py's MAX_NOTES), so the window is exactly a.md, d.gz,
# e-dash.md, long.md: a note named after long.md is dropped from the window,
# one named before a.md pushes e-dash.md out (SV:0557.1 caught the second
# worker's emdash.md naming it exactly backwards).
start_mock "$tmp/h1.mm" "$tmp/mm_h1" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0015 < /dev/null \
    > "$tmp/r15.out" 2> "$tmp/r15.err"
rc15=$?
mm_stop
# Fabricate 0015's 15-item todowrite into its OWN stream.jsonl (the note's "file
# a record the driver fabricated is the teeth idiom"). A 15-item todowrite with
# >=141-char contents is ~2.5 KB of JSON -- far over mockmodel's 1024-byte
# per-line script cap (tests/tools/mm_core.c linebuf), so it CANNOT be a scripted
# `tool todowrite` line: h1.mm scripts only the two read_files + the reply. The
# stream's tool_call stores args as a JSON STRING (main.c hl_on_tool_call), so
# appending valid JSON here is PARSE-EQUAL to what jichi would have written
# (SV:0518.3(a): the streams decode to the same event sequence; they are not
# byte-identical, and are not asserted to be -- the writer's spacing is its own).
{
printf '%s\n' '{"v": 1, "type": "tool_call", "name": "todowrite", "args": "{\"todos\": [{\"content\": \"PROBE-T00 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T01 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T02 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T03 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"in_progress\"}, {\"content\": \"PROBE-T04 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T05 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T06 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T07 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"in_progress\"}, {\"content\": \"PROBE-T08 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T09 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T10 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T11 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"in_progress\"}, {\"content\": \"PROBE-T12 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T13 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}, {\"content\": \"PROBE-T14 a todo content that is deliberately over one forty characters long so the handoff one_line cap at one forty fires on every single item in th\", \"status\": \"pending\"}]}", "id": "c15t"}'
printf '%s\n' '{"v": 1, "type": "tool_result", "name": "todowrite", "is_error": false, "preview": "todos written", "id": "c15t"}'
} >> "$drive/runs/0015/stream.jsonl"
mkdir -p "$drive/runs/0015/notes"
printf 'note a short body line\n' > "$drive/runs/0015/notes/a.md"
printf 'note z short body line\n' > "$drive/runs/0015/notes/z.md"
printf 'not really gzip, just a name\n' > "$drive/runs/0015/notes/d.gz"
# e-dash.md: 600 em-dashes (U+2014), written as LITERAL characters -- never
# printf \xNN (the dash's builtin printf does not interpret \xNN): ten groups
# of ten dashes, six times. 600 code points = 1800 bytes (verified below;
# check 15 asserts the size, the teeth).
_ed100=''
for _i in 1 2 3 4 5 6 7 8 9 10; do _ed100="${_ed100}——————————"; done
printf '%s%s%s%s%s%s' "$_ed100" "$_ed100" "$_ed100" "$_ed100" "$_ed100" "$_ed100" \
    > "$drive/runs/0015/notes/e-dash.md"
_edash_sz=$(wc -c < "$drive/runs/0015/notes/e-dash.md" | tr -d ' ')
{
printf '%s\n' \
'LONGNOTE first supervisor note body for run fifteen: the handoff must collapse this whitespace and cut it at five hundred characters, so every later word is stripped' \
'filler words that keep the note well over the five hundred character cap in order to prove the cut LONGNOTE first supervisor note body for run fifteen: the handoff must' \
'collapse this whitespace and cut it at five hundred characters, so every later word is stripped filler words that keep the note well over the five hundred charac—→r cap in order to H1NOTE-CUT-PROOF'
} > "$drive/runs/0015/notes/long.md"
# 0016: a text-only reader; its prompt must carry 0015's section.
start_mock "$tmp/happy.mm" "$tmp/mm_h16" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0016 < /dev/null \
    > "$tmp/r16.out" 2> "$tmp/r16.err"
rc16=$?
mm_stop
_p16="$drive/runs/0016/prompt.md"; _m16="$drive/runs/0016/meta.txt"
_h15=$(git -C "$work" rev-parse --short HEAD 2>/dev/null)
_nt16=$($G -F -c "  - [" "$_p16" 2>/dev/null)
# SV:1937.1 the seam pin: the handoff header sits on its OWN line, preceded
# by an EMPTY line (the reference's blank line between the records sentence
# and the section). awk walks the lines and reports yes ONLY when an empty
# line is immediately followed by the exact header line; a header fused onto
# the records sentence (the M1058 bug) yields no.
_seam16=$(awk 'BEGIN{p=""} {if(p=="" && $0=="## What the previous run left you"){print "yes"; exit} p=$0}' "$_p16" 2>/dev/null)
if [ "$rc15" -ne 0 ] || [ "$rc16" -ne 0 ] || [ "$_seam16" != yes ] || \
   ! $G -q "## What the previous run left you" "$_p16" 2>/dev/null || \
   ! $G -qF "*Written by the harness from run 0015's own records" "$_p16" 2>/dev/null || \
   ! $G -qF "rc 0" "$_p16" 2>/dev/null || \
   ! $G -qF "it ended itself" "$_p16" 2>/dev/null || \
   ! $G -qF "It ended at commit \`$_h15\`" "$_p16" 2>/dev/null || \
   ! $G -qF "Its last message:" "$_p16" 2>/dev/null || \
   $G -qF "H1LASTMSG-CUT-PROOF" "$_p16" 2>/dev/null || \
   ! $G -qF "Its todo list, as it last wrote it:" "$_p16" 2>/dev/null || \
   [ "$_nt16" != 15 ] || \
   ! $G -qF "The files it read or changed" "$_p16" 2>/dev/null || \
   ! $G -qF "`alpha15.txt` (read 1)" "$_p16" 2>/dev/null || \
   ! $G -qF "`beta15.txt` (read 1)" "$_p16" 2>/dev/null || \
   ! $G -qF "The manager's notes it was given" "$_p16" 2>/dev/null || \
   ! $G -qF "runs/0015/notes" "$_p16" 2>/dev/null || \
   $G -qF "/records/runs" "$_p16" 2>/dev/null || \
   ! $G -qF '  - `a.md`: "' "$_p16" 2>/dev/null || \
   ! $G -qF '  - `d.gz`: "' "$_p16" 2>/dev/null || \
   ! $G -qF '  - `e-dash.md`: "' "$_p16" 2>/dev/null || \
   ! $G -qF '  - `long.md`: "' "$_p16" 2>/dev/null || \
   ! $G -qF "and 1 more" "$_p16" 2>/dev/null || \
   ! $G -qF "(unreadable)" "$_p16" 2>/dev/null || \
   ! $G -qF "charac—→r" "$_p16" 2>/dev/null || \
   $G -qF "H1NOTE-CUT-PROOF" "$_p16" 2>/dev/null || \
   ! $G -qF "handoff=from run 0015" "$_m16" 2>/dev/null; then
    t_fail "12: run 0016's prompt did not carry run 0015's handoff section (the \
todo list, the two (read 1) files, the four rendered note lines BY NAME -- \
a.md, d.gz, e-dash.md, long.md -- incl. the (unreadable) .gz, the capped last \
message) -- rc15=$rc15 rc16=$rc16, todo items=$_nt16, prompt head: \
$(head_bytes 240 "$_p16")"
else
    t_ok "12: run 0016's prompt carries run 0015's handoff -- header + the run 0015's-own-records guard, rc 0 + it ended itself + It ended at commit (the team_head_after sha), Its last message: capped, Its todo list with 15 items, the files naming both (read 1), the notes with the Mode-A path + each of the four rendered lines BY NAME (a.md, d.gz, e-dash.md, long.md) + and 1 more + the (unreadable) .gz + the long note's 500-char cut; 0016's meta says handoff=from run 0015"
fi

# --- 13: H2 the batches (six spawn_parallel, 8 tasks each) -------------------
# PLUS the SV:0959.3 pairing tooth (M1058 leg 5) on a NEW pair 0022 -> 0023:
# NOT run 0013/0014, which carries the 6000-byte cap and would swallow the
# fabricated events (leg 4's check-14 cell needs 0014's handoff cut as-is,
# h2.mm stays as committed). Run 0022 is a REAL text-only run (happy.mm);
# its OWN stream.jsonl then gets TWO fabricated spawn_parallel calls with
# DISTINCT ids c13a/c13b and their TWO tool_results in REVERSE stream order
# (PREV-B first, PREV-A second) -- the H1/CHANGED-6 fabrication idiom
# (parse-equal, byte-wise not asserted). The handoff pairs a batch with its
# result BY id (last-wins over the stream -- M442's own reason: "order is
# exactly what a concurrent or reordered result set does not preserve"), so
# run 0023's prompt must quote PREV-A under `Its batch 1:` (call id c13a)
# and PREV-B under `Its batch 2:` (call id c13b): a position-pairing
# handoff would swap them. One task per call (not 8) keeps 0023's handoff
# comfortably under the cap, so check 14 is untouched.
start_mock "$tmp/h2.mm" "$tmp/mm_h2" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0013 < /dev/null \
    > "$tmp/r13.out" 2> "$tmp/r13.err"
rc13=$?
mm_stop
start_mock "$tmp/happy.mm" "$tmp/mm_h14" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0014 < /dev/null \
    > "$tmp/r14.out" 2> "$tmp/r14.err"
rc14=$?
mm_stop
# The SV:0959.3 pair: real 0022 (text-only), fabrication into its stream,
# then 0023 whose prompt carries 0022's handoff.
start_mock "$tmp/happy.mm" "$tmp/mm_h22" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0022 < /dev/null \
    > "$tmp/r22.out" 2> "$tmp/r22.err"
rc22=$?
mm_stop
{
printf '%s\n' '{"v": 1, "type": "tool_call", "name": "spawn_parallel", "args": "{\"tasks\": [{\"agent\": \"no-such-agent\", \"model\": \"m\", \"task\": \"SV0959 pairing tooth batch one: a single task whose result must pair with this call by its id, not by stream position\"}]}", "id": "c13a"}'
printf '%s\n' '{"v": 1, "type": "tool_result", "name": "spawn_parallel", "is_error": false, "preview": "PREV-B second batch result preview: the reverse-order result for id c13b must pair with the second call", "id": "c13b"}'
printf '%s\n' '{"v": 1, "type": "tool_call", "name": "spawn_parallel", "args": "{\"tasks\": [{\"agent\": \"no-such-agent\", \"model\": \"m\", \"task\": \"SV0959 pairing tooth batch two: a single task whose result must pair with this call by its id, not by stream position\"}]}", "id": "c13b"}'
printf '%s\n' '{"v": 1, "type": "tool_result", "name": "spawn_parallel", "is_error": false, "preview": "PREV-A first batch result preview: the reverse-order result for id c13a must pair with the first call", "id": "c13a"}'
} >> "$drive/runs/0022/stream.jsonl"
start_mock "$tmp/happy.mm" "$tmp/mm_h23" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0023 < /dev/null \
    > "$tmp/r23.out" 2> "$tmp/r23.err"
rc23=$?
mm_stop
_p14="$drive/runs/0014/prompt.md"
_p23="$drive/runs/0023/prompt.md"
_h2bad=""
for _i in 1 2 3 4 5 6; do
    $G -qF "Its batch $_i:** 8 task(s); the result began:" "$_p14" 2>/dev/null || _h2bad="$_h2bad $_i"
done
# The pairing assert, line by line: the batch-1 line must carry PREV-A and the
# batch-2 line PREV-B (grep -F per line; a position-pairing handoff renders
# batch 1 with PREV-B and batch 2 with PREV-A -- the reverse).
_p23b1=$(head_bytes 4000 "$_p23" 2>/dev/null | $G -F "Its batch 1:**" 2>/dev/null)
_p23b2=$(head_bytes 4000 "$_p23" 2>/dev/null | $G -F "Its batch 2:**" 2>/dev/null)
_p23ok=yes
[ -n "$_p23b1" ] || _p23ok=no
printf '%s' "$_p23b1" | $G -qF "PREV-A first batch result preview" 2>/dev/null || _p23ok=no
printf '%s' "$_p23b1" | $G -qF "PREV-B second batch result preview" 2>/dev/null && _p23ok=no
[ -n "$_p23b2" ] || _p23ok=no
printf '%s' "$_p23b2" | $G -qF "PREV-B second batch result preview" 2>/dev/null || _p23ok=no
printf '%s' "$_p23b2" | $G -qF "PREV-A first batch result preview" 2>/dev/null && _p23ok=no
if [ "$rc13" -ne 0 ] || [ "$rc14" -ne 0 ] || [ -n "$_h2bad" ] || \
   [ "$rc22" -ne 0 ] || [ "$rc23" -ne 0 ] || [ "$_p23ok" != yes ]; then
    t_fail "13: H2 batches and/or the SV:0959.3 pairing tooth failed -- \
0014 missing batch line(s)($_h2bad), 0023 pairing=$_p23ok (batch 1 must \
quote PREV-A, batch 2 must quote PREV-B: results arrive in REVERSE stream \
order, the handoff pairs by id not by position) -- rc13=$rc13 rc14=$rc14 \
rc22=$rc22 rc23=$rc23; 0023 prompt head: $(head_bytes 240 "$_p23")"
else
    t_ok "13: run 0014's prompt carries Its batch 1: through Its batch 6:, each 8 task(s); the result began: (the six spawn_parallel calls, 8 no-such-agent tasks each); and run 0023's prompt pairs the fabricated reverse-order results BY id: batch 1 quotes PREV-A (id c13a), batch 2 quotes PREV-B (id c13b) (SV:0959.3)"
fi

# --- 14: H3 the byte cap (run 0014's handoff.md is built from 0013's batches) -
_ho14="$drive/runs/0014/handoff.md"
_sz14=$(wc -c < "$_ho14" 2>/dev/null | tr -d ' ')
# The -- terminator (SV:1217): the marker starts with a dash, and without it
# grep reads it as its own option (invalid option, exit 2, hidden by
# 2>/dev/null) and reports marker=no for EVERY file.
_mark14=$($G -qF -- "- *(cut at 6000 bytes)*" "$_ho14" 2>/dev/null && echo yes || echo no)
if [ ! -f "$_ho14" ] || [ "$_mark14" != yes ] || [ -z "$_sz14" ] || [ "$_sz14" -gt 6040 ]; then
    t_fail "14: run 0014's handoff.md is not the 6000-byte-capped section from \
0013's eight-task batches (marker=$_mark14, size=$_sz14 bytes; expected the cut \
marker and a size <= 6040) -- head: $(head_bytes 120 "$_ho14" 2>/dev/null)"
else
    t_ok "14: run 0014's handoff.md carries '- *(cut at 6000 bytes)*' and is <= 6040 bytes (the section from 0013's eight-task batches exceeds the cap, so the cut and the marker both fire)"
fi

# --- 15: H4 the two behaviours with no cell yet (SV:0417.1 / SV:0518.2) ------
# The reserved slot becomes a REAL cell: two assertions, each with its own
# teeth, one t_ok/t_fail (t_plan stays 17).
# (a) CHANGED 6 -- apply_patch has NO top-level path; its paths live in
# edits[].path (verified against src/tools/jc_tool_apply_patch.c's per-edit
# path). The reference's `args.path or args.file` therefore NEVER counts an
# apply_patch edit -- every apply_patch run under-reports its file table
# silently. Run 0019 is a text-only mock run; its OWN stream.jsonl then gets a
# tool_call with name apply_patch (args = the JSON string {"edits":[{...p19a.txt
# ...},{...p19b.txt ...}]}, id c19) and a tool_result with the same id -- the
# H1 todowrite fabrication idiom, the same printf shape: PARSE-EQUAL to what
# jichi would have written, NOT byte-identical. Run 0020 (text-only mock) reads
# 0019's records: its prompt's files line must carry BOTH p19a.txt and p19b.txt,
# each (changed 1). Teeth: remove the port's edits[] branch and 0020's prompt
# drops both paths.
cat > "$tmp/h19.mm" <<'MMEOF'
wire openai
rule
  text The run answers its one round so the next run can read its stream record for the CHANGED-6 cell.
MMEOF
start_mock "$tmp/h19.mm" "$tmp/mm_h19" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0019 < /dev/null \
    > "$tmp/r19.out" 2> "$tmp/r19.err"
rc19=$?
mm_stop
{
printf '%s\n' '{"v": 1, "type": "tool_call", "name": "apply_patch", "args": "{\"edits\": [{\"path\": \"p19a.txt\", \"old_string\": \"x\", \"new_string\": \"y\"}, {\"path\": \"p19b.txt\", \"old_string\": \"x\", \"new_string\": \"z\"}]}", "id": "c19"}'
printf '%s\n' '{"v": 1, "type": "tool_result", "name": "apply_patch", "is_error": false, "preview": "edits applied", "id": "c19"}'
} >> "$drive/runs/0019/stream.jsonl"
start_mock "$tmp/happy.mm" "$tmp/mm_h20" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0020 < /dev/null \
    > "$tmp/r20.out" 2> "$tmp/r20.err"
rc20=$?
mm_stop
_p20="$drive/runs/0020/prompt.md"
# (b) b11, code points -- the cut is at exactly n CODE POINTS, never mid-
# sequence (SV:0319.3): H1's e-dash.md is 600 em-dashes (U+2014, 3 bytes each,
# 1800 bytes); one_line at 500 renders 499 dashes plus the 3-byte ellipsis, so
# run 0016's handoff.md (H1's own handoff, already produced) must carry a
# COMPLETE em-dash immediately followed by \u2026 (the needle, literal in this
# script, not \xNN), and the whole file must STRICT-DECODE as UTF-8 (iconv -- a
# tool that decodes, not a byte count). The 1800-byte fixture check is the
# teeth: a byte-counting one_line cuts at byte 500, mid-dash, and both fail.
# No new run for (b): it asserts H1's already-produced handoff.
_ho16="$drive/runs/0016/handoff.md"
_utf816=$(iconv -f UTF-8 -t UTF-8 "$_ho16" > /dev/null 2>&1 && echo yes || echo no)
_needle16=$([ -f "$_ho16" ] && $G -qF "—…" "$_ho16" 2>/dev/null && echo yes || echo no)
if [ "$rc19" -ne 0 ] || [ "$rc20" -ne 0 ] || \
   ! $G -qF '`p19a.txt` (changed 1)' "$_p20" 2>/dev/null || \
   ! $G -qF '`p19b.txt` (changed 1)' "$_p20" 2>/dev/null || \
   [ "${_edash_sz:-0}" != 1800 ] || \
   [ "$_utf816" != yes ] || [ "$_needle16" != yes ]; then
    t_fail "15: H4 -- (a) 0020's prompt must carry 0019's fabricated apply_patch \
as CHANGED 6 (p19a.txt and p19b.txt, each (changed 1)); (b) 0016's handoff.md \
must strict-decode UTF-8 and carry the complete em-dash followed by the \
ellipsis (the 1800-byte e-dash.md note cut at 500 CODE POINTS) -- rc19=$rc19 \
rc20=$rc20, e-dash.md=${_edash_sz:-absent} bytes [want 1800], utf8-strict=$_utf816, \
needle(—…)=$_needle16, 0020 prompt head: $(head_bytes 160 "$_p20")"
else
    t_ok "15: H4 -- (a) apply_patch's edits[].path are counted as changes: 0020's prompt names p19a.txt and p19b.txt, each (changed 1) (CHANGED 6, the reference's under-report fixed); (b) the 1800-byte e-dash.md (600 em-dashes) renders cut at 500 CODE POINTS: 0016's handoff.md strict-decodes as UTF-8 and carries the complete em-dash immediately followed by the ellipsis"
fi

# --- 16: H5 the fail-open (0017, delete its stream, then 0018) ---------------
start_mock "$tmp/happy.mm" "$tmp/mm_h17" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0017 < /dev/null \
    > "$tmp/r17.out" 2> "$tmp/r17.err"
rc17=$?
mm_stop
rm -f "$drive/runs/0017/stream.jsonl"
start_mock "$tmp/happy.mm" "$tmp/mm_h18" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0018 < /dev/null \
    > "$tmp/r18.out" 2> "$tmp/r18.err"
rc18=$?
mm_stop
_p18="$drive/runs/0018/prompt.md"; _m18="$drive/runs/0018/meta.txt"
_ho18="$drive/runs/0018/handoff.md"; _he18="$drive/runs/0018/handoff.err"
_hdr18=$($G -q "## What the previous run left you" "$_p18" 2>/dev/null && echo yes || echo no)
_ho18empty=$([ -f "$_ho18" ] && [ ! -s "$_ho18" ] && echo yes || echo no)
_he18nonempty=$([ -f "$_he18" ] && [ -s "$_he18" ] && echo yes || echo no)
_none18=$($G -qF "handoff=none" "$_m18" 2>/dev/null && echo yes || echo no)
if [ "$rc18" -ne 0 ] || [ "$_hdr18" != no ] || [ "$_ho18empty" != yes ] || \
   [ "$_he18nonempty" != yes ] || [ "$_none18" != yes ]; then
    t_fail "16: run 0018 (after 0017's stream.jsonl was deleted) did not fail open \
(rc18=$rc18; header in prompt=$_hdr18 [want no]; handoff.md empty=$_ho18empty [want yes]; \
handoff.err non-empty=$_he18nonempty [want yes]; meta handoff=none=$_none18 [want yes]); \
prompt head: $(head_bytes 200 "$_p18" 2>/dev/null)"
else
    t_ok "16: run 0018 fails open on 0017's deleted stream -- exits the child's rc (0), its prompt has NO handoff header, handoff.md is EMPTY, handoff.err is NON-EMPTY (the filed reason), and meta says handoff=none"
fi

# --- 17: H6 the first run has no header --------------------------------------
_p01="$drive/runs/0001/prompt.md"; _m01="$drive/runs/0001/meta.txt"
_he01="$drive/runs/0001/handoff.err"
_hdr01=$($G -q "## What the previous run left you" "$_p01" 2>/dev/null && echo yes || echo no)
_none01=$($G -qF "handoff=none" "$_m01" 2>/dev/null && echo yes || echo no)
# SV:1937.2 check-17's tooth: a GENUINE first run (0001, whose previous dir
# 0000 never existed) has no drop to file, so runs/0001/handoff.err must be
# EMPTY -- the guard `[ -d prev ]` alone cannot prove that, since a port that
# files a spurious reason for a first run still passes the header/meta
# asserts. An existing NON-empty handoff.err here is red.
_he01empty=$([ -f "$_he01" ] && [ ! -s "$_he01" ] && echo yes || echo no)
if [ "$_hdr01" != no ] || [ "$_none01" != yes ] || [ "$_he01empty" != yes ]; then
    t_fail "17: run 0001 (the first run) still carries a handoff header, does not \
say handoff=none, or files a spurious handoff.err (header in prompt=$_hdr01 \
[want no]; meta handoff=none=$_none01 [want yes]; handoff.err empty=$_he01empty \
[want yes]) -- the M1057 first-run stub must be gone and a genuine first run \
has no previous to file"
else
    t_ok "17: run 0001's prompt has NO '## What the previous run left you' line, its meta says handoff=none (the M1057 first-run stub is gone), and runs/0001/handoff.err is EMPTY (SV:1937.2: a genuine first run files no reason)"
fi

# --- 18: M1063 the run consumes ONLY the notes it read (SV:0904.2) -----------
# The defect: the harness READS the inbox at prompt assembly (readdir order)
# but MOVES every .md present when the child exits. A note dropped between
# those two moments is filed under a run that never read it (the run 0077
# instance, SV:0904.2), and the handoff's "notes given" lists it too. The
# fix: the move takes the assembly-time list (name-sorted) and moves exactly
# those; a note that appears after assembly is never moved.
#
# 0024: pre-place m1063-a.md; at t+3s (the mock's `delay 9000` holds the
# child's one reply until t+9s) drop m1063-d.md from a background sleep.
# After: 0024's prompt carries A but NOT D; A is filed in 0024/notes/, D is
# STILL in the inbox and NOT filed. 0025 (happy mock): D's body IS in its
# prompt (delivered to the next run), and the two ordering notes (a-z-order,
# z-a-order) appear in NAME order in the prompt (### a-z-order before
# ### z-a-order); all three are filed in 0025/notes/.
# Teeth: pre-fix, the move is a bare readdir at exit -- D is filed under 0024
# and its body is absent from 0025's prompt (the first clause below goes red).
# A readdir-order assembly could also put z-a before a-z in the prompt; the
# header-position assert pins the sort. The harness calls are deadline-bounded
# as checks 2/9/10/11 bound theirs (with_deadline 60).
printf 'M1063 note A body line\n' > "$drive/inbox/m1063-a.md"
# The note D's body, staged beside $tmp: the drop (t+3s) copies it into the
# inbox while 0024 runs.
printf 'M1063 note D dropped during the run\n' > "$tmp/m1063-d-src.md"
start_mock "$tmp/delay9.mm" "$tmp/mm_d24" m
# exec sh -c '...': a backgrounded subshell whose $! is captured must exec
# (smoke_lint 15: on OpenBSD ksh $! is the SUBSHELL, so wait would hit the
# wrong process). The sleep and the cp are one shell, so exec that shell.
( exec sh -c 'sleep 3; cp -f "$0" "$1/m1063-d.md"' "$tmp/m1063-d-src.md" "$drive/inbox" ) &
_m1063_drop=$!
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0024 < /dev/null \
    > "$tmp/r24.out" 2> "$tmp/r24.err"
rc24=$?
mm_stop
wait "$_m1063_drop" 2>/dev/null
_p24="$drive/runs/0024/prompt.md"
# 0024's view of D, captured BEFORE 0025 runs (0025 delivers D and moves it,
# so the inbox state must be snapshotted here).
_d_inbox_after24=$([ -f "$drive/inbox/m1063-d.md" ] && echo yes || echo no)
# 0025: the still-pending D note plus two ordering notes.
printf 'M1063 order A body\n' > "$drive/inbox/a-z-order.md"
printf 'M1063 order Z body\n' > "$drive/inbox/z-a-order.md"
start_mock "$tmp/happy.mm" "$tmp/mm_h25" m
with_deadline 60 "$HBIN" run --config "$hcfg" --tenant lead --run 0025 < /dev/null \
    > "$tmp/r25.out" 2> "$tmp/r25.err"
rc25=$?
mm_stop
_p25="$drive/runs/0025/prompt.md"
# Note D's body, one needle: the drop at t+3s copied $tmp/m1063-d-src.md.
_d_body_present24=$($G -qF "M1063 note D dropped during the run" "$_p24" 2>/dev/null && echo yes || echo no)
_a_body_present24=$($G -qF "M1063 note A body line" "$_p24" 2>/dev/null && echo yes || echo no)
_d_body_present25=$($G -qF "M1063 note D dropped during the run" "$_p25" 2>/dev/null && echo yes || echo no)
_ln_az=$($G -n "^### a-z-order$" "$_p25" 2>/dev/null | head -1 | cut -d: -f1)
_ln_za=$($G -n "^### z-a-order$" "$_p25" 2>/dev/null | head -1 | cut -d: -f1)
_m1063_ok=yes
[ "$rc24" -eq 0 ] || _m1063_ok=no
[ "$_a_body_present24" = yes ] || _m1063_ok=no
[ "$_d_body_present24" = no ] || _m1063_ok=no
[ "$_d_inbox_after24" = yes ] || _m1063_ok=no
[ ! -e "$drive/runs/0024/notes/m1063-d.md" ] || _m1063_ok=no
[ -f "$drive/runs/0024/notes/m1063-a.md" ] || _m1063_ok=no
[ "$rc25" -eq 0 ] || _m1063_ok=no
[ "$_d_body_present25" = yes ] || _m1063_ok=no
[ -n "$_ln_az" ] || _m1063_ok=no
[ -n "$_ln_za" ] || _m1063_ok=no
if [ -n "$_ln_az" ] && [ -n "$_ln_za" ] && [ "$_ln_az" -gt "$_ln_za" ]; then
    _m1063_ok=no
fi
[ -f "$drive/runs/0025/notes/m1063-d.md" ] || _m1063_ok=no
[ -f "$drive/runs/0025/notes/a-z-order.md" ] || _m1063_ok=no
[ -f "$drive/runs/0025/notes/z-a-order.md" ] || _m1063_ok=no
if [ "$_m1063_ok" != yes ]; then
    t_fail "18: M1063 -- a note dropped into the inbox AFTER 0024's prompt was \
assembled must stay in the inbox (not be filed under 0024, not be read by \
0024) and must be delivered to the NEXT run (0025), whose notes reach the \
prompt NAME-sorted -- rc24=$rc24 rc25=$rc25; A in 0024 prompt=$_a_body_present24 \
[want yes]; D in 0024 prompt=$_d_body_present24 [want no]; D still in inbox \
after 0024=$_d_inbox_after24 [want yes]; D filed \
in 0024 notes=$([ -e "$drive/runs/0024/notes/m1063-d.md" ] && echo yes || echo no) \
[want no]; A filed in 0024 notes=$([ -f "$drive/runs/0024/notes/m1063-a.md" ] \
&& echo yes || echo no) [want yes]; D in 0025 prompt=$_d_body_present25 [want \
yes]; 0025 header lines a-z=$_ln_az z-a=$_ln_za (a-z must precede z-a); 0025 \
notes: D=$([ -f "$drive/runs/0025/notes/m1063-d.md" ] && echo yes || echo no) \
a-z=$([ -f "$drive/runs/0025/notes/a-z-order.md" ] && echo yes || echo no) \
z-a=$([ -f "$drive/runs/0025/notes/z-a-order.md" ] && echo yes || echo no)"
else
    t_ok "18: M1063 -- moved == read into the prompt: 0024's prompt carries the pre-placed note A (filed in 0024/notes/) but NOT the note D dropped at t+3s; D is STILL in inbox/ after 0024 and its body reaches run 0025's prompt, where the ordering notes a-z-order precede z-a-order (name-sorted at assembly) and all three are filed in 0025/notes/"
fi

t_done
