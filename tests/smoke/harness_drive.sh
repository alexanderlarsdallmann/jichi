#!/bin/sh
# smoke: `jichi-harness drive` -- the C89 port of the drive's loop.sh (the
# jichi-harness drive of 2026-10-23, batch H5, M1059). Mode A (the plan 7.1):
# the harness and the agent run as the same user, so the port drops the
# container half (the one `podman kill --signal INT` becomes a SIGINT to the
# run's process group, CHANGED 1) and keeps the loop, the snapshot and the
# three-quick-failure pause, re-expressed in C. The driver runs a REAL jichi
# against the REAL mockmodel and REAL git repositories under /tmp -- no fake
# pinned binary for the loop cells: a fake jichi that ignores SIGINT would let
# the deadline-SIGINT cell (D5) pass vacuously, and only the tree's real jichi
# (src/main.c on_sigint, exit 130, journal written) satisfies D5's assert that
# the journal is NON-EMPTY and that no jichi child survives the loop.
#
# M1059 (H5). Design note:
#   docs/internal/drive/2026-10-23-harness/notes/M1059.md
# Ported reference (read line by line; its comments name the incidents):
#   docs/internal/drive/2026-10-23-harness/harness/loop.sh
# The SECOND reference stands beside it as the Mode A stand-in (its done-when
# comparison is M1060's, not this port's body):
#   docs/internal/drive/2026-10-23-harness/harness/rehearsal/rehearse.sh
#   -- it cannot RUN on this host (podman is absent, measured), so the
#   side-by-side is a behaviour listing; this driver IS that stand-in for
#   the loop-facing checks (one run, --tick 3 the scaled ZD_TICK=30, STOP).
# Output-shape precedent: tests/smoke/harness_run.sh (the $G for every grep,
# the argv-dump wrapper idiom, the canary, the git-config neutralisation) and
# tests/smoke/harness_init.sh (the rehearsal init invocation this driver
# reuses: the fixture layout is REAL init output).
#
# THE RED THIS IS HELD BY (SV:1818.1 style -- the note's words, quoted): on
# the unfixed tree `jichi-harness drive --config … --tenant lead --once`
# prints `jichi-harness: drive: not built yet (see docs/HARNESS.md)` and exits
# 2 -- the dispatch's H0 refusal, the same class the run driver's R0 holds;
# D1-D6e and D8 all sit on that refusal, D7 on its rc accounting. MEASURED on
# this tree, the FIRST drive invocation D1 runs dies one step earlier:
# `jichi-harness: unknown option '--tenant'` (exit 2) -- the parser does not
# yet accept drive's flags, so the not-built dispatch catch-all it would reach
# is never reached; D1's fail line quotes whichever it gets. Either way D1 is
# red for the same reason (no `loop start` is ever logged), and the port's
# parser hunk moves D1's red onto the not-built line. NO assert below is
# weakened to pass now: the log lines are the note's verbatim, the counts are
# the note's counts, the file shapes are the note's shapes.
#
# WHAT IS CHECKED (the twelve cells, M1059's label convention -- D1-D8 plus
# D6d, D6e, D3f and D1f):
#   1. D1 the counter and the two runs: planted runs/0008/ read as EIGHT in
#      base 10 (the note's own incident: 0008 is octal), the drive's one run
#      is 0009; exit 0; the log's start/end/loop lines; the start line's
#      ', deadline <ISO>' (loop.sh:102's shape) and at least one line stamped
#      with the [+-][0-9]{4} offset (loop.sh:45's date '+%F %T %z'; the
#      two log-shape teeth, SV:0317.2 -- a bare 'loop start (pid N)' or a
#      stamp that dropped the offset is red); meta.txt's rc=0 and
#      team_head_after=; the stream non-empty (the mock answered).
#   2. D2 no caps on the run, the run's child got the key (rides D1's
#      fixture): the argv dump has no --deadline/--budget-tokens and has
#      --max-tool-calls; the canary is in NO argv dump, NO harness.log, NO
#      file under recs/ -- and IS in the mock's captured request.
#   3. D3 the save_team push reached the stand-in: standin.git holds
#      refs/heads/harness-plan/main equal to the team workdir's HEAD (the
#      run committed nothing), and the log carries the mirror push report
#      lines (M1056's say output, captured by the loop, CHANGED 9).
#   4. D3f the teamPush=false switch (behaviour 3, SV:2037.1): a fresh
#      layout whose config carries "teamPush": false (a REAL JSON bool, the
#      lenient reader); one --once drive, a fast happy run. The stand-in
#      LACKS refs/heads/harness-plan/main (the fetch happened, the push did
#      not -- the switch's teeth), the log has `team push off: save_team
#      fetched only`, and the run's record is otherwise complete (rc=0; the
#      snapshot's final commit landed in recs -- the switch is the TEAM push
#      only, never the fetch or the snapshot).
#   5. D4 the STOP file ends the loop after the current run: the driver
#      (itself the operator) touches dir/STOP 1 s after `run 0009: start`
#      appears in the log; the run completes, `STOP file present: loop
#      ends`, `loop end`, exit 0, NO `run 0010: start`.
#   6. D5 the deadline SIGINT (behaviours 8/9, CHANGED 1): drive.end = NOW
#      + 6 s, no --tick override (the 10-s poll), a mock reply delayed 20000
#      ms so the run is alive past the deadline AND past the first 10-s poll
#      (an 8000 ms reply ends ~8.2 s wall, before the poll, so no SIGINT
#      lands and the cell is red on a fast host -- SV:2157.1); `run 0009:
#      deadline, SIGINT to the lead` (loop.sh:126's shape, SV:0317.2),
#      `run 0009: end rc=130`, meta.txt's three lines (rc=130, end=,
#      team_head_after= -- the SV:1838.2 split), journal NON-EMPTY, no run
#      0010, exit 0, and ps shows no jichi or `harness run` child of the
#      fixture after the loop.
#   7. D6 the snapshot's XOR, the two kinds, the stale-twin removal:
#      --once, --tick 3, a mock reply delayed 12000 ms (the partial fires
#      at the first 10-s poll), a background plant chain (ONE sh -c, driven
#      by the log) that at +2 s plants notes/n.txt (10 bytes) and big.bin
#      (1000001 bytes) and, when `periodic push done` appears, shrinks
#      big.bin to 500 bytes: recs holds stream/meta/prompt as PLAIN (no .gz
#      twin), notes/n.txt.gz (the subdir is always gzipped, no plain twin),
#      big.bin 500 bytes PLAIN (the final's rm -f of the stale .gz twin),
#      the two commits `records: run 0009 (partial)` AND `records: run
#      0009 (final)`, and the periodic line between start and end.
#   8. D6d the symlink is skipped (CHANGED 7, SV:1838.3): a fresh layout,
#      a fast mock, the plant chain creates runs/0009/keylink -- a SYMLINK
#      to the model-key file OUTSIDE runs/ -- at +2 s; recs has NO keylink
#      and NO keylink.gz, the canary is in NO file under recs/, and the log
#      has `skipped the symlink keylink`.
#   9. D6e the recs git child is neutralised (CHANGED 10, SV:1838.4): a
#      fresh layout, a fast mock, an EXECUTABLE recs/.git/hooks/pre-commit
#      that touches $MARKER planted BEFORE the drive; after it, $MARKER
#      does not exist and `records: run 0009 (final)` is still in
#      `git -C recs log --oneline` (the commit landed; the M2 set kills the
#      hooks, not the commit).
#   10. D7 the three quick failures and the pause (behaviour 12, CHANGED 8):
#      a fresh layout, drive.end = NOW + 50 s, NO --once, the key file
#      REMOVED so the run refuses (rc=2, ~0 s each); runs 0009/0010/0011
#      end rc=2, EXACTLY ONE `three quick failures in a row: waiting 10
#      minutes`, the sliced pause's slice check sees the deadline,
#      `deadline reached: loop ends`, exit 0, NO `run 0012: start` -- all
#      inside with_deadline 80 (CHANGED 8's teeth: the reference's SOLID
#      sleep 600 is killed, a wall-time red).
#   11. D8 the refusal classes: (a) `drive --config f` (no --tenant) ->
#      exit 2, the refusal named; (b) `--tenant nobody --once` -> exit 2,
#      the run's refusal `no tenant named nobody` and NO new runs/ entry
#      (the counter is read after the refusals); (c) `--tick 0` -> exit 2,
#      named; (d) a config whose drive.end is `not a date` -> exit 1,
#      `the end time does not parse` BEFORE `loop start` in the log (the
#      log must not have a start line).
#   12. D1f the mirror-fetch report is stamped in harness.log and OFF the
#      loop's stdout (M1064, card 93, SV:0904.3; rides D1's fixture, check
#      1's $tmp/d1.out and $D1/harness.log, no new run): the loop's stdout
#      carries NO 'mirror fetch:' line (site A's pre-run fetch is captured
#      by a fork-child, never printed by the loop), and harness.log carries
#      at least TWO STAMPED 'mirror fetch:' lines (the behaviour-14
#      'YYYY-MM-DDTHH:MM:SS' prefix at column 0: one per fetch site, site A
#      pre-run and site B in save_team, in D1's single --once run; an
#      unstamped line sits at column 0 WITHOUT the stamp, so the column-0
#      stamp is the discriminator). D6 (check 7) is the load-bearing tooth
#      for the capture approach itself: the loop's OWN fd 1/fd 2 must never
#      be dup2'd (attempt 1's dup2-the-loop-stdout poisoned the snapshot's
#      gzip child's execvp, EFAULT -- run 0085's root cause).
#
# SECRETS: the model key is a RANDOM canary the driver draws (od -An -N12
# -tx1 /dev/urandom, the M1053 idiom) -- never a real secret; the git token
# is a NAMED canary string. The tenant's jichi config carries the key by ENV
# NAME (apiKeyEnv, the harness_run.sh convention -- a literal apiKey would
# win over the env and make D2's "the model got the key" control vacuous).
# Every `jichi-harness drive` invocation takes `< /dev/null`: the run binary
# itself passes devnull to jichi (fd_stdin = JC_SMOKE_... DEVNULL), and the
# driver keeps the loop's stdin closed to be safe.

. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"
# /usr/bin/grep only with its fallback (portability_lint check 36, V2f).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# The loader resolves each top-level reference from its JICHI_HARNESS_* env
# setting FIRST (env wins over the config's own value). An ambient setting
# would defeat the fixtures below, so clear all three.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

t_plan 12
smoke_home
tmp=$(smoke_tmp)

# Neutralise the operator's ambient git config for the FIXTURES: the local
# repos init creates must take their identity from the -c flags, not from a
# machine-level gitconfig.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0

if [ ! -x "$HBIN" ]; then
    echo "not ok - the harness binary $HBIN is not built (run make jichi-harness first)" >&2
    exit 1
fi

# A FAKE pinned jichi, init ONLY (init's layout needs a --version answer);
# the drive cells replace the pin with the real $BIN (or an argv-dump wrapper
# around it) -- the loop cells never run the fake.
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$tmp/fjichi"
chmod 755 "$tmp/fjichi"

# The mockmodel scripts: a fast happy reply (no delay), and two long replies
# (D5's 20 s reply OUTLIVES the first 10-s poll by a wide margin -- SV:2157.1:
# a real run against an 8000 ms reply ends ~8.2 s wall, BEFORE the 10-s poll,
# so no SIGINT lands and D5 is red on a fast host; 20 s puts the run's end at
# ~+20 s, safely after the first poll at ~+10 s, with the +6-s deadline past,
# so the poll's SIGINT lands deterministically mid-run. D6's 12 s keeps the run
# alive through the first 10-s poll so the partial snapshot fires).
cat > "$tmp/fast.mm" <<'MMEOF'
wire openai
rule
  text Done: the drive run finished its one turn.
MMEOF
cat > "$tmp/delay20.mm" <<'MMEOF'
wire openai
rule
  delay 20000
  text Done: the drive run finished its one turn, late.
MMEOF
cat > "$tmp/delay12.mm" <<'MMEOF'
wire openai
rule
  delay 12000
  text Done: the drive run finished its one turn, late.
MMEOF

# mklayout NAME [FAR|ENDSECS] -- one FRESH rehearsal layout per cell that
# needs a clean runs/ (the note's preamble: the cells' timelines do not
# share a runs/ dir). Returns 0 on success. The layout (REAL init output):
#   NAME/team       origin /nonexistent-the-harness-pushes, on harness-plan/main
#   NAME/mirror.git bare, origin = NAME/standin.git (re-pointed below)
#   NAME/recs       orphan, origin = NAME/standin.git
#   NAME/standin.git, NAME/runs, NAME/inbox, NAME/bin, NAME/home
# The RE-POINT (the driver's job, the note's wording "the driver re-points
# them where a cell needs a real sink"): team/mirror/recs are re-pointed at
# the LOCAL stand-in -- the team's workdir is the mirror fetch's SOURCE, and
# the mirror's origin is where `mirror push` (the save_team push, M1056)
# lands; with the stand-in as origin the D3 assertion (the team push landed
# in standin.git) holds. The STORED config copy (mirror fetch's check 3) is
# updated with the same bytes, or every fetch is refused. The far end is the
# note's own default shape (drive.env's ZD_END_DEFAULT); a numeric end is
# computed NOW+<n> s in UTC (the port's documented bare form).
mklayout() {
    _mn="$1"; _mend="${2:-FAR}"
    _mcd="$tmp/$1"
    if [ "$_mend" = FAR ]; then _mendv="2027-01-31 18:00:00 +0100"; else _mendv=$(date -u -d "@$(($(date +%s) + $_mend))" '+%Y-%m-%d %H:%M:%S'); fi
    _mkc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_mkc" ] || _mkc="keycanary$$_"
    printf '%s\n' "$_mkc" > "$_mcd.key"; chmod 600 "$_mcd.key"
    printf 'harness-drive-token-canary-%s\n' "$_mn" > "$_mcd.tok"; chmod 600 "$_mcd.tok"
    cat > "$_mcd.cfg" <<EOF
{"dir":"$_mcd",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com","end":"$_mendv","tick":1200},
"modelKeyFile":"$_mcd.key","gitTokenFile":"$_mcd.tok",
"tenants":[{"name":"lead","home":"$_mcd/home","workdir":"$_mcd/team","jichiConfig":"$_mcd/jicfg","promptFile":"$_mcd.prompt.md","bin":"$_mcd/bin/jichi","branchPrefix":"harness-plan"}]}
EOF
    printf 'You are the lead of the test drive; do one turn and stop.\n' > "$_mcd.prompt.md"
    "$HBIN" init --config "$_mcd.cfg" rehearsal "$_mcd" \
        --from "$src" --jichi-bin "$tmp/fjichi" < /dev/null \
        > "$_mcd.init" 2>&1 || return 1
    # The tenant's jichi config lives at $_mcd/jicfg (INSIDE the drive dir,
    # which init just created): the SAME path setjicfg writes and the
    # tenant's jichiConfig references -- the one-path idiom harness_run.sh
    # uses. (The fixture bug: mklayout pointed jichiConfig at the SIBLING
    # dot file $_mcd.jicfg while setjicfg wrote $_mcd/jicfg, so the run
    # child read the {} stub and every model turn was 'no model
    # configured'.) A {} placeholder; setjicfg overwrites it once the mock's
    # port is known.
    printf '{}' > "$_mcd/jicfg" || return 1
    # The stored config copy (mirror fetch's check 3) must carry the SAME bytes
    # as the live mirror.git/config AFTER the re-point, so set the live URL and
    # re-copy the live config byte-for-byte (no sed: the `$#` in a sed delimiter
    # collides with the shell's arg-count expansion).
    git -C "$_mcd/mirror.git" remote set-url origin "$_mcd/standin.git" || return 1
    cp "$_mcd/mirror.git/config" "$_mcd/mirror.git-config.harness-copy" || return 1
    git -C "$_mcd/team" remote set-url origin "$_mcd/standin.git" || return 1
    git -C "$_mcd/recs" remote set-url origin "$_mcd/standin.git" || return 1
    return 0
}

# pin REALNAME -- install the tenant's pinned bin/jichi (copy of the tree's
# real jichi), or, for D2, the argv-dump wrapper around it (the M1055/M1057
# idiom): dump $* and HOME to a file, then exec the real binary.
pin_real() { cp "$BIN" "$1/bin/jichi"; chmod 755 "$1/bin/jichi"; }
pin_dump() {
    printf '%s\n' '#!/bin/sh' \
        "printf '%s\n' \"\$*\" > \"$tmp/d2.argv\"" \
        "printf 'HOME=%s\n' \"\$HOME\" >> \"$tmp/d2.argv\"" \
        "exec \"$BIN\" \"\$@\"" > "$1/bin/jichi"
    chmod 755 "$1/bin/jichi"
}

# setjicfg DIR -- rewrite the tenant's jichi config against the CURRENT mock
# port (the mock was already started): one openai-wire model "m", the key by
# ENV NAME (JICHI_API_KEY), no literal apiKey (it would win over the env and
# vacuate D2's key control), toolProfile none (the one text reply ends the
# turn with no tool work), maxRetries 0, lowResource false pinned.
setjicfg() {
    cat > "$1/jicfg" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKeyEnv":"JICHI_API_KEY","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"none","lowResource":false,"maxRetries":0}
EOF
}

# A local source repo with a plan branch (init clones --branch harness-plan/plan).
src="$tmp/src"
git init -q "$src"
git -C "$src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: the plan branch's base"
git -C "$src" branch harness-plan/plan

# smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once.
"$BIN" --version > "$tmp/binv" 2>/dev/null

# ============================================================================
# 1: D1 the counter and the two runs (the PRIMARY red)
# ============================================================================
mklayout d1 || { t_fail "1: D1 the layout init failed: $(head -n 3 "$tmp/d1.init")"; t_done; }
D1="$tmp/d1"
mkdir -p "$D1/runs/0008"   # the note's plant: the counter must read EIGHT (base 10)
pin_dump "$D1"
mm_start "$tmp/fast.mm" "$tmp/mm_d1" 8 || { t_fail "1: D1 the mockmodel did not start"; t_done; }
setjicfg "$D1"
with_deadline 60 "$HBIN" --config "$D1.cfg" drive --tenant lead --once --tick 3 \
    < /dev/null > "$tmp/d1.out" 2> "$tmp/d1.err"
rc_d1=$?
mm_stop
_d1log="$D1/harness.log"
_d1meta="$D1/runs/0009/meta.txt"
# The two LOG-SHAPE teeth (SV:0317.2), extracted into _d1cls (0=ok,
# 1=the start line lacks ', deadline <ISO>' -- loop.sh:102's shape; a port
# that logs a bare 'loop start (pid N)' is red here, 2=no log line carries
# the stamp's [+-][0-9][0-9][0-9][0-9] offset -- loop.sh:45's date '+%F %T %z'; a port
# that drops the offset to a bare %F %T is red here) so the fail message
# can name which shape was dropped.
_d1start=$($G -F "loop start (pid" "$_d1log" 2>/dev/null | sed -n 1p)
_d1cls=0
{ [ -n "$_d1start" ] && printf '%s\n' "$_d1start" | $G -qF ", deadline " 2>/dev/null; } || _d1cls=1
$G -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} [+-][0-9]{4} ' "$_d1log" 2>/dev/null || _d1cls=2
if [ "$rc_d1" -ne 0 ] || [ ! -d "$D1/runs/0008" ] || [ ! -d "$D1/runs/0009" ] || \
   [ ! -f "$_d1log" ] || ! $G -qF "loop start (pid" "$_d1log" || \
   ! $G -qF "run 0009: start" "$_d1log" || \
   ! $G -qF "run 0009: end rc=0 after" "$_d1log" || \
   ! $G -qF "loop end" "$_d1log" || \
   [ ! -f "$_d1meta" ] || ! $G -q "^rc=0" "$_d1meta" 2>/dev/null || \
   ! $G -q "team_head_after=" "$_d1meta" 2>/dev/null || \
   [ ! -s "$D1/runs/0009/stream.jsonl" ] || [ "$_d1cls" -ne 0 ]; then
    # THE RED. Quoted: whichever refusal the current tree gives D1's first
    # drive invocation (on THIS tree the parser says `unknown option
    # '--tenant'`; the note's words are the dispatch catch-all `jichi-harness:
    # drive: not built yet (see docs/HARNESS.md)` -- the port's parser hunk
    # moves D1's red onto that line).
    t_fail "1: D1 the counter and the two runs (log-shape class $_d1cls: 1=the start line lacks ', deadline <ISO>' (loop.sh:102's shape) 2=no log line carries the stamp's [+-][0-9][0-9][0-9][0-9] offset (loop.sh:45's date '+%F %T %z')) -- the first drive invocation did not run the happy loop (rc=$rc_d1; its refusal: $(head -n 1 "$tmp/d1.err" 2>/dev/null)); the note's red for this cell is the not-built refusal \`jichi-harness: drive: not built yet (see docs/HARNESS.md)\`; want exit 0, runs/ holding 0008 AND 0009 (the counter read 8 in base 10), the log's 'loop start (pid'/'run 0009: start'/'run 0009: end rc=0 after'/'loop end', the start line's ', deadline <ISO>' and a stamped line with the [+-][0-9][0-9][0-9][0-9] offset (SV:0317.2), meta rc=0 + team_head_after=, a non-empty stream (log present: $([ -f "$_d1log" ] && echo yes || echo no); log head: $(head -n 2 "$_d1log" 2>/dev/null))"
else
    t_ok "1: D1 the planted runs/0008 is read as EIGHT (base 10, not octal) and the drive's one run is 0009: exit 0, the log's start/end/loop lines -- including the start line's ', deadline <ISO>' (loop.sh:102) and at least one line stamped with the [+-][0-9]{4} offset (loop.sh:45; SV:0317.2) -- meta rc=0 + team_head_after=, the stream non-empty (the mock answered)"
fi

# ============================================================================
# 2: D2 no caps on the run, the run's child got the key (rides D1's fixture)
# ============================================================================
_kc_d1=$(cat "$D1.key")
_d2leak=0
if [ -f "$tmp/d2.argv" ] && $G -qF -- "--deadline" "$tmp/d2.argv"; then _d2leak=1; fi
if [ -f "$tmp/d2.argv" ] && $G -qF -- "--budget-tokens" "$tmp/d2.argv"; then _d2leak=2; fi
_d2caps=0
[ -f "$tmp/d2.argv" ] || _d2caps=1
[ "$_d2leak" -eq 0 ] || _d2caps=1
$G -qF -- "--max-tool-calls" "$tmp/d2.argv" 2>/dev/null || _d2caps=1
$G -qF "$_kc_d1" "$tmp/d2.argv" 2>/dev/null && _d2leak=3
$G -qF "$_kc_d1" "$D1/harness.log" 2>/dev/null && _d2leak=4
for _d2f in $(find "$D1/recs" -type f 2>/dev/null); do
    $G -qF "$_kc_d1" "$_d2f" 2>/dev/null && _d2leak=5
done
_d2got=0
for _d2r in "$tmp/mm_d1"/req.*; do
    [ -f "$_d2r" ] && { $G -qF "$_kc_d1" "$_d2r" && _d2got=1; }
done
if [ "$_d2caps" -ne 0 ]; then
    t_fail "2: D2 the spawn argv is wrong or missing (caps leak: $_d2leak; missing dump/--max-tool-calls: yes) -- want NO --deadline, NO --budget-tokens, --max-tool-calls PRESENT (caps off, fences on, the run's own cell)"
elif [ "$_d2leak" -ne 0 ]; then
    t_fail "2: D2 the model key canary leaked (class $_d2leak: 3=argv dump 4=harness.log 5=a file under recs/) -- the key never reaches argv, a log or the records"
elif [ "$_d2got" -ne 1 ]; then
    t_fail "2: D2 the model did NOT receive the key from the FILE (no canary in the mock's captured request) -- the env delivery did not reach it, the loop broke it"
else
    t_ok "2: D2 the run's argv carries no --deadline/--budget-tokens and has --max-tool-calls; the canary is in NO argv dump, NO harness.log, NO file under recs/ (find + grep) -- and IS in the mock's captured request (env delivery worked, the loop did not break it)"
fi

# ============================================================================
# 3: D3 the save_team push reached the stand-in (rides D1's fixture)
# ============================================================================
_d3want=$(git -C "$D1/team" rev-parse --short HEAD 2>/dev/null)
_d3got=$(git -C "$D1/standin.git" rev-parse --short "refs/heads/harness-plan/main" 2>/dev/null)
_d3say=0
$G -qF "mirror push:" "$D1/harness.log" 2>/dev/null || _d3say=1
$G -qF "pushed harness-plan/main and the harness-plan/work refset" "$D1/harness.log" 2>/dev/null || _d3say=2
if [ -z "$_d3want" ] || [ "$_d3got" != "$_d3want" ]; then
    t_fail "3: D3 the stand-in lacks refs/heads/harness-plan/main or it does not equal the team workdir's HEAD (stand-in '$_d3got' vs team '$_d3want') -- the run committed nothing, so the push carried the drive's INITIAL head"
elif [ "$_d3say" -ne 0 ]; then
    t_fail "3: D3 the log lacks the mirror push report lines (M1056's say output, captured by the loop per CHANGED 9; missing class $_d3say)"
else
    t_ok "3: D3 after the drive, standin.git holds refs/heads/harness-plan/main equal to the team workdir's HEAD (the run committed nothing) and the log carries the mirror push report lines (M1056's say output, captured by the loop per CHANGED 9)"
fi

# ============================================================================
# 4: D3f the teamPush=false switch (behaviour 3, SV:2037.1)
# ============================================================================
# A FRESH layout whose config carries "teamPush": false (a REAL JSON bool, the
# lenient reader's job -- the driver does not spell "true"/"off"); one --once
# happy drive. The switch sits in save_team (both call sites, the end-of-run
# one is the teeth here): when FALSE the loop still does `mirror fetch` but
# SKIPS `mirror push` and logs `team push off: save_team fetched only`. So the
# stand-in LACKS refs/heads/harness-plan/main (the push never ran -- the cell's
# primary assertion), the log carries the off-line, and the run's record is
# otherwise COMPLETE: rc=0, the snapshot's final commit landed in recs (the
# switch is the TEAM push only, never the fetch or the snapshot). (init pushes
# refs/heads/harness-plan/PLAN into the stand-in, so the ABSENCE of
# .../main is the push's teeth, not a layout gap.)
mklayout d3f || { t_fail "4: D3f the layout init failed: $(head -n 3 "$tmp/d3f.init")"; t_done; }
D3f="$tmp/d3f"
mkdir -p "$D3f/runs/0008"
pin_real "$D3f"
# "teamPush": false into the drive block (the layout wrote the drive object
# inline; sed the literal substring, no colons in the delimiter position).
sed 's/"tick":1200/"tick":1200,"teamPush":false/' "$D3f.cfg" > "$D3f.cfg.new" && mv "$D3f.cfg.new" "$D3f.cfg"
$G -qF '"teamPush":false' "$D3f.cfg" 2>/dev/null || { t_fail "4: D3f the config patch did not carry teamPush:false (the cell is vacuous)"; t_done; }
mm_start "$tmp/fast.mm" "$tmp/mm_d3f" 8 || { t_fail "4: D3f the mockmodel did not start"; t_done; }
setjicfg "$D3f"
with_deadline 60 "$HBIN" --config "$D3f.cfg" drive --tenant lead --once \
    < /dev/null > "$tmp/d3f.out" 2> "$tmp/d3f.err"
rc_d3f=$?
mm_stop
_d3flog="$D3f/harness.log"
_d3frecs="$D3f/recs"
_d3fmeta="$D3f/runs/0009/meta.txt"
_d3fmain=$(git -C "$D3f/standin.git" rev-parse --short "refs/heads/harness-plan/main" 2>/dev/null)
_d3fplan=$(git -C "$D3f/standin.git" rev-parse --short "refs/heads/harness-plan/plan" 2>/dev/null)
_d3fbad=0
[ -n "$_d3fmain" ] && _d3fbad=1      # the push ran despite teamPush=false (the teeth)
! $G -qF "team push off: save_team fetched only" "$_d3flog" 2>/dev/null && _d3fbad=2
[ -z "$_d3fplan" ] && _d3fbad=3      # init's plan push is missing (layout, not port)
! $G -qF "run 0009: end rc=0 after" "$_d3flog" 2>/dev/null && _d3fbad=4
[ ! -f "$_d3fmeta" ] || ! $G -q "^rc=0" "$_d3fmeta" 2>/dev/null && _d3fbad=5
git -C "$_d3frecs" log --oneline 2>/dev/null | $G -qF "records: run 0009 (final)" || _d3fbad=6
if [ "$rc_d3f" -ne 0 ] || [ "$_d3fbad" -ne 0 ]; then
    t_fail "4: D3f the teamPush=false switch (class $_d3fbad: 1=the stand-in HAS refs/heads/harness-plan/main -- the mirror push ran despite teamPush:false (the switch's teeth) 2=no 'team push off: save_team fetched only' log line 3=init's refs/heads/harness-plan/plan missing (a layout gap, the cell is not witnessable) 4=no 'run 0009: end rc=0 after' (the run did not complete) 5=meta missing rc=0 (the record is incomplete) 6=the snapshot's 'records: run 0009 (final)' commit did not land in recs; rc=$rc_d3f; main=[$_d3fmain] plan=[$_d3fplan]; log: $(head -n 3 "$_d3flog" 2>/dev/null))"
else
    t_ok "4: D3f with drive.teamPush=false the save_team still fetches but SKIPS the push: the stand-in LACKS refs/heads/harness-plan/main (init's .../plan is still there, so the absence is the push's), the log has 'team push off: save_team fetched only', and the run's record is otherwise complete (rc=0; meta rc=0; the snapshot's 'records: run 0009 (final)' commit landed in recs) -- the switch is the TEAM push only, never the fetch or the snapshot (behaviour 3, SV:2037.1)"
fi

# ============================================================================
# 5: D4 the STOP file ends the loop after the current run
# ============================================================================
mklayout d4 || { t_fail "5: D4 the layout init failed: $(head -n 3 "$tmp/d4.init")"; t_done; }
D4="$tmp/d4"
mkdir -p "$D4/runs/0008"
pin_real "$D4"
mm_start "$tmp/fast.mm" "$tmp/mm_d4" 8 || { t_fail "5: D4 the mockmodel did not start"; t_done; }
setjicfg "$D4"
# No --once: the driver IS the operator. It polls the log; 1 s after
# `run 0009: start` appears it touches dir/STOP (no second racing process).
with_deadline 120 "$HBIN" --config "$D4.cfg" drive --tenant lead --tick 3 \
    < /dev/null > "$tmp/d4.out" 2> "$tmp/d4.err" &
_d4pid=$!
_d4i=0
while ! $G -qF "run 0009: start" "$D4/harness.log" 2>/dev/null; do
    kill -0 "$_d4pid" 2>/dev/null || break
    _d4i=$((_d4i + 1)); [ "$_d4i" -gt 30 ] && break
    sleep 1
done
if $G -qF "run 0009: start" "$D4/harness.log" 2>/dev/null; then
    sleep 1
    touch "$D4/STOP"
fi
wait "$_d4pid"
rc_d4=$?
mm_stop
_d4log="$D4/harness.log"
_d4end=0
$G -qF "run 0009: end rc=0 after" "$_d4log" 2>/dev/null || _d4end=1
$G -qF "STOP file present: loop ends" "$_d4log" 2>/dev/null || _d4end=2
$G -qF "loop end" "$_d4log" 2>/dev/null || _d4end=3
$G -qF "run 0010: start" "$_d4log" 2>/dev/null && _d4end=4
if [ "$rc_d4" -ne 0 ] || [ "$_d4end" -ne 0 ]; then
    t_fail "5: D4 the STOP file did not end the loop after the current run (rc=$rc_d4, class $_d4end: 1=no end rc=0 2=no STOP line 3=no loop end 4=run 0010 started anyway; the driver touched STOP 1 s after 'run 0009: start' appeared; log head: $(head -n 2 "$_d4log" 2>/dev/null))"
else
    t_ok "5: D4 a STOP touched 1 s after 'run 0009: start' ends the loop after the run completes: 'run 0009: end rc=0 after', 'STOP file present: loop ends', 'loop end', exit 0, and NO 'run 0010: start'"
fi

# ============================================================================
# 6: D5 the deadline SIGINT (behaviours 8/9, CHANGED 1) -- wrap in 60
# ============================================================================
# drive.end = NOW + 6 s (computed at layout), NO --tick override (the poll is
# the reference's 10-s granularity), a mock reply delayed 20000 ms (SV:2157.1:
# an 8 s reply ends ~8.2 s wall, BEFORE the 10-s poll, so no SIGINT lands and
# the cell is red on a fast host; 20 s keeps the run ALIVE past the deadline
# and past the first poll): the first poll at ~+10 sends the group SIGINT
# mid-delay, jichi exits 130 with its journal, the run SURVIVES (it never
# re-raises, SV:1838.2) and writes its meta, the loop breaks at the deadline.
# The wall time is MEASURED on the fixed tree at 30 s (+0 run start, the +6-s
# deadline, the +10-s first poll that SIGINTs the run, the run's rc=130 end +
# save_team + final snapshot, the +10-s end-of-iteration spacing, then the
# deadline break); with_deadline 40 is set FROM that measured 30 s with a 10-s
# margin, so a correct port finishes before it and the deadline never fires
# its own cell.
mklayout d5 6 || { t_fail "6: D5 the layout init failed: $(head -n 3 "$tmp/d5.init")"; t_done; }
D5="$tmp/d5"
mkdir -p "$D5/runs/0008"
pin_real "$D5"
mm_start "$tmp/delay20.mm" "$tmp/mm_d5" 8 || { t_fail "6: D5 the mockmodel did not start"; t_done; }
setjicfg "$D5"
with_deadline 40 "$HBIN" --config "$D5.cfg" drive --tenant lead \
    < /dev/null > "$tmp/d5.out" 2> "$tmp/d5.err"
rc_d5=$?
mm_stop
_d5log="$D5/harness.log"
_d5meta="$D5/runs/0009/meta.txt"
_d5ps=$(ps -ww -e -o args= 2>/dev/null | $G -F "$D5" | $G -v "$$" || true)
_d5left=0
printf '%s' "$_d5ps" | $G -q "jichi-harness run" && _d5left=1
printf '%s' "$_d5ps" | $G -qF "$ROOT/jichi" && _d5left=2
_d5bad=0
$G -qF "run 0009: end rc=130" "$_d5log" 2>/dev/null || _d5bad=1
[ -f "$_d5meta" ] || _d5bad=2
$G -q "^rc=130" "$_d5meta" 2>/dev/null || _d5bad=2
$G -q "^end=" "$_d5meta" 2>/dev/null || _d5bad=2
$G -q "team_head_after=" "$_d5meta" 2>/dev/null || _d5bad=2
[ -s "$D5/runs/0009/journal.jsonl" ] || _d5bad=3
$G -qF "run 0010: start" "$_d5log" 2>/dev/null && _d5bad=4
# The SIGINT line's shape (SV:0317.2): loop.sh:126 logs 'run $N: deadline,
# SIGINT to the lead' BEFORE the kill. The driver plants runs/0008 so this
# run is 0009, and D5's 20-s mock reply is in flight past the +6-s deadline
# so the per-poll check fires -- on a correct port the line MUST be there.
$G -qF "run 0009: deadline, SIGINT to the lead" "$_d5log" 2>/dev/null || _d5bad=5
if [ "$rc_d5" -ne 0 ] || [ "$_d5bad" -ne 0 ] || [ "$_d5left" -ne 0 ]; then
    t_fail "6: D5 the deadline SIGINT (class $_d5bad: 1=no 'end rc=130' 2=meta missing rc=130/end=/team_head_after= (the SV:1838.2 three lines) 3=journal empty 4=run 0010 started 5=no 'run 0009: deadline, SIGINT to the lead' (loop.sh:126's shape, SV:0317.2); rc=$rc_d5; a fixture child survived: left=$_d5left -- CHANGED 1's teeth: no jichi or 'harness run' child after 'loop end'; log head: $(head -n 2 "$_d5log" 2>/dev/null))"
else
    t_ok "6: D5 the deadline at NOW+6 s SIGINTs the run's group (the 20-s mock reply is in flight, past the first 10-s poll): 'run 0009: deadline, SIGINT to the lead' (loop.sh:126's shape, SV:0317.2), 'run 0009: end rc=130', the meta's THREE lines (rc=130, end=, team_head_after= -- the run wrote them AFTER jichi was dead), the journal NON-EMPTY (jichi's SIGINT, exit 130), no run 0010, exit 0, and ps shows no jichi or 'harness run' child of the fixture after the loop (CHANGED 1)"
fi

# ============================================================================
# 7: D6 the snapshot's XOR, the two kinds, the stale-twin removal
# ============================================================================
# --once, --tick 3, a mock reply delayed 12000 ms: the first 10-s poll fires
# the PARTIAL (~+10 s, it saw both plants), the final at ~+12 s (big.bin
# already shrunk). ONE background sh -c chain, driven by the log: at +2 s it
# plants notes/n.txt (10 bytes) and big.bin (1000001 bytes); when the log
# shows 'periodic push done' (the partial HAS run) it shrinks big.bin to 500
# bytes. The driver waits on the chain before asserting.
mklayout d6 || { t_fail "7: D6 the layout init failed: $(head -n 3 "$tmp/d6.init")"; t_done; }
D6="$tmp/d6"
mkdir -p "$D6/runs/0008"
pin_real "$D6"
mm_start "$tmp/delay12.mm" "$tmp/mm_d6" 8 || { t_fail "7: D6 the mockmodel did not start"; t_done; }
setjicfg "$D6"
sh -c '
    sleep 2
    mkdir -p "$1/runs/0009/notes"
    printf "n plant x\n" > "$1/runs/0009/notes/n.txt"
    dd if=/dev/zero of="$1/runs/0009/big.bin" bs=1024 count=976 2>/dev/null
    dd if=/dev/zero of="$1/runs/0009/big.bin" bs=1 count=1 seek=1000000 2>/dev/null
    _i=0
    while ! grep -qF "periodic push done" "$1/harness.log" 2>/dev/null; do
        _i=$((_i + 1)); [ "$_i" -gt 15 ] && break
        sleep 1
    done
    if grep -qF "periodic push done" "$1/harness.log" 2>/dev/null; then
        dd if=/dev/zero of="$1/runs/0009/big.bin" bs=1 count=500 2>/dev/null
    fi
' _ "$D6" &
_d6plant=$!
with_deadline 120 "$HBIN" --config "$D6.cfg" drive --tenant lead --once --tick 3 \
    < /dev/null > "$tmp/d6.out" 2> "$tmp/d6.err"
rc_d6=$?
wait "$_d6plant" 2>/dev/null
mm_stop
_d6log="$D6/harness.log"
_d6recs="$D6/recs/runs/0009"
_d6bad=0
for _d6f in stream.jsonl meta.txt prompt.md; do
    [ -f "$_d6recs/$_d6f" ] || _d6bad=1
    [ -e "$_d6recs/$_d6f.gz" ] && _d6bad=1
done
[ -f "$_d6recs/notes/n.txt.gz" ] || _d6bad=2
[ -e "$_d6recs/notes/n.txt" ] && _d6bad=2
_d6sz=$( { [ -f "$_d6recs/big.bin" ] && wc -c < "$_d6recs/big.bin" 2>/dev/null; } | tr -d ' ')
[ "$_d6sz" = 500 ] || _d6bad=3
[ -e "$_d6recs/big.bin.gz" ] && _d6bad=3
git -C "$D6/recs" log --oneline 2>/dev/null | $G -qF "records: run 0009 (partial)" || _d6bad=4
git -C "$D6/recs" log --oneline 2>/dev/null | $G -qF "records: run 0009 (final)" || _d6bad=4
_d6order=$($G -nF -e "run 0009: start" -e "run 0009: periodic push done" -e "run 0009: end" "$_d6log" 2>/dev/null | cut -d: -f1)
_d6n=$(printf '%s\n' "$_d6order" | $G -c . 2>/dev/null)
[ "$_d6n" = 3 ] || _d6bad=5
_d6l1=$(printf '%s\n' "$_d6order" | sed -n 1p)
_d6l2=$(printf '%s\n' "$_d6order" | sed -n 2p)
_d6l3=$(printf '%s\n' "$_d6order" | sed -n 3p)
{ [ -n "$_d6l1" ] && [ -n "$_d6l2" ] && [ -n "$_d6l3" ] && [ "$_d6l1" -lt "$_d6l2" ] && [ "$_d6l2" -lt "$_d6l3" ]; } || _d6bad=5
if [ "$rc_d6" -ne 0 ] || [ "$_d6bad" -ne 0 ]; then
    t_fail "7: D6 the snapshot's XOR / two kinds / stale twin (class $_d6bad: 1=stream/meta/prompt not PLAIN or a .gz twin present 2=notes/n.txt.gz missing or the plain twin present 3=big.bin not 500 bytes PLAIN or big.bin.gz present (the final's rm -f of the stale twin) 4=a 'records: run 0009 (partial)'/'(final)' commit missing 5='periodic push done' not between start and end; rc=$rc_d6; recs tree: $(find "$_d6recs" -type f 2>/dev/null | head -n 8 | tr '\n' ' '))"
else
    t_ok "7: D6 the partial (at the first 10-s poll) and the final snapshot: stream.jsonl/meta.txt/prompt.md PLAIN with no .gz twin, notes/n.txt.gz present with the plain twin gone (the subdir is always gzipped), big.bin 500 bytes PLAIN with the partial's stale big.bin.gz removed, recs has 'records: run 0009 (partial)' AND 'records: run 0009 (final)', and the log's 'run 0009: periodic push done' sits between start and end"
fi

# ============================================================================
# 8: D6d the symlink is skipped (CHANGED 7, SV:1838.3)
# ============================================================================
mklayout d6d || { t_fail "8: D6d the layout init failed: $(head -n 3 "$tmp/d6d.init")"; t_done; }
D6d="$tmp/d6d"
mkdir -p "$D6d/runs/0008"
pin_real "$D6d"
mm_start "$tmp/fast.mm" "$tmp/mm_d6d" 8 || { t_fail "8: D6d the mockmodel did not start"; t_done; }
setjicfg "$D6d"
# The plant chain: at +2 s create runs/0009/keylink, a SYMLINK to the
# fixture's model-key file (the canary) OUTSIDE runs/.
sh -c '
    sleep 2
    mkdir -p "$1/runs/0009"
    ln -s "$1.key" "$1/runs/0009/keylink"
' _ "$D6d" &
_d6dplant=$!
with_deadline 120 "$HBIN" --config "$D6d.cfg" drive --tenant lead --once \
    < /dev/null > "$tmp/d6d.out" 2> "$tmp/d6d.err"
rc_d6d=$?
wait "$_d6dplant" 2>/dev/null
mm_stop
_kc_d6d=$(cat "$D6d.key")
_d6dbad=0
[ -e "$D6d/recs/runs/0009/keylink" ] && _d6dbad=1
[ -e "$D6d/recs/runs/0009/keylink.gz" ] && _d6dbad=1
for _d6df in $(find "$D6d/recs" -type f 2>/dev/null); do
    $G -qF "$_kc_d6d" "$_d6df" 2>/dev/null && _d6dbad=2
done
$G -qF "skipped the symlink keylink" "$D6d/harness.log" 2>/dev/null || _d6dbad=3
if [ "$rc_d6d" -ne 0 ] || [ "$_d6dbad" -ne 0 ]; then
    t_fail "8: D6d the planted symlink was not skipped (class $_d6dbad: 1=keylink or keylink.gz in recs/runs/0009 2=the canary in a file under recs/ (a snapshot that FOLLOWED the symlink -- the reference's [ -f ]/cp -p shape) 3=no 'skipped the symlink keylink' log line; rc=$rc_d6d)"
else
    t_ok "8: D6d the planted runs/0009/keylink (a SYMLINK to the model-key file OUTSIDE runs/) is skipped by name: recs has NO keylink and NO keylink.gz, the canary is in NO file under recs/ (find + grep), and the log has 'skipped the symlink keylink' (CHANGED 7, SV:1838.3)"
fi

# ============================================================================
# 9: D6e the recs git child is neutralised (CHANGED 10, SV:1838.4)
# ============================================================================
mklayout d6e || { t_fail "9: D6e the layout init failed: $(head -n 3 "$tmp/d6e.init")"; t_done; }
D6e="$tmp/d6e"
mkdir -p "$D6e/runs/0008"
pin_real "$D6e"
MARKER="$tmp/d6e.marker"
rm -f "$MARKER"
# BEFORE the drive: plant an EXECUTABLE recs/.git/hooks/pre-commit that
# touches $MARKER. The fixture's recs clone is REAL init output, so
# .git/hooks/ exists as init laid it.
printf '#!/bin/sh\ntouch %s\n' "$MARKER" > "$D6e/recs/.git/hooks/pre-commit"
chmod 755 "$D6e/recs/.git/hooks/pre-commit"
mm_start "$tmp/fast.mm" "$tmp/mm_d6e" 8 || { t_fail "9: D6e the mockmodel did not start"; t_done; }
setjicfg "$D6e"
with_deadline 120 "$HBIN" --config "$D6e.cfg" drive --tenant lead --once \
    < /dev/null > "$tmp/d6e.out" 2> "$tmp/d6e.err"
rc_d6e=$?
mm_stop
_d6ebad=0
[ -e "$MARKER" ] && _d6ebad=1
git -C "$D6e/recs" log --oneline 2>/dev/null | $G -qF "records: run 0009 (final)" || _d6ebad=2
if [ "$rc_d6e" -ne 0 ] || [ "$_d6ebad" -ne 0 ]; then
    t_fail "9: D6e the planted pre-commit hook was NOT neutralised (class $_d6ebad: 1=\$MARKER exists -- a snapshot git commit ran recs' hooks, no core.hooksPath=/dev/null and no M2 set 2='records: run 0009 (final)' missing -- the commit must still land, the CHANGED 10 kills the hooks not the commit; rc=$rc_d6e)"
else
    t_ok "9: D6e a planted executable recs/.git/hooks/pre-commit that touches \$MARKER left NO marker after the drive (the recs git children run the M2 set + core.hooksPath=/dev/null), and 'records: run 0009 (final)' IS in git -C recs log --oneline (the commit still landed) (CHANGED 10, SV:1838.4)"
fi

# ============================================================================
# 10: D7 the three quick failures and the pause (behaviour 12, CHANGED 8)
#    -- wrap in with_deadline 80 (CHANGED 8's teeth: the SOLID 600 s is killed)
# ============================================================================
# The counter is read from disk once at loop START (behaviour 2) and a
# pre-run refusal CREATES NOTHING (M1057 behaviour 1: the refusal precedes
# the layout), so the driver plants the run directory itself (reusing D1's
# plant) and REMOVES the key file BEFORE the loop: the runs refuse rc=2 in
# ~0 s each (the fastest failure, rc != 0 && dur < 120), quick hits 3 on
# run 0011, the pause is logged, the 600 s is SLEEPED IN 10-s SLICES, a
# slice check sees now >= END (50 s) and exits the pause, the top check logs
# 'deadline reached: loop ends', exit 0. NO --once. One mock stays up (the
# layout's doctor, per the note: the run refuses before spawn, no model call)
# but it is never called.
mklayout d7 50 || { t_fail "10: D7 the layout init failed: $(head -n 3 "$tmp/d7.init")"; t_done; }
D7="$tmp/d7"
mkdir -p "$D7/runs/0008"
pin_real "$D7"
rm -f "$D7.key"
mm_start "$tmp/fast.mm" "$tmp/mm_d7" 8 || { t_fail "10: D7 the mockmodel did not start"; t_done; }
setjicfg "$D7"
with_deadline 80 "$HBIN" --config "$D7.cfg" drive --tenant lead \
    < /dev/null > "$tmp/d7.out" 2> "$tmp/d7.err"
rc_d7=$?
mm_stop
_d7log="$D7/harness.log"
_d7pause=$($G -cF "three quick failures in a row: waiting 10 minutes" "$_d7log" 2>/dev/null)
_d7bad=0
$G -qF "run 0009: end rc=2" "$_d7log" 2>/dev/null || _d7bad=1
$G -qF "run 0010: end rc=2" "$_d7log" 2>/dev/null || _d7bad=1
$G -qF "run 0011: end rc=2" "$_d7log" 2>/dev/null || _d7bad=1
[ "$_d7pause" = 1 ] || _d7bad=2
$G -qF "deadline reached: loop ends" "$_d7log" 2>/dev/null || _d7bad=3
$G -qF "run 0012: start" "$_d7log" 2>/dev/null && _d7bad=4
if [ "$rc_d7" -ne 0 ] || [ "$_d7bad" -ne 0 ]; then
    t_fail "10: D7 the three quick failures and the sliced pause (class $_d7bad: 1=a run's 'end rc=2' line missing (0009/0010/0011) 2=the pause line not EXACTLY once (got ${_d7pause:-0} -- a port that does not count a 0-s failure has no pause line at all) 3=no 'deadline reached: loop ends' (the sliced pause's slice check did not see the deadline) 4='run 0012: start' -- the deadline break came AFTER the next run; rc=$rc_d7; a SOLID 600-s pause is killed by this cell's with_deadline 80 (CHANGED 8's teeth); log: $(head -n 3 "$_d7log" 2>/dev/null))"
else
    t_ok "10: D7 with the key file removed, the loop reads 8 from disk and refuses runs 0009/0010/0011 in a row ('end rc=2' each, ~0 s): EXACTLY ONE 'three quick failures in a row: waiting 10 minutes', the sliced pause's slice check sees the NOW+50 s deadline, 'deadline reached: loop ends', exit 0, NO 'run 0012: start' -- all inside with_deadline 80 (CHANGED 8's teeth: the reference's SOLID sleep 600 would be killed, a wall-time red)"
fi

# ============================================================================
# 11: D8 the refusal classes
# ============================================================================
mklayout d8 || { t_fail "11: D8 the layout init failed: $(head -n 3 "$tmp/d8.init")"; t_done; }
D8="$tmp/d8"
mkdir -p "$D8/runs/0008"
pin_real "$D8"
# (a) no --tenant: a usage refusal, exit 2, the refusal NAMED.
"$HBIN" --config "$D8.cfg" drive < /dev/null > "$tmp/d8a.out" 2> "$tmp/d8a.err"
rc_d8a=$?
# (b) a tenant the config does not name: the RUN's refusal (reused), exit 2,
# and NOTHING run (no new runs/ entry -- the counter is read after the
# refusals).
"$HBIN" --config "$D8.cfg" drive --tenant nobody --once < /dev/null \
    > "$tmp/d8b.out" 2> "$tmp/d8b.err"
rc_d8b=$?
# (c) --tick 0: a NAMED refusal (a 0 tick is a cap-on-housekeeping).
"$HBIN" --config "$D8.cfg" drive --tenant lead --once --tick 0 < /dev/null \
    > "$tmp/d8c.out" 2> "$tmp/d8c.err"
rc_d8c=$?
# (d) an unparseable deadline: exit 1 (the reference's own class for a
# broken SETTING), the line BEFORE 'loop start' in the log.
sed -e 's#"end":"[^"]*#"end":"not a date"#' "$D8.cfg" > "$D8.badcfg"
"$HBIN" --config "$D8.badcfg" drive --tenant lead --once < /dev/null \
    > "$tmp/d8d.out" 2> "$tmp/d8d.err"
rc_d8d=$?
_d8bad=0
[ "$rc_d8a" -eq 2 ] || _d8bad=1
$G -qi "tenant" "$tmp/d8a.err" 2>/dev/null || _d8bad=1
[ "$rc_d8b" -eq 2 ] || _d8bad=2
$G -qF "no tenant named nobody" "$tmp/d8b.err" 2>/dev/null || _d8bad=2
[ -e "$D8/runs/0009" ] && _d8bad=2
[ "$rc_d8c" -eq 2 ] || _d8bad=3
$G -qF -- "--tick" "$tmp/d8c.err" 2>/dev/null || _d8bad=3
[ "$rc_d8d" -eq 1 ] || _d8bad=4
$G -qF "the end time does not parse" "$tmp/d8d.err" 2>/dev/null || _d8bad=4
[ -f "$D8/harness.log" ] && $G -qF "loop start" "$D8/harness.log" 2>/dev/null && _d8bad=4
if [ "$_d8bad" -ne 0 ]; then
    t_fail "11: D8 the refusal classes (class $_d8bad: 1=no-tenant not a named exit-2 refusal (rc=$rc_d8a) 2='no tenant named nobody' missing or runs/0009 created (rc=$rc_d8b) 3='--tick 0' not a named exit-2 refusal (rc=$rc_d8c) 4=the unparseable deadline not exit 1 + 'the end time does not parse' before any 'loop start' (rc=$rc_d8d); a: $(head -n 1 "$tmp/d8a.err" 2>/dev/null) b: $(head -n 1 "$tmp/d8b.err" 2>/dev/null) c: $(head -n 1 "$tmp/d8c.err" 2>/dev/null) d: $(head -n 1 "$tmp/d8d.err" 2>/dev/null))"
else
    t_ok "11: D8 the refusal classes -- (a) no --tenant: exit 2, named; (b) --tenant nobody --once: exit 2, the run's refusal 'no tenant named nobody', and NO new runs/ entry (the counter is read after the refusals); (c) --tick 0: exit 2, named; (d) drive.end 'not a date': exit 1, 'the end time does not parse' BEFORE 'loop start' in the log (no start line at all)"
fi

# ============================================================================
# 12: D1f the mirror-fetch report is stamped in harness.log, OFF the loop's
#     stdout (M1064, card 93, SV:0904.3; rides D1's fixture, no new run)
# ============================================================================
# D1's single --once run fires the pre-run mirror fetch (site A) and the
# end-of-run save_team fetch (site B) -- so the FIXED tree stamps the
# fetch's say() lines into harness.log (one 'mirror fetch: done' per site,
# >= 2 total) and leaves the loop's stdout clean. An UNSTAMPED line (the
# defect, and attempt 1's shape) sits at column 0 WITHOUT the date prefix,
# so the column-0 stamp regex is the discriminator. The stamp is log_line's
# behaviour-14 prefix: 'YYYY-MM-DD HH:MM:SS +-HHMM' (a SPACE, then a signed
# UTC offset -- NOT the 'T' ISO form; verified against stamp_of/strftime
# and attempt 1's measured lines '2026-10-28 02:14:23 +0100 mirror fetch:
# done').
_m12out=0
$G -qF "mirror fetch:" "$tmp/d1.out" 2>/dev/null && _m12out=1
# The stamped count: grep -c prints the count AND exits 1 on a zero match,
# so an `|| echo 0` fallback appends a SECOND 0 (the run-0086 red state: the
# log exists but has ZERO stamped lines -> '0\n0' -> `[: Illegal number`).
# Capture the count directly and normalise it to a single clean number: the
# `|| true` swallows grep's exit-1 without adding output, the case resets an
# empty/non-numeric capture to 0.
_m12stamped=0
[ -f "$D1/harness.log" ] && _m12stamped=$($G -cE '^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9] [+-][0-9][0-9][0-9][0-9] mirror fetch:' "$D1/harness.log" 2>/dev/null || true)
case "$_m12stamped" in ''|*[!0-9]*) _m12stamped=0 ;; esac
_m12=0
# The three defect classes are mutually exclusive, primary first (class 1 =
# site A's 'mirror fetch:' leaked onto the loop's stdout is the PRIMARY
# defect; class 2 = zero stamped lines; class 3 = fewer than two). They are
# evaluated in that order and only the first that holds reports: on the red
# path _m12out=1 AND the clean count is 0, so class 1, class 2 and the
# count<2 all hold -- last-wins (the old three independent lines) would have
# misreported class 3 and masked the true primary class 1. (Before the count
# was cleaned the dirty "0\n0" made class 2/3 throw `[: Illegal number` and
# evaluate false, so the baseline happened to show class 1; the clean count
# exposes the real, simultaneous hold, which the if/elif now orders
# primary-first.) Green: _m12out=0 and count 2 -> no class holds -> _m12=0.
if [ "$_m12out" -eq 1 ]; then
    _m12=1
elif [ "${_m12stamped:-0}" -eq 0 ]; then
    _m12=2
elif [ "${_m12stamped}" -lt 2 ]; then
    _m12=3
fi
if [ "$_m12" -ne 0 ]; then
    t_fail "12: D1f the mirror-fetch report (M1064) is not stamped in harness.log and off the loop's stdout (class $_m12: 1=a 'mirror fetch:' line is STILL on the loop's stdout (site A's pre-run fetch must be captured by the fork-child, not printed) 2=NO stamped 'mirror fetch:' line in harness.log (the behaviour-14 'YYYY-MM-DD HH:MM:SS +-HHMM' stamp at column 0 must precede it) 3=FEWER than 2 stamped 'mirror fetch:' lines (the single --once run fires site A pre-run AND site B in save_team); stdout-mirror-fetch=$_m12out log-stamped=${_m12stamped:-0}; d1.out mirror-fetch lines: $( { $G -F 'mirror fetch:' "$tmp/d1.out" 2>/dev/null; } | head -n 2 | tr '\n' ';'); harness.log stamped mirror-fetch lines: $( { [ -f "$D1/harness.log" ] && $G -E '^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9] [+-][0-9][0-9][0-9][0-9] mirror fetch:' "$D1/harness.log" 2>/dev/null; } | head -n 3 | tr '\n' ';'))"
else
    t_ok "12: D1f the drive loop's two in-process mirror FETCH reports (site A pre-run, site B save_team) land in harness.log STAMPED ('YYYY-MM-DD HH:MM:SS +-HHMM mirror fetch: ...', the behaviour-14 log_line prefix, at least two lines) and the loop's stdout carries NO 'mirror fetch:' line (M1064, the fork-child capture -- the loop's own fd 1/fd 2 are never dup2'd, so D6 check 7's snapshot children stay unpoisoned)"
fi

t_done
