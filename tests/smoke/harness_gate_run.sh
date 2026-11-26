#!/bin/sh
# smoke: `jichi-harness gate` -- the gate RUN's two PURE stages, addressable on
# their own (M1154, batch H9b; the C89 port of the 2026-10-27 hardening-host
# drive's ledger.sh / sweep-verdict.sh). This driver pins the `ledger` and the
# `verdict` stages RED FIRST: wave 1 (T1) builds only the `gate` verb's usage
# arms and the two config fields, so every stage cell is RED BY CONSTRUCTION
# (the stages are "not built yet (M1154 wave 1)", exit 2). T2 builds the ledger
# body (half the cells go green); T3 builds the verdict body (the other half).
# The usage cells (U1-U5) and the fixture guard (F0) are GREEN.
#
# Design note (behaviours 2 / 4 / 13 / 14 / 15):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1154.md
# Brief (the cell table, the stage invocation contract, red-first order):
#   docs/internal/drive/2026-11-05-harness-port/notes/briefs-2026-11-09-M1154-T1.md
# Reference scripts (read line by line; the expected bytes are derived from them):
#   docs/internal/drive/2026-10-27-hardening-host/ledger.sh
#   docs/internal/drive/2026-10-27-hardening-host/sweep-verdict.sh
#   docs/internal/drive/2026-10-27-hardening-host/tests/fixtures/sweep-832c7930.txt
#   docs/internal/drive/2026-10-27-hardening-host/tests/fixtures/sweep-9ada7cd7.txt
#
# THE STAGE INVOCATION CONTRACT (the driver calls the stages THIS way):
#   ledger:  gate run --config <cfg> --stage ledger <REPO> <BASE> <HEAD>
#            --ledger-address <ADDR> [--ledger-accept <FILE>]
#   verdict: gate run --config <cfg> --stage verdict <FILE> <SHA8> [--push-repo <R>]
#
# WHAT IS CHECKED -- 107 cells (M1187-b: +V13 +V14, so 1..99; M1191-a: the
# D1-D5 dead-clone collision group, 100-104; M1197-b: 105 the B3 teeth, the
# sweep build must build all three programs so the install driver is swept;
# see the briefs' cell tables for
# U1-U5, L1-L13, V1-V14, S1-S13, C1-C11, R1-R9, and G1-G26 + G14b + G30a +
# G30b (67-95, the orchestrator's end-to-end cells) + G15r + G15v (96-97) +
# the two M1178 cells (98-99); M1202 wave 2b: 106 the D2 default-hold arm
# + the D3 landing.decision record, 107 the approve verb skeleton;
# M1202 wave 2b follow-up (hunk-audit F2/F3/F4 + F5-F8/F11 teeth):
# extensions of 76/77 + 106 pin the D3 record's field bytes
# (policy=/batch=/time %z/land-rc=/no-land-step), 108 the land step's
# <s>-idiom substitution, 109 the landed record's field bytes, 110 the
# approve usage arms, 111 the land-step
# config fallback + the absent-record refusal, 112 D2's AUTO
# case-sensitivity, 113 the non-fatal approved-record append). The
# fixture is REAL git under
# the driver's TMPDIR: a bare push.git holding the p-chain
# P0<P1<P2<P3<P4<P5<P6 (P2 adds a smoke driver that never invokes "$BIN",
# P4 one that does, P5 is an empty commit, P6 rewrites both to invoke it) +
# the vp7 branch P6<P7<P9 (P7 adds tests/smoke/smoke_lint.sh WITH "$BIN" --
# its name IS a table entry, the REL3 shape; P9 empties the EXCLUDE table)
# and a scratch ledger repo holding base B + the per-cell branches l1..l13.
# P0's tree carries scripts/mutant-sweep.sh (the two-line EXCLUDE table,
# byte-for-byte the live one); every p-chain commit inherits it until P9
# rewrites it (M1187-b: the second count reads the table FROM THE COMMIT).
#
# Every fixture commit carries an explicit -c user.name / -c user.email + a
# FIXED GIT_AUTHOR/COMMITTER_DATE (the card-23 lesson), so the shas -- and the
# cells' expectations -- do not depend on the operator's clock. The driver runs
# the tree's binary by path ($HBIN, the third_program idiom) and -- for
# smoke_lint check 5 -- also runs the tree's "$BIN" once.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 115
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: the fixture git children are neutralised, but $BIN must be
# exercised so the lint sees the driver validate the just-built tree.
"$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# Neutralise ambient harness loader settings (env wins over the config) and the
# operator's ambient git config for the fixture repos.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

# The driver's own --ledger-address: the address the ledger checks author,
# committer and every Co-Authored-By against (the reference's hard-coded ADDR).
ADDR="ops@example.com"
OTHER="other@example.com"        # the "wrong" identity / co address
JB="53a1ef4e"                    # a stable hex8 the good commits carry
JM="jlu/qwen3-coder-next"        # the good Jichi-Model (never a priced id)
PRICED="anthropic/claude-x"      # a stable fake priced-model id

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt "$T_PLAN" ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the ledger commit helper -------------------------------------------------
# _lc REPO AUTHOR_EMAIL COMMITTER_EMAIL SUBJECT TRAILERS
# Makes one commit (fixed identity + date, a unique tree via a counter) and
# echoes the new HEAD sha. TRAILERS (a multi-line string of `Key: Value` lines)
# is added as a second paragraph so git treats it as a trailer block.
_LCN=0
_lc() {
    _r="$1"; _LCN=$((_LCN + 1))
    printf 'subject=%s n=%s\n' "$4" "$_LCN" > "$_r/f" 2>/dev/null || printf '%s\n' "$4" > "$_r/f"
    git -C "$_r" add f
    GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$2" \
    GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$3" \
    GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
    git -C "$_r" -c user.name="Ledger Smoke" -c user.email="$2" \
    commit -q -m "$4" ${5:+-m "$5"}
    git -C "$_r" rev-parse HEAD
}
# The good trailers every clean ledger commit carries (author/committer = ADDR).
GOODCO="Co-Authored-By: jichi 0.13.0 <$ADDR>
Jichi-Build: $JB
Jichi-Model: $JM"

# --- the fixture: a bare push.git (p-chain) + a ledger working repo ------------
# p-chain (the verdict's second-count subjects, from the reference's fixtures):
#   P0 <- P1 <- P2 <- P3 <- P4 <- P5 <- P6, then the vp7 branch P6 <- P7 <- P9
#   P0 : plain (file f) + scripts/mutant-sweep.sh (M1187-b: the two-line
#        EXCLUDE table, byte-for-byte the live one -- every commit's tree
#        carries it until P9 rewrites it)
#   P1 : plain (file h)  -- V5's MUTANT_BASE (a plain, a-f-bearing commit)
#   P2 : adds tests/smoke/nobin_lint.sh, NO "$BIN"  -- the 9ada7cd7 false-RED
#   P3 : plain (file g)  -- the 832c7930 false-RED
#   P4 : adds tests/smoke/usesbin_lint.sh WITH "$BIN" -- V6's disagreement
#   P5 : empty commit (no file) -- V10's empty-batch subject (diff P5..P5 = {})
#   P6 : rewrites both drivers to invoke "$BIN" (the sweep cells' commit)
#   P7 : on vp7, adds tests/smoke/smoke_lint.sh WITH "$BIN"; the name IS a
#        table entry -- V7/V13's REL3 shape (the only changed driver excluded)
#   P9 : on vp7, rewrites mutant-sweep.sh's table to EXCLUDE='' -- V14's
#        empty-table subject (no P8; the branch is P6<-P7<-P9)
# The second-count ranges (the brief's cells). nall is `git diff --name-only
# <mb> <s>` ALONE (sweep-verdict.sh:42 -- the file creation order fixes it: f
# at P0, h at P1, the smoke drivers at P2/P4/P7, so a range's diff is only the
# files ADDED after <mb>): the T1 driver's 2/2/3 counted diff+own, the
# reference does not. nsm is the union's tests/smoke/*.sh line count (the :43,
# before the _smoke|run exclusion); nd is of those, the ones that invoke
# "$BIN" AND are not in the EXCLUDE table (:50); nx the ones that invoke
# "$BIN" AND are in the table (:49); nxm is the line's (<n> excluded).
#   V4 (832 shape, 0 drivers):  mb=P2, s=P3 : diff P2..P3 = {g} -> nall=1, nsm=0, nd=0, nx=0
#   V5 (9ada shape, 1 no-$BIN): mb=P1, s=P2 : diff P1..P2 = {nobin} -> nall=1, nsm=1, nd=0, nx=0
#   V6 (disagreement, a $BIN):  mb=P2, s=P4 : diff P2..P4 = {g,usesbin} -> nall=2, nsm=1, nd=1
#   V7 (REL3, re-pointed GREEN):mb=P6, s=P7 : diff P6..P7 = {smoke_lint} -> nall=1, nsm=1, nd=0, nx=1==nxm
#   V13 (cross-check red):      mb=P6, s=P7 : as V7 but the line says (2 excluded) -> nxm=2 != nx=1
#   V14 (empty-table red):      mb=P7, s=P9 : diff P7..P9 = {mutant-sweep.sh} -> nall=1, nsm=0, nd=0, nx=0
#   V9 (git diff fails):         mb=<fake>, s=P3 ; V10 (empty batch): mb=P5, s=P5 (nall=0)
# Each range's mb..s must change >= 1 file (nall>0, V10 excepted) and the driver must exist at s.
PCHAIN="$tmp/pchain"; LREPO="$tmp/lrepo"
if ! mkdir -p "$PCHAIN" "$LREPO" "$tmp/dir" "$tmp/dir/gate"; then
    fixture_fail "the scratch dirs could not be made"
fi
git -C "$PCHAIN" init -q
_pc() { # $1=commit-message; makes one commit (fixed identity + a step date) and
        # echoes the new HEAD sha. The caller stages the file(s) first.
    GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
    GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
    GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
    git -C "$PCHAIN" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "$1"
    git -C "$PCHAIN" rev-parse HEAD
}
# P0: plain + scripts/mutant-sweep.sh (M1187-b: every commit's tree carries
# the two-line EXCLUDE table, byte-for-byte the live one).
mkdir -p "$PCHAIN/scripts"
printf '#!/bin/sh\nEXCLUDE='\''faults faults_net faults_net_midstream smoke_lint project_records_lint\ndoc_commands_lint'\''\n' > "$PCHAIN/scripts/mutant-sweep.sh"
printf 'p0\n' > "$PCHAIN/f"; git -C "$PCHAIN" add f scripts/mutant-sweep.sh
P0=$(_pc "P0")
# P1: plain (V5's MUTANT_BASE).
printf 'p1\n' > "$PCHAIN/h"; git -C "$PCHAIN" add h
P1=$(_pc "P1")
# P2: adds the non-"$BIN" smoke driver (the FV-9ada7cd7 false-RED subject).
mkdir -p "$PCHAIN/tests/smoke"
printf '#!/bin/sh\necho "no bin here"\n' > "$PCHAIN/tests/smoke/nobin_lint.sh"
git -C "$PCHAIN" add tests/smoke/nobin_lint.sh
P2=$(_pc "P2")
# P3: plain (a distinct file; P2..P3 = {f,g} -> the FV-832c7930 shape).
printf 'p3\n' > "$PCHAIN/g"; git -C "$PCHAIN" add g
P3=$(_pc "P3")
# P4: adds the "$BIN" smoke driver (V6's second-count-disagreement subject).
printf '#!/bin/sh\n"$BIN" --version\necho done\n' > "$PCHAIN/tests/smoke/usesbin_lint.sh"
git -C "$PCHAIN" add tests/smoke/usesbin_lint.sh
P4=$(_pc "P4")
# P5: an empty commit (no file) -- V10's empty-batch subject (diff P5..P5 = {}).
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
git -C "$PCHAIN" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q --allow-empty -m "P5"
P5=$(git -C "$PCHAIN" rev-parse HEAD)

# P6 (the sweep cells' commit): rewrites nobin_lint.sh so it, like
# usesbin_lint.sh, invokes "$BIN" -- the brief's S3 expects BOTH drivers red
# (`red: nobin_lint usesbin_lint`) when the --bin stub fails, and P4's
# nobin_lint.sh (`echo "no bin here"`) can never fail. P0..P4 (the verdict
# cells' second-count subjects) are byte-identical; P6 is a NEW commit ON TOP
# of P5, so the clone (the sweep's source) reaches P6 and origin/master stays
# at P0. The sweep cells check out P6.
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
git -C "$PCHAIN" checkout -q "$P5"
# BOTH drivers end in the $BIN call (no trailing echo): the driver's rc is the
# stub's rc, so S3's failing --bin stub (exit 1) -> both drivers rc=1 (the
# brief's S3 `  RED <b> rc=1` lines), and S1's clean stub (exit 0) -> rc=0.
printf '#!/bin/sh\n"$BIN" --version\n' > "$PCHAIN/tests/smoke/nobin_lint.sh"
printf '#!/bin/sh\n"$BIN" --version\n' > "$PCHAIN/tests/smoke/usesbin_lint.sh"
git -C "$PCHAIN" add tests/smoke/nobin_lint.sh tests/smoke/usesbin_lint.sh
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-09T00:00:09Z" GIT_COMMITTER_DATE="2026-11-09T00:00:09Z" \
git -C "$PCHAIN" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "P6"
P6=$(git -C "$PCHAIN" rev-parse HEAD)
# master's tip is P6 (the sweep's commit): the bare push.git (refs/heads/* ->
# P6) carries P6 and P0..P5, and the sweep's `git checkout <P6-8>` succeeds.
git -C "$PCHAIN" checkout -q "$P6"

# vp7 (M1187-b): the REL3-shape branch, P6 <- P7 <- P9 (no P8).
#   P7 (parent P6): adds tests/smoke/smoke_lint.sh whose LIVE lines invoke
#     "$BIN"; the name IS a table name -- the only changed smoke script is an
#     EXCLUDED bin-driver (the 2026-11-14 06:00 false-RED shape, V7's subject).
#   P9 (parent P7): rewrites scripts/mutant-sweep.sh with the table emptied
#     (EXCLUDE='' -- the prepared awk joins nothing, V14's empty-table arm).
# master's tip stays P6 (the S-cells check out P6); the verdict cells only
# need P7/P9 reachable in push.git, which the ref update below achieves.
git -C "$PCHAIN" checkout -q -b vp7 "$P6"
printf '#!/bin/sh\n"$BIN" --version\n' > "$PCHAIN/tests/smoke/smoke_lint.sh"
git -C "$PCHAIN" add tests/smoke/smoke_lint.sh
P7=$(_pc "P7")
printf '#!/bin/sh\nEXCLUDE='\'''\''\n' > "$PCHAIN/scripts/mutant-sweep.sh"
git -C "$PCHAIN" add scripts/mutant-sweep.sh
P9=$(_pc "P9")
git -C "$PCHAIN" checkout -q "$P6"

# A BARE push.git: the verdict cells' second-count repo. origin/master at P0
# (a plain commit) and every p-chain commit P0..P6 present (reachable from P6)
# + refs/heads/vp7 at P9 (P7 and P9 reachable, M1187-b's V7/V13/V14 subjects).
git clone -q --bare "$PCHAIN" "$tmp/push.git"
git -C "$tmp/push.git" update-ref refs/remotes/origin/master "$P0"
git -C "$tmp/push.git" update-ref refs/heads/vp7 "$P9"
# (V9's "MUTANT_BASE not a commit" uses a fake id, not an absent repo.)

# The ledger working repo: base B, then per-cell branches l1..l13.
git -C "$LREPO" init -q
printf 'base\n' > "$LREPO/f"; git -C "$LREPO" add f
mkdir -p "$LREPO/docs/internal"
printf 'base inner\n' > "$LREPO/docs/internal/base.txt"; git -C "$LREPO" add docs/internal/base.txt
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-09T00:00:05Z" GIT_COMMITTER_DATE="2026-11-09T00:00:05Z" \
git -C "$LREPO" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "B"
B=$(git -C "$LREPO" rev-parse HEAD)

# --- the ledger per-cell branches (each: B + one or two commits) --------------
# L1 clean (2 good commits), L2 author wrong, L3 committer wrong, L4 no
# Co-Authored-By, L5 Co-Authored-By wrong, L6 no Jichi-Build, L7 no Jichi-Model,
# L8 priced model, L9 empty range (the branch is B itself), L10 accept-listed
# missing-trailers-only (good co, no jb/jm), L11 accept-listed but wrong address,
# L12 the DD6 supervisor commit (docs/internal/ only), L13 the same trailer with
# one changed path OUTSIDE docs/internal/ (the fall-through guard).
# Every branch is parented ON B (a new branch off B), so the range B..<branch>
# is exactly the cell's own commits -- _cell checks out B first, makes the
# commit(s), and leaves the driver on the new branch.
_cell() { # $1=branch-name  then _lc args for one commit: author committer subj trailers
    _cb="$1"; shift
    git -C "$LREPO" checkout -q "$B"
    _lc "$LREPO" "$@" > "$tmp/_csha"
    git -C "$LREPO" checkout -q -b "$_cb"
    cat "$tmp/_csha"
}
# L1: two clean commits (the brief's "clean 2-commit range").
git -C "$LREPO" checkout -q "$B"
L1A=$(_lc "$LREPO" "$ADDR" "$ADDR" "L1a" "$GOODCO")
L1=$(_lc  "$LREPO" "$ADDR" "$ADDR" "L1b" "$GOODCO")
git -C "$LREPO" checkout -q -b l1
L2=$(_cell l2 "$OTHER" "$ADDR" "L2" "$GOODCO")
L3=$(_cell l3 "$ADDR" "$OTHER" "L3" "$GOODCO")
L4=$(_cell l4 "$ADDR" "$ADDR" "L4" "Jichi-Build: $JB
Jichi-Model: $JM")
L5=$(_cell l5 "$ADDR" "$ADDR" "L5" "Co-Authored-By: jichi 0.13.0 <$OTHER>
Jichi-Build: $JB
Jichi-Model: $JM")
L6=$(_cell l6 "$ADDR" "$ADDR" "L6" "Co-Authored-By: jichi 0.13.0 <$ADDR>
Jichi-Model: $JM")
L7=$(_cell l7 "$ADDR" "$ADDR" "L7" "Co-Authored-By: jichi 0.13.0 <$ADDR>
Jichi-Build: $JB")
L8=$(_cell l8 "$ADDR" "$ADDR" "L8" "Co-Authored-By: jichi 0.13.0 <$ADDR>
Jichi-Build: $JB
Jichi-Model: $PRICED")
# L9: an empty range -- the branch is B itself (no new commit).
git -C "$LREPO" checkout -q "$B"
git -C "$LREPO" checkout -q -b l9
# L10: good author/committer + a good Co-Authored-By, but no Jichi-Build and no
# Jichi-Model -- ONLY missing-trailer reasons (the DD11 accept-list shape).
L10=$(_cell l10 "$ADDR" "$ADDR" "L10" "Co-Authored-By: jichi 0.13.0 <$ADDR>")
# L11: a WRONG ADDRESS (author) + good trailers -- a non-trailer reason, so the
# accept list cannot exempt it (it stays bad even when listed).
L11=$(_cell l11 "$OTHER" "$ADDR" "L11" "$GOODCO")

# The DD6 supervisor's Co-Authored-By trailer (SV:0137's SUP -- the value the
# docs/analysis/COMMIT-ATTRIBUTION.md rules give an agent other than jichi),
# the L12/L13 fixture commits' single trailer (no Jichi-* trailers).
SUP="Claude Opus 5.5 <noreply@anthropic.com>"
# L12/L13's changed paths (the brief's cell 30 fixture): one NEW file under
# docs/internal/ + one MODIFIED file under docs/internal/ (B's own
# docs/internal/base.txt, below). The reference's grep -v -q
# '^docs/internal/' accepts only when EVERY changed path is under
# docs/internal/ -- a modified file at the repo root would fall through.
# _lcdd6 BRANCH SUBJECT EXTRA -- one commit off B (author=committer=ADDR,
# the single Co-Authored-By SUP, no Jichi-* trailers) that ADDS one NEW file
# under docs/internal/ and MODIFIES B's docs/internal/base.txt, and, when
# EXTRA is non-empty, ALSO adds docs/analysis/dd6.txt (cell 31's path
# outside docs/internal/). The changed paths are exactly what `git
# diff-tree -r --name-only --no-commit-id <c^> <c>` reports.
_lcdd6() {
    _cb="$1"; _cs="$2"; _ce="${3:-}"
    git -C "$LREPO" checkout -q "$B"
    printf 'l%s inner n1\n' "$_cs" > "$LREPO/docs/internal/inner-$L12N.txt"
    printf 'l%s modified\n' "$_cs" > "$LREPO/docs/internal/base.txt"
    [ -n "$_ce" ] && { mkdir -p "$LREPO/docs/analysis"; printf 'l%s outside\n' "$_cs" > "$LREPO/docs/analysis/dd6.txt"; }
    git -C "$LREPO" add docs/internal/inner-$L12N.txt docs/internal/base.txt ${_ce:+docs/analysis/dd6.txt}
    GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
    GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
    GIT_AUTHOR_DATE="2026-11-09T00:00:12Z" GIT_COMMITTER_DATE="2026-11-09T00:00:12Z" \
    git -C "$LREPO" -c user.name="Ledger Smoke" -c user.email="$ADDR" \
    commit -q -m "$_cs" -m "Co-Authored-By: $SUP"
    git -C "$LREPO" checkout -q -b "$_cb"
    git -C "$LREPO" rev-parse HEAD
}
# L12: docs/internal/ only (one NEW file + one MODIFIED) -- the DD6 accept.
L12N=1
L12=$(_lcdd6 l12 "L12 dd6" "")
# L13: the same commit PLUS one changed file outside docs/internal/
# (docs/analysis/dd6.txt) -- the FALL-THROUGH guard (it must stay bad).
L12N=2
L13=$(_lcdd6 l13 "L13 dd6-outside" "docs/analysis/dd6.txt")

# The accept files: the driver captures each commit's FULL 40-hex id (which it
# cannot predict) and writes one id per line, the reference's ledger-accept shape.
printf '%s\n' "$L10" > "$tmp/accept10"
printf '%s\n' "$L11" > "$tmp/accept11"

# --- the driver's config (the host block names the ledger fields) -------------
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/dir/k","gitTokenFile":"$tmp/dir/t",
"drive":{"branchPrefix":"drive/test-r","gitName":"Ledger Smoke","gitEmail":"$ADDR"},
"host":{"pushHome":"$tmp","pushRepo":"$tmp/push.git","ledgerAddress":"$ADDR","ledgerAccept":"$tmp/accept10"}}
EOF
touch "$tmp/dir/k" "$tmp/dir/t"

# --- the verdict's sha8 tokens (real p-chain commits for the second count) ----
P0_8=$(printf '%.8s' "$P0"); P1_8=$(printf '%.8s' "$P1")
P2_8=$(printf '%.8s' "$P2"); P3_8=$(printf '%.8s' "$P3")
P4_8=$(printf '%.8s' "$P4"); P5_8=$(printf '%.8s' "$P5")
P6_8=$(printf '%.8s' "$P6"); P7_8=$(printf '%.8s' "$P7")
P9_8=$(printf '%.8s' "$P9")
VS="$P3_8"        # a generic sha8 for the verdict cells that do no second count
FAKE_MBC="deadbeef"   # a plausible 7-40-hex id that is NOT a commit (V9)

# --- the sweep.txt generator --------------------------------------------------
# _sweep FILE SHA8 MUTANTLINE N [NOSAN] [NOBUILD]
#   MUTANTLINE : the `smoke-mutant` line's tail (e.g. `(MUTANT_BASE=<mb>): mutant: OK`)
#   N          : the sweep's driver count
#   NOSAN=1    : omit the `unit san` line (V11)
#   NOBUILD=1  : omit the `build:` line (V2)
# The shape is the reference's sweep-*.txt fixture (build / sweep / smoke-mutant /
# unit san / unit o2 / container fetch_ssrf_reuse / container resolve_timeout_connect).
_sweep() {
    _sf="$1"; _ss="$2"; _sm="$3"; _sn="$4"; _snsan="${5:-}"; _snobuild="${6:-}"
    # T3 fix: write to $_sf. The 12 call sites pass a path with no `>` redirect,
    # so an unredirected body left every sweep.txt EMPTY (the content leaked to
    # the driver's stdout); V2 then "passed" only by substring-matching the
    # all-missing reason. The generator owns its file.
    {
    if [ -n "$_snobuild" ]; then :; else
        printf '  build: %s\n' "$_ss"
    fi
    printf 'sweep: ran %s drivers on %s; red: none\n' "$_sn" "$_ss"
    printf '  smoke-mutant rc=0 %s\n' "$_sm"
    if [ -n "$_snsan" ]; then :; else
        printf '  unit san rc=0 37482 checks, 0 failures\n'
    fi
    printf '  unit o2 rc=0 37482 checks, 0 failures\n'
    printf '  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok\n'
    printf '  container resolve_timeout_connect rc=0 5 ok, 0 not ok\n'
    printf 'records: %s/verify/sweep-%s\n' "$tmp" "$_ss"
    } > "$_sf"
}
# _vrun <sweep-file> <sha8> [push-repo] : run the verdict stage, capture rc/out.
_vrun() {
    _vf="$1"; _vs8="$2"; _vrepo="${3:-}"
    if [ -n "$_vrepo" ]; then
        "$HBIN" gate run --config "$tmp/cfg.json" --stage verdict "$_vf" "$_vs8" \
            --push-repo "$_vrepo" < /dev/null > "$tmp/vo" 2> "$tmp/ve"
    else
        "$HBIN" gate run --config "$tmp/cfg.json" --stage verdict "$_vf" "$_vs8" \
            < /dev/null > "$tmp/vo" 2> "$tmp/ve"
    fi
    _vrc=$?
}
_vfail() { # $1=cellnum  $2=why
    t_fail "$1: verdict stage not green: $2 (got rc=$_vrc: $(tr '\n' '|' < "$tmp/ve" | head_bytes 160))"
}

# _srun SHA8 EXTRA-ARGS... : run the sweep stage with the fixture's push.git
# (the cells' --work / --records / --bin / --build-run / ... flags), capture
# rc + out/err into $_sout / $_serr / $_src. The _vrun shape for the sweep.
_srun() {
    _ssha="$1"; shift
    "$HBIN" gate run --config "$tmp/cfg.json" --stage sweep "$_ssha" \
        "$@" < /dev/null > "$tmp/sout" 2> "$tmp/serr"
    _src=$?
    _sout=$(cat "$tmp/sout" 2>/dev/null)
    _serr=$(tr '\n' '|' < "$tmp/serr" | head_bytes 160)
}
_sfail() { # $1=cellnum  $2=why
    t_fail "$1: sweep stage not green: $2 (got rc=$_src: $_serr)"
}

# --- 1 (F0): the fixture guard -----------------------------------------------
# The push.git + history the ledger/verdict cells rely on is laid out and its
# ancestor relationships hold (the guard's shape from harness_gate_watch.sh cell 1).
if [ ! -x "$HBIN" ]; then
    t_fail "1: the harness binary $HBIN is not built (run \`make jichi-harness\` first) -- the fixture never ran"
else
    _g=1; _gm=""
    git -C "$tmp/push.git" rev-parse --verify -q "$P4" >/dev/null || { _g=0; _gm="$_gm P4 missing"; }
    git -C "$tmp/push.git" merge-base --is-ancestor "$P0" "$P2" || { _g=0; _gm="$_gm P0 not under P2"; }
    git -C "$tmp/push.git" merge-base --is-ancestor "$P2" "$P3" || { _g=0; _gm="$_gm P2 not under P3"; }
    git -C "$tmp/push.git" merge-base --is-ancestor "$P3" "$P4" || { _g=0; _gm="$_gm P3 not under P4"; }
    git -C "$tmp/push.git" rev-parse --verify -q "$P7" >/dev/null || { _g=0; _gm="$_gm P7 missing"; }
    git -C "$tmp/push.git" rev-parse --verify -q "$P9" >/dev/null || { _g=0; _gm="$_gm P9 missing"; }
    git -C "$tmp/push.git" merge-base --is-ancestor "$P6" "$P7" || { _g=0; _gm="$_gm P6 not under P7"; }
    git -C "$tmp/push.git" merge-base --is-ancestor "$P7" "$P9" || { _g=0; _gm="$_gm P7 not under P9"; }
    git -C "$tmp/push.git" rev-parse -q --verify refs/heads/vp7 | grep -qx "$P9" || { _g=0; _gm="$_gm vp7 not at P9"; }
    git -C "$tmp/push.git" show "$P0:scripts/mutant-sweep.sh" 2>/dev/null | grep -q "^EXCLUDE='faults faults_net" || { _g=0; _gm="$_gm P0's mutant-sweep.sh has no two-line table"; }
    git -C "$tmp/push.git" show "$P9:scripts/mutant-sweep.sh" 2>/dev/null | grep -qx "EXCLUDE=''" || { _g=0; _gm="$_gm P9's mutant-sweep.sh table is not emptied"; }
    git -C "$tmp/push.git" show "$P7:tests/smoke/smoke_lint.sh" 2>/dev/null | grep -q '"\$BIN"' || { _g=0; _gm="$_gm P7's smoke_lint driver does not invoke BIN (it must)"; }
    git -C "$tmp/push.git" rev-parse -q --verify refs/remotes/origin/master | grep -qx "$P0" || { _g=0; _gm="$_gm origin/master not at P0"; }
    [ -f "$LREPO/f" ] || { _g=0; _gm="$_gm ledger repo missing"; }
    git -C "$LREPO" rev-parse --verify -q "l11^{commit}" >/dev/null || { _g=0; _gm="$_gm l11 branch missing"; }
    git -C "$LREPO" rev-parse --verify -q "l9" >/dev/null || { _g=0; _gm="$_gm l9 branch missing"; }
    git -C "$LREPO" rev-parse --verify -q "l12^{commit}" >/dev/null || { _g=0; _gm="$_gm l12 branch missing"; }
    git -C "$LREPO" rev-parse --verify -q "l13^{commit}" >/dev/null || { _g=0; _gm="$_gm l13 branch missing"; }
    [ -f "$tmp/cfg.json" ] || { _g=0; _gm="$_gm cfg.json missing"; }
    [ -s "$tmp/accept10" ] && [ -s "$tmp/accept11" ] || { _g=0; _gm="$_gm accept files empty"; }
    # The p-chain's smoke drivers: P2 adds one that never invokes "$BIN", P4 one that does.
    git -C "$tmp/push.git" show "$P2:tests/smoke/nobin_lint.sh" 2>/dev/null | grep -q '"\$BIN"' && { _g=0; _gm="$_gm P2's nobin driver invokes BIN (it must not)"; }
    git -C "$tmp/push.git" show "$P4:tests/smoke/usesbin_lint.sh" 2>/dev/null | grep -q '"\$BIN"' || { _g=0; _gm="$_gm P4's usesbin driver does not invoke BIN (it must)"; }
    if [ "$_g" -eq 1 ]; then
        t_ok "1: the fixture -- a bare push.git with origin/master at P0 and the p-chain P0<P1<P2<P3<P4 (P2 adds a smoke driver that never invokes \"\$BIN\", P4 one that does) + a ledger repo with base B and branches l1..l13; the ancestor relationships and the config/accept files the cells rely on all hold"
    else
        t_fail "1: the git fixture is wrong:$_gm -- the cells that assert on it are void"
    fi
fi

# --- 2 (U1): bare `gate --config` -> exit 2, names run + units ---------------
"$HBIN" gate --config "$tmp/cfg.json" < /dev/null > "$tmp/u1.out" 2> "$tmp/u1.err"
_rc_u1=$?
if [ "$_rc_u1" -ne 2 ]; then
    t_fail "2: bare \`gate --config\` exited $_rc_u1 (want 2): $(tr '\n' ' ' < "$tmp/u1.err" | head_bytes 160)"
elif ! grep -qiE "run" "$tmp/u1.err" || ! grep -qiE "units" "$tmp/u1.err"; then
    t_fail "2: bare \`gate\` did not name BOTH \`run\` and \`units\`: $(tr '\n' '|' < "$tmp/u1.err" | head_bytes 160)"
else
    t_ok "2: bare \`gate --config cfg\` -> exit 2, naming \`run\` and \`units\`"
fi

# --- 3 (U2): `gate run` (no SHA8) -> exit 2 -----------------------------------
"$HBIN" gate --config "$tmp/cfg.json" run < /dev/null > "$tmp/u2.out" 2> "$tmp/u2.err"
_rc_u2=$?
if [ "$_rc_u2" -ne 2 ]; then
    t_fail "3: \`gate run\` (no SHA8) exited $_rc_u2 (want 2): $(tr '\n' ' ' < "$tmp/u2.err" | head_bytes 160)"
else
    t_ok "3: \`gate --config cfg run\` (the SHA8 missing) -> exit 2"
fi

# --- 4 (U3): `gate frob` (unknown sub) -> exit 2 ------------------------------
"$HBIN" gate --config "$tmp/cfg.json" frob < /dev/null > "$tmp/u3.out" 2> "$tmp/u3.err"
_rc_u3=$?
if [ "$_rc_u3" -ne 2 ]; then
    t_fail "4: \`gate frob\` (unknown subcommand) exited $_rc_u3 (want 2): $(tr '\n' ' ' < "$tmp/u3.err" | head_bytes 160)"
else
    t_ok "4: \`gate --config cfg frob\` (an unknown subcommand) -> exit 2"
fi

# --- 5 (U4): `gate run --stage ledger ...` with NO --ledger-address -> exit 2,
# naming the flag. The config carries NO ledgerAddress (a dedicated cfg), so
# the refusal is the REQUIRED-address arm (the note's CHANGED: the reference
# hard-codes ADDR; the port takes it). L1 (cell 7) is the same stage WITH the
# address: it expects the real `ledger: 2 commit(s), 0 bad` (T2, wave 1).
cat > "$tmp/cfg-noaddr.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/dir/k","gitTokenFile":"$tmp/dir/t",
"drive":{"branchPrefix":"drive/test-r","gitName":"Ledger Smoke","gitEmail":"$ADDR"},
"host":{"pushHome":"$tmp","pushRepo":"$tmp/push.git"}}
EOF
"$HBIN" gate run --config "$tmp/cfg-noaddr.json" --stage ledger "$LREPO" "$B" "$L1" \
    < /dev/null > "$tmp/u4.out" 2> "$tmp/u4.err"
_rc_u4=$?
if [ "$_rc_u4" -ne 2 ]; then
    t_fail "5: \`gate run --stage ledger\` (no --ledger-address, config has none) exited $_rc_u4 (want 2): $(tr '\n' ' ' < "$tmp/u4.err" | head_bytes 160)"
elif ! grep -qF -- "--ledger-address" "$tmp/u4.err"; then
    t_fail "5: \`gate run --stage ledger\` did not name --ledger-address: $(tr '\n' '|' < "$tmp/u4.err" | head_bytes 200)"
else
    t_ok "5: \`gate run --stage ledger <REPO> <BASE> <HEAD>\` (no --ledger-address, none in config) -> exit 2, names --ledger-address"
fi

# --- 6 (U5): an invalid --stage (the T3 re-purpose) -> exit 2, the stage
#             must be 'ledger', 'verdict', 'sweep', 'ci' or 'redfirst'
#             (got 'frob') ------
# T1's cell pinned the verdict's "not built yet" refusal; the body is built
# now (T3), so the cell keeps its usage shape but pins the INVALID --stage
# refusal main.c names (the T2 review's unpinned-refusal gap). T5 (wave 2)
# made sweep the THIRD named stage, T8 (wave 3) the ci the fourth, and T9
# (wave 3) the redfirst the fifth, so the message names all five (the byte
# pin moves WITH the message: this cell is GREEN today and must stay green
# after the dispatch-tail change).
"$HBIN" gate --config "$tmp/cfg.json" run --stage frob \
    < /dev/null > "$tmp/u5.out" 2> "$tmp/u5.err"
_rc_u5=$?
if [ "$_rc_u5" -ne 2 ]; then
    t_fail "6: \`gate run --stage frob\` exited $_rc_u5 (want 2): $(tr '\n' ' ' < "$tmp/u5.err" | head_bytes 160)"
elif ! grep -qF "the stage must be 'ledger', 'verdict', 'sweep', 'ci' or 'redfirst' (got 'frob')" "$tmp/u5.err"; then
    t_fail "6: \`gate run --stage frob\` did not name the five stages (got 'frob'): $(tr '\n' '|' < "$tmp/u5.err" | head_bytes 200)"
else
    t_ok "6: \`gate run --stage frob\` (an invalid stage) -> exit 2, \"the stage must be 'ledger', 'verdict', 'sweep', 'ci' or 'redfirst' (got 'frob')\""
fi

# --- 7-17 (L1-L11): the ledger stage (RED by construction) --------------------
# Each cell invokes the ledger stage with the contract's argv and asserts the
# reference-derived bytes (ledger.sh:24-27 + the accept-list behaviour, 2). The
# stage body is not built in this wave, so every cell is red ("not built yet").
# T2 builds the body and these go green. The <sha8> of the branch tip is
# computed from the captured full id.
# _ledrun <branch> [accept-file]: run the ledger stage for B..<branch>, capture
# rc/stdout/stderr into $tmp/lrc / $tmp/lo / $tmp/le.
_ledrun() {
    _lbr="$1"; _lac="${2:-}"
    if [ -n "$_lac" ]; then
        "$HBIN" gate run --config "$tmp/cfg.json" --stage ledger "$LREPO" "$B" "$_lbr" \
            --ledger-address "$ADDR" --ledger-accept "$_lac" < /dev/null > "$tmp/lo" 2> "$tmp/le"
    else
        "$HBIN" gate run --config "$tmp/cfg.json" --stage ledger "$LREPO" "$B" "$_lbr" \
            --ledger-address "$ADDR" < /dev/null > "$tmp/lo" 2> "$tmp/le"
    fi
    _lrc=$?
}
# A ledger cell is green when rc + the expected line(s) all match; red otherwise.
# The red reason here is the missing body, so a t_fail names the expected bytes
# that are absent.
_lfail() { # $1=cellnum  $2=why
    t_fail "$1: ledger stage not green: $2 (got rc=$_lrc: $(tr '\n' '|' < "$tmp/le" | head_bytes 160))"
}

# L1: clean 2-commit range -> "ledger: 2 commit(s), 0 bad", rc 0.
_ledrun l1
if [ "$_lrc" -eq 0 ] && grep -qF "ledger: 2 commit(s), 0 bad" "$tmp/lo"; then
    t_ok "7: L1 clean 2-commit range -> \"ledger: 2 commit(s), 0 bad\", rc 0"
else
    _lfail 7 "expected \"ledger: 2 commit(s), 0 bad\" rc 0"
fi

# L2: author wrong -> "<sha8> author=other@example.com", count "1 commit(s), 1 bad", rc 1.
_ledrun l2
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L2") author=$OTHER" "$tmp/lo" && grep -qF "ledger: 1 commit(s), 1 bad" "$tmp/lo"; then
    t_ok "8: L2 author wrong -> \"<sha8> author=$OTHER\", \"1 commit(s), 1 bad\", rc 1"
else
    _lfail 8 "expected \"<sha8> author=$OTHER\" + \"1 commit(s), 1 bad\" rc 1"
fi

# L3: committer wrong -> "<sha8> committer=other@example.com", rc 1.
_ledrun l3
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L3") committer=$OTHER" "$tmp/lo"; then
    t_ok "9: L3 committer wrong -> \"<sha8> committer=$OTHER\", rc 1"
else
    _lfail 9 "expected \"<sha8> committer=$OTHER\" rc 1"
fi

# L4: no Co-Authored-By -> "<sha8> no-Co-Authored-By", rc 1.
_ledrun l4
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L4") no-Co-Authored-By" "$tmp/lo"; then
    t_ok "10: L4 no Co-Authored-By -> \"<sha8> no-Co-Authored-By\", rc 1"
else
    _lfail 10 "expected \"<sha8> no-Co-Authored-By\" rc 1"
fi

# L5: Co-Authored-By wrong -> "<sha8> co-authored-by=[jichi 0.13.0 <other@example.com>]", rc 1.
_ledrun l5
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L5") co-authored-by=[jichi 0.13.0 <$OTHER>]" "$tmp/lo"; then
    t_ok "11: L5 Co-Authored-By wrong -> \"<sha8> co-authored-by=[jichi 0.13.0 <$OTHER>]\", rc 1"
else
    _lfail 11 "expected \"<sha8> co-authored-by=[jichi 0.13.0 <$OTHER>]\" rc 1"
fi

# L6: no Jichi-Build -> "<sha8> no-Jichi-Build", rc 1.
_ledrun l6
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L6") no-Jichi-Build" "$tmp/lo"; then
    t_ok "12: L6 no Jichi-Build -> \"<sha8> no-Jichi-Build\", rc 1"
else
    _lfail 12 "expected \"<sha8> no-Jichi-Build\" rc 1"
fi

# L7: no Jichi-Model -> "<sha8> no-Jichi-Model", rc 1.
_ledrun l7
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L7") no-Jichi-Model" "$tmp/lo"; then
    t_ok "13: L7 no Jichi-Model -> \"<sha8> no-Jichi-Model\", rc 1"
else
    _lfail 13 "expected \"<sha8> no-Jichi-Model\" rc 1"
fi

# L8: priced model -> "<sha8> priced-model=anthropic/claude-x", rc 1.
_ledrun l8
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L8") priced-model=$PRICED" "$tmp/lo"; then
    t_ok "14: L8 priced model -> \"<sha8> priced-model=$PRICED\", rc 1"
else
    _lfail 14 "expected \"<sha8> priced-model=$PRICED\" rc 1"
fi

# L9: empty range (BASE==HEAD) -> "ledger: 0 commit(s), 0 bad", rc 1 (N must be > 0).
_ledrun l9
if [ "$_lrc" -eq 1 ] && grep -qF "ledger: 0 commit(s), 0 bad" "$tmp/lo"; then
    t_ok "15: L9 empty range -> \"ledger: 0 commit(s), 0 bad\", rc 1"
else
    _lfail 15 "expected \"ledger: 0 commit(s), 0 bad\" rc 1"
fi

# L10: accept-listed, missing-trailers-only -> "<sha8> accepted: no-Jichi-Build no-Jichi-Model (L10)",
# count excludes it (0 bad), rc 0.
_ledrun l10 "$tmp/accept10"
if [ "$_lrc" -eq 0 ] && grep -qF "$(printf '%.8s' "$L10") accepted: no-Jichi-Build no-Jichi-Model (L10)" "$tmp/lo" && grep -qF "ledger: 1 commit(s), 0 bad" "$tmp/lo"; then
    t_ok "16: L10 accept-listed missing-trailers-only -> \"<sha8> accepted: no-Jichi-Build no-Jichi-Model (L10)\", count 0 bad, rc 0"
else
    _lfail 16 "expected \"<sha8> accepted: no-Jichi-Build no-Jichi-Model (L10)\" + \"1 commit(s), 0 bad\" rc 0"
fi

# L11: accept-listed but wrong ADDRESS (a non-trailer reason) -> stays bad:
# "<sha8> author=other@example.com", rc 1.
_ledrun l11 "$tmp/accept11"
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L11") author=$OTHER" "$tmp/lo" && grep -qF "ledger: 1 commit(s), 1 bad" "$tmp/lo"; then
    t_ok "17: L11 accept-listed wrong address -> stays bad \"<sha8> author=$OTHER\", \"1 commit(s), 1 bad\", rc 1"
else
    _lfail 17 "expected \"<sha8> author=$OTHER\" (accepted does not exempt a non-trailer reason) + \"1 commit(s), 1 bad\" rc 1"
fi

# --- 18-31 (V1-V14): the verdict stage (M1187-b, the prepared second count) -----
# Each cell builds a sweep.txt (the reference's shape) + the push repo the second
# count reads, invokes the verdict stage, and asserts the reference-derived bytes
# (sweep-verdict.sh:18-58). The body is not built in this wave, so every cell is
# red ("not built yet"). T3 builds the body and these go green.

# V1: clean -> "green", rc 0 (mutant: OK).
_sweep "$tmp/sv1.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 99
_vrun "$tmp/sv1.txt" "$VS"
if [ "$_vrc" -eq 0 ] && grep -qxF "green" "$tmp/vo"; then
    t_ok "18: V1 clean -> \"green\", rc 0 (build line, ran N>0 red: none, mutant: OK, both unit rc=0 0 failures, both container ok)"
else
    _vfail 18 "expected \"green\" rc 0 (mutant: OK)"
fi

# V2: missing build line -> "red: missing or not clean: the build line for <s>", rc 1.
# The NOBUILD flag is the 6th positional (5 is NOSAN -- V11's `1`); a bare `1`
# here omitted the SAN suite instead of the build line.
_sweep "$tmp/sv2.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 99 "" 1
_vrun "$tmp/sv2.txt" "$VS"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: the build line for $VS" "$tmp/vo"; then
    t_ok "19: V2 missing build line -> \"red: missing or not clean: the build line for $VS\", rc 1"
else
    _vfail 19 "expected \"red: missing or not clean: the build line for $VS\" rc 1"
fi

# V3: "sweep: ran 0 drivers" -> red (the lint/docs sweep name), rc 1.
_sweep "$tmp/sv3.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 0
_vrun "$tmp/sv3.txt" "$VS"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: the lint/docs sweep" "$tmp/vo"; then
    t_ok "20: V3 \"sweep: ran 0 drivers\" -> \"red: missing or not clean: the lint/docs sweep\", rc 1"
else
    _vfail 20 "expected \"red: missing or not clean: the lint/docs sweep\" rc 1"
fi

# V4: FV-832c7930 (green via the second count; mb=P2, s=P3: diff = {g}, 0
# smoke scripts). M1187-b: the prepared green note names the EXCLUDE table.
_sweep "$tmp/sv4.txt" "$P3_8" "(MUTANT_BASE=$P2_8): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv4.txt" "$P3_8" "$tmp/push.git"
if [ "$_vrc" -eq 0 ] && grep -qF "green (smoke-mutant had nothing to sweep: of the 1 file(s) changed from $P2_8 to $P3_8, none of the 0 smoke script(s) is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE table), counted again by git)" "$tmp/vo"; then
    t_ok "21: V4 FV-832c7930 -> green via the second count (of the 1 file(s) changed from $P2_8 to $P3_8, none of the 0 smoke script(s) is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE table), counted again by git), rc 0"
else
    _vfail 21 "expected the FV-832c7930 green note (none of the 0 smoke script(s)) rc 0"
fi

# V5: FV-9ada7cd7 (green via the second count; mb=P1, s=P2: diff = {nobin}, 1
# smoke script, none invokes "$BIN").
_sweep "$tmp/sv5.txt" "$P2_8" "(MUTANT_BASE=$P1_8): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv5.txt" "$P2_8" "$tmp/push.git"
if [ "$_vrc" -eq 0 ] && grep -qF "green (smoke-mutant had nothing to sweep: of the 1 file(s) changed from $P1_8 to $P2_8, none of the 1 smoke script(s) is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE table), counted again by git)" "$tmp/vo"; then
    t_ok "22: V5 FV-9ada7cd7 -> green via the second count (of the 1 file(s) changed from $P1_8 to $P2_8, none of the 1 smoke script(s) is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE table), counted again by git), rc 0"
else
    _vfail 22 "expected the FV-9ada7cd7 green note (none of the 1 smoke script(s)) rc 0"
fi

# V6: the second count disagrees (mb=P2, s=P4: diff = {g,usesbin}; the changed
# driver usesbin_lint.sh DOES invoke "$BIN" and is NOT in the table). M1187-b:
# the re-worded reason adds "and are not in mutant-sweep.sh's EXCLUDE table".
_sweep "$tmp/sv6.txt" "$P4_8" "(MUTANT_BASE=$P2_8): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv6.txt" "$P4_8" "$tmp/push.git"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant (it swept no driver, but git counts 1 changed smoke driver(s) that invoke the binary and are not in mutant-sweep.sh's EXCLUDE table, of 1 changed smoke script(s) in 2 changed file(s) from $P2_8 to $P4_8)" "$tmp/vo"; then
    t_ok "23: V6 second-count disagrees -> \"red: ... git counts 1 changed smoke driver(s) that invoke the binary and are not in mutant-sweep.sh's EXCLUDE table, of 1 changed smoke script(s) in 2 changed file(s) from $P2_8 to $P4_8)\", rc 1"
else
    _vfail 23 "expected the V6 disagreement red reason (1 driver invokes the binary, not in the table) rc 1"
fi

# V7 (M1187-b, re-pointed): the REL3 shape, now GREEN. The mutant line says
# (1 excluded) (the prepared fix accepts any count), s=P7: diff P6..P7 =
# {smoke_lint.sh}; smoke_lint invokes "$BIN" AND its name is in the table ->
# nx=1 == nxm, nd=0 -> the green note. This is the byte the host printed
# false on at 2026-11-14 06:00 (the old (0 excluded) literal rejected the
# line, a missing arm, a silent red).
_sweep "$tmp/sv7.txt" "$P7_8" "(MUTANT_BASE=$P6_8): mutant: no changed drivers to sweep (1 excluded)" 99
_vrun "$tmp/sv7.txt" "$P7_8" "$tmp/push.git"
if [ "$_vrc" -eq 0 ] && grep -qF "green (smoke-mutant had nothing to sweep: of the 1 file(s) changed from $P6_8 to $P7_8, none of the 1 smoke script(s) is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE table), counted again by git)" "$tmp/vo"; then
    t_ok "24: V7 the REL3 shape (1 excluded, the only changed driver IS in the table) -> green (of the 1 file(s) changed from $P6_8 to $P7_8, none of the 1 smoke script(s) is swept by mutant-sweep.sh ... counted again by git), rc 0"
else
    _vfail 24 "expected the V7 REL3 green note (nx=1 == nxm=1) rc 0"
fi

# V8: the smoke-mutant line is rc=1 (not rc=0) -> red (smoke-mutant), rc 1.
_sweep "$tmp/sv8.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 99
# Portable in-place edit (no `sed -i`): rewrite the smoke-mutant line rc=0 -> rc=1.
_s8="$tmp/sv8.txt"
awk '{ sub(/^  smoke-mutant rc=0 /, "  smoke-mutant rc=1 "); print }' "$_s8" > "$_s8.tmp" && mv "$_s8.tmp" "$_s8"
_vrun "$tmp/sv8.txt" "$VS"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant" "$tmp/vo"; then
    t_ok "25: V8 smoke-mutant rc=1 -> \"red: missing or not clean: smoke-mutant\", rc 1"
else
    _vfail 25 "expected \"red: missing or not clean: smoke-mutant\" rc 1"
fi

# V9: MUTANT_BASE=<not-a-commit> -> git diff fails -> the prepared
# git-failure reason (M1187-b: the third counting child, the show of
# scripts/mutant-sweep.sh, adds the middle clause), rc 1.
_sweep "$tmp/sv9.txt" "$P3_8" "(MUTANT_BASE=$FAKE_MBC): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv9.txt" "$P3_8" "$tmp/push.git"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant (it swept no driver, and git could not count the batch's files, or read scripts/mutant-sweep.sh, from $FAKE_MBC to $P3_8)" "$tmp/vo"; then
    t_ok "26: V9 MUTANT_BASE=$FAKE_MBC (not a commit) -> \"red: ... git could not count the batch's files, or read scripts/mutant-sweep.sh, from $FAKE_MBC to $P3_8)\", rc 1"
else
    _vfail 26 "expected the V9 git-count-failure red reason (the prepared, with the show clause) rc 1"
fi

# V10: empty batch (mb=P5, s=P5: git counts 0 files, nall=0) -> the prepared
# else arm ("no changed file", singular; the old "in 0 changed file(s)" text
# is gone), rc 1.
_sweep "$tmp/sv10.txt" "$P5_8" "(MUTANT_BASE=$P5_8): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv10.txt" "$P5_8" "$tmp/push.git"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant (it swept no driver, and git counts no changed file from $P5_8 to $P5_8)" "$tmp/vo"; then
    t_ok "27: V10 empty batch (mb=s=$P5_8, git counts 0 files) -> red (smoke-mutant, \"git counts no changed file from $P5_8 to $P5_8)\", rc 1"
else
    _vfail 27 "expected the V10 empty-batch red reason (no changed file, singular) rc 1"
fi

# V11: the "unit san" line is missing -> red (the SAN unit suite), rc 1.
_sweep "$tmp/sv11.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 99 1
_vrun "$tmp/sv11.txt" "$VS"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: the SAN unit suite" "$tmp/vo"; then
    t_ok "28: V11 missing \"unit san\" line -> \"red: missing or not clean: the SAN unit suite\", rc 1"
else
    _vfail 28 "expected \"red: missing or not clean: the SAN unit suite\" rc 1"
fi

# V12: a "  RED <b> rc=1" line anywhere -> red, "flagged: <first-three, cut-120, newline->|>", rc 1.
_sweep "$tmp/sv12.txt" "$VS" "(MUTANT_BASE=$P0_8): mutant: OK" 99
printf '  RED tests/smoke/whatever.sh rc=1 some error text\n' >> "$tmp/sv12.txt"
_vrun "$tmp/sv12.txt" "$VS"
if [ "$_vrc" -eq 1 ] && grep -qF "flagged:   RED tests/smoke/whatever.sh rc=1 some error text" "$tmp/vo" && grep -qF "red:" "$tmp/vo"; then
    t_ok "29: V12 a \"  RED <b> rc=1\" line -> \"red: ... flagged: <the RED line>\", rc 1"
else
    _vfail 29 "expected \"red: ... flagged: <the RED line>\" rc 1"
fi

# V13 (M1187-b, NEW): the cross-check red. The SAME sweep as V7 (mb=P6, s=P7:
# diff = {smoke_lint.sh}, nx=1, nd=0) but the tail says (2 excluded) -- the
# mutant claims 2, git finds nx=1. Falls through the green guard to the
# cross-check arm. This pins the nx != nxm guard (a count mismatch is red,
# never a silent green) and the prepared cross-check reason.
_sweep "$tmp/sv13.txt" "$P7_8" "(MUTANT_BASE=$P6_8): mutant: no changed drivers to sweep (2 excluded)" 99
_vrun "$tmp/sv13.txt" "$P7_8" "$tmp/push.git"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant (it reports 2 excluded, but git counts 1 changed smoke driver(s) that invoke the binary and are in mutant-sweep.sh's EXCLUDE table, from $P6_8 to $P7_8)" "$tmp/vo"; then
    t_ok "30: V13 the cross-check red (the line says (2 excluded), git counts nx=1) -> \"red: ... it reports 2 excluded, but git counts 1 changed smoke driver(s) that invoke the binary and are in mutant-sweep.sh's EXCLUDE table, from $P6_8 to $P7_8)\", rc 1"
else
    _vfail 30 "expected the V13 cross-check red reason (reports 2, git counts 1) rc 1"
fi

# V14 (M1187-b, NEW): the empty-table red. mb=P7, s=P9: diff = {scripts/
# mutant-sweep.sh} (the table rewrite, NO smoke script) -> nall=1, nsm=0,
# nd=0, nx=0, nxm=0 -- the counts WOULD be green -- but the table AT P9 reads
# empty (EXCLUDE=''). The empty-table arm sits ABOVE the green guard, so red.
# This pins that a table that reads as empty never yields a silent green.
_sweep "$tmp/sv14.txt" "$P9_8" "(MUTANT_BASE=$P7_8): mutant: no changed drivers to sweep (0 excluded)" 99
_vrun "$tmp/sv14.txt" "$P9_8" "$tmp/push.git"
if [ "$_vrc" -eq 1 ] && grep -qF "red: missing or not clean: smoke-mutant (it swept no driver, and the EXCLUDE table of scripts/mutant-sweep.sh at $P9_8 read as empty)" "$tmp/vo"; then
    t_ok "31: V14 the empty-table red (the counts would be green, but the table at s reads empty) -> \"red: ... the EXCLUDE table of scripts/mutant-sweep.sh at $P9_8 read as empty\", rc 1"
else
    _vfail 31 "expected the V14 empty-table red reason (table at $P9_8 read as empty) rc 1"
fi

# --- 32-33 (L12-L13): the DD6 supervisor-commit shape (M1154 T4) -------------
# The host's ledger.sh accepts ONE more shape than the 2026-10-27 reference
# T2 ported: DD6, the supervisor's docs/internal-only commit (author AND
# committer the operator, the single Co-Authored-By exactly SUP, every
# changed path under docs/internal/), printed as accepted, never counted.
# Cell 30 is RED FIRST against T2's body (it prints the co-authored-by
# reason, rc 1); cell 31 is the FALL-THROUGH guard -- the same trailer with
# one path OUTSIDE docs/internal/ (it was already green; it must stay green,
# pinning that the accept arm cannot over-fire).

# L12: the DD6 accept (docs/internal/ only: one new file + one modified).
_ledrun l12
if [ "$_lrc" -eq 0 ] && grep -qF "$(printf '%.8s' "$L12") accepted: the supervisor's commit (Claude's trailer, docs/internal/ only; DD6)" "$tmp/lo" && grep -qF "ledger: 1 commit(s), 0 bad" "$tmp/lo"; then
    t_ok "32: L12 DD6 supervisor commit (docs/internal/ only, SUP trailer, operator author+committer) -> \"<sha8> accepted: the supervisor's commit (Claude's trailer, docs/internal/ only; DD6)\", \"1 commit(s), 0 bad\", rc 0"
else
    _lfail 32 "expected \"<sha8> accepted: the supervisor's commit (Claude's trailer, docs/internal/ only; DD6)\" + \"1 commit(s), 0 bad\" rc 0"
fi

# L13: the same trailer with one changed path OUTSIDE docs/internal/
# (docs/analysis/dd6.txt) -> STILL bad: the normal co-authored-by reason.
_ledrun l13
if [ "$_lrc" -eq 1 ] && grep -qF "$(printf '%.8s' "$L13") co-authored-by=[$SUP] no-Jichi-Build no-Jichi-Model" "$tmp/lo" && grep -qF "ledger: 1 commit(s), 1 bad" "$tmp/lo"; then
    t_ok "33: L13 DD6 trailer but one path outside docs/internal/ -> stays bad \"<sha8> co-authored-by=[$SUP] no-Jichi-Build no-Jichi-Model\", \"1 commit(s), 1 bad\", rc 1"
else
    _lfail 33 "expected \"<sha8> co-authored-by=[$SUP] no-Jichi-Build no-Jichi-Model\" + \"1 commit(s), 1 bad\" rc 1 (the accept arm must not over-fire)"
fi

# --- 34-44 (S1-S11): the sweep stage (M1154 T5, wave 2) -----------------------
# The brief's S-cells: the pre-gate sweep on exactly P6 in a scratch clone of
# the fixture's bare push.git (origin/master at P0 -- the MUTANT_BASE). The
# build / mutant / unit / container commands are the brief's stubs (the
# synthetic repo has no Makefile; the sweep's lines are the STUBS' output,
# assembled by the pure line-assembly the unit table pins). P6 (the sweep's
# commit, added to the p-chain above) has BOTH lint drivers invoking "$BIN"
# (the brief's S3: a failing --bin stub -> both drivers red). The cells are
# RED first: the committed binary's --stage check names only ledger|verdict,
# so --stage sweep exits 2 before any of the --work/... flags are even seen.
_sbin() { # $1=path  then the stub's body lines: a #!/bin/sh driver loop bin.
    _sp="$1"; shift
    { printf '#!/bin/sh\n'
      while [ "$#" -gt 0 ]; do printf '%s\n' "$1"; shift; done
    } > "$_sp"
    chmod +x "$_sp"
}
_sstub() { # the S1 clean bin: prints one line and exits 0.
    _sbin "$tmp/sbin" 'echo "bin ran $@"' 'exit 0'
}
_smokeline() { # the brief's S1 mutant stub: the reference's OK line.
    echo "mutant: OK (1 drivers swept, 0 excluded)"
}
_mokstub() { # --mutant-run: the S1 mutant stub's command.
    echo "(mutant stub)"; _smokeline
}
_s1c() { # the S1 container stub: the reference's two green lines.
    echo "  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok"
    echo "  container resolve_timeout_connect rc=0 5 ok, 0 not ok"
}
_sfull() { # the S1/S3/S4/S6/S7 flag set: the build/mutant/unit/container stubs
    # that echo the reference's lines. $1=bin-path $2=work $3=records
    # $4..extra flags (the cell's overrides: --mutant-run / --unit-run-* /
    # --container-run / --build-run).
    _sb="$1"; _sw="$2"; _sr="$3"; shift 3
    _srun "$P6_8" --work "$_sw" --records "$_sr" --bin "$_sb" \
        --build-run "true" --mutant-run "echo '(mutant stub)'; echo 'mutant: OK (1 drivers swept, 0 excluded)'" \
        --unit-run-san "true" --unit-run-o2 "true" --container-run "echo '  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok'; echo '  container resolve_timeout_connect rc=0 5 ok, 0 not ok'" \
        --push-repo "$tmp/push.git" "$@"
}

# S1 (32): the clean full sweep (the brief's S1 + S1b's work-tree assert,
# folded -- the brief's S1b is an assertion ON S1's run, not a separate cell:
# the driver's 11 numbered cells 32-42 are S1..S11).
_sstub
_sfull "$tmp/sbin" "$tmp/sw1" "$tmp/sr1"
if [ "$_src" -eq 0 ] && grep -qF "  build: $P6_8" "$tmp/sout" \
    && grep -qF "sweep: ran 2 drivers on $P6_8; red: none" "$tmp/sout" \
    && grep -qF "  smoke-mutant rc=0 (MUTANT_BASE=$P0_8): mutant: OK (1 drivers swept, 0 excluded)" "$tmp/sout" \
    && grep -qF "  unit san rc=0 NO SUMMARY (build failed or aborted)" "$tmp/sout" \
    && grep -qF "  unit o2 rc=0 NO SUMMARY (build failed or aborted)" "$tmp/sout" \
    && grep -qF "  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok" "$tmp/sout" \
    && grep -qF "  container resolve_timeout_connect rc=0 5 ok, 0 not ok" "$tmp/sout" \
    && grep -qF "records: $tmp/sr1" "$tmp/sout" \
    && [ -d "$tmp/sw1/j" ] && [ "$(git -C "$tmp/sw1/j" rev-parse HEAD 2>/dev/null)" = "$P6" ]; then
    t_ok "34: S1 clean full sweep: \"  build: $P6_8\", \"sweep: ran 2 drivers on $P6_8; red: none\", the mutant line (MUTANT_BASE=$P0_8), the two NO SUMMARY unit lines (the stubs print no summary), the two container lines, \"records: $tmp/sr1\", rc 0; S1b the work tree <work>/j is a clone of push.git at P6 (rev-parse HEAD == $P6)"
else
    _sfail 34 "expected the clean sweep's lines (build $P6_8 / red: none / the stubs' mutant + NO SUMMARY unit + container lines / records) + the <work>/j clone at P6 rc 0"
fi

# S2 (33): the build failure -> build failed: INVALID, the stage ends, rc 1.
_sfull "$tmp/sbin" "$tmp/sw2" "$tmp/sr2" --build-run "false"
if [ "$_src" -eq 1 ] && grep -qF "build failed: INVALID" "$tmp/sout" \
    && ! grep -qF "sweep: ran" "$tmp/sout" && ! grep -qF "smoke-mutant" "$tmp/sout" \
    && ! grep -qF "records:" "$tmp/sout"; then
    t_ok "35: S2 the build failed: \"build failed: INVALID\", no driver loop / count / mutant / records line, rc 1"
else
    _sfail 35 "expected \"build failed: INVALID\" + no loop/count/mutant/records rc 1"
fi

# S3 (34): the driver loop's red count (the brief's S3: the failing --bin stub
# -> BOTH drivers red, the count line's space-joined basenames, the two RED
# lines + each driver's not-ok excerpt; the sweep COMPLETES, rc 0).
_sbin "$tmp/sbin3" 'echo "not ok 1 - the bin failed"' 'exit 1'
_sfull "$tmp/sbin3" "$tmp/sw3" "$tmp/sr3"
if [ "$_src" -eq 0 ] && grep -qF "sweep: ran 2 drivers on $P6_8; red: nobin_lint usesbin_lint" "$tmp/sout" \
    && grep -qF "  RED nobin_lint rc=1" "$tmp/sout" \
    && grep -qF "  RED usesbin_lint rc=1" "$tmp/sout" \
    && grep -qF "not ok 1 - the bin failed" "$tmp/sout"; then
    t_ok "36: S3 the driver loop's red count: \"red: nobin_lint usesbin_lint\" (both drivers red), the two \"  RED <b> rc=1\" lines + the not-ok excerpt, rc 0 (the red count is the stage's line, not its rc)"
else
    _sfail 36 "expected \"red: nobin_lint usesbin_lint\" + the two RED lines rc 0"
fi

# S4 (35): the mutant line's first-`mutant:` extract (the brief's S4).
_sfull "$tmp/sbin" "$tmp/sw4" "$tmp/sr4" \
    --mutant-run "echo noise; echo 'mutant: OK (1 drivers swept, 0 excluded)'; echo more"
if [ "$_src" -eq 0 ] && grep -qF "  smoke-mutant rc=0 (MUTANT_BASE=$P0_8): mutant: OK (1 drivers swept, 0 excluded)" "$tmp/sout" \
    && ! grep -qF "noise" "$tmp/sout" && ! grep -qF "more" "$tmp/sout"; then
    t_ok "37: S4 the mutant line's first-^mutant: extract (the noise / more lines are not in the sweep's output)"
else
    _sfail 37 "expected the mutant line with only the first ^mutant: line"
fi

# S5 (36): no origin/master in the push.git -> the INVALID line, rc 1. The cell
# makes a SECOND bare push.git: a plain bare clone of the p-chain carries
# refs/heads/* (master -> P6, so the sweep's clone + checkout P6_8 succeed)
# but NO refs/remotes/origin/master (the fixture's push.git adds it explicitly
# for the verdict cells; this one does not) -- the mutant stage's MUTANT_BASE
# read (git rev-parse --verify -q refs/remotes/origin/master) comes back empty.
git clone -q --bare "$PCHAIN" "$tmp/push5.git"
_sfull "$tmp/sbin" "$tmp/sw5" "$tmp/sr5" --push-repo "$tmp/push5.git"
if [ "$_src" -eq 1 ] && grep -qF "  smoke-mutant: no origin/master in push.git: INVALID" "$tmp/sout"; then
    t_ok "38: S5 no origin/master in push.git: \"  smoke-mutant: no origin/master in push.git: INVALID\", rc 1 (the stage ended)"
else
    _sfail 38 "expected the no-origin/master INVALID line rc 1"
fi

# S6 (37): the unit summary arm (the brief's S6: the stubs print the
# `N checks, M failures` line; the o2 stub exits 1 -> rc=1).
_sfull "$tmp/sbin" "$tmp/sw6" "$tmp/sr6" \
    --unit-run-san "echo '7 checks, 0 failures'" \
    --unit-run-o2 "sh -c 'echo 9 checks, 1 failures; exit 1'"
if [ "$_src" -eq 0 ] && grep -qF "  unit san rc=0 7 checks, 0 failures" "$tmp/sout" \
    && grep -qF "  unit o2 rc=1 9 checks, 1 failures" "$tmp/sout"; then
    t_ok "39: S6 the unit summary arm: \"  unit san rc=0 7 checks, 0 failures\" + \"  unit o2 rc=1 9 checks, 1 failures\" (the o2 stub exits 1 -> rc=1), rc 0"
else
    _sfail 39 "expected the two unit lines with the stubs' summaries"
fi

# S7 (38): the container lines as the stub prints them (the brief's S7).
_sfull "$tmp/sbin" "$tmp/sw7" "$tmp/sr7" \
    --container-run "echo '  container fetch_ssrf_reuse rc=0 3 ok, 0 not ok'; echo '  container resolve_timeout_connect rc=1 2 ok, 1 not ok'"
if [ "$_src" -eq 0 ] && grep -qF "  container fetch_ssrf_reuse rc=0 3 ok, 0 not ok" "$tmp/sout" \
    && grep -qF "  container resolve_timeout_connect rc=1 2 ok, 1 not ok" "$tmp/sout"; then
    t_ok "40: S7 the container line's ok/not-ok counts (the stub's lines as the sweep prints them), rc 0"
else
    _sfail 40 "expected the stub's two container lines"
fi

# S8 (39): the records line is the sweep's LAST line (the brief's S8, after S1).
_sfull "$tmp/sbin" "$tmp/sw8" "$tmp/sr8"
if [ "$_src" -eq 0 ] && [ "$(tail -1 "$tmp/sout")" = "records: $tmp/sr8" ]; then
    t_ok "41: S8 the records line: the last line of the sweep output is \"records: $tmp/sr8\""
else
    _sfail 41 "expected the records line as the output's last line"
fi

# S9 (40): the --stage sweep positional count (no <SHA8> -> exit 2).
"$HBIN" gate run --config "$tmp/cfg.json" --stage sweep < /dev/null \
    > "$tmp/sout" 2> "$tmp/serr"
_s9rc=$?
if [ "$_s9rc" -eq 2 ] && grep -qF "needs a <SHA8>" "$tmp/serr"; then
    t_ok "42: S9 --stage sweep with no <SHA8>: exit 2 naming \"needs a <SHA8>\""
else
    t_fail "42: S9 expected exit 2 naming \"needs a <SHA8>\" (got rc=$_s9rc: $(tr '\n' '|' < "$tmp/serr" | head_bytes 160))"
fi

# S10 (41): the --push-repo requirement (no --push-repo, none in the config).
# The driver's cfg.json names host.pushRepo, so a copy WITHOUT the key (a
# surgical sed: the cfg is one line, a grep -v would delete the whole line)
# is the cell's config.
sed 's/"pushRepo":"[^"]*",//; s/,"pushRepo":"[^"]*"//' "$tmp/cfg.json" > "$tmp/cfg-s10.json"
"$HBIN" gate run --config "$tmp/cfg-s10.json" --stage sweep "$P6_8" \
    < /dev/null > "$tmp/sout" 2> "$tmp/serr"
_s10rc=$?
if [ "$_s10rc" -eq 2 ] && grep -qF -- "--push-repo" "$tmp/serr"; then
    t_ok "43: S10 --stage sweep with no --push-repo (and none in the config): exit 2 naming --push-repo"
else
    t_fail "43: S10 expected exit 2 naming --push-repo (got rc=$_s10rc: $(tr '\n' '|' < "$tmp/serr" | head_bytes 160))"
fi

# S11 (42): KEYS UNSET in the child env (the 2026-11-01 incident, sweep.sh:8).
# The cell's --bin stub prints its own JICHI_API_KEY (${JICHI_API_KEY:-ABSENT});
# the cell exports JICHI_API_KEY=canary in the DRIVER's environment (the gate
# process inherits it) and asserts the sweep's driver-loop output does NOT
# contain `canary` -- the child env scrubbed it (the driver's log carries the
# ABSENT fallback instead).
_sbin "$tmp/sbin11" 'echo "JICHI_API_KEY=${JICHI_API_KEY:-ABSENT}"' 'exit 0'
export JICHI_API_KEY=canary
_sfull "$tmp/sbin11" "$tmp/sw11" "$tmp/sr11"
_rc11=$_src
_s11leak=0; _s11absent=0
for _s11f in "$tmp/sr11/nobin_lint.txt" "$tmp/sr11/usesbin_lint.txt"; do
    [ -f "$_s11f" ] || continue
    _s11leak=$(( _s11leak + $(grep -c "canary" "$_s11f") ))
    _s11absent=$(( _s11absent + $(grep -c "JICHI_API_KEY=ABSENT" "$_s11f") ))
done
unset JICHI_API_KEY
if [ "$_rc11" -eq 0 ] && [ "$_s11leak" -eq 0 ] && [ "$_s11absent" -ge 2 ]; then
    t_ok "44: S11 KEYS UNSET: the gate ran with JICHI_API_KEY=canary exported; the driver-loop child env scrubbed it (the two driver logs have no canary, both show the ABSENT fallback), rc 0"
else
    t_fail "44: S11 the driver's child env must have scrubbed JICHI_API_KEY (canary count=$_s11leak, ABSENT count=$_s11absent, rc=$_rc11)"
fi

# S12 (43): F3 clean -- the sweep's build stub leaves the clone CLEAN, so the
# reference's sweep.sh:27 (`git status --short | head -3`) prints NOTHING: the
# count line is immediately followed by the mutant line, no `?? `/` M ` line
# between them. (The T5 fixture: the `true` build-run touches no file.)
_sfull "$tmp/sbin" "$tmp/sw12" "$tmp/sr12"
_s12mid=$(grep -A1 "^sweep: ran 2 drivers on $P6_8; red: none$" "$tmp/sout" | tail -1)
_s12mut=$(grep -c '^  smoke-mutant rc=0' "$tmp/sout")
if [ "$_src" -eq 0 ] && [ "$_s12mid" = "  smoke-mutant rc=0 (MUTANT_BASE=$P0_8): mutant: OK (1 drivers swept, 0 excluded)" ] \
    && [ "$_s12mut" -eq 1 ] \
    && [ "$(grep -c '?? artifact' "$tmp/sout")" -eq 0 ]; then
    t_ok "45: S12 F3 clean: the count line is immediately followed by the mutant line (a clean clone's git status --short | head -3 prints nothing between them), rc 0"
else
    t_fail "45: S12 expected the count line directly followed by the mutant line (no git status lines in a clean clone); got next line: $_s12mid (rc=$_src)"
fi

# S13 (44): F3 artifacts -- the build stub leaves FOUR untracked files in the
# clone; the sweep output prints exactly the FIRST THREE `git status --short`
# lines (the reference's sweep.sh:27 `| head -3`, as-is / bare, no label)
# between the count line and the mutant line; the fourth is NOT printed.
# git status --short lists untracked files in directory order, so the first
# three are artifact1..3 (C collation).
_sfull "$tmp/sbin" "$tmp/sw13" "$tmp/sr13" \
    --build-run "true; touch '$tmp/sw13/j/artifact1' '$tmp/sw13/j/artifact2' '$tmp/sw13/j/artifact3' '$tmp/sw13/j/artifact4'"
_s13mid=$(grep -A3 "^sweep: ran 2 drivers on $P6_8; red: none$" "$tmp/sout" | tail -3)
_s13want=$(printf '?? artifact1\n?? artifact2\n?? artifact3')
if [ "$_src" -eq 0 ] && [ "$_s13mid" = "$_s13want" ] \
    && [ "$(grep -c 'artifact4' "$tmp/sout")" -eq 0 ]; then
    t_ok "46: S13 F3 artifacts: four untracked files -> exactly the first three git status --short lines (?? artifact1/2/3, bare, no label) between the count and mutant lines; artifact4 NOT printed, rc 0"
else
    t_fail "46: S13 expected exactly '?? artifact1', '?? artifact2', '?? artifact3' after the count line (artifact4 absent); got: $(printf '%s' "$_s13mid" | tr '\n' '|') (rc=$_src)"
fi

# --- 47-54 (C1-C8): the ci stage (M1154 T8, wave 3) ---------------------------
# The brief's C-cells: `make ci` -- or the cell's --ci-run STUB -- UNCAPPED, in
# a fresh clone of the fixture's bare push.git at exactly $P6_8 (the synthetic
# repo has no Makefile, so the reference's real `make ci` is never run in a
# cell: the stub prints the lines its cell needs into its stdout, which the
# stage redirects to <log-dir>/ci-<s>.log). The cells are RED BY CONSTRUCTION
# today: the committed binary's --stage check names only ledger|verdict|sweep,
# so --stage ci exits 2 with "the stage must be 'ledger', 'verdict' or
# 'sweep'" before any of the --work/... flags are seen.
_crun() { # $1=sha8 $2=work $3=records $4=cirun-cmd [extra flags...]
    # The _srun shape for the ci stage (rc + out/err into $_cout/$_cerr/$_crc).
    _cs="$1"; _cw="$2"; _cr="$3"; _ccmd="$4"; shift 4
    "$HBIN" gate run --config "$tmp/cfg.json" --stage ci "$_cs" \
        --work "$_cw" --records "$_cr" --ci-run "$_ccmd" \
        "$@" < /dev/null > "$tmp/cout" 2> "$tmp/cerr"
    _crc=$?
}
_cfail() { # $1=cellnum  $2=why
    t_fail "$1: ci stage not green: $2 (got rc=$_crc: $(tr '\n' '|' < "$tmp/cerr" | head_bytes 160))"
}
# The C1/C4/C7 green stub: the reference's 6 lines (the 160-char tail the RED
# half's join exercises is C2's; here the log is short).
_c1stub() {
    echo "ok 1 - one"
    echo "not ok 2 - two"
    echo "*** [FAIL] x"
    echo "smoke: FAILED -- y"
    echo "smoke: OK (24 drivers, 0 failed)"
    echo "ci: OK"
}
CSTUB="$tmp/ci1stub"
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'echo "not ok 2 - two"'
  printf '%s\n' 'echo "*** [FAIL] x"'
  printf '%s\n' 'echo "smoke: FAILED -- y"'
  printf '%s\n' 'echo "smoke: OK (24 drivers, 0 failed)"'
  printf '%s\n' 'echo "ci: OK"'
  printf '%s\n' 'exit 0'
} > "$CSTUB"
chmod +x "$CSTUB"

# C1 (45): the green ci stage (the brief's C1): the stub's 6 lines are the log;
# the meta's start/end UTC-second shape; the record's `  ci: rc=0` line carries
# the LAST `^ci: OK` line; the `records:` line is the LAST stdout line; no
# excerpt file; no RED line (the success arm prints only the rc line).
_crun "$P6_8" "$tmp/cw1" "$tmp/cr1" "$CSTUB"
if [ "$_crc" -eq 0 ] \
    && grep -qE '^start [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+Z$' "$tmp/cw1/ci/ci-$P6_8.log.meta" \
    && grep -qE '^end .*rc=0$' "$tmp/cw1/ci/ci-$P6_8.log.meta" \
    && [ "$(cat "$tmp/cw1/ci/ci-$P6_8.log")" = "$(printf 'ok 1 - one\nnot ok 2 - two\n*** [FAIL] x\nsmoke: FAILED -- y\nsmoke: OK (24 drivers, 0 failed)\nci: OK\n')" ] \
    && grep -qF "  ci: rc=0 ci: OK" "$tmp/cout" \
    && [ "$(tail -1 "$tmp/cout")" = "records: $tmp/cr1" ] \
    && [ ! -e "$tmp/cw1/ci/ci-$P6_8.excerpt" ] \
    && ! grep -qF "  ci: RED" "$tmp/cout"; then
    t_ok "47: C1 the green ci stage: the log is the stub's 6 lines, the meta's ^start/^end UTC-second lines (rc=0), \"  ci: rc=0 ci: OK\" (the LAST ^ci: OK tail), the records line LAST, no excerpt file, no RED line, rc 0"
else
    _cfail 47 "expected the 6-line log + the start/end meta + \"  ci: rc=0 ci: OK\" + records last + no excerpt/RED"
fi

# C2 (46): the RED half (rc 2, but the stub printed a ci: OK line -> the rc != 0
# arm): the record's `  ci: rc=2 ci: OK` line, then the note's b6 RED half (the
# first four matching log lines cut 160, joined by ';' with no added space),
# then the excerpt file with the not-ok line's one-before / twenty-after
# window (the short log: everything remaining is in). Stage rc 0 (the stage
# COMPLETED: the orchestrator reads RED from the record).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'echo "not ok 2 - two failed because of a very long reason that keeps going and going and going to test the cut"'
  printf '%s\n' 'echo "*** [FAIL] the suite"'
  printf '%s\n' 'echo "ci: OK"'
  printf '%s\n' 'exit 2'
} > "$tmp/ci2stub"
chmod +x "$tmp/ci2stub"
_crun "$P6_8" "$tmp/cw2" "$tmp/cr2" "$tmp/ci2stub"
# gate-run.sh:79's exact bytes: grep | head -4 | cut -c1-160 | tr '\n' ';'
# -- only the `^not ok` and `^\*\* \[` lines match (ok/ci:OK do not), and tr
# turns the last line's newline into a trailing ';'.
_c2L="not ok 2 - two failed because of a very long reason that keeps going and going and going to test the cut;*** [FAIL] the suite;"
if [ "$_crc" -eq 0 ] \
    && grep -qF "  ci: rc=2 ci: OK" "$tmp/cout" \
    && grep -qF "  ci: RED make ci rc=2. $_c2L Log on the host: $tmp/cw2/ci/ci-$P6_8.log. (excerpt: $tmp/cw2/ci/ci-$P6_8.excerpt)" "$tmp/cout" \
    && [ -f "$tmp/cw2/ci/ci-$P6_8.excerpt" ] \
    && grep -qF "ok 1 - one" "$tmp/cw2/ci/ci-$P6_8.excerpt" \
    && grep -qF "not ok 2 - two failed" "$tmp/cw2/ci/ci-$P6_8.excerpt" \
    && grep -qF '*** [FAIL] the suite' "$tmp/cw2/ci/ci-$P6_8.excerpt" \
    && grep -qF "ci: OK" "$tmp/cw2/ci/ci-$P6_8.excerpt"; then
    t_ok "48: C2 the RED half (rc 2 + the stub's ci: OK): \"  ci: rc=2 ci: OK\", the b6 RED line (the first four matching lines cut 160, ';'-joined), the excerpt with the not-ok line + one before + the rest after, stage rc 0"
else
    _cfail 48 "expected \"  ci: rc=2 ci: OK\" + the RED b6 line + the excerpt (ok 1 / not ok 2 / *** [FAIL] / ci: OK)"
fi

# C3 (47): rc 0 but NO ci: OK line -> the `no 'ci: OK' line` arm + the RED line.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'exit 0'
} > "$tmp/ci3stub"
chmod +x "$tmp/ci3stub"
_crun "$P6_8" "$tmp/cw3" "$tmp/cr3" "$tmp/ci3stub"
if [ "$_crc" -eq 0 ] \
    && grep -qF "  ci: rc=0 no 'ci: OK' line" "$tmp/cout" \
    && grep -qF "  ci: RED make ci rc=0.  Log on the host: $tmp/cw3/ci/ci-$P6_8.log. (excerpt: $tmp/cw3/ci/ci-$P6_8.excerpt)" "$tmp/cout"; then
    t_ok "49: C3 rc 0 with no ci: OK line: \"  ci: rc=0 no 'ci: OK' line\" + the RED line (the \"rc != 0 OR no ci: OK\" arm 2; no matching log lines -> gate-run.sh:79's empty substitution, the reference's double space), stage rc 0"
else
    _cfail 49 "expected \"  ci: rc=0 no 'ci: OK' line\" + the RED b6 line"
fi

# C4 (48): a dead clone left at the work home is MOVED ASIDE (the reference's
# refusal is CHANGED to the mv, gate-run.sh:72): the marker line, the old dir
# gone, a .dead-* holding stale, and the stage continues green.
mkdir -p "$tmp/cw4/ci-$P6_8"
printf 'stale\n' > "$tmp/cw4/ci-$P6_8/stale"
_crun "$P6_8" "$tmp/cw4" "$tmp/cr4" "$CSTUB"
_c4dead=$(ls "$tmp/cw4" 2>/dev/null | grep -c '^ci-'"$P6_8"'\.dead-')
_c4stale=$(cat "$tmp/cw4"/ci-$P6_8.dead-*/stale 2>/dev/null | head -1)
if [ "$_crc" -eq 0 ] \
    && grep -qF "  ci: moved a dead clone aside: $tmp/cw4/ci-$P6_8 -> " "$tmp/cout" \
    && [ ! -e "$tmp/cw4/ci-$P6_8/stale" ] \
    && [ -n "$_c4dead" ] && [ "$_c4stale" = "stale" ] \
    && grep -qF "  ci: rc=0 ci: OK" "$tmp/cout"; then
    t_ok "50: C4 the dead clone moved aside: the \"  ci: moved a dead clone aside: <home> -> \" marker, the old home's stale GONE (the fresh clone carries no stale), the .dead-* holding stale, the stage continues green (the C1 stub), rc 0"
else
    _cfail 50 "expected the moved-aside marker + the .dead-* holding stale + a green continuation (marker in cout, dead=$_c4dead, stale=$_c4stale)"
fi

# C5 (49): no origin/master in the push.git -> the INVALID line, stage rc 1.
# The S5 idiom: a SECOND bare clone of the p-chain WITHOUT the update-ref.
git clone -q --bare "$PCHAIN" "$tmp/pushci5.git"
_crun "$P6_8" "$tmp/cw5" "$tmp/cr5" "$CSTUB" --push-repo "$tmp/pushci5.git"
if [ "$_crc" -eq 1 ] && grep -qF "  ci: no origin/master in push.git: INVALID" "$tmp/cout"; then
    t_ok "51: C5 no origin/master in push.git: \"  ci: no origin/master in push.git: INVALID\", rc 1 (the stage ended)"
else
    _cfail 51 "expected the no-origin/master INVALID line rc 1"
fi

# C6 (50): KEYS UNSET + the run env (the S11 idiom, ~:905): the stub prints its
# own JICHI_API_KEY (${JICHI_API_KEY:-ABSENT}) and its MUTANT_BASE /
# JC_SMOKE_KEEP_GOING; the cell exports JICHI_API_KEY=canary for the stage
# call and passes --ci-smoke-keep-going 1. The log has no canary, the ABSENT
# fallback, the full 40-hex MUTANT_BASE (the p-chain tip P6) and =1.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "JICHI_API_KEY=${JICHI_API_KEY:-ABSENT}"'
  printf '%s\n' 'echo "MUTANT_BASE=$MUTANT_BASE"'
  printf '%s\n' 'echo "JC_SMOKE_KEEP_GOING=$JC_SMOKE_KEEP_GOING"'
  printf '%s\n' 'echo "ci: OK"'
  printf '%s\n' 'exit 0'
} > "$tmp/ci6stub"
chmod +x "$tmp/ci6stub"
export JICHI_API_KEY=canary
_crun "$P6_8" "$tmp/cw6" "$tmp/cr6" "$tmp/ci6stub" --ci-smoke-keep-going 1
_rc6=$_crc
unset JICHI_API_KEY
_c6leak=$(grep -c "canary" "$tmp/cw6/ci/ci-$P6_8.log")
_c6absent=$(grep -c "JICHI_API_KEY=ABSENT" "$tmp/cw6/ci/ci-$P6_8.log")
_c6mb=$(grep -cE "MUTANT_BASE=[0-9a-f]{40}" "$tmp/cw6/ci/ci-$P6_8.log")
_c6kg=$(grep -c "JC_SMOKE_KEEP_GOING=1" "$tmp/cw6/ci/ci-$P6_8.log")
if [ "$_rc6" -eq 0 ] && [ "$_c6leak" -eq 0 ] && [ "$_c6absent" -ge 1 ] \
    && [ "$_c6mb" -ge 1 ] && [ "$_c6kg" -ge 1 ]; then
    t_ok "52: C6 KEYS UNSET + the run env: the gate ran with JICHI_API_KEY=canary exported; the child env scrubbed it (no canary, the ABSENT fallback), and the child ALSO saw MUTANT_BASE=<P6 full 40-hex> and JC_SMOKE_KEEP_GOING=1, rc 0"
else
    t_fail "52: C6 the run child env must have scrubbed JICHI_API_KEY and carried MUTANT_BASE / JC_SMOKE_KEEP_GOING (canary count=$_c6leak, ABSENT=$_c6absent, MUTANT_BASE=$_c6mb, KEEP_GOING=$_c6kg, rc=$_rc6)"
fi

# C7 (51): the merge-base warning (the BRANCHED fixture the cell builds):
# sideci branches off the p-chain at P2 (one commit touching f, the card-23
# idiom); a bare clone of $PCHAIN carries sideci, and update-ref puts
# origin/master at P4. merge-base(P4, sideci) = P2 != P4 -> the KEPT-prefix
# warning + the MUTANT_BASE line; the stage then runs the C1 green stub.
git -C "$PCHAIN" checkout -q -b sideci "$P2"
printf 'sideci\n' > "$PCHAIN/f"
git -C "$PCHAIN" add f
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-09T00:00:11Z" GIT_COMMITTER_DATE="2026-11-09T00:00:11Z" \
git -C "$PCHAIN" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "sideci"
C7S=$(git -C "$PCHAIN" rev-parse HEAD)
C7S8=$(printf '%.8s' "$C7S")
git -C "$PCHAIN" checkout -q "$P6"
git clone -q --bare "$PCHAIN" "$tmp/ci7.git"
git -C "$tmp/ci7.git" update-ref refs/remotes/origin/master "$P4"
_crun "$C7S8" "$tmp/cw7" "$tmp/cr7" "$CSTUB" --push-repo "$tmp/ci7.git"
if [ "$_crc" -eq 0 ] \
    && grep -qF "ci-gate.sh: GitLab master $P4_8 is not an ancestor of $C7S8; the sweep uses their merge-base" "$tmp/cout" \
    && grep -qF "ci-gate.sh: MUTANT_BASE=$P2 (GitLab master as push.sh fetched it: $P4_8)" "$tmp/cout" \
    && grep -qF "  ci: rc=0 ci: OK" "$tmp/cout"; then
    t_ok "53: C7 the merge-base warning (B=P4, MB=P2 != B): the KEPT-prefix \"ci-gate.sh: GitLab master $P4_8 is not an ancestor of $C7S8; the sweep uses their merge-base\" + \"ci-gate.sh: MUTANT_BASE=$P2 (GitLab master as push.sh fetched it: $P4_8)\", then the C1 green stub, rc 0"
else
    _cfail 53 "expected the KEPT-prefix warning + the MUTANT_BASE=P2 line + a green run"
fi

# C8 (52): usage -- `--stage ci` with NO <SHA8> (exit 2 naming "needs a
# <SHA8>", the S9 shape) AND a non-numeric --ci-smoke-keep-going (exit 2
# naming the flag). Two assertions, one cell (the S9/S10 pair idiom).
"$HBIN" gate run --config "$tmp/cfg.json" --stage ci \
    < /dev/null > "$tmp/cout" 2> "$tmp/c8a.err"
_c8a=$?
"$HBIN" gate run --config "$tmp/cfg.json" --stage ci "$P6_8" \
    --ci-smoke-keep-going 2x < /dev/null > "$tmp/cout" 2> "$tmp/c8b.err"
_c8b=$?
if [ "$_c8a" -eq 2 ] && grep -qF "needs a <SHA8>" "$tmp/c8a.err" \
    && [ "$_c8b" -eq 2 ] && grep -qF -- "--ci-smoke-keep-going" "$tmp/c8b.err"; then
    t_ok "54: C8 usage: --stage ci with no <SHA8> -> exit 2 naming \"needs a <SHA8>\"; a non-numeric --ci-smoke-keep-going 2x -> exit 2 naming the flag"
else
    t_fail "54: C8 expected the two usage refusals (got rc=$_c8a / rc=$_c8b: $(tr '\n' '|' < "$tmp/c8a.err" | head_bytes 80) || $(tr '\n' '|' < "$tmp/c8b.err" | head_bytes 80))"
fi

# C9 (53): F1's pin -- --stage with NO value (end the argv there): exit 2,
# stderr names ALL FIVE stages (ledger | verdict | sweep | ci | redfirst).
# RED by construction on the head before the F1 fix: the value-missing
# refusal prints the three-name string. T9 (wave 3) moves it to five: the
# redfirst stage is now the fifth named stage, so the byte pin moves WITH
# the message (this cell is GREEN today and must stay green).
"$HBIN" gate run --config "$tmp/cfg.json" --stage \
    < /dev/null > "$tmp/cout" 2> "$tmp/c9.err"
_c9=$?
if [ "$_c9" -eq 2 ] \
    && grep -qF -- "--stage needs a value (ledger | verdict | sweep | ci | redfirst)" "$tmp/c9.err"; then
    t_ok "55: C9 the --stage value-missing refusal names all five stages: exit 2, \"--stage needs a value (ledger | verdict | sweep | ci | redfirst)\" on stderr"
else
    t_fail "55: C9 the --stage value-missing refusal must name (ledger | verdict | sweep | ci | redfirst) (got rc=$_c9: $(tr '\n' '|' < "$tmp/c9.err" | head_bytes 160))"
fi

# C10 (54): the excerpt WRITE-FAULT arm (T8-f residual R1). A C2-style FAIL stub
# (rc 2, a `not ok` line so the excerpt would be non-empty) reaches step 10; the
# excerpt path is PRE-CREATED as a DIRECTORY so `fopen(excpath,"w")` fails
# (EISDIR) at the arm -- the log (step 8) and meta (step 7) both succeed first.
# The stage COMPLETED the run: the record (the `  ci: rc=2` line + the RED line)
# must be PRINTED (not discarded), rc 1, NO `records:` line, and the fault named
# on stderr. RED on the current head: the arm does `return 1` with stdout EMPTY.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'echo "not ok 2 - two"'
  printf '%s\n' 'exit 2'
} > "$tmp/ci10stub"
chmod +x "$tmp/ci10stub"
_c10logdir="$tmp/cw10/ci"
mkdir -p "$_c10logdir"
mkdir "$_c10logdir/ci-$P6_8.excerpt"
_crun "$P6_8" "$tmp/cw10" "$tmp/cr10" "$tmp/ci10stub" --ci-log-dir "$_c10logdir"
if [ "$_crc" -eq 1 ] \
    && grep -qF "  ci: rc=2" "$tmp/cout" \
    && grep -qF "  ci: RED" "$tmp/cout" \
    && ! grep -qF "records:" "$tmp/cout" \
    && grep -qF "the excerpt write failed" "$tmp/cerr" \
    && grep -qF "INVALID" "$tmp/cerr"; then
    t_ok "56: C10 the excerpt write-fault arm: the excerpt path pre-created as a directory makes fopen fail at step 10; the record (the \"  ci: rc=2\" + RED lines) is PRINTED (not discarded), NO records: line, the fault named on stderr + INVALID, rc 1"
else
    t_fail "56: C10 the excerpt write-fault must PRINT the record (rc line + RED), no records: line, and name the fault on stderr (got rc=$_crc: stdout=$(tr '\n' '|' < "$tmp/cout" | head_bytes 120) stderr=$(tr '\n' '|' < "$tmp/cerr" | head_bytes 120))"
fi

# C11 (55): the empty-excerpt FAIL (T8-f residual R1b). A C3-style stub
# (`ok 1 - one`, rc 0, NO `ci: OK` line, NO `not ok` line) is a FAIL on the
# "no 'ci: OK' line" arm with an EMPTY excerpt (no `^not ok` line). The stage
# COMPLETES (rc 0); the record has the `  ci: rc=0 no 'ci: OK' line` line + the
# RED line (which still names the excerpt path) + the `records:` line; and the
# excerpt file must NOT exist. RED on the current head: the empty case
# create+truncates the file, so it EXISTS.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'exit 0'
} > "$tmp/ci11stub"
chmod +x "$tmp/ci11stub"
_crun "$P6_8" "$tmp/cw11" "$tmp/cr11" "$tmp/ci11stub"
if [ "$_crc" -eq 0 ] \
    && grep -qF "  ci: rc=0 no 'ci: OK' line" "$tmp/cout" \
    && grep -qF "  ci: RED make ci rc=0.  Log on the host: $tmp/cw11/ci/ci-$P6_8.log. (excerpt: $tmp/cw11/ci/ci-$P6_8.excerpt)" "$tmp/cout" \
    && [ "$(tail -1 "$tmp/cout")" = "records: $tmp/cr11" ] \
    && [ ! -e "$tmp/cw11/ci/ci-$P6_8.excerpt" ]; then
    t_ok "57: C11 the empty-excerpt FAIL (no not ok line, no ci: OK): the record has the \"  ci: rc=0 no 'ci: OK' line\" + RED line (naming the excerpt path) + records: line, rc 0, and the excerpt file does NOT exist"
else
    t_fail "57: C11 the empty-excerpt FAIL must NOT create the excerpt file and must keep the RED line naming it (got rc=$_crc: excerpt_exists=$( [ -e "$tmp/cw11/ci/ci-$P6_8.excerpt" ] && echo yes || echo no ) stdout=$(tr '\n' '|' < "$tmp/cout" | head_bytes 160))"
fi


# ===========================================================================
# G-cells (65-93): the `gate run` ORCHESTRATOR end to end (M1159, batch H9b,
# wave 4). RED BY CONSTRUCTION on the current tree: a full `gate run <SHA8>
# [BATCH]` reaches the dispatch tail and is refused `jichi-harness: gate:
# not built yet (see docs/HARNESS.md)`, exit 2 -- so every G-cell except G1
# (65, the missing-SHA8 usage, which predates the orchestrator) is red.
# M1159 Task B turns them green in three commits.
#
# CONTRACT (byte shapes quoted into the assertions below):
#   notes/M1159.md -- D4 the rc/verdict table, D6 say/state/note, D3/D5 homes,
#     D10 the INVALID promotion, D12 the cells, D13 the `gate units` template.
#   reference docs/internal/drive/2026-10-27-hardening-host/gate-run.sh (read
#     line by line): say :26, verdict :27, red :29-31, ledger join :41, sweep
#     join :59, rfnote :65-68, ci join :79, HOLD note :86, land-failed note :93,
#     GREEN-and-LANDED note :104.
#   notes/M1154.md :261-285 (the land-refused / land-failed / re-pinned /
#     NOT-re-pinned / hcn note bytes the port keeps).
#
# THE G-FIXTURE (real git under the driver's TMPDIR, distinct from the p-chain):
#   gc work chain: G0 (f + tests/smoke/gcbin_lint.sh invoking "$BIN", good
#     trailers) < G1c (g, good) = origin/master < G2c (h, good) = the GREEN
#     target; G3bad (i, author = committer = ADDR, NO trailers) = the
#     ledger-red target; branch gside off G0 (s, good) = the not-ff target.
#   The two diverging branches are pinned by the contract, not the draft:
#   G3bad MUST branch off G1c -- the ledger's range is origin/master(G1c)..
#   G3bad = G3bad ALONE, N=1 (D12's G7 row says so; a G2c-parented G3bad makes
#   the range G2c+G3bad, N=2, and G3bad would then be reachable from master
#   without the g3 branch). And gside MUST branch off G0 -- off G1c its push
#   would fast-forward (G1c is its ancestor), so the not-ff refusal could not
#   fire (verified against gpush.git with a scratch clone before committing).
#   gpush.git = bare clone of gc (refs/heads/*: master=G2c, g3=G3bad, gside)
#   + update-ref origin/master G1c (the p-chain idiom).
#   gcfg.json (host.gateHome $tmp/gh, host.gateRun $tmp/gh/standin.sh);
#   gcfg2.json the same WITHOUT gateRun.
#   Full-run form: $HBIN gate run --config <cfg> <SHA8> [BATCH] <flags...>.
#   The <ts> state field is `date '+%F %T %z'`-shaped (D6).
#   D12 pins distinct state files on the cells that assert exact line sets
#   (the driver's --state flag, b13); those use $tmp/gstate<N> so a shared
#   <gh>/state cannot make an exact-count assert flaky.
if ! mkdir -p "$tmp/gdir" "$tmp/gh" "$tmp/gc"; then
    _gn=65; while [ "$_gn" -le 93 ]; do t_fail "$_gn: G-fixture: could not make the scratch dirs"; _gn=$((_gn + 1)); done
    t_done; exit 1
fi
git -C "$tmp/gc" init -q
# _gcm AUTHOR COMMITTER SUBJECT TRAILERS SECOND: one gc commit (fixed identity,
# the date's seconds = the explicit SECOND argument; a unique tree per commit
# via SECOND + the subject), echoes the new HEAD sha. The _lc pattern in gc.
_gcm() {
    _gc_a="$1"; _gc_c="$2"; _gc_s="$3"; _gc_t="$4"; _gc_d="$5"
    printf 'gc%s n%s\n' "$_gc_s" "$_gc_d" > "$tmp/gc/txt" 2>/dev/null || printf '%s\n' "$_gc_s" > "$tmp/gc/txt"
    git -C "$tmp/gc" add -A
    GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$_gc_a" \
    GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$_gc_c" \
    GIT_AUTHOR_DATE="2026-11-11T00:00:${_gc_d}Z" GIT_COMMITTER_DATE="2026-11-11T00:00:${_gc_d}Z" \
    git -C "$tmp/gc" -c user.name="Ledger Smoke" -c user.email="$_gc_a" \
    commit -q -m "$_gc_s" ${_gc_t:+-m "$_gc_t"}
    git -C "$tmp/gc" rev-parse HEAD
}
mkdir -p "$tmp/gc/tests/smoke"
printf '#!/bin/sh\n"$BIN" --version\n' > "$tmp/gc/tests/smoke/gcbin_lint.sh"
printf 'f0\n' > "$tmp/gc/f"
git -C "$tmp/gc" add -A
G0=$(_gcm "$ADDR" "$ADDR" "G0" "$GOODCO" 01)
printf 'g1\n' > "$tmp/gc/g"; git -C "$tmp/gc" add g
G1c=$(_gcm "$ADDR" "$ADDR" "G1c" "$GOODCO" 02)
printf 'h2\n' > "$tmp/gc/h"; git -C "$tmp/gc" add h
G2c=$(_gcm "$ADDR" "$ADDR" "G2c" "$GOODCO" 03)
# G3bad: off G1c (a SIBLING of G2c, N=1), NO trailers (the ledger-red target).
git -C "$tmp/gc" checkout -q "$G1c"
printf 'ibad\n' > "$tmp/gc/i"; git -C "$tmp/gc" add i
G3bad=$(_gcm "$ADDR" "$ADDR" "G3bad" "" 04)
git -C "$tmp/gc" checkout -q -b g3
# gside: off G0 (NOT off G1c -- a G1c parent fast-forwards, verified in /tmp).
git -C "$tmp/gc" checkout -q "$G0"
printf 'side\n' > "$tmp/gc/s"; git -C "$tmp/gc" add s
Gside=$(_gcm "$ADDR" "$ADDR" "Gside" "$GOODCO" 05)
git -C "$tmp/gc" checkout -q -b gside
git -C "$tmp/gc" checkout -q "$G2c"
G1c_8=$(printf '%.8s' "$G1c"); G2c_8=$(printf '%.8s' "$G2c")
G3bad_8=$(printf '%.8s' "$G3bad"); Gside_8=$(printf '%.8s' "$Gside")
git clone -q --bare "$tmp/gc" "$tmp/gpush.git"
git -C "$tmp/gpush.git" update-ref refs/remotes/origin/master "$G1c"
cat > "$tmp/gcfg.json" <<EOF
{"dir":"$tmp/gdir","modelKeyFile":"$tmp/gdir/k","gitTokenFile":"$tmp/gdir/t",
"host":{"pushHome":"$tmp","pushRepo":"$tmp/gpush.git","ledgerAddress":"$ADDR","gateHome":"$tmp/gh","gateRun":"$tmp/gh/standin.sh"}}
EOF
sed 's/,"gateRun":"[^"]*"//' "$tmp/gcfg.json" > "$tmp/gcfg2.json"
sed 's/"ledgerAddress":"[^"]*",//' "$tmp/gcfg.json" > "$tmp/gcfg3.json"
# M1202 wave 2b: gcfga = gcfg with a TOP-LEVEL "drive":{"landingPolicy":
# "auto"} block (inserted after the top-level "dir" key). D2's product
# DEFAULT is hold (absent landingPolicy = hold): a green run that would
# otherwise reach the land step now ends green-held via the HOLD the gate
# creates from the config. Every cell that pins a GREEN terminal (the
# land-step cells 77-81 and the no-land cells 76/85/88/89/90/91/92) names
# gcfga so the policy word "auto" carries it to the land step; the cell
# headers below document this. The D2 cells (106-107) use the BASE gcfg
# (no landingPolicy key = the default hold).
cat > "$tmp/gcfga.json" <<EOF
{"dir":"$tmp/gdir","modelKeyFile":"$tmp/gdir/k","gitTokenFile":"$tmp/gdir/t",
"drive":{"landingPolicy":"auto"},
"host":{"pushHome":"$tmp","pushRepo":"$tmp/gpush.git","ledgerAddress":"$ADDR","gateHome":"$tmp/gh","gateRun":"$tmp/gh/standin.sh"}}
EOF
touch "$tmp/gdir/k" "$tmp/gdir/t"
# G15's ci-fail stub (M1159 D12): the failing lines, rc 0, NO `ci: OK`.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'echo "not ok 2 - two"'
  printf '%s\n' 'echo "*** [FAIL] x"'
  printf '%s\n' 'echo "smoke: FAILED -- y"'
  printf '%s\n' 'exit 0'
} > "$tmp/gstubcifail"; chmod +x "$tmp/gstubcifail"
# G15r's ci stub (M1160-d cell 94): the failing lines, NO `ci: OK`, and
# `exit 3` -- the RUN's rc is 3, but the stage COMPLETES (its own rc is 0),
# so the orchestrator must still say/RED with rc=3 (the RUN rc), not rc=0
# (the STAGE rc).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "ok 1 - one"'
  printf '%s\n' 'echo "not ok 2 - two"'
  printf '%s\n' 'echo "*** [FAIL] x"'
  printf '%s\n' 'echo "smoke: FAILED -- y"'
  printf '%s\n' 'exit 3'
} > "$tmp/gstubci3"; chmod +x "$tmp/gstubci3"
# G15v's ci stub (M1160-d cell 95, SV:1438.1): the OK lines AND `exit 3` --
# a run whose `make ci` exits 3 yet prints a `ci: OK` line. The port must
# still RED (the reference reds on `cr -ne 0 || -z okl`), not LAND.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "smoke: OK (24 drivers, 0 failed)"'
  printf '%s\n' 'echo "ci: OK"'
  printf '%s\n' 'exit 3'
} > "$tmp/gstubci3ok"; chmod +x "$tmp/gstubci3ok"
# G12 et al.'s ci-green stub (D12's byte shape).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'echo "smoke: OK (24 drivers, 0 failed)"'
  printf '%s\n' 'echo "ci: OK"'
  printf '%s\n' 'exit 0'
} > "$tmp/gstubcigreen"; chmod +x "$tmp/gstubcigreen"
# land / repin stubs (they record their argv on $1 so a cell can prove the
# land step was -- or was not -- called).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'printf "%s\\n" "$@" > "$1"'
  printf '%s\n' 'echo "land-stub: boom"'
  printf '%s\n' 'exit 1'
} > "$tmp/gstublandfail"; chmod +x "$tmp/gstublandfail"
{ printf '#!/bin/sh\n'
  printf '%s\n' 'printf "%s\\n" "$@" > "$1"'
  printf '%s\n' 'exit 0'
} > "$tmp/gstublandok"; chmod +x "$tmp/gstublandok"
_gmkrepin() { # $1=path  $2=repin-out-line  $3=exit-code
    { printf '#!/bin/sh\n'
      printf '%s\n' 'printf "%s\\n" "$@" > "$1"'
      printf '%s\n' "echo \"$2\""
      printf '%s\n' "exit $3"
    } > "$1"; chmod +x "$1"
}
_gmkrepin "$tmp/gstubrepinclean" "repin: done \$2, doctors clean" 0
_gmkrepin "$tmp/gstubrepinhcn" "src/harness files changed abcdef12..<s>: 2" 0
_gmkrepin "$tmp/gstubrepinrc2" "repin: failed: rc2" 2
# The sweep stage's clean flag set (the S1 idiom, :756-763): one green driver
# (gcbin_lint.sh at G2c) swept with --bin "true". A QUOTED WORD SET, not a
# string (M1159 Task B-fix D3): the old G_SWEEP variable carried embedded
# quotes that an unquoted $G_SWEEP word-split into 46 broken tokens
# (--build-run got the literal "true" with its quotes; the container lines lost
# their two leading spaces the verdict's ^  container grep needs). Written out
# once here (inside _grun's `_gflags` expansion), the six flags are separate
# quoted words; the values are byte-identical to the old G_SWEEP. A cell names
# the set with the literal marker arg `_gflags` at its call site -- _grun
# expands the marker IN PLACE before dispatching, so the marker never reaches
# `gate run` as a third positional (the b15 usage refusal, M1159 DRV-b: every
# sweep-carrying cell's rc 2). The flag parser is last-occurrence-wins, so a
# cell's explicit override placed AFTER the marker still wins (the G8 idiom:
# `--build-run "false"` at the END of its call).
# _grun CFG SHA8 [BATCH] [FLAGS...] : the FULL form (the _srun shape); rc/
# stdout/stderr into $_grc/$_gout/$_gerr.
_grun() {
    _gc1="$1"; _gsh="$2"; _gb="${3:-}"; [ -n "$_gb" ] && shift 3 || shift 2
    # M1159 DRV-b: expand each literal `_gflags` marker, in place, into the
    # six clean sweep flags as separate quoted words. The splice goes through
    # a one-arg-per-line temp file (the flag values are single-line by the
    # contract), so every cell argument's quoting survives byte-identical.
    _gf="$tmp/.gargs"
    : > "$_gf"
    while [ "$#" -gt 0 ]; do
        if [ "$1" = "_gflags" ]; then
            {
            printf '%s\n' \
                --build-run "true" \
                --bin "true" \
                --mutant-run "echo 'mutant: OK (1 drivers swept, 0 excluded)'" \
                --unit-run-san "echo '37960 checks, 0 failures'" \
                --unit-run-o2 "echo '37960 checks, 0 failures'" \
                --container-run "echo '  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok'; echo '  container resolve_timeout_connect rc=0 5 ok, 0 not ok'"
            } >> "$_gf"
        else
            printf '%s\n' "$1" >> "$_gf"
        fi
        shift
    done
    set --
    while IFS= read -r _gl; do
        set -- "$@" "$_gl"
    done < "$_gf"
    "$HBIN" gate run --config "$_gc1" "$_gsh" ${_gb:+$_gb} \
        "$@" < /dev/null > "$tmp/g.out" 2> "$tmp/g.err"
    _grc=$?
    _gout=$(cat "$tmp/g.out" 2>/dev/null)
    _gerr=$(tr '\n' '|' < "$tmp/g.err" | head_bytes 240)
}
# _gnote SHA8 : the notify's inbox note body (newest if several: cells that
# re-run a sha8 clean the inbox between runs).
_gnote() { _gf=$(ls -1 "$tmp/gdir/inbox/"*-gate-"$1".md 2>/dev/null | tail -1); [ -n "$_gf" ] && cat "$_gf" 2>/dev/null; :; }
# _gcleaninbox SHA8 : drop the inbox notes for SHA8 (a cell re-running it).
_gcleaninbox() { rm -f "$tmp/gdir/inbox/"*-gate-"$1".md 2>/dev/null; :; }
# _gstateline FILE N SHA8 WORD : line N is "<sha8> <word> <ts>" (ts the
# %F %T %z shape, the 4th field). Returns 0 iff so.
_gstateline() {
    _gsf="$1"; _gsn="$2"; _gss="$3"; _gsw="$4"
    _gsl=$(sed -n "${_gsn}p" "$_gsf" 2>/dev/null) || return 1
    set -- $_gsl
    [ "$1" = "$_gss" ] && [ "$2" = "$_gsw" ] && [ $# -eq 5 ] || return 1
    printf '%s' "$3 $4 $5" | grep -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} [-+][0-9]{4}$'
}
# _gstateok STATEFILE "SHA8:WORD ..." ... : SILENT predicate -- every line the
# expected word (in order), the line count exact, each <ts> well-formed. The
# caller's else branch reports (exactly one t_fail per cell, the driver's
# honesty rule: a helper that also t_fail's would double-count a red cell).
_gstateok() {
    _gs2="$1"; shift
    [ -f "$_gs2" ] || return 1
    _gn2=$(wc -l < "$_gs2" | tr -d ' '); [ "$_gn2" -eq $# ] || return 1
    _gi=1
    for _gw in "$@"; do
        case "$_gw" in *:*) _gsa="${_gw%%:*}"; _gwd="${_gw##*:}" ;; *) _gsa="$_gw"; _gwd="" ;; esac
        if [ -n "$_gwd" ] && ! _gstateline "$_gs2" "$_gi" "$_gsa" "$_gwd"; then
            return 1
        fi
        _gi=$((_gi + 1))
    done
    return 0
}
# _ggreenexp : print the GREEN-and-LANDED note text (M1154 :276-285 / M1159
# D4.9) for the G2c run, read from the run's own files (the cell sets $_gl
# (land argv-file), $_cl (ci log dir), $_rl (run.log home), $_repin (the
# re-pinned tail, or empty for the NOT half); D4.9's sed strip on the repin
# last line gives the tail, so the cell passes it pre-stripped).
_ggreenexp() {
    _sweepline=$(grep -F "sweep: ran 1 drivers on $G2c_8" "$_rl/sweep.txt" 2>/dev/null | tail -1)
    _smokel=$(grep '^smoke: OK' "$_cl/ci-$G2c_8.log" 2>/dev/null | tail -1)
    _okl=$(grep '^ci: OK' "$_cl/ci-$G2c_8.log" 2>/dev/null | tail -1)
    _rflast=$(tail -1 "$_rl/redfirst.txt" 2>/dev/null)
    _rfg=$(grep -E '^[0-9a-f]{8} (GREEN|NO SUMMARY|BUILD FAILED)' "$_rl/redfirst.txt" 2>/dev/null | cut -c1-120 | tr '\n' ';')
    printf 'GREEN and LANDED: BATCH B READY at %s is master now (1 commits; %s; %s; %s; %s%s). bin/ %s; your NEXT run uses it. Branch the next batch from the team branch as before.' \
        "$G2c_8" "$_sweepline" "$_smokel" "$_okl" "$_rflast" "${_rfg:+ -- never observed failing at its own commit, or did not build/finish there (a pin of fixed behaviour, a build-only test fix, or a test that cannot fire: say which in LOG-3): $_rfg}" "$_repin"
}

# --- 65 (G1): `gate run` with no SHA8 -> rc 2, "needs a <SHA8> [BATCH]" ------
# GREEN by construction: the current dispatch (jc_harness_main.c:1713-1718)
# already says this. Must stay green after the orchestrator lands.
"$HBIN" gate run --config "$tmp/gcfg.json" < /dev/null > "$tmp/g65.out" 2> "$tmp/g65.err"
_rc65=$?
if [ "$_rc65" -eq 2 ] && grep -qF "needs a <SHA8> [BATCH]" "$tmp/g65.err"; then
    t_ok "67: G1 gate run (no SHA8) -> rc 2 naming \"needs a <SHA8> [BATCH]\""
else
    t_fail "67: G1 expected rc 2 + \"needs a <SHA8> [BATCH]\" (got rc=$_rc65: $(tr '\n' '|' < "$tmp/g65.err" | head_bytes 160))"
fi

# --- 66 (G2): three positionals -> rc 2, naming that the BATCH is at most one
# positional (b15).
"$HBIN" gate run --config "$tmp/gcfg.json" "$G2c_8" B extra < /dev/null > "$tmp/g.out" 2> "$tmp/g.err"
_rc66=$?
if [ "$_rc66" -eq 2 ] && grep -qF "BATCH" "$tmp/g.err" && grep -qF "positional" "$tmp/g.err"; then
    t_ok "68: G2 three positionals (SHA8 + two) -> rc 2, names that the BATCH is at most one positional (b15)"
else
    t_fail "68: G2 expected rc 2 + a positional-count/BATCH usage refusal (got rc=$_rc66: $(tr '\n' '|' < "$tmp/g.err" | head_bytes 160))"
fi

# --- 67 (G3): gcfg3 (gcfg WITHOUT ledgerAddress) + no --ledger-address ->
# rc 2, naming --ledger-address (b1's CHANGED REQUIRED).
"$HBIN" gate run --config "$tmp/gcfg3.json" "$G2c_8" B < /dev/null > "$tmp/g.out" 2> "$tmp/g.err"
_rc67=$?
if [ "$_rc67" -eq 2 ] && grep -qF -- "--ledger-address" "$tmp/g.err"; then
    t_ok "69: G3 no --ledger-address and none in the config -> rc 2, names --ledger-address (b1's CHANGED REQUIRED)"
else
    t_fail "69: G3 expected rc 2 naming --ledger-address (got rc=$_rc67: $(tr '\n' '|' < "$tmp/g.err" | head_bytes 160))"
fi

# --- 68 (G4): <fake8> not in gpush.git -> rc 1; the state file EXACTLY one
# line "<fake8> red-setup <ts>" (D8: setup refused, no gated); the D4.10 note;
# the say on stdout. THE RED-FIRST EVIDENCE CELL (rc 2 + "not built yet"
# today: the dispatch's refusal, not an orchestrator run).
FAKE8="deadbeef"
_grun "$tmp/gcfg.json" "$FAKE8" B --state "$tmp/gstate4"
_gg4note=$(_gnote "$FAKE8")
_gg4tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate4" "${FAKE8}:red-setup" \
    && printf '%s\n' "$_gg4note" | grep -qF "RED (setup) on BATCH B READY at $FAKE8. The commit is not in the host's push.git.${_gg4tail}" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT red (setup): The commit is not in the host's push.git."; then
    t_ok "70: G4 <fake8> not in the host's push.git -> rc 1; state EXACTLY one line \"<fake8> red-setup <ts>\"; the D4.10 RED (setup) note; the say on stdout"
else
    t_fail "70: G4 expected rc 1 + the red-setup state + the D4.10 note (got rc=$_grc; state: $( [ -f "$tmp/gstate4" ] && tr '\n' '|' < "$tmp/gstate4" | head_bytes 120 || echo ABSENT ); note: $(printf '%s' "$_gg4note" | tr '\n' '|' | head_bytes 160))"
fi

# --- 69 (G5): the gside commit (off G0: NOT a fast-forward of G1c =
# origin/master, verified in a scratch clone before the fixture was pinned)
# -> rc 1; state "<s8> red-not-ff"; the D4.10 note naming master <G1c-8>.
_grun "$tmp/gcfg.json" "$Gside_8" B --state "$tmp/gstate5"
_gg5note=$(_gnote "$Gside_8")
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate5" "${Gside_8}:red-not-ff" \
    && printf '%s\n' "$_gg5note" | grep -qF "RED (not-ff) on BATCH B READY at $Gside_8. It is not a fast-forward of master $G1c_8.${_gg4tail}" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT red (not-ff): It is not a fast-forward of master $G1c_8."; then
    t_ok "71: G5 the gside commit -> rc 1; state \"<s8> red-not-ff\"; the D4.10 RED (not-ff) note naming master $G1c_8"
else
    t_fail "71: G5 expected rc 1 + the red-not-ff state + the D4.10 not-ff note (got rc=$_grc; state: $( [ -f "$tmp/gstate5" ] && tr '\n' '|' < "$tmp/gstate5" | head_bytes 120 || echo ABSENT ))"
fi

# --- 71 (G7): the G3bad run (ledger-red). rc 1; the say line "ledger: 1
# commit(s), 1 bad" (N=1: the range origin/master(G1c)..G3bad is G3bad alone
# -- D12's G7 row; the candidate's G2c-parented G3bad made it N=2, corrected);
# the D4.10 note whose join is the one expected bad line, cut 140, tr '\n' ';'.
# Its OWN state file so 70 (G6) can assert on it.
_grun "$tmp/gcfg.json" "$G3bad_8" B --state "$tmp/gstate7"
_gg7rl="$tmp/gh/$G3bad_8/run.log"
_gg7note=$(_gnote "$G3bad_8")
_gg7bad="$G3bad_8 no-Co-Authored-By no-Jichi-Build no-Jichi-Model"
_gg7join=$(printf '%s\n' "$_gg7bad" | cut -c1-140 | tr '\n' ';')
_gg7tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && grep -qF "ledger: 1 commit(s), 1 bad" "$_gg7rl" \
    && _gstateok "$tmp/gstate7" "${G3bad_8}:gated" "${G3bad_8}:red-ledger" \
    && printf '%s\n' "$_gg7note" | grep -qF "RED (ledger) on BATCH B READY at $G3bad_8. ledger: 1 commit(s), 1 bad: commit identity or trailers broken (author, committer and every Co-Authored-By on the operator's address; Jichi-Build and Jichi-Model present): ${_gg7join}${_gg7tail}" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT red (ledger): ledger: 1 commit(s), 1 bad: commit identity or trailers broken"; then
    t_ok "73: G7 the G3bad run -> rc 1; say line \"ledger: 1 commit(s), 1 bad\" (N=1, the corrected count); state {gated, red-ledger}; the D4.10 RED (ledger) note (the join = the one bad line, cut 140, tr '\n' ';')"
else
    t_fail "73: G7 expected rc 1 + \"ledger: 1 commit(s), 1 bad\" + {gated, red-ledger} state + the D4.10 ledger note (got rc=$_grc; run.log: $( [ -f "$_gg7rl" ] && tr '\n' '|' < "$_gg7rl" | head_bytes 160 || echo ABSENT ); note: $(printf '%s' "$_gg7note" | tr '\n' '|' | head_bytes 160))"
fi

# --- 70 (G6): the G3bad run's state file: line 1 = "<s8> gated <ts>", line 2 =
# "<s8> red-ledger <ts>" (D9: gated FIRST).
if _gstateok "$tmp/gstate7" "${G3bad_8}:gated" "${G3bad_8}:red-ledger"; then
    t_ok "72: G6 the G3bad run's state file: line 1 = \"<s8> gated <ts>\", line 2 = \"<s8> red-ledger <ts>\" (D9: gated first, then the ledger word)"
else
    t_fail "72: G6 the G3bad state file must be {gated, red-ledger} in order (got: $( [ -f "$tmp/gstate7" ] && tr '\n' '|' < "$tmp/gstate7" | head_bytes 120 || echo ABSENT ))"
fi

# --- 72 (G8): the G2c run with --build-run "false" -> the sweep child rc 1
# ("build failed: INVALID", no records line) -> the D10 INVALID promotion,
# rc 0.
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate8" _gflags --build-run "false"
_gg8note=$(_gnote "$G2c_8")
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate8" "${G2c_8}:gated" "${G2c_8}:invalid-sweep" \
    && printf '%s\n' "$_gg8note" | grep -qF "INVALID (sweep) on BATCH B READY at $G2c_8. build failed: INVALID; Change nothing, wait: the gate's own tool faulted, the batch is not judged. (A re-gate is the watcher's regate arm, or fix the tool and let the next tick take it.)" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT invalid (sweep)" \
    && [ ! -e "$tmp/gh/$G2c_8/sweep.verdict" ]; then
    t_ok "74: G8 --build-run \"false\" -> the sweep child rc 1 (build failed: INVALID, no records line) -> the D10 INVALID promotion: rc 0; state {gated, invalid-sweep}; the D4.11 INVALID (sweep) note (the record's last three lines, cut 200); no sweep.verdict"
else
    t_fail "74: G8 expected rc 0 + {gated, invalid-sweep} + the INVALID (sweep) note (got rc=$_grc; note: $(printf '%s' "$_gg8note" | tr '\n' '|' | head_bytes 160))"
fi

# --- 73 (G9): the HOLD arm (b7). $tmp/gh/HOLD created; the full stub set;
# land+repin stubs NAMED -> rc 0; state {gated, green-held}; the land stub's
# argv file ABSENT (no land call); the green-held note; sweep.verdict = green.
_gg9l="$tmp/gland9.txt"; rm -f "$_gg9l"
mkdir -p "$tmp/gh/HOLD"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate9" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $_gg9l" --repin-run "$tmp/gstubrepinclean /tmp/grepin9.txt"
_gg9note=$(_gnote "$G2c_8")
_gg9sv="$tmp/gh/$G2c_8/sweep.verdict"
_gg9sweepline=$(grep -F "sweep: ran 1 drivers on $G2c_8" "$tmp/gh/$G2c_8/sweep.txt" 2>/dev/null | tail -1)
_gg9smokel=$(grep '^smoke: OK' "$tmp/gh/ci/ci-$G2c_8.log" 2>/dev/null | tail -1)
_gg9okl=$(grep '^ci: OK' "$tmp/gh/ci/ci-$G2c_8.log" 2>/dev/null | tail -1)
_gg9rflast=$(tail -1 "$tmp/gh/$G2c_8/redfirst.txt" 2>/dev/null)
_gg9want="GREEN on BATCH B READY at $G2c_8 (1 commits; $_gg9sweepline; $_gg9smokel; $_gg9okl; $_gg9rflast). Held for the operator ($tmp/gh/HOLD): master is not moved yet. Go on with the next batch."
rm -rf "$tmp/gh/HOLD"
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate9" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ ! -e "$_gg9l" ] \
    && grep -qxF "green" "$_gg9sv" \
    && printf '%s\n' "$_gg9note" | grep -qF "$_gg9want" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT green, held ($tmp/gh/HOLD)"; then
    t_ok "75: G9 HOLD -> rc 0; state {gated, green-held}; the land stub's argv file ABSENT (no land call); the green-held note (the D12 byte shape, the HOLD path); sweep.verdict = green"
else
    t_fail "75: G9 expected rc 0 + green-held state + the green-held note + no land call + sweep.verdict green (got rc=$_grc; land-argv-present=$([ -e "$_gg9l" ] && echo yes || echo no ); note: $(printf '%s' "$_gg9note" | tr '\n' '|' | head_bytes 200))"
fi

# --- 74 (G10): no HOLD, no --land-run, no host.landRun -> rc 1; state
# {gated, green-land-refused}; M1154's named-refusal note.
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate10" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci"
_gg10note=$(_gnote "$G2c_8")
# M1202 wave 2b: D2's default would hold this cell (the gate creates the
# HOLD from the config); the cell names auto to reach the land step (here:
# the green-land-refused terminal). H10 IS built (wave 1), so the note
# drops the "H10 is not built" words (the switch-over message stays).
_gg10want="GREEN on BATCH B READY at $G2c_8, but no land step is named (host.landRun absent; the reference's land.sh stands for the switch-over). master is not moved."
# F5d (M1202 wave 2b follow-up): the D3 record's FIELD bytes for the
# no-land-step terminal -- the LAST landing.decision line carries
# 'no-land-step' AND 'decision=refused'.
_gg10last=$(tail -1 "$tmp/gh/$G2c_8/landing.decision" 2>/dev/null)
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate10" "${G2c_8}:gated" "${G2c_8}:green-land-refused" \
    && printf '%s\n' "$_gg10note" | grep -qF "$_gg10want" \
    && printf '%s' "$_gg10last" | grep -q "no-land-step" \
    && printf '%s' "$_gg10last" | grep -q "decision=refused"; then
    t_ok "76: G10 no HOLD / no land step -> rc 1; state {gated, green-land-refused}; M1154's named-refusal note; the last landing.decision line carries 'no-land-step' AND 'decision=refused' (F5d)"
else
    t_fail "76: G10 expected rc 1 + green-land-refused state + the named-refusal note + the no-land-step/decision=refused record fields (got rc=$_grc; note: $(printf '%s' "$_gg10note" | tr '\n' '|' | head_bytes 200); last-decision: $(printf '%s' "$_gg10last" | head_bytes 160))"
fi

# --- 75 (G11): --land-run the land-fail stub -> rc 1; state
# {gated, green-land-failed}; the land-failed note (last line cut 160); the
# stub's output appended to run.log. M1202 wave 2b: D2's default would hold
# this cell; the cell names auto to reach the land step.
_gg11l="$tmp/gland11.txt"; rm -f "$_gg11l"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate11" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandfail $_gg11l"
_gg11note=$(_gnote "$G2c_8")
_gg11want="GREEN on BATCH B READY at $G2c_8, but the landing failed (land-stub: boom). The operator is told; master may be unchanged. Go on with the next batch."
# F5c (M1202 wave 2b follow-up): the D3 record's FIELD bytes for the
# land-failed terminal -- the LAST landing.decision line carries
# 'decision=refused' AND 'land-rc=<n>' (the stub's rc, any number).
_gg11last=$(tail -1 "$tmp/gh/$G2c_8/landing.decision" 2>/dev/null)
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate11" "${G2c_8}:gated" "${G2c_8}:green-land-failed" \
    && [ -s "$_gg11l" ] \
    && grep -qF "land-stub: boom" "$tmp/gh/$G2c_8/run.log" \
    && printf '%s\n' "$_gg11note" | grep -qF "$_gg11want" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT green, the landing FAILED rc=1" \
    && printf '%s' "$_gg11last" | grep -qE 'decision=refused.*land-rc=[0-9]+'; then
    t_ok "77: G11 the land-fail stub -> rc 1; state {gated, green-land-failed}; the land-failed note (the stub's last line, cut 160); the stub's output appended to run.log; the last landing.decision line carries 'decision=refused' AND 'land-rc=<n>' (F5c)"
else
    t_fail "77: G11 expected rc 1 + green-land-failed + the land-failed note + the land output in run.log + the decision=refused/land-rc= record fields (got rc=$_grc; note: $(printf '%s' "$_gg11note" | tr '\n' '|' | head_bytes 200); last-decision: $(printf '%s' "$_gg11last" | head_bytes 160))"
fi

# --- 76 (G12): land-ok + repin-clean -> rc 0; state {gated, landed, repin-rc0};
# the say "VERDICT green, LANDED: master is <G2c-8>"; the D4.9 GREEN-and-
# LANDED note with "bin/ re-pinned at <G2c-8> (done <G2c-8>, doctors clean)"
# (the sed strip of the repin's last line). M1202 wave 2b: D2's default
# would hold this cell; the cell names auto to reach the land step.
_gg12l="$tmp/gland12.txt"; rm -f "$_gg12l"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate12" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $_gg12l" --repin-run "$tmp/gstubrepinclean /tmp/grepin12.txt"
cp "$tmp/g.out" "$tmp/g12.out"
_gg12note=$(_gnote "$G2c_8")
_gl="$_gg12l"; _cl="$tmp/gh/ci"; _rl="$tmp/gh/$G2c_8"; _repin="re-pinned at $G2c_8 (done $G2c_8, doctors clean)"
_gg12want=$(_ggreenexp)
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate12" "${G2c_8}:gated" "${G2c_8}:landed" "${G2c_8}:repin-rc0" \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT green, LANDED: master is $G2c_8" \
    && [ -s "$_gg12l" ] \
    && printf '%s\n' "$_gg12note" | grep -qF "$_gg12want"; then
    t_ok "78: G12 land-ok + repin-clean -> rc 0; state {gated, landed, repin-rc0}; the say \"VERDICT green, LANDED: master is $G2c_8\"; the D4.9 GREEN-and-LANDED note with \"bin/ re-pinned at $G2c_8 (done $G2c_8, doctors clean)\" (the sed strip)"
else
    t_fail "78: G12 expected rc 0 + {gated, landed, repin-rc0} + the LANDED say + the D4.9 note (got rc=$_grc; want: $(printf '%s' "$_gg12want" | tr '\n' ' ' | head_bytes 160); note: $(printf '%s' "$_gg12note" | tr '\n' ' ' | head_bytes 160))"
fi

# --- 77 (G13): the repin hcn stub -> the note TAIL names 2 changed
# src/harness file(s) (the D4.9 <hcn> half). M1202 wave 2b: D2's default
# would hold this cell; the cell names auto to reach the land step.
_gg13l="$tmp/gland13.txt"; rm -f "$_gg13l"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate13" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $_gg13l" --repin-run "$tmp/gstubrepinhcn /tmp/grepin13.txt"
_gg13note=$(_gnote "$G2c_8")
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate13" "${G2c_8}:gated" "${G2c_8}:landed" "${G2c_8}:repin-rc0" \
    && printf '%s\n' "$_gg13note" | grep -qF "Branch the next batch from the team branch as before. The landing changed 2 src/harness file(s): the drive loop keeps its old harness code until the operator restarts it."; then
    t_ok "79: G13 the repin hcn stub (src/harness files changed abcdef12..<s>: 2) -> the note TAIL \"Branch the next batch from the team branch as before. The landing changed 2 src/harness file(s): the drive loop keeps its old harness code until the operator restarts it.\""
else
    t_fail "79: G13 expected the <hcn> note tail (The landing changed 2 src/harness file(s): ... restarts it.) (got rc=$_grc; note: $(printf '%s' "$_gg13note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 78 (G14): the repin rc-2 stub -> state {gated, landed, repin-rc2}; rc 0
# (the repin's rc NEVER fails the run, b10). M1202 wave 2b: D2's default
# would hold this cell; the cell names auto to reach the land step.
_gg14l="$tmp/gland14.txt"; rm -f "$_gg14l"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate14" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $_gg14l" --repin-run "$tmp/gstubrepinrc2 /tmp/grepin14.txt"
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate14" "${G2c_8}:gated" "${G2c_8}:landed" "${G2c_8}:repin-rc2"; then
    t_ok "80: G14 the repin rc-2 stub -> state {gated, landed, repin-rc2}; rc 0 (the repin's rc never fails the run, b10)"
else
    t_fail "80: G14 expected rc 0 + repin-rc2 in state (got rc=$_grc; state: $( [ -f "$tmp/gstate14" ] && tail -1 "$tmp/gstate14" | head_bytes 80 || echo ABSENT ))"
fi

# --- 79 (G14b): land-ok, NO --repin-run / host.repinRun -> state {gated,
# landed} (NO repin word); rc 0; the D4.9 note with the "NOT re-pinned" half.
# M1202 wave 2b: D2's default would hold this cell; the cell names auto to
# reach the land step.
_gg14bl="$tmp/gland14b.txt"; rm -f "$_gg14bl"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate14b" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $_gg14bl"
_gg14bnote=$(_gnote "$G2c_8")
_gl="$_gg14bl"; _cl="$tmp/gh/ci"; _rl="$tmp/gh/$G2c_8"; _repin="NOT re-pinned (host.repinRun absent: H11 is not built; the reference's repin.sh stands for the switch-over)"
_gg14bwant=$(_ggreenexp)
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate14b" "${G2c_8}:gated" "${G2c_8}:landed" \
    && printf '%s\n' "$_gg14bnote" | grep -qF "$_gg14bwant"; then
    t_ok "81: G14b land-ok, no repin step -> state {gated, landed} (NO repin word); rc 0; the D4.9 note with \"bin/ NOT re-pinned (host.repinRun absent: H11 is not built; the reference's repin.sh stands for the switch-over); your NEXT run uses it.\" (D4.9's CHANGED half)"
else
    t_fail "81: G14b expected rc 0 + {gated, landed} (no repin word) + the NOT-re-pinned note (got rc=$_grc; want: $(printf '%s' "$_gg14bwant" | tr '\n' ' ' | head_bytes 160))"
fi

# --- 80 (G15): the ci-fail stub, --ci-log-dir $tmp/gci -> rc 1; state
# {gated, red-ci}; the D4.6 RED (ci) note (make ci rc=0. + the :79 join + the
# Log-on-the-host tail); the EXCERPT ATTACH (the ## Attached: section + the
# file under $tmp/gci/).
_gg15l="$tmp/gland15.txt"; rm -f "$_gg15l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate15" _gflags --ci-run "$tmp/gstubcifail" --ci-log-dir "$tmp/gci" --land-run "$tmp/gstublandok $_gg15l"
_gg15note=$(_gnote "$G2c_8")
_gg15log="$tmp/gci/ci-$G2c_8.log"
_gg15join=$(grep -E '^smoke: FAILED --|^not ok|^\*\*\* \[|^ *FAIL ' "$_gg15log" 2>/dev/null | head -4 | cut -c1-160 | tr '\n' ';')
_gg15tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate15" "${G2c_8}:gated" "${G2c_8}:red-ci" \
    && printf '%s\n' "$_gg15note" | grep -qF "RED (ci) on BATCH B READY at $G2c_8. make ci rc=0. ${_gg15join} Log on the host: $tmp/gci/ci-$G2c_8.log.${_gg15tail}" \
    && [ -e "$tmp/gci/ci-$G2c_8.excerpt" ] \
    && printf '%s\n' "$_gg15note" | grep -qF "## Attached: ci-$G2c_8.excerpt" \
    && printf '%s\n' "$_gg15note" | grep -qF "not ok 2 - two"; then
    t_ok "82: G15 the ci-fail stub -> rc 1; state {gated, red-ci}; the D4.6 RED (ci) note (make ci rc=0. + the :79 join + the Log-on-the-host tail); the excerpt attached (## Attached: ci-$G2c_8.excerpt) AND the file exists under $tmp/gci/"
else
    t_fail "82: G15 expected rc 1 + red-ci state + the D4.6 note + the excerpt attach (got rc=$_grc; note: $(printf '%s' "$_gg15note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 81 (G16): the ci INVALID: --ci-log-dir /proc/nonexistent-<$$>/ci (the
# body's home-mkdir fails -> stage rc 1 -> the D10 invalid-ci arm) -> rc 0;
# state {gated, invalid-ci}; the note prefix.
_gg16l="$tmp/gland16.txt"; rm -f "$_gg16l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate16" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "/proc/nonexistent-$$/ci" --land-run "$tmp/gstublandok $_gg16l"
_gg16note=$(_gnote "$G2c_8")
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate16" "${G2c_8}:gated" "${G2c_8}:invalid-ci" \
    && printf '%s\n' "$_gg16note" | grep -qF "INVALID (ci) on BATCH B READY at $G2c_8." \
    && printf '%s\n' "$_gg16note" | grep -qF "Change nothing, wait: the gate's own tool faulted, the batch is not judged." \
    && printf '%s\n' "$_gout" | grep -qF "VERDICT invalid (ci)"; then
    t_ok "83: G16 --ci-log-dir /proc/nonexistent-<$$>/ci (the home cannot be made; the body's rc 1) -> the D10 invalid-ci arm: rc 0; state {gated, invalid-ci}; the note prefix \"INVALID (ci) on BATCH B READY at $G2c_8.\""
else
    t_fail "83: G16 expected rc 0 + invalid-ci state + the INVALID (ci) note (got rc=$_grc; note: $(printf '%s' "$_gg16note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 82 (G17): the G12 run's run.log: the say lines present (timestamp-
# prefixed; the cell greps the text half).
_gg17rl="$tmp/gh/$G2c_8/run.log"
if [ -f "$_gg17rl" ] \
    && grep -qF "gate: BATCH B at $G2c; master $G1c" "$_gg17rl" \
    && grep -qF "sweep: start" "$_gg17rl" \
    && grep -qF "ledger: 1 commit(s), 0 bad" "$_gg17rl" \
    && grep -qF "sweep: green" "$_gg17rl" \
    && grep -qF "redfirst: 0 red-first commit(s), 0 red, 0 green, 0 skip" "$_gg17rl" \
    && grep -qF "ci: rc=0 ci: OK" "$_gg17rl" \
    && grep -qF "VERDICT green, LANDED: master is $G2c_8" "$_gg17rl"; then
    t_ok "84: G17 the G12 run.log carries the say lines (timestamp-prefixed): \"gate: BATCH B at <G2c-full>; master <G1c-full>\", \"sweep: start\", \"ledger: 1 commit(s), 0 bad\", \"sweep: green\", \"redfirst: 0 red-first commit(s), 0 red, 0 green, 0 skip\", \"ci: rc=0 ci: OK\", \"VERDICT green, LANDED: master is $G2c_8\""
else
    t_fail "84: G17 the G12 run.log is missing a say line (run.log: $( [ -f "$_gg17rl" ] && tr '\n' '|' < "$_gg17rl" | head_bytes 240 || echo ABSENT ))"
fi

# --- 83 (G18): the state file APPENDS: two consecutive refused runs (a fresh
# state file via --state) -> 4 lines; the first run's gated +
# green-land-refused still at lines 1-2 (b13 NEVER truncating). M1202 wave
# 2b: D2's default would hold this cell; the cell names auto to reach the
# land step (here: the green-land-refused terminal).
_gg18s="$tmp/gstate18"
_grun "$tmp/gcfga.json" "$G2c_8" B _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --state "$_gg18s"
_grun "$tmp/gcfga.json" "$G2c_8" B _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --state "$_gg18s"
if [ "$_grc" -eq 1 ] \
    && _gstateok "$_gg18s" "${G2c_8}:gated" "${G2c_8}:green-land-refused" "${G2c_8}:gated" "${G2c_8}:green-land-refused"; then
    t_ok "85: G18 the state file APPENDS: two consecutive refused runs (a fresh --state) -> 4 lines; the first run's gated + green-land-refused still at lines 1-2 (b13 NEVER truncating)"
else
    t_fail "85: G18 expected 4 state lines (gated/green-land-refused twice) (got: $( [ -f "$_gg18s" ] && tr '\n' '|' < "$_gg18s" | head_bytes 160 || echo ABSENT ))"
fi

# --- 84 (G19): the say-on-stdout (b12 CHANGED): the G12 run's stdout carries
# the gate + VERDICT says.
if grep -qF "gate: BATCH B at $G2c; master $G1c" "$tmp/g12.out" \
    && grep -qF "VERDICT green, LANDED: master is $G2c_8" "$tmp/g12.out"; then
    t_ok "86: G19 the say-on-stdout (b12 CHANGED): the run's stdout carries \"gate: BATCH B at\" and \"VERDICT green, LANDED\" (the reference's say is run.log only)"
else
    t_fail "86: G19 the stdout must carry the say lines (stdout: $(printf '%s' "$_gout" | tr '\n' '|' | head_bytes 200))"
fi

# --- 85 (G20): the notify delivery: the G12 run's inbox note is the
# GREEN-and-LANDED text, and $tmp/gh/digest.md gained a line.
# G18 (the two refused runs) clobbered the $G2c_8 inbox slot and _gnote
# reads the newest -- so clean and re-run the G12 land-ok + repin-clean
# command (fresh artifacts) to restore the GREEN-and-LANDED note. M1202 wave
# 2b: D2's default would hold this cell; the cell names auto to reach the
# land step.
_gcleaninbox "$G2c_8"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate20" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --land-run "$tmp/gstublandok $tmp/gland20.txt" --repin-run "$tmp/gstubrepinclean /tmp/grepin20.txt"
_gg20note=$(_gnote "$G2c_8")
if [ -n "$_gg20note" ] && printf '%s\n' "$_gg20note" | grep -qF "GREEN and LANDED: BATCH B READY at $G2c_8 is master now" \
    && [ -f "$tmp/gh/digest.md" ] \
    && grep -qE "$G2c_8.*GREEN and LANDED" "$tmp/gh/digest.md"; then
    t_ok "87: G20 the notify delivery: the run's inbox note carries the GREEN-and-LANDED text and $tmp/gh/digest.md gained a line (b13's in-process notify; the driver's config dir is the drive dir)"
else
    t_fail "87: G20 expected the GREEN-and-LANDED inbox note + a digest.md line (note: $(printf '%s' "$_gg20note" | tr '\n' ' ' | head_bytes 160); digest: $( [ -f "$tmp/gh/digest.md" ] && tail -1 "$tmp/gh/digest.md" | head_bytes 120 || echo ABSENT ))"
fi

# --- 86 (G21): `gate run <G2c-8>` with NO batch -> the note names "BATCH ?"
# (the reference's ${2:-?}). M1202 wave 2b: D2's default would hold this
# cell; the cell names auto to reach the land step (here: the
# green-land-refused terminal).
_gcleaninbox "$G2c_8"
_grun "$tmp/gcfga.json" "$G2c_8" --state "$tmp/gstate21" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci"
_gg21note=$(_gnote "$G2c_8")
if [ "$_grc" -eq 1 ] \
    && printf '%s\n' "$_gg21note" | grep -qF "BATCH ? READY at $G2c_8" \
    && ! printf '%s\n' "$_gg21note" | grep -qF "BATCH B READY at $G2c_8"; then
    t_ok "88: G21 gate run <G2c-8> with NO batch -> the note names \"BATCH ?\" (the reference's \${2:-?})"
else
    t_fail "88: G21 expected the note to name BATCH ? (got rc=$_grc; note: $(printf '%s' "$_gg21note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 87 (G22): --state $tmp/gst -> the state lines in $tmp/gst, NOT in
# <gh>/state. M1202 wave 2b: D2's default would hold this cell; the cell
# names auto to reach the land step (here: the green-land-refused terminal).
_gg22s="$tmp/gst"
_gg22before=$( [ -f "$tmp/gh/state" ] && wc -l < "$tmp/gh/state" | tr -d ' ' || echo 0 )
_grun "$tmp/gcfga.json" "$G2c_8" B _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --state "$_gg22s"
_gg22after=$( [ -f "$tmp/gh/state" ] && wc -l < "$tmp/gh/state" | tr -d ' ' || echo 0 )
if [ "$_grc" -eq 1 ] \
    && _gstateok "$_gg22s" "${G2c_8}:gated" "${G2c_8}:green-land-refused" \
    && [ "$_gg22before" = "$_gg22after" ]; then
    t_ok "89: G22 --state $tmp/gst -> the state lines in $tmp/gst, NOT in <gh>/state"
else
    t_fail "89: G22 expected the state in $tmp/gst and <gh>/state unchanged (got rc=$_grc; gst: $( [ -f "$_gg22s" ] && tr '\n' '|' < "$_gg22s" | head_bytes 120 || echo ABSENT ); gh-state $_gg22before->$_gg22after)"
fi

# --- 88 (G23): --gate-home $tmp/gh2 -> run.log at $tmp/gh2/<G2c-8>/run.log,
# state at $tmp/gh2/state (the flag beats the config's gateHome). M1202 wave
# 2b: D2's default would hold this cell; the cell names auto to reach the
# land step (here: the green-land-refused terminal).
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gh2/state" --gate-home "$tmp/gh2" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh2/ci"
if [ "$_grc" -eq 1 ] \
    && [ -f "$tmp/gh2/$G2c_8/run.log" ] \
    && _gstateok "$tmp/gh2/state" "${G2c_8}:gated" "${G2c_8}:green-land-refused"; then
    t_ok "90: G23 --gate-home $tmp/gh2 -> run.log at $tmp/gh2/$G2c_8/run.log, state at $tmp/gh2/state (the flag beats the config's gateHome)"
else
    t_fail "90: G23 expected the run.log + state under $tmp/gh2 (got rc=$_grc; run.log: $([ -f "$tmp/gh2/$G2c_8/run.log" ] && echo present || echo ABSENT ))"
fi

# --- 89 (G24): --ledger-accept $tmp/gaccept (G3bad's full id) -> the run
# PROCEEDS past the ledger: the say line "ledger: 1 commit(s), 0 bad" (N=1,
# the corrected count; accepted commits do not count bad, b2's CHANGED), the
# ledger.txt "accepted:" line, and (full stub set, no landRun) the run ends
# in green-land-refused rc 1. M1202 wave 2b: D2's default would hold this
# cell; the cell names auto to reach the land step (here: the
# green-land-refused terminal).
printf '%s\n' "$G3bad" > "$tmp/gaccept"
_gg24s="$tmp/gstate24"
_grun "$tmp/gcfga.json" "$G3bad_8" B --state "$_gg24s" _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --ledger-accept "$tmp/gaccept"
_gg24rl="$tmp/gh/$G3bad_8/run.log"
_gg24note=$(_gnote "$G3bad_8")
if [ "$_grc" -eq 1 ] \
    && grep -qF "ledger: 1 commit(s), 0 bad" "$_gg24rl" \
    && grep -qE "$G3bad_8 accepted: no-Co-Authored-By no-Jichi-Build no-Jichi-Model \(G3bad\)" "$tmp/gh/$G3bad_8/ledger.txt" \
    && _gstateok "$_gg24s" "${G3bad_8}:gated" "${G3bad_8}:green-land-refused" \
    && printf '%s\n' "$_gg24note" | grep -qF "but no land step is named"; then
    t_ok "91: G24 --ledger-accept (G3bad's full id) -> the run PROCEEDS past the ledger: say \"ledger: 1 commit(s), 0 bad\" (N=1, corrected), the ledger.txt \"accepted: ...\" line, and (no landRun) ends in green-land-refused rc 1 (the accept path end to end, b2's CHANGED)"
else
    t_fail "91: G24 expected the ledger accept to proceed to green-land-refused (got rc=$_grc; run.log: $( [ -f "$_gg24rl" ] && tr '\n' '|' < "$_gg24rl" | head_bytes 160 || echo ABSENT ))"
fi

# --- 90 (G25): the config's gateHome (NO --gate-home flag; gcfg's gateHome is
# $tmp/gh) -> run.log at $tmp/gh/<G2c-8>/run.log (D3's flag-else-config).
# M1202 wave 2b: D2's default would hold this cell; the cell names auto to
# reach the land step (here: the green-land-refused terminal).
_grun "$tmp/gcfga.json" "$G2c_8" B _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gh/ci" --state "$tmp/gstate25"
if [ "$_grc" -eq 1 ] && [ -f "$tmp/gh/$G2c_8/run.log" ] \
    && _gstateok "$tmp/gstate25" "${G2c_8}:gated" "${G2c_8}:green-land-refused"; then
    t_ok "92: G25 the config's gateHome (no --gate-home flag) -> run.log at $tmp/gh/$G2c_8/run.log (D3's flag-else-config)"
else
    t_fail "92: G25 expected the run.log under $tmp/gh (got rc=$_grc; run.log: $([ -f "$tmp/gh/$G2c_8/run.log" ] && echo present || echo ABSENT ))"
fi

# --- 91 (G26): the sweep VERDICT red: the full set but --container-run with a
# failing line -> sweep rc 0 (completed), the verdict rc 1 -> rc 1; state
# {gated, red-sweep}; the D4.4 RED (sweep) note (the verdict's reason + the
# :59 grep-set join); NO sweep.verdict. The reason is read from the run's own
# say line (not re-derived): the `sweep: ` line of run.log that is neither
# `start` nor `ran`.
# The "NO sweep.verdict" assert tests that THIS red run does not write a green
# verdict (the reference's `red sweep` does `exit 1` BEFORE `echo green >
# sweep.verdict`, so a red run leaves no verdict of its own). G25 (the green
# cell above) just ran on the SAME $G2c_8 at gcfg's gate-home $tmp/gh and left
# `green` in <gh>/$G2c_8/sweep.verdict; the C (reference-faithful) never
# deletes it on a red path. Clear that stale file so the assert measures the
# red run's own behaviour, not G25's leftover (a real host gate-home is fresh
# per run, and green-then-red on one sha8 never happens without a reset).
rm -f "$tmp/gh/$G2c_8/sweep.verdict"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate26" \
    --build-run "true" --bin "true" --mutant-run "echo 'mutant: OK (1 drivers swept, 0 excluded)'" \
    --unit-run-san "echo '37960 checks, 0 failures'" --unit-run-o2 "echo '37960 checks, 0 failures'" \
    --container-run "echo '  container fetch_ssrf_reuse rc=1 0 ok, 0 not ok'; echo '  container resolve_timeout_connect rc=0 5 ok, 0 not ok'" \
    --ci-run "$tmp/gstubcigreen" --ci-log-dir "$tmp/gci26"
_gg26rl="$tmp/gh/$G2c_8/run.log"
_gg26note=$(_gnote "$G2c_8")
_gg26sv=$(grep -F "sweep: " "$_gg26rl" 2>/dev/null | grep -vF "sweep: start" | grep -vF "sweep: ran" | tail -1 | sed 's/^[0-9-]* [0-9:]* [+-][0-9]* sweep: //;s/^red: //')
_gg26lines=$(grep -E '^  RED|^sweep:|rc=[1-9]|not ok|INVALID' "$tmp/gh/$G2c_8/sweep.txt" 2>/dev/null | head -4 | cut -c1-140 | tr '\n' ';')
_gg26tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate26" "${G2c_8}:gated" "${G2c_8}:red-sweep" \
    && [ -n "$_gg26sv" ] \
    && printf '%s\n' "$_gg26note" | grep -qF "RED (sweep) on BATCH B READY at $G2c_8. The pre-gate sweep: ${_gg26sv}. Its lines: ${_gg26lines}${_gg26tail}" \
    && printf '%s\n' "$_gg26note" | grep -qF "container fetch_ssrf_reuse rc=1" \
    && [ ! -e "$tmp/gh/$G2c_8/sweep.verdict" ]; then
    t_ok "93: G26 the sweep VERDICT red (a failing container line) -> sweep rc 0 (completed), verdict rc 1 -> rc 1; state {gated, red-sweep}; the D4.4 RED (sweep) note (The pre-gate sweep: <verdict reason>. Its lines: <the :59 grep-set join>); NO sweep.verdict"
else
    t_fail "93: G26 expected rc 1 + red-sweep + the D4.4 note (got rc=$_grc; sv: $_gg26sv; lines: $_gg26lines; note: $(printf '%s' "$_gg26note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 92 (G30a): `gate units` (gcfg, gateRun present) -> rc 0; stdout the
# jhp-gate@.service template (the # GENERATED marker, [Unit], the Description
# naming %i, an ExecStart carrying the config's gateRun argv) + a generated
# marker (D13).
"$HBIN" gate units --config "$tmp/gcfg.json" < /dev/null > "$tmp/g92.out" 2> "$tmp/g92.err"
_rc92=$?
if [ "$_rc92" -eq 0 ] \
    && grep -qF "# GENERATED by jichi-harness gate units" "$tmp/g92.out" \
    && grep -qF "[Unit]" "$tmp/g92.out" \
    && grep -qE 'Description=.*%i' "$tmp/g92.out" \
    && grep -qF "ExecStart=$tmp/gh/standin.sh" "$tmp/g92.out" \
    && grep -qF "jhp-gate@" "$tmp/g92.out"; then
    t_ok "94: G30a gate units (gcfg, gateRun present) -> rc 0; stdout the jhp-gate@.service template (# GENERATED marker, [Unit], the Description naming %i, an ExecStart carrying the config's gateRun argv)"
else
    t_fail "94: G30a expected rc 0 + the jhp-gate@.service template (got rc=$_rc92: $(tr '\n' '|' < "$tmp/g92.out" | head_bytes 160))"
fi

# --- 93 (G30b): `gate units` (gcfg2, no gateRun) -> rc 2 naming host.gateRun
# (the note's behaviour 15 pin).
"$HBIN" gate units --config "$tmp/gcfg2.json" < /dev/null > "$tmp/g93.out" 2> "$tmp/g93.err"
_rc93=$?
if [ "$_rc93" -eq 2 ] && grep -qF "host.gateRun" "$tmp/g93.err"; then
    t_ok "95: G30b gate units (gcfg2, no gateRun) -> rc 2 naming host.gateRun (the note's behaviour 15 pin)"
else
    t_fail "95: G30b expected rc 2 + a host.gateRun naming (got rc=$_rc93: $(tr '\n' '|' < "$tmp/g93.err" | head_bytes 160))"
fi

# --- 94 (G15r): M1160-d cell 1 -- the ci RUN rc, not the STAGE rc. The stub
# fails like G15's but `exit 3`s: the RUN's rc is 3, yet the stage COMPLETES
# (its own rc is 0, so chrc is 0). The orchestrator must say `ci: rc=3 no
# 'ci: OK' line` and RED the note `make ci rc=3. ...`. RED BY CONSTRUCTION
# against the unfixed tree (the say + the note carry `rc=0` -- the STAGE rc).
# A FRESH gate-home ($tmp/gh94) so run.log / state measure only this run.
_gg94l="$tmp/gland94.txt"; rm -f "$_gg94l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate94" --gate-home "$tmp/gh94" \
    _gflags --ci-run "$tmp/gstubci3" --ci-log-dir "$tmp/gh94/ci"
_gg94rl="$tmp/gh94/$G2c_8/run.log"
_gg94note=$(_gnote "$G2c_8")
_gg94join=$(grep -E '^smoke: FAILED --|^not ok|^\*\*\* \[|^ *FAIL ' "$tmp/gh94/ci/ci-$G2c_8.log" 2>/dev/null | head -4 | cut -c1-160 | tr '\n' ';')
_gg94tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate94" "${G2c_8}:gated" "${G2c_8}:red-ci" \
    && grep -qF "ci: rc=3 no 'ci: OK' line" "$_gg94rl" \
    && printf '%s\n' "$_gg94note" | grep -qF "RED (ci) on BATCH B READY at $G2c_8. make ci rc=3. ${_gg94join} Log on the host: $tmp/gh94/ci/ci-$G2c_8.log.${_gg94tail}"; then
    t_ok "96: G15r the ci RUN rc (the failing stub `exit 3`s, the stage COMPLETES rc 0) -> rc 1; state {gated, red-ci}; the say \"ci: rc=3 no 'ci: OK' line\" AND the note \"make ci rc=3. ...\" (the RUN rc, not the STAGE rc 0)"
else
    t_fail "96: G15r expected rc 1 + red-ci + the say \"ci: rc=3 no 'ci: OK' line\" + the note \"make ci rc=3. ...\" (got rc=$_grc; run.log ci say: $(grep -F 'ci: rc=' "$_gg94rl" 2>/dev/null | head_bytes 120); note: $(printf '%s' "$_gg94note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- 95 (G15v): M1160-d cell 2 (SV:1438.1) -- the VERDICT half. The stub
# prints `ci: OK` AND `exit 3`s: a run whose `make ci` exits 3 yet has a
# non-empty OK line. The port (unfixed) sees the non-empty okl and LANDS;
# the reference reds on `cr -ne 0 || -z okl`. Expect RED (ci), NOT landed.
# A FRESH gate-home ($tmp/gh95).
_gg95l="$tmp/gland95.txt"; rm -f "$_gg95l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate95" --gate-home "$tmp/gh95" \
    _gflags --ci-run "$tmp/gstubci3ok" --ci-log-dir "$tmp/gh95/ci" --land-run "$tmp/gstublandok $_gg95l"
_gg95rl="$tmp/gh95/$G2c_8/run.log"
_gg95note=$(_gnote "$G2c_8")
_gg95tail=" Nothing was landed; master is unchanged. Fix it, then write a new BATCH READY line."
if [ "$_grc" -eq 1 ] \
    && _gstateok "$tmp/gstate95" "${G2c_8}:gated" "${G2c_8}:red-ci" \
    && [ ! -e "$_gg95l" ] \
    && grep -qF "ci: rc=3 ci: OK" "$_gg95rl" \
    && printf '%s\n' "$_gg95note" | grep -qF "RED (ci) on BATCH B READY at $G2c_8. make ci rc=3.  Log on the host: $tmp/gh95/ci/ci-$G2c_8.log.${_gg95tail}"; then
    t_ok "97: G15v the ci RUN rc in the VERDICT (SV:1438.1; the stub prints `ci: OK` AND `exit 3`s) -> RED (ci), NOT landed (no land call); the say \"ci: rc=3 ci: OK\" AND the note \"make ci rc=3. ...\" (the reference reds on `cr -ne 0 || -z okl`)"
else
    t_fail "97: G15v expected RED (ci) + no land call + the say \"ci: rc=3 ci: OK\" + the note \"make ci rc=3. ...\" (got rc=$_grc; state: $( [ -f "$tmp/gstate95" ] && tr '\n' '|' < "$tmp/gstate95" | head_bytes 120 || echo ABSENT ); land-argv-present=$([ -e "$_gg95l" ] && echo yes || echo no ); run.log ci say: $(grep -F 'ci: rc=' "$_gg95rl" 2>/dev/null | head_bytes 120); note: $(printf '%s' "$_gg95note" | tr '\n' ' ' | head_bytes 200))"
fi

# --- R1-R9: the redfirst stage (the redfirst.sh port) --------------------------
# A working repo <tmp>/refsrc + a bare <tmp>/ref.git (disjoint from the
# p-chain / push.git), commits by the _pc idiom (fixed identity + step dates).
# The per-cell commits use DISTINCT driver basenames so ONE stub (dispatching
# on the driver's basename, passed as $1) services every cell. R8 is the only
# unstubbed cell (it lands last, before R9).
RREPO="$tmp/refsrc"
mkdir -p "$RREPO"
git -C "$RREPO" init -q
_rcm() { # $1=commit-message (the RREPO analogue of _pc)
    GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
    GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
    GIT_AUTHOR_DATE="2026-11-09T00:00:00Z" GIT_COMMITTER_DATE="2026-11-09T00:00:00Z" \
    git -C "$RREPO" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "$1"
    git -C "$RREPO" rev-parse HEAD
}
# R0: plain (the range's BASE -- never red-first on its own).
printf 'r0\n' > "$RREPO/f"; git -C "$RREPO" add f
R0=$(_rcm "r0 plain")
# R1: the GREEN driver.
mkdir -p "$RREPO/tests/smoke"
printf '#!/bin/sh\necho ok\n' > "$RREPO/tests/smoke/gd.sh"
git -C "$RREPO" add tests/smoke/gd.sh
R1=$(_rcm "add driver gd")
# R2: the unit (any bytes).
printf 'int gu;\n' > "$RREPO/tests/test_gu.c"
git -C "$RREPO" add tests/test_gu.c
R2=$(_rcm "add unit gu")
# R3: the SKIP driver.
printf '#!/bin/sh\necho skip\n' > "$RREPO/tests/smoke/gk.sh"
git -C "$RREPO" add tests/smoke/gk.sh
R3=$(_rcm "add skip driver")
# R4: a src/ file (NOT red-first).
mkdir -p "$RREPO/src"
printf 'int x;\n' > "$RREPO/src/x.c"
git -C "$RREPO" add src/x.c
R4=$(_rcm "add src")
# R5: the excluded lib glob (NOT red-first).
mkdir -p "$RREPO/tests/smoke/lib"
printf '#!/bin/sh\necho common\n' > "$RREPO/tests/smoke/lib/common.sh"
git -C "$RREPO" add tests/smoke/lib/common.sh
R5=$(_rcm "add common lib")
# R6: the BUILD-FAIL + the unit (distinct driver gd_red).
printf '#!/bin/sh\necho red\n' > "$RREPO/tests/smoke/gd_red.sh"
printf 'int gu6;\n' > "$RREPO/tests/test_gu6.c"
git -C "$RREPO" add tests/smoke/gd_red.sh tests/test_gu6.c
R6=$(_rcm "retouch driver")
# R7: a docs note (NOT red-first).
mkdir -p "$RREPO/docs/internal"
printf 'note\n' > "$RREPO/docs/internal/note.md"
git -C "$RREPO" add docs/internal/note.md
R7=$(_rcm "add doc")
# R8: the driver-RED-by-not-ok (distinct driver gd_notok).
printf '#!/bin/sh\necho red\n' > "$RREPO/tests/smoke/gd_notok.sh"
git -C "$RREPO" add tests/smoke/gd_notok.sh
R8c=$(_rcm "add notok driver")
# R8a: the UNSTUBBED cell's Makefile fixture (non-red-first: the Makefile at
# root is "other"). Committed in its own step so it is present at R8b's
# checkout when the real build form runs.
printf 'jichi:\n\t@true\n\nsmoke-tools:\n\t@true\n' > "$RREPO/Makefile"
git -C "$RREPO" add Makefile
R8a=$(_rcm "add Makefile fixture")
# R8b: the UNSTUBBED cell's driver (red-first: only gd_make.sh changed; the
# Makefile is present from R8a). The real build form runs (make -j8 ...),
# seconds, not the host's 48-minute replay.
# The driver ALSO prints a `TMPDIR=<seen>` marker line (NOT ^ok / NOT ^not ok,
# so it does not change the verdict counts) carrying the TMPDIR its child saw:
# the M1 pin asserts the per-check file's marker is the STAGE's work tmp
# ($tmp/rw8/tmp), not the ambient TMPDIR.
printf '#!/bin/sh\nprintf "TMPDIR=%%s\\n" "$TMPDIR"; printf "not ok 1 - the red line\\n"; exit 1\n' > "$RREPO/tests/smoke/gd_make.sh"
git -C "$RREPO" add tests/smoke/gd_make.sh
R8b=$(_rcm "add make driver")
# The bare ref.git (origin/master at R0).
git clone -q --bare "$RREPO" "$tmp/ref.git"
git -C "$tmp/ref.git" update-ref refs/remotes/origin/master "$R0"
R0_8=$(printf '%.8s' "$R0"); R1_8=$(printf '%.8s' "$R1")
R2_8=$(printf '%.8s' "$R2"); R3_8=$(printf '%.8s' "$R3")
R5_8=$(printf '%.8s' "$R5"); R6_8=$(printf '%.8s' "$R6"); R8_8=$(printf '%.8s' "$R8c")
R8b_8=$(printf '%.8s' "$R8b"); R7_8=$(printf '%.8s' "$R7")
# The redfirst per-check stubs: the flag value is `sh $tmp/stubX.sh <d> <o>
# <b>` (the brief's 5-token surface). The stage does the redirection (the
# child's stdout goes to the per-check file); the stub PRINTS to stdout. The
# BUILD form passes <d> EMPTY; the DRIVER form passes the driver path -- so the
# stub tells them apart with `[ -z "$1" ]` and dispatches the driver on the
# <b> basename.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'if [ -z "$1" ]; then exit 0; fi   # the build (R1-R3,R5)'
  printf '%s\n' 'case $3 in'
  printf '%s\n' '  gd) printf "ok 1 - one\\n"; exit 0;;'
  printf '%s\n' '  gk) printf "1..0 # skip\\n"; exit 0;;'
  printf '%s\n' '  gd_notok) printf "ok 1 - one\\nnot ok 2 - two\\n"; exit 0;;'
  printf '%s\n' '  *) exit 0;; esac'
} > "$tmp/stubR.sh"
chmod +x "$tmp/stubR.sh"
# The R4 stub: the build PASSES, the gd driver exits 1 printing NOTHING
# (RED by rc, 0 ok, 0 not ok -- the brief's R4 shape).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'if [ -z "$1" ]; then exit 0; fi   # the build passes'
  printf '%s\n' 'case $3 in'
  printf '%s\n' '  gd) exit 1;;'
  printf '%s\n' '  *) exit 0;; esac'
} > "$tmp/stubR4.sh"
chmod +x "$tmp/stubR4.sh"
# The R6 stub: the build FAILS (exit 1, prints a line to the build.log); the
# driver never runs (the stage's build gate skips the checks).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'if [ -z "$1" ]; then echo build-fail; exit 1; fi'
  printf '%s\n' 'exit 0'
} > "$tmp/stubR6.sh"
chmod +x "$tmp/stubR6.sh"
# The per-cell unit stub (prints the R2 unit's failures summary).
{ printf '#!/bin/sh\n'
  printf '%s\n' 'printf "2 checks, 1 failures\\n"; exit 0'
} > "$tmp/stubRu.sh"
chmod +x "$tmp/stubRu.sh"
# _rrun <BASE> <HEAD> <work> <out> <records> <runflag> <unitflag> : run the
# redfirst stage; rc into $_rrc, combined out/err into $_rout. <runflag> is a
# FULL --redfirst-run "..." (build+driver, one template); <unitflag> a full
# --redfirst-unit-run "..." (or empty). An empty flag-string is dropped.
_rrun() {
    _rb="$1"; _rh="$2"; _rw="$3"; _ro="$4"; _rcr="$5"; _rf="$6"; _uf="$7"
    _rout="$tmp/rrcap.txt"
    mkdir -p "$_rw" "$_ro" "$_rcr"
    rm -f "$_rout"
    _rrc=2
    if [ -n "$_rf" ]; then
        if [ -n "$_uf" ]; then
            "$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "$_rb" "$_rh" \
                --push-repo "$tmp/ref.git" --work "$_rw" --out "$_ro" --records "$_rcr" \
                --redfirst-run "$_rf" --redfirst-unit-run "$_uf" \
                < /dev/null > "$_rout" 2>&1
        else
            "$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "$_rb" "$_rh" \
                --push-repo "$tmp/ref.git" --work "$_rw" --out "$_ro" --records "$_rcr" \
                --redfirst-run "$_rf" \
                < /dev/null > "$_rout" 2>&1
        fi
    else
        if [ -n "$_uf" ]; then
            "$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "$_rb" "$_rh" \
                --push-repo "$tmp/ref.git" --work "$_rw" --out "$_ro" --records "$_rcr" \
                --redfirst-unit-run "$_uf" \
                < /dev/null > "$_rout" 2>&1
        else
            "$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "$_rb" "$_rh" \
                --push-repo "$tmp/ref.git" --work "$_rw" --out "$_ro" --records "$_rcr" \
                < /dev/null > "$_rout" 2>&1
        fi
    fi
    _rrc=$?
}
# R1 (56): the selection + the GREEN driver (range R0..R1: only R1 qualifies).
_rrun "$R0_8" "$R1_8" "$tmp/rw1" "$tmp/r1out" "$tmp/r1rec" \
    "sh $tmp/stubR.sh <d> <o> <b>" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R1_8} GREEN driver gd (rc=0, 1 ok, 0 not ok) -- add driver gd" "$_rout" \
    && grep -qF "redfirst: 1 red-first commit(s), 0 red, 1 green, 0 skip" "$_rout" \
    && [ "$(tail -1 "$_rout")" = "records: $tmp/r1rec" ]; then
    t_ok "58: R1 the selection + the GREEN driver (R0..R1): the <R1-8> GREEN line, no src/lib/docs line, summary 1/0/1/0, records line, rc 0"
else
    t_fail "58: R1 the R0..R1 GREEN driver record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R2 (57): the unit RED + the driver GREEN in one range (R0..R2: R1 driver,
# R2 unit).
_rrun "$R0_8" "$R2_8" "$tmp/rw2" "$tmp/r2out" "$tmp/r2rec" \
    "sh $tmp/stubR.sh <d> <o> <b>" "sh $tmp/stubRu.sh"
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R1_8} GREEN driver gd (rc=0, 1 ok, 0 not ok) -- add driver gd" "$_rout" \
    && grep -qF "${R2_8} RED unit suite (2 checks, 1 failures; changed: tests/test_gu.c ) -- add unit gu" "$_rout" \
    && grep -qF "redfirst: 2 red-first commit(s), 1 red, 1 green, 0 skip" "$_rout"; then
    t_ok "59: R2 the unit RED + the driver GREEN (R0..R2): the R1 GREEN line, the R2 RED unit line, summary 2/1/1/0, rc 0"
else
    t_fail "59: R2 the R0..R2 driver-GREEN + unit-RED record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R3 (58): the SKIP (range R0..R3: R1 green, R2 unit red, R3 skip).
_rrun "$R0_8" "$R3_8" "$tmp/rw3" "$tmp/r3out" "$tmp/r3rec" \
    "sh $tmp/stubR.sh <d> <o> <b>" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R3_8} SKIP driver gk (rc=0, 0 ok, 0 not ok) -- add skip driver" "$_rout" \
    && grep -qF "redfirst: 3 red-first commit(s), 1 red, 1 green, 1 skip" "$_rout"; then
    t_ok "60: R3 the SKIP (R0..R3): the <R3-8> SKIP line, summary 3/1/1/1 (R2's unit red, R1 green, R3 skip), rc 0"
else
    t_fail "60: R3 the R0..R3 SKIP record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R4 (59): the driver RED by rc (range R0..R1; the R4 stub's build passes,
# the gd driver exits 1 printing NOTHING -- 0 ok, 0 not ok, the brief's R4
# line shape).
_rrun "$R0_8" "$R1_8" "$tmp/rw4" "$tmp/r4out" "$tmp/r4rec" \
    "sh $tmp/stubR4.sh <d> <o> <b>" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R1_8} RED driver gd (rc=1, 0 ok, 0 not ok) -- add driver gd" "$_rout"; then
    t_ok "61: R4 the driver RED by rc (R0..R1): the <R1-8> RED driver line (rc=1, 0 ok, 0 not ok), rc 0"
else
    t_fail "61: R4 the R0..R1 driver-RED-by-rc record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R5 (60): the driver RED by a not ok line (range R6..R8: only R8's gd_notok
# is red-first there; its build passes, the driver prints ok+not ok, rc 0).
_rrun "$R6_8" "$R8_8" "$tmp/rw5" "$tmp/r5out" "$tmp/r5rec" \
    "sh $tmp/stubR.sh <d> <o> <b>" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R8_8} RED driver gd_notok (rc=0, 1 ok, 1 not ok) -- add notok driver" "$_rout"; then
    t_ok "62: R5 the driver RED by not ok (R6..R8, the R8 notok): the <R8-8> RED driver line (rc=0, 1 ok, 1 not ok), rc 0"
else
    t_fail "62: R5 the R6..R8 driver-RED-by-not-ok record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R6 (61): the BUILD FAILED (range R5..R6: R6 is the ONLY red-first commit,
# its build stub exits 1, so R6's checks are skipped; R6 is in n but adds
# nothing to nr/ng/ns).
_rrun "$R5_8" "$R6_8" "$tmp/rw6" "$tmp/r6out" "$tmp/r6rec" \
    "sh $tmp/stubR6.sh <d> <o> <b>" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R6_8} BUILD FAILED at the red-first commit (retouch driver): see $tmp/r6out/${R6_8}-build.log" "$_rout" \
    && ! grep -qF "${R6_8} driver gd_red" "$_rout" \
    && grep -qF "redfirst: 1 red-first commit(s), 0 red, 0 green, 0 skip" "$_rout"; then
    t_ok "63: R6 the BUILD FAILED (R5..R6): the <R6-8> BUILD FAILED line (naming the build.log), no R6 driver line, summary 1/0/0/0 (R6 in n), rc 0"
else
    t_fail "63: R6 the R0..R6 BUILD FAILED record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R7 (62): the empty range (R0..R0) -> only the 0-summary + the records line.
_rrun "$R0_8" "$R0_8" "$tmp/rw7" "$tmp/r7out" "$tmp/r7rec" "" ""
if [ "$_rrc" -eq 0 ] \
    && grep -qF "redfirst: 0 red-first commit(s), 0 red, 0 green, 0 skip" "$_rout" \
    && [ "$(tail -1 "$_rout")" = "records: $tmp/r7rec" ]; then
    t_ok "64: R7 the empty range (R0..R0): only the 0-summary + the records line, rc 0"
else
    t_fail "64: R7 the R0..R0 empty-range record (got rc=$_rrc: $(tr '\n' '|' < "$_rout" | head_bytes 240))"
fi
# R8 (63): the UNSTUBBED cell -- the real build + driver forms against the
# two-target Makefile fixture (R8b). No --redfirst-run / --redfirst-unit-run:
# the reference's real forms run (the host's 48-minute replay is seconds here).
_rrun "$R8_8" "$R8b_8" "$tmp/rw8" "$tmp/r8out" "$tmp/r8rec" "" ""
# The M1 pin: the UNSTUBBED driver's per-check file carries the TMPDIR the
# child SAW (the `TMPDIR=<seen>` marker line the fixture driver prints). It
# must be the STAGE's work tmp ($tmp/rw8/tmp), not the ambient TMPDIR -- so
# the real (unstubbed) per-check child received TMPDIR=<work>/tmp.
if [ "$_rrc" -eq 0 ] \
    && grep -qF "${R8b_8} RED driver gd_make (rc=1, 0 ok, 1 not ok) -- add make driver" "$_rout" \
    && grep -qF "redfirst: 1 red-first commit(s), 1 red, 0 green, 0 skip" "$_rout" \
    && [ "$(tail -1 "$_rout")" = "records: $tmp/r8rec" ] \
    && grep -qF "TMPDIR=$tmp/rw8/tmp" "$tmp/r8out/${R8b_8}-gd_make.txt"; then
    t_ok "65: R8 the UNSTUBBED cell (R8c..R8b): the real build + driver forms (the two-target Makefile) -> the <R8b-8> RED driver gd_make line (the not-ok count in no), summary 1/1/0/0, records line, rc 0, AND the driver child saw TMPDIR=$tmp/rw8/tmp (the M1 env pin)"
else
    t_fail "65: R8 the R8c..R8b UNSTUBBED record (got rc=$_rrc; per-check TMPDIR: $(grep -F 'TMPDIR=' "$tmp/r8out/${R8b_8}-gd_make.txt" 2>/dev/null | head_bytes 80); $(tr '\n' '|' < "$_rout" | head_bytes 200))"
fi
# R9 (64): usage -- ONE positional -> exit 2 naming <BASE> and <HEAD>; a
# non-hex BASE -> exit 2. (Two assertions, one cell, the C8 idiom.)
"$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "$R0_8" \
    < /dev/null > "$tmp/r9a.out" 2> "$tmp/r9a.err"
_r9a=$?
"$HBIN" gate run --config "$tmp/cfg.json" --stage redfirst "zzzzzzz" "$R1_8" \
    < /dev/null > "$tmp/r9b.out" 2> "$tmp/r9b.err"
_r9b=$?
if [ "$_r9a" -eq 2 ] && grep -qF "needs <BASE> and <HEAD>" "$tmp/r9a.err" \
    && [ "$_r9b" -eq 2 ] && grep -qF "7-40 lowercase hex" "$tmp/r9b.err"; then
    t_ok "66: R9 usage: --stage redfirst with ONE positional -> exit 2 naming <BASE> and <HEAD>; a non-hex BASE -> exit 2"
else
    t_fail "66: R9 the redfirst usage refusals (got rc=$_r9a / rc=$_r9b: $(tr '\n' '|' < "$tmp/r9a.err" | head_bytes 80) || $(tr '\n' '|' < "$tmp/r9b.err" | head_bytes 80))"
fi

# --- 96 (M1178): a <SHA8> that is not a commit id -> rc 2, before anything is
# touched. Until M1178 the orchestrator pasted <SHA8> bare into `sh -c` (the
# setup's `cd '<repo>' && git rev-parse --verify -q <SHA8>^{commit}`), so a `;`
# in it ran the rest on the host; it also named the record home
# <gate-home>/<SHA8>. The payload creates $tmp/inj96, which only a shell
# running the injected command can make (the record home's mkdir -p nests the
# payload's slashes under <gate-home>, never at $tmp/inj96). Red on the
# pre-M1178 binary: the file exists and the state file has a red-setup line.
rm -f "$tmp/inj96" "$tmp/gstate96"
_grun "$tmp/gcfg.json" "abcdef0;touch $tmp/inj96;#" B --state "$tmp/gstate96"
if [ "$_grc" -eq 2 ] && [ ! -e "$tmp/inj96" ] && [ ! -e "$tmp/gstate96" ] \
    && grep -qF "the <SHA8> must be 7-40 lowercase hex" "$tmp/g.err"; then
    t_ok "98: a <SHA8> carrying ';touch ...' -> rc 2 naming 7-40 lowercase hex; the injected command did not run and no state line was written (M1178)"
else
    t_fail "98: expected rc 2, no \$tmp/inj96, no state file (got rc=$_grc; inj96: $( [ -e "$tmp/inj96" ] && echo PRESENT || echo absent ); state: $( [ -f "$tmp/gstate96" ] && tr '\n' '|' < "$tmp/gstate96" | head_bytes 120 || echo ABSENT ); err: $_gerr)"
fi

# --- 97 (M1178): a pushRepo path holding a `'` -> the setup still reads it.
# The setup's git children ran `cd '<pushRepo>' && git ...`, so a `'` in the
# path ended the quote and the `cd` failed: every commit read as "not in the
# host's push.git" (red-setup). Every word is single-quoted now. The gside
# commit (cell 69's) is used because its verdict comes from the setup's LAST
# git child, the fast-forward check: red-not-ff proves all three children
# reached the repo, where the pre-M1178 binary says red-setup.
ln -s "$tmp/gpush.git" "$tmp/q'push.git"
sed "s|\"pushRepo\":\"$tmp/gpush.git\"|\"pushRepo\":\"$tmp/q'push.git\"|" "$tmp/gcfg.json" > "$tmp/gcfgq.json"
_grun "$tmp/gcfgq.json" "$Gside_8" B --state "$tmp/gstate97"
if grep -qF "q'push.git" "$tmp/gcfgq.json" \
    && [ "$_grc" -eq 1 ] && _gstateok "$tmp/gstate97" "${Gside_8}:red-not-ff"; then
    t_ok "99: a pushRepo path holding a ' -> the setup's git children reach the repo: rc 1, state \"<s8> red-not-ff\", not red-setup (M1178)"
else
    t_fail "99: expected rc 1 + the red-not-ff state through a quoted pushRepo (got rc=$_grc; state: $( [ -f "$tmp/gstate97" ] && tr '\n' '|' < "$tmp/gstate97" | head_bytes 120 || echo ABSENT ); err: $_gerr)"
fi

# --- 100-104 (M1191-a, D1-D5): the dead-clone name collision (def-396) -------
# Deterministic red-first (the host's red depended on a runner's speed, so this
# group must NOT hope for a fast machine): the colliding name is PRE-CREATED --
# S0 = the current UTC second, <home>.dead-<S0> a real directory -- so a move
# in second S0 collides by construction, whatever the host's speed. A fake dead
# clone (the first <work>/ci-<P6_8>, with a .git dir) + one marker file per run
# (dead-1..dead-4, each placed just before its run) let every check say what
# went wrong. Four back-to-back ci runs on ONE work dir: each stage finds the
# previous stage's clone as the dead home and moves it aside.
#   D1 (100): all four stages end rc 0 (the unfixed same-second move nests,
#             then fails with rc 1 and NO record line -- the host's shape).
#   D2 (101): every "moved a dead clone aside: X -> Y" line's Y exists as a
#             directory and X no longer holds the dead clone (its marker) --
#             the record names the target ACTUALLY used.
#   D3 (102): the four Y targets are DISTINCT (a duplicate is the collision).
#   D4 (103): NO dead directory contains another clone (a .git below its own
#             top level) or a .dead-* directory (the mv-on-dir nesting).
#   D5 (104): the red-first precondition held -- at least two of the four
#             moves share a second (or a move landed in the pre-created
#             name's second); otherwise FAIL with the honest message, never
#             a skip.
_cD_w="$tmp/cwd100"
_cD_home="$_cD_w/ci-$P6_8"
_cD_S0=$(date -u +%Y%m%d-%H%M%S)
mkdir -p "$_cD_home/.git"
printf 'dead 1\n' > "$_cD_home/dead-1"
mkdir -p "$_cD_home.dead-$_cD_S0"
_cD_rcs=""
_cD_Y1=""; _cD_Y2=""; _cD_Y3=""; _cD_Y4=""; _cD_LN=0; _cD_FL=""
_cD_i=1
while [ "$_cD_i" -le 4 ]; do
    _crun "$P6_8" "$_cD_w" "$tmp/crd100" "$CSTUB"
    _cD_rcs="$_cD_rcs $_crc"
    _cD_ln=$(grep -F "moved a dead clone aside: " "$tmp/cout" | tail -1)
    if [ -n "$_cD_ln" ]; then
        _cD_LN=$((_cD_LN + 1))
        case "$_cD_i" in
            1) _cD_Y1=${_cD_ln#* -> } ;;
            2) _cD_Y2=${_cD_ln#* -> } ;;
            3) _cD_Y3=${_cD_ln#* -> } ;;
            4) _cD_Y4=${_cD_ln#* -> } ;;
        esac
    fi
    _cD_f=$(grep -F "could not move a dead clone aside" "$tmp/cout" | tail -1)
    [ -n "$_cD_f" ] && _cD_FL="$_cD_FL run$_cD_i: $_cD_f; "
    if [ "$_cD_i" -lt 4 ]; then
        printf 'dead %s\n' "$((_cD_i + 1))" > "$_cD_home/dead-$((_cD_i + 1))"
    fi
    _cD_i=$((_cD_i + 1))
done
# D1 (100): all four stages rc 0.
_cD_aok=1; _cD_awhy=""
for _cD_r in $_cD_rcs; do
    [ "$_cD_r" -eq 0 ] || { _cD_aok=0; _cD_awhy="$_cD_awhy rc=$_cD_r;"; }
done
[ -z "$_cD_FL" ] || _cD_awhy="$_cD_awhy failed-lines: $_cD_FL"
if [ "$_cD_aok" -eq 1 ]; then
    t_ok "100: D1 four back-to-back ci runs on one work dir (the colliding <home>.dead-<$_cD_S0> pre-created) all end rc 0 (no silent rc 1)"
else
    t_fail "100: D1 all four stages must end rc 0 (the same-second move must not nest or fail silently): rcs=[$_cD_rcs] $_cD_awhy"
fi
# D2 (101): every moved-aside line's Y is a directory holding the marker, and
# the source home no longer does (the record names the target actually used).
_cD_bother=""
_cD_i=1
while [ "$_cD_i" -le 4 ]; do
    case "$_cD_i" in
        1) _cD_y="$_cD_Y1" ;; 2) _cD_y="$_cD_Y2" ;; 3) _cD_y="$_cD_Y3" ;; 4) _cD_y="$_cD_Y4" ;;
    esac
    if [ -n "$_cD_y" ]; then
        [ -d "$_cD_y" ] || _cD_bother="$_cD_bother run$_cD_i's target is not a directory ($_cD_y); "
        [ ! -e "$_cD_home/dead-$_cD_i" ] || _cD_bother="$_cD_bother run$_cD_i's dead clone still at the source (dead-$_cD_i); "
        [ -e "$_cD_y/dead-$_cD_i" ] || _cD_bother="$_cD_bother $_cD_y does not hold dead-$_cD_i (the record does not name the real target); "
    else
        _cD_bother="$_cD_bother run$_cD_i has no moved-aside record line; "
    fi
    _cD_i=$((_cD_i + 1))
done
if [ -z "$_cD_bother" ]; then
    t_ok "101: D2 every 'moved a dead clone aside' line names a target that exists as a directory and whose source (the dead clone's marker) is gone from the home -- the record names the target actually used"
else
    t_fail "101: D2 the moved-aside records must name the real target and vacate the source: $_cD_bother"
fi
# D3 (102): the four targets are distinct (a duplicate is the collision).
_cD_c_ok=1
[ "$_cD_LN" -eq 4 ] || _cD_c_ok=0
_cD_seen=""
for _cD_y in "$_cD_Y1" "$_cD_Y2" "$_cD_Y3" "$_cD_Y4"; do
    [ -n "$_cD_y" ] || { _cD_c_ok=0; continue; }
    case " $_cD_seen " in
        *" $_cD_y "*) _cD_c_ok=0 ;;
    esac
    _cD_seen="$_cD_seen $_cD_y"
done
if [ "$_cD_c_ok" -eq 1 ]; then
    t_ok "102: D3 the four moved-aside targets are distinct ($_cD_Y1 | $_cD_Y2 | $_cD_Y3 | $_cD_Y4)"
else
    t_fail "102: D3 the four targets must be distinct (got $_cD_LN moved-aside lines): $_cD_Y1 | $_cD_Y2 | $_cD_Y3 | $_cD_Y4"
fi
# D4 (103): no dead directory contains another clone or a .dead-* directory.
_cD_d_other=""
for _cD_dead in $(find "$_cD_w" -name '*.dead-*' -type d 2>/dev/null); do
    for _cD_g in $(find "$_cD_dead" -name '.git' 2>/dev/null); do
        [ "$_cD_g" = "$_cD_dead/.git" ] || _cD_d_other="$_cD_d_other $_cD_dead holds another clone ($_cD_g); "
    done
    for _cD_dd in $(find "$_cD_dead" -mindepth 1 -name '*.dead-*' -type d 2>/dev/null); do
        _cD_d_other="$_cD_d_other $_cD_dead holds a .dead-* ($_cD_dd); "
    done
done
if [ -z "$_cD_d_other" ]; then
    t_ok "103: D4 no dead directory contains another clone (.git below its top level) or a .dead-* directory (no mv-on-dir nesting)"
else
    t_fail "103: D4 no dead directory may contain another clone or a .dead-* directory (the nesting): $_cD_d_other"
fi
# D5 (104): the red-first precondition held -- at least two moves share a
# second, or a move landed in the pre-created name's second. The stamp is the
# target with <home>.dead- stripped and any -N first-free suffix dropped.
_cD_secof() {
    _cD_s=${1#$_cD_home.dead-}
    case "$_cD_s" in
        *-*-*) _cD_s=${_cD_s%-*} ;;
    esac
    printf '%s' "$_cD_s"
}
_cD_s1=""; _cD_s2=""; _cD_s3=""; _cD_s4=""
[ -n "$_cD_Y1" ] && _cD_s1=$(_cD_secof "$_cD_Y1")
[ -n "$_cD_Y2" ] && _cD_s2=$(_cD_secof "$_cD_Y2")
[ -n "$_cD_Y3" ] && _cD_s3=$(_cD_secof "$_cD_Y3")
[ -n "$_cD_Y4" ] && _cD_s4=$(_cD_secof "$_cD_Y4")
_cD_pre=0
_cD_i=1
while [ "$_cD_i" -le 4 ]; do
    case "$_cD_i" in
        1) _cD_a="$_cD_s1" ;; 2) _cD_a="$_cD_s2" ;; 3) _cD_a="$_cD_s3" ;; 4) _cD_a="$_cD_s4" ;;
    esac
    if [ -n "$_cD_a" ]; then
        [ "$_cD_a" = "$_cD_S0" ] && { _cD_pre=1; break; }
        _cD_j=$((_cD_i + 1))
        while [ "$_cD_j" -le 4 ]; do
            case "$_cD_j" in
                2) _cD_b="$_cD_s2" ;; 3) _cD_b="$_cD_s3" ;; 4) _cD_b="$_cD_s4" ;;
            esac
            [ -n "$_cD_b" ] && [ "$_cD_a" = "$_cD_b" ] && { _cD_pre=1; break 2; }
            _cD_j=$((_cD_j + 1))
        done
    fi
    _cD_i=$((_cD_i + 1))
done
if [ "$_cD_pre" -eq 1 ]; then
    t_ok "104: D5 the red-first precondition held: at least two of the four moves share a second (or a move landed in the pre-created name's second) (S0=$_cD_S0; move seconds: $_cD_s1 $_cD_s2 $_cD_s3 $_cD_s4)"
else
    t_fail "104: D5 the red-first precondition did not hold: no same-second collision in four back-to-back moves (a host over 4x slower than expected); S0=$_cD_S0; move seconds: $_cD_s1 $_cD_s2 $_cD_s3 $_cD_s4"
fi

# --- 105 (M1197-b, B3): the teeth -- the sweep's BUILD must build all three
# programs so a driver that installs the full set (docs_install's shape) is
# SWEEPED, not skipped. Proves the EFFECT, not the string: a fixture repo
# whose ONE driver, d_gd_install.sh, t_skip's the whole driver unless three
# named program files exist (jichi, jichi-convert, jichi-harness -- the
# docs_install precondition shape) and then runs `"$BIN" --version`, CHECKING
# its output (so the hollow binary the mutant installs makes it RED, i.e. it
# "notices" the hollow bin -- the sweep's whole point).
#
# The cell drives the port's sweep stage with the DEFAULT build (NO
# --build-run) and the REAL `make smoke-mutant` (NO --mutant-run): the
# fixture's Makefile answers the build's <target>:
#   all   -> touch jichi + jichi-convert + jichi-harness (all three)
#   jichi -> touch jichi + jichi-harness ONLY (jichi-convert MISSING)
# so the B1 default (`make ... all smoke-tools`) builds all three and the
# driver runs its `"$BIN"` call -> the real scripts/mutant-sweep.sh sweeps it
# -> the record's mutant line reads "mutant: OK (1 drivers swept, 0 excluded)".
# WITHOUT B1 (the build reverts to `make ... jichi smoke-tools`) the driver's
# jichi-convert precondition fails -> it t_skip's -> the real mutant-sweep.sh
# counts it as "not swept" -> the mutant line reads "mutant: no changed
# drivers to sweep (1 excluded)" -> the cell goes RED. That is the instrument
# that fires on the defect: a NEW driver that installs the full program set,
# left unswept by a build that does not build the full set.
#
# The driver is named d_gd_install.sh (NOT *lint*, NOT docs_*, NOT
# reading_trace.sh), so the sweep's DRIVER LOOP does not collect it (the loop
# line is "ran 0 drivers") -- the teeth are in the MUTANT stage, exactly where
# the 2026-11-18 04:20 c54767bf gate went RED (the verdict's re-count expected
# docs_install swept; the sweep had skipped it). The unit san/o2 + container
# stages are STUBBED (the fixture has no test / drive-image); only the BUILD
# and the MUTANT are real.
_m105src="$tmp/m105src"
mkdir -p "$_m105src/tests/smoke" "$_m105src/scripts"
git -C "$_m105src" init -q
# The fixture's Makefile: the build's <target> is the cell's knob (all -> all
# three programs; jichi -> two, jichi-convert missing). smoke-mutant runs the
# REAL scripts/mutant-sweep.sh (copied from $ROOT, byte-for-byte).
printf 'all:\n\t@touch jichi jichi-convert jichi-harness && chmod +x jichi jichi-convert jichi-harness\njichi:\n\t@touch jichi jichi-harness && chmod +x jichi jichi-harness\nsmoke-tools:\n\t@true\nsmoke-mutant:\n\tsh scripts/mutant-sweep.sh\n' > "$_m105src/Makefile"
cp "$ROOT/scripts/mutant-sweep.sh" "$_m105src/scripts/mutant-sweep.sh"
# commit1 (the BASE, origin/master = MUTANT_BASE): Makefile + the real
# mutant-sweep.sh, NO driver.
git -C "$_m105src" add Makefile scripts/mutant-sweep.sh
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-18T00:00:00Z" GIT_COMMITTER_DATE="2026-11-18T00:00:00Z" \
git -C "$_m105src" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "M105 base (Makefile + real mutant-sweep, no driver)"
_M105_BASE=$(git -C "$_m105src" rev-parse HEAD)
# commit2 (the TIP, swept): ADDS the driver.
printf '#!/bin/sh\nBIN="${JC_SMOKE_BIN:-$PWD/jichi}"\nif [ ! -x jichi-convert ] || [ ! -x jichi-harness ] || [ ! -x jichi ]; then\n    echo "1..0"\n    echo "# skip: the three programs are not built (the sweep must build all three)"\n    exit 0\nfi\nout=$("$BIN" --version)\nif [ -n "$out" ]; then\n    echo "ok 1 - the binary reports a version"\nelse\n    echo "not ok 1 - the binary printed nothing (it is hollow)"\nfi\n' > "$_m105src/tests/smoke/d_gd_install.sh"
git -C "$_m105src" add tests/smoke/d_gd_install.sh
GIT_AUTHOR_NAME="Ledger Smoke" GIT_AUTHOR_EMAIL="$ADDR" \
GIT_COMMITTER_NAME="Ledger Smoke" GIT_COMMITTER_EMAIL="$ADDR" \
GIT_AUTHOR_DATE="2026-11-18T00:00:01Z" GIT_COMMITTER_DATE="2026-11-18T00:00:01Z" \
git -C "$_m105src" -c user.name="Ledger Smoke" -c user.email="$ADDR" commit -q -m "M105 add the install driver"
_M105_TIP=$(git -C "$_m105src" rev-parse HEAD)
_M105_TIP8=$(printf '%.8s' "$_M105_TIP")
# The bare push.git (the sweep's clone source): origin/master at the BASE
# (MUTANT_BASE), master's tip at the TIP (the swept commit).
git clone -q --bare "$_m105src" "$tmp/m105.git"
git -C "$tmp/m105.git" update-ref refs/remotes/origin/master "$_M105_BASE"
# Run the sweep stage with the DEFAULT build + REAL mutant (NO --build-run,
# NO --mutant-run). --bin is the host's built jichi (the driver loop runs 0
# drivers, so it is not invoked); the unit/container stages are stubbed.
_srun "$_M105_TIP8" --work "$tmp/m105sw" --records "$tmp/m105rec" \
    --bin "$ROOT/jichi" \
    --unit-run-san "true" --unit-run-o2 "true" \
    --container-run "echo '  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok'; echo '  container resolve_timeout_connect rc=0 5 ok, 0 not ok'" \
    --push-repo "$tmp/m105.git"
if [ "$_src" -eq 0 ] \
    && grep -qF "  build: $_M105_TIP8" "$tmp/sout" \
    && grep -qF "mutant: OK (1 drivers swept, 0 excluded)" "$tmp/sout" \
    && ! grep -qF "no changed drivers to sweep" "$tmp/sout" \
    && [ -x "$tmp/m105sw/j/jichi" ] \
    && [ -x "$tmp/m105sw/j/jichi-convert" ] \
    && [ -x "$tmp/m105sw/j/jichi-harness" ]; then
    t_ok "105: M1197-b B3 the teeth: the sweep's DEFAULT build (no --build-run) builds ALL THREE programs -- the fixture's d_gd_install.sh (a whole-driver t_skip unless jichi/jichi-convert/jichi-harness exist, then a checked \$BIN --version) is NOT skipped and IS swept by the real make smoke-mutant: the record's mutant line reads \"mutant: OK (1 drivers swept, 0 excluded)\", and the clone's three program files are all present and executable"
else
    t_fail "105: M1197-b B3 the teeth: the sweep's default build must build all three programs so the install driver is swept, not skipped (got rc=$_src; the clone's jichi/jichi-convert/jichi-harness must all be present and executable, and the mutant line must read \"mutant: OK (1 drivers swept, 0 excluded)\", NOT \"no changed drivers to sweep\"). sout: $(tr '\n' '|' < "$tmp/sout" | head_bytes 400)"
fi

# --- 106 (M1202 wave 2b, D2 + D3 + approve): the orchestrator's landing-
# policy half.
#   Part A -- the D2 DEFAULT hold arm. The BASE gcfg has NO landingPolicy
#   key, so D2's product DEFAULT (hold) applies: the gate CREATES
#   <gate-home>/HOLD from the config and the existing M1159 HOLD arm fires
#   green-held WITHOUT the land step. A FRESH gate home ($tmp/ghh) so HOLD /
#   landing.decision / the land argv file measure only this run. land+repin
#   stubs are NAMED -- so a defect that skips the D2 hold arm (lands instead
#   of holding) would create the land argv file and end `landed`, not
#   `green-held`: the cell is toothed on the land argv file's ABSENCE + the
#   green-held state. Expect: rc 0; state {gated, green-held}; HOLD PRESENT
#   (the gate created it); the land argv file ABSENT; the LAST
#   landing.decision line carries 'decision=pending'.
#   Part B -- the approve verb (D3's scope: the record + refuse-until-
#   approved + approve; the pending-queue UI is OUT). approve <s8> --gate-
#   home --land-run <stub> --operator the-operator reads the pending record,
#   runs the NAMED land step (the stub, the gate's capture idiom: <cmd> <s8>),
#   appends 'decision=approved ... operator=the-operator', and returns the
#   land step's rc (0). The stub's argv file is now PRESENT, carrying <s8>.
_gghh="$tmp/ghh"
_gghl="$tmp/glandhh.txt"; rm -f "$_gghl" "$tmp/glandap.txt"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstateh" --gate-home "$_gghh" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_gghh/ci" \
    --land-run "$tmp/gstublandok $_gghl" \
    --repin-run "$tmp/gstubrepinclean /tmp/gghr.txt"
_gghdec="$_gghh/$G2c_8/landing.decision"
_ggh_last=$(tail -1 "$_gghdec" 2>/dev/null)
# F5a (M1202 wave 2b follow-up): the D3 record's FIELD bytes for the pending
# terminal -- 'policy=hold' (D2's effective default, the word pinned),
# 'batch=B', and the time= field carrying the host's UTC offset, gf_now's %z.
# The time= field pin is ONE grep: the full 'time=<ts> <offset>' bytes (a
# `grep -qF time= | grep <offset>` pipeline would always fail -- -q prints
# nothing for the second grep to read).
# THE OFFSET IS THE HOST'S, read here with `date +%z` (2026-11-25). These four
# cells (106, 109, 111, 112) pinned ' +0100', Europe/Berlin in winter, so they
# went red on a hosted runner in UTC (GitHub run 45676610638: +0000, correct)
# and would go red on this host in summer (+0200). Compared with the host's
# own offset, a harness that wrote UTC where it should write local time is
# still caught wherever local time is not UTC. An offset `date` cannot give
# becomes a string no record holds, so a check can never pass on an empty one.
_gtz=$(date +%z)
case "$_gtz" in
    [+-][0-9][0-9][0-9][0-9]) _gtz_re="[${_gtz%????}]${_gtz#?}" ;;
    *) _gtz="date-gave-no-offset"; _gtz_re="date-gave-no-offset" ;;
esac
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstateh" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ -e "$_gghh/HOLD" ] \
    && [ ! -e "$_gghl" ] \
    && printf '%s' "$_ggh_last" | grep -q "decision=pending" \
    && printf '%s' "$_ggh_last" | grep -q "policy=hold" \
    && printf '%s' "$_ggh_last" | grep -q "batch=B" \
    && printf '%s' "$_ggh_last" | grep -qE "time=[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} $_gtz_re"; then
    # Part B: approve.
    "$HBIN" approve "$G2c_8" --gate-home "$_gghh" \
        --land-run "$tmp/gstublandok $tmp/glandap.txt" \
        --operator the-operator < /dev/null > "$tmp/gapprove106.out" 2>&1
    _ggaprc=$?
    _ggap_last=$(tail -1 "$_gghdec" 2>/dev/null)
    if [ "$_ggaprc" -eq 0 ] \
        && [ -e "$tmp/glandap.txt" ] \
        && grep -qF "$G2c_8" "$tmp/glandap.txt" \
        && printf '%s' "$_ggap_last" | grep -q "decision=approved" \
        && printf '%s' "$_ggap_last" | grep -q "operator=the-operator"; then
        t_ok "106: D2 default hold + the D3 landing.decision record + approve: the BASE config (no landingPolicy) HOLDS -- the gate CREATES \$tmp/ghh/HOLD, rc 0, state {gated, green-held}, NO land call (the land argv file ABSENT), the last landing.decision line 'decision=pending'; approve <s8> --operator the-operator runs the land stub (the argv file PRESENT, carrying <s8>) and the last line becomes 'decision=approved ... operator=the-operator' (rc 0)"
    else
        t_fail "106b: D2 default hold + approve: the approve half failed (approve rc=$_ggaprc; landap-argv=$([ -e "$tmp/glandap.txt" ] && echo present || echo absent ); last-decision: $(printf '%s' "$_ggap_last" | head_bytes 160))"
    fi
else
    t_fail "106: D2 default hold + the D3 record: the gate must CREATE the HOLD from the config and end green-held with NO land call + a 'decision=pending' landing.decision line (got rc=$_grc; HOLD: $([ -e "$_gghh/HOLD" ] && echo present || echo absent ); land-argv: $([ -e "$_gghl" ] && echo PRESENT || echo absent ); last-decision: $(printf '%s' "$_ggh_last" | head_bytes 160); state: $( [ -f "$tmp/gstateh" ] && tr '\n' '|' < "$tmp/gstateh" | head_bytes 120 || echo ABSENT ))"
fi

# --- 107 (M1202 wave 2b, D3): refuse-until-approved. A SECOND gate home
# ($tmp/ghl) with a FABRICATED green run whose landing.decision LAST line is
# 'decision=landed ...' (NOT pending): approve must REFUSE (rc 1, 'approve:
# refused: no pending landing for <s8>'), because only a PENDING landing may
# be approved -- approving a landed / held / absent one is refused. (No
# --land-run: the refusal fires at the pending-read, before the land-step
# resolution.)
_gglh="$tmp/ghl"
mkdir -p "$_gglh/$G2c_8"
printf 'decision=landed policy=auto sha8=%s batch=B time=2026-11-19 03:54:29 +0100 land-rc=0\n' "$G2c_8" > "$_gglh/$G2c_8/landing.decision"
"$HBIN" approve "$G2c_8" --gate-home "$_gglh" < /dev/null > "$tmp/gapprove107.out" 2>&1
_gglrc=$?
if [ "$_gglrc" -eq 1 ] \
    && grep -qF "approve: refused: no pending landing for $G2c_8" "$tmp/gapprove107.out"; then
    t_ok "107: refuse-until-approved: a gate home whose landing.decision LAST line is 'decision=landed ...' (not pending) -> approve <s8> REFUSES rc 1, 'approve: refused: no pending landing for <s8>' (only a pending landing may be approved)"
else
    t_fail "107: refuse-until-approved: approve must refuse a non-pending landing (rc=$_gglrc; out: $(tr '\n' '|' < "$tmp/gapprove107.out" 2>/dev/null | head_bytes 160))"
fi

# --- 108 (M1202 wave 2b follow-up, F4): the land step's <s> idiom. The
# host's land step may be written in the <s> substitution idiom (the
# gate's gf_subst_s: every literal "<s>" token is replaced with the sha8
# AFTER the sha8 is appended -- the exported jc_capture_cmd's contract,
# jc_hp_gate.h: the caller "has already appended the sha8 and substituted
# <s>"). A fresh gate home, the BASE gcfg (NO landingPolicy key) -> the
# D2 default holds: green-held, a 'decision=pending' record, NO land call
# (cell 106's Part A shape; no land stub named at all). Then approve
# with --config whose host.landRun is the <s> idiom:
#   "$tmp/gstublandok2 <s> $tmp/glandF4.txt"
# gsstublandok2 records its argv on $2 (the argv file -- the stub is
# invoked as <cmd> <s8> <argvfile> <s8-appended>, so the sha8 arrives
# where the <s> WAS and as the LAST argv element, the appended one).
# WITHOUT the substitution pass approve runs sh -c
# 'gstublandok2 <s> <argvfile> <s8>': the shell parses <s> as an INPUT
# REDIRECTION from a file named "s" (absent) -- the stub is never invoked,
# the argv file is absent, the land step's rc != 0, approve rc 1.
{ printf '#!/bin/sh\n'
  printf '%s\n' 'printf "%s\\n" "$@" > "$2"'
  printf '%s\n' 'exit 0'
} > "$tmp/gstublandok2"; chmod +x "$tmp/gstublandok2"
cat > "$tmp/gcfg4.json" <<EOF
{"dir":"$tmp/gdir","modelKeyFile":"$tmp/gdir/k","gitTokenFile":"$tmp/gdir/t",
"host":{"pushHome":"$tmp","pushRepo":"$tmp/gpush.git","ledgerAddress":"$ADDR","gateHome":"$tmp/gh","gateRun":"$tmp/gh/standin.sh","landRun":"$tmp/gstublandok2 <s> $tmp/glandF4.txt"}}
EOF
_ggh4="$tmp/gh4"
_ggh4l="$tmp/glandF4.txt"; rm -f "$_ggh4l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate108" --gate-home "$_ggh4" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh4/ci"
_ggh4dec="$_ggh4/$G2c_8/landing.decision"
_ggh4_last=$(tail -1 "$_ggh4dec" 2>/dev/null)
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate108" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ -e "$_ggh4/HOLD" ] \
    && printf '%s' "$_ggh4_last" | grep -q "decision=pending"; then
    "$HBIN" approve "$G2c_8" --gate-home "$_ggh4" --config "$tmp/gcfg4.json" \
        < /dev/null > "$tmp/gapprove108.out" 2>&1
    _gg4rc=$?
    if [ "$_gg4rc" -eq 0 ] \
        && [ -e "$_ggh4l" ] \
        && grep -qF "$G2c_8" "$_ggh4l" \
        && printf '%s' "$(tail -1 "$_ggh4l" 2>/dev/null)" | grep -qxF "$G2c_8"; then
        t_ok "108: the land step's <s> idiom (F4): the BASE config (no landingPolicy) ends green-held with a pending record; approve --config (host.landRun = 'gstublandok2 <s> <argvfile>', the <s> idiom) runs the land step with <s> SUBSTITUTED by the sha8 (the gate's gf_subst_s idiom, after the append) -- the argv file PRESENT, carrying <s8> as its LAST argv element (the appended one), approve rc 0"
    else
        t_fail "108: the <s>-idiom land step must run with <s> substituted (the gate's assembly idiom: sha8 appended, THEN every '<s>' replaced) -- the argv file must be PRESENT, carrying <s8> as the LAST argv element, and approve rc 0 (approve rc=$_gg4rc; argv-file: $([ -e "$_ggh4l" ] && printf 'present: %s' "$(tr '\n' '|' < "$_ggh4l" | head_bytes 120)" || echo ABSENT); approve-out: $(tr '\n' '|' < "$tmp/gapprove108.out" 2>/dev/null | head_bytes 160))"
    fi
else
    t_fail "108: the <s>-idiom pending setup failed (got rc=$_grc; HOLD: $([ -e "$_ggh4/HOLD" ] && echo present || echo absent ); last-decision: $(printf '%s' "$_ggh4_last" | head_bytes 160); state: $( [ -f "$tmp/gstate108" ] && tr '\n' '|' < "$tmp/gstate108" | head_bytes 120 || echo ABSENT ))"
fi

# --- 109 (M1202 wave 2b follow-up, F5b): the D3 record's FIELD bytes for
# the landed terminal -- an auto-policy (gcfga) land-ok gate run, FRESH
# gate home: the LAST landing.decision line carries 'decision=landed' AND
# 'land-rc=0'.
_ggh9="$tmp/gh9"
_ggh9l="$tmp/gland109.txt"; rm -f "$_ggh9l"
_grun "$tmp/gcfga.json" "$G2c_8" B --state "$tmp/gstate109" --gate-home "$_ggh9" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh9/ci" \
    --land-run "$tmp/gstublandok $_ggh9l"
_ggh9dec="$_ggh9/$G2c_8/landing.decision"
_ggh9_last=$(tail -1 "$_ggh9dec" 2>/dev/null)
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate109" "${G2c_8}:gated" "${G2c_8}:landed" \
    && [ -s "$_ggh9l" ] \
    && printf '%s' "$_ggh9_last" | grep -q "decision=landed" \
    && printf '%s' "$_ggh9_last" | grep -q "land-rc=0" \
    && printf '%s' "$_ggh9_last" | grep -qF " $_gtz"; then
    t_ok "109: the D3 record's landed fields (F5b): the auto-policy land-ok gate run (fresh home) ends rc 0 {gated, landed}, the last landing.decision line carries 'decision=landed' AND 'land-rc=0' + the host's offset in time= ($_gtz)"
else
    t_fail "109: the auto land-ok run's last landing.decision line must carry decision=landed + land-rc=0 (got rc=$_grc; state: $( [ -f "$tmp/gstate109" ] && tr '\n' '|' < "$tmp/gstate109" | head_bytes 120 || echo ABSENT ); last-decision: $(printf '%s' "$_ggh9_last" | head_bytes 160))"
fi

# --- 110 (M1202 wave 2b follow-up, F6): the approve's two usage arms --
# rc 2, the exact phrases pinned byte-for-byte (the dispatch's words,
# jc_harness_main.c: the missing-SHA8 arm names what is absent, the
# missing---gate-home arm names the required flag).
"$HBIN" approve < /dev/null > "$tmp/gapprove110a.out" 2>&1
_g110arc=$?
"$HBIN" approve "$G2c_8" < /dev/null > "$tmp/gapprove110b.out" 2>&1
_g110brc=$?
if [ "$_g110arc" -eq 2 ] \
    && grep -qF "jichi-harness: approve: needs a <SHA8> (the pending landing to approve)" "$tmp/gapprove110a.out" \
    && [ "$_g110brc" -eq 2 ] \
    && grep -qF "jichi-harness: approve: --gate-home <dir> is required" "$tmp/gapprove110b.out"; then
    t_ok "110: approve's usage arms (F6): a bare 'approve' (no SHA8) -> rc 2 naming 'jichi-harness: approve: needs a <SHA8> (the pending landing to approve)'; 'approve <s8>' (no --gate-home) -> rc 2 naming 'jichi-harness: approve: --gate-home <dir> is required'"
else
    t_fail "110: approve's usage arms must refuse rc 2 naming what is missing (bare: rc=$_g110arc out: $(tr '\n' '|' < "$tmp/gapprove110a.out" 2>/dev/null | head_bytes 160); with <s8>, no --gate-home: rc=$_g110brc out: $(tr '\n' '|' < "$tmp/gapprove110b.out" 2>/dev/null | head_bytes 160))"
fi

# --- 111 (M1202 wave 2b follow-up, F7 + F11): the land step's CONFIG
# fallback + the absent-record refusal.
#   Part A (F7): a fresh gate home, the BASE gcfg (no landingPolicy) ->
#   green-held, the pending record (policy=hold, the D2 effective
#   default). A fixture config naming host.landRun in the PLAIN form
#   (no <s> token): "gstublandok <argvfile>" -- $1 is the argv file, the
#   sha8 the appended LAST argv element. approve with --config but NO
#   --land-run: the land step resolves from the CONFIG, runs (rc 0), the
#   argv file PRESENT carrying <s8>. The approved record ECHOES the
#   pending line's policy= word (F2 -- the word is extracted from the
#   pending line, so a regression that stops echoing it goes red), and
#   carries the drive offset in time= (F3) + operator + land-rc=0.
#   Part B (F11): approve on a FRESH EMPTY gate home (no
#   landing.decision at all) -> the SAME refusal words as the
#   not-pending arm (cell 107): rc 1, 'approve: refused: no pending
#   landing for <s8>'.
_ggh11="$tmp/gh11"
_ggh11l="$tmp/gland111.txt"; rm -f "$_ggh11l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate111" --gate-home "$_ggh11" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh11/ci"
cat > "$tmp/gcfg11.json" <<EOF
{"dir":"$tmp/gdir","modelKeyFile":"$tmp/gdir/k","gitTokenFile":"$tmp/gdir/t",
"host":{"pushHome":"$tmp","pushRepo":"$tmp/gpush.git","ledgerAddress":"$ADDR","gateHome":"$tmp/gh","gateRun":"$tmp/gh/standin.sh","landRun":"$tmp/gstublandok $tmp/gland111.txt"}}
EOF
_ggh11dec="$_ggh11/$G2c_8/landing.decision"
_ggh11_pend=$(grep "decision=pending" "$_ggh11dec" 2>/dev/null | tail -1)
_ggh11_pol=$(printf '%s' "$_ggh11_pend" | sed -n 's/.* policy=\([^ ]*\).*/\1/p')
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate111" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ -n "$_ggh11_pend" ]; then
    "$HBIN" approve "$G2c_8" --gate-home "$_ggh11" --config "$tmp/gcfg11.json" \
        --operator cfg-fallback < /dev/null > "$tmp/gapprove111.out" 2>&1
    _g111rc=$?
    _ggh11_appr=$(grep "decision=approved" "$_ggh11dec" 2>/dev/null | tail -1)
    # Part B (F11): the absent-record arm (a fresh EMPTY gate home).
    mkdir -p "$tmp/gh11b/$G2c_8"
    "$HBIN" approve "$G2c_8" --gate-home "$tmp/gh11b" \
        < /dev/null > "$tmp/gapprove111b.out" 2>&1
    _g111brc=$?
    if [ "$_g111rc" -eq 0 ] \
        && [ -e "$_ggh11l" ] \
        && grep -qF "$G2c_8" "$_ggh11l" \
        && [ -n "$_ggh11_appr" ] \
        && printf '%s' "$_ggh11_appr" | grep -qF "policy=$_ggh11_pol " \
        && printf '%s' "$_ggh11_appr" | grep -qF "sha8=$G2c_8" \
        && printf '%s' "$_ggh11_appr" | grep -qF "batch=B" \
        && printf '%s' "$_ggh11_appr" | grep -qF "operator=cfg-fallback" \
        && printf '%s' "$_ggh11_appr" | grep -qF "land-rc=0" \
        && printf '%s' "$_ggh11_appr" | grep -qF " $_gtz" \
        && [ "$_g111brc" -eq 1 ] \
        && grep -qF "approve: refused: no pending landing for $G2c_8" "$tmp/gapprove111b.out"; then
        t_ok "111: the land step's config fallback (F7) + the absent-record refusal (F11): a pending gate home (the BASE config, no landingPolicy) + a config naming host.landRun (the plain 'gstublandok <argvfile>' form, no <s>) -> approve --config with NO --land-run runs the land step (rc 0, the argv file PRESENT carrying <s8>); the approved line ECHOES the pending line's policy= word (policy=$_ggh11_pol, F2) + sha8=<s8> + batch=B + operator=cfg-fallback + land-rc=0 + the drive offset in time= (F3); AND approve on a FRESH EMPTY gate home (no landing.decision at all) -> rc 1, 'approve: refused: no pending landing for <s8>' (the absent-file path says the SAME words as the not-pending arm)"
    else
        t_fail "111: the config-fallback approve must run the land step from the config (the argv file PRESENT, carrying <s8>) and ECHO the pending line's policy= word (pending policy=$_ggh11_pol; approve rc=$_g111rc; argv-file: $([ -e "$_ggh11l" ] && printf 'present: %s' "$(tr '\n' '|' < "$_ggh11l" | head_bytes 120)" || echo ABSENT); approved-line: $(printf '%s' "$_ggh11_appr" | head_bytes 200); approve-out: $(tr '\n' '|' < "$tmp/gapprove111.out" 2>/dev/null | head_bytes 160); F11 arm: rc=$_g111brc out: $(tr '\n' '|' < "$tmp/gapprove111b.out" 2>/dev/null | head_bytes 160))"
    fi
else
    t_fail "111: the config-fallback pending setup failed (got rc=$_grc; pending-line: $(printf '%s' "$_ggh11_pend" | head_bytes 160); state: $( [ -f "$tmp/gstate111" ] && tr '\n' '|' < "$tmp/gstate111" | head_bytes 120 || echo ABSENT ))"
fi

# --- 112 (M1202 wave 2b follow-up, F8): D2's case-sensitivity -- the
# word "AUTO" (uppercase) is NOT byte-exact "auto": the gate FAILS
# CLOSED -- it CREATES the HOLD and ends green-held WITHOUT the land
# step (the land stub named; its argv file must stay ABSENT), and the
# pending record carries the word ECHOED as-is: 'policy=AUTO' (a
# non-default word in the policy= field: the field pins the config's
# word, not a re-normalised one).
sed 's/"landingPolicy":"auto"/"landingPolicy":"AUTO"/' \
    "$tmp/gcfga.json" > "$tmp/gcfg12.json"
_ggh12="$tmp/gh12"
_ggh12l="$tmp/gland112.txt"; rm -f "$_ggh12l"
_grun "$tmp/gcfg12.json" "$G2c_8" B --state "$tmp/gstate112" --gate-home "$_ggh12" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh12/ci" \
    --land-run "$tmp/gstublandok $_ggh12l"
_ggh12dec="$_ggh12/$G2c_8/landing.decision"
_ggh12_last=$(tail -1 "$_ggh12dec" 2>/dev/null)
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate112" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ -e "$_ggh12/HOLD" ] \
    && [ ! -e "$_ggh12l" ] \
    && printf '%s' "$_ggh12_last" | grep -q "decision=pending" \
    && printf '%s' "$_ggh12_last" | grep -q "policy=AUTO" \
    && printf '%s' "$_ggh12_last" | grep -qF " $_gtz"; then
    t_ok "112: D2's case-sensitivity (F8): 'landingPolicy':'AUTO' (uppercase) is NOT byte-exact 'auto' -> the gate CREATES the HOLD, ends green-held WITHOUT the land call (the land argv file ABSENT), and the last landing.decision line carries 'decision=pending' AND 'policy=AUTO' (the word echoed as-is, the non-default policy field) + the host's offset in time= ($_gtz)"
else
    t_fail "112: 'AUTO' must FAIL CLOSED to the hold arm, echoing the word as-is in the pending record (got rc=$_grc; HOLD: $([ -e "$_ggh12/HOLD" ] && echo present || echo absent ); land-argv: $([ -e "$_ggh12l" ] && echo PRESENT || echo absent ); last-decision: $(printf '%s' "$_ggh12_last" | head_bytes 160); state: $( [ -f "$tmp/gstate112" ] && tr '\n' '|' < "$tmp/gstate112" | head_bytes 120 || echo ABSENT ))"
fi

# --- 113 (M1202 wave 2b follow-up, F9): the approved record's append is
# NON-FATAL (the D3 doctrine; the LOG run 0075 disposition: KEEP -- the
# record is telemetry beside the decision; the landing itself succeeded;
# the operator sees the named stderr). landing.decision is made READ-ONLY
# (chmod 444) AFTER the pending record exists: approve runs the land
# step (rc 0, the argv file PRESENT carrying <s8>), the append fails, is
# NAMED on stderr ('could not append the approved decision to'), and the
# verb STILL returns the land step's rc (0). The chmod is restored after.
# (This host runs the driver as a non-root operator, so the 444 bit
# fences the owner; a root host would make the bit ineffective -- the
# assertion then pins the rc/argv half only, noted in the follow-up
# report.)
_ggh13="$tmp/gh13"
_ggh13l="$tmp/gland113.txt"; rm -f "$_ggh13l"
_grun "$tmp/gcfg.json" "$G2c_8" B --state "$tmp/gstate113" --gate-home "$_ggh13" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh13/ci"
_ggh13dec="$_ggh13/$G2c_8/landing.decision"
if [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate113" "${G2c_8}:gated" "${G2c_8}:green-held" \
    && [ -f "$_ggh13dec" ] \
    && grep -q "decision=pending" "$_ggh13dec"; then
    chmod 444 "$_ggh13dec"
    "$HBIN" approve "$G2c_8" --gate-home "$_ggh13" \
        --land-run "$tmp/gstublandok $_ggh13l" \
        < /dev/null > "$tmp/gapprove113.out" 2>&1
    _g113rc=$?
    chmod 644 "$_ggh13dec" 2>/dev/null
    if [ "$_g113rc" -eq 0 ] \
        && [ -e "$_ggh13l" ] \
        && grep -qF "$G2c_8" "$_ggh13l" \
        && { [ "$(id -u)" -eq 0 ] \
             || { grep -qF "could not append the approved decision to" "$tmp/gapprove113.out" \
                  && ! grep -q "decision=approved" "$_ggh13dec"; }; }; then
        t_ok "113: the approved record's append is NON-FATAL (F9, KEEP per run 0075): landing.decision chmod 444 (read-only) AFTER the pending record -> approve runs the land step (rc 0, the argv file PRESENT carrying <s8>); the record's failure is NAMED on stderr ('could not append the approved decision to'), NOT fatal -- the verb returns the land step's rc 0"
    else
        t_fail "113: a failed approved-record append must be NAMED on stderr, not fatal: the verb must return the land step's rc (0) with the argv file PRESENT carrying <s8> (approve rc=$_g113rc; argv-file: $([ -e "$_ggh13l" ] && printf 'present: %s' "$(tr '\n' '|' < "$_ggh13l" | head_bytes 120)" || echo ABSENT); approve-out: $(tr '\n' '|' < "$tmp/gapprove113.out" 2>/dev/null | head_bytes 200); approved-lines: $(grep -c "decision=approved" "$_ggh13dec" 2>/dev/null || echo 0); uid=$(id -u))"
    fi
else
    t_fail "113: the non-fatal setup failed (got rc=$_grc; pending: $([ -f "$_ggh13dec" ] && grep -q "decision=pending" "$_ggh13dec" && echo yes || echo no ); state: $( [ -f "$tmp/gstate113" ] && tr '\n' '|' < "$tmp/gstate113" | head_bytes 120 || echo ABSENT ))"
fi

# --- 114 (M1202 H10, review R-1): the full-40-hex-sha approve must be
# SUBSTITUTED, matching the gate's gf_subst_s. The gate's own assembly
# (gf_capture_cmd) appends the sha it is GIVEN (no length guard on the
# sha -- gf_subst_s substitutes whatever string it is passed), and the
# gate's <SHA8> door (gf_is_commit_token) accepts 7-40 lowercase hex,
# so a full 40-hex sha is a legitimate key. approve's inlined
# substitution pass copied the gate's shape but kept a stray
# `slen > 8` conjunct in its bounds pre-check -- it REFUSES a longer sha
# with "the land step command is too long", mis-naming the rejection
# (the sha is not the "land step command"; the land step command is
# `cmd + " " + sha`). Cell 114 makes the refusal reachable: reuse
# cell 108's fixture shape (a fresh BASE-config gate home, no
# landingPolicy, that ends green-held with a decision=pending record)
# but derive the sha from the fixture's REAL commit -- the FULL 40-hex
# value, not the 8-hex prefix -- run the gate keyed on that full sha
# (so the record lands at <home>/<fullsha>/landing.decision), and
# approve with the same full sha.
#   RED (before the fix): approve hits the slen>8 conjunct, returns 1,
#   names "the land step command is too long", and the land stub is
#   never invoked (the argv file ABSENT).
#   GREEN (after the fix): approve assembles `cmd + " " + <fullsha>`
#   and runs the <s> substitution with the full value (like
#   gf_subst_s would) -- the argv file PRESENT, its LAST line
#   byte-exact <fullsha>, approve rc 0.
# gsstublandok2 + gcfg4 were created by cell 108 (this cell reuses
# them; the land run's argv file is a fresh name, so no clobber).
_fullsha=$(git -C "$tmp/gc" rev-parse "$G2c_8" 2>/dev/null)
_ggh14="$tmp/gh14"
_ggh14l="$tmp/gland114.txt"; rm -f "$_ggh14l"
_grun "$tmp/gcfg.json" "$_fullsha" B --state "$tmp/gstate114" --gate-home "$_ggh14" \
    _gflags --ci-run "$tmp/gstubcigreen" --ci-log-dir "$_ggh14/ci"
_ggh14dec="$_ggh14/$_fullsha/landing.decision"
_ggh14_last=$(tail -1 "$_ggh14dec" 2>/dev/null)
if [ -n "$_fullsha" ] \
    && [ "$_grc" -eq 0 ] \
    && _gstateok "$tmp/gstate114" "${_fullsha}:gated" "${_fullsha}:green-held" \
    && [ -e "$_ggh14/HOLD" ] \
    && printf '%s' "$_ggh14_last" | grep -q "decision=pending"; then
    "$HBIN" approve "$_fullsha" --gate-home "$_ggh14" \
        --land-run "$tmp/gstublandok2 <s> $_ggh14l" --operator fullsha \
        < /dev/null > "$tmp/gapprove114.out" 2>&1
    _g114rc=$?
    if [ "$_g114rc" -eq 0 ] \
        && [ -e "$_ggh14l" ] \
        && printf '%s' "$(tail -1 "$_ggh14l" 2>/dev/null)" | grep -qxF "$_fullsha"; then
        t_ok "114: the full-40-hex-sha approve (R-1): the BASE config ends green-held with a pending record keyed at <home>/<fullsha>; approve --land-run '<s> idiom' with the FULL sha (not the 8-hex prefix) substitutes <s> with the full value, matching the gate's gf_subst_s -- approve rc 0, the argv file PRESENT, its LAST line byte-exact the full sha (NOT the 'too long' refusal)"
    else
        t_fail "114: the full-40-hex-sha approve must substitute <s> with the full value like the gate's gf_subst_s (no slen>8 refusal) -- approve rc 0, argv file PRESENT, last line byte-exact the full sha (fullsha=$_fullsha; approve rc=$_g114rc; argv-file: $([ -e "$_ggh14l" ] && printf 'present: %s' "$(tr '\n' '|' < "$_ggh14l" | head_bytes 120)" || echo ABSENT); approve-out: $(tr '\n' '|' < "$tmp/gapprove114.out" 2>/dev/null | head_bytes 160))"
    fi
else
    t_fail "114: the full-sha pending setup failed (fullsha=$_fullsha; gate rc=$_grc; HOLD: $([ -e "$_ggh14/HOLD" ] && echo present || echo absent ); last-decision: $(printf '%s' "$_ggh14_last" | head_bytes 160); state: $( [ -f "$tmp/gstate114" ] && tr '\n' '|' < "$tmp/gstate114" | head_bytes 120 || echo ABSENT ))"
fi


# --- 115 (M1202 H10, SV:1059): the ci stage's clone+checkout command slot
# must not be CUT. The reference runs it as an uncapped shell command line;
# the port's 512-byte char slot + the `jc_snprintf < 0`-only check cut it
# silently for any (pr, home, s) whose expansion exceeds 512 bytes, and the
# cut command went to sh -c (a truncated quoted string is a SHELL SYNTAX
# ERROR: git never runs, no clone home is created, and the stage's record
# reads "clone/checkout failed: INVALID" for the wrong reason). SV:1059's
# measured case was cell 114 (a 76-char TMPDIR + a full 40-hex sha); this
# cell reaches the bound on the PATH ALONE (a deep gate WORK dir -- the
# clone's home is <work>/ci-<sha8>) whose clone+checkout command expands to
# ~1700 bytes > the old 512 slot, while the WORK dir itself stays ~800
# characters: under gci_mkdir_p's own 2048-byte buf (so the stage reaches
# the clone, not a second truncation) and under PATH_MAX 4096 (so git can
# create the home). 3 components of 250 legal path characters, no space,
# with an ordinary 8-hex sha and ANY TMPDIR, so it is deterministic. The
# unfixed slot: the cut command -> sh -c rc 2 -> NO clone home -> the
# record's "clone/checkout failed" INVALID line (the cut, misnamed). The
# fixed slot (the stage's unbounded command builder: the command is never
# cut; a would-not-fit case NAMES the cut instead of running it, the M1178
# idiom): git runs with the full command, the clone home is created, the ci
# stage is green, and the record carries no cut message.
_g115w="$tmp/w115"; _g115c="$tmp/c115"
# The deep component (250 characters, no space: the path is single-quoted in
# the command and the cell's own arithmetic must not wrap); 3 levels of it
# => the WORK dir is ~800 characters (fits gci_mkdir_p's 2048 buf and
# PATH_MAX), while the clone+checkout command (pr + 2*<work>/ci-<s> + the
# fixed ~88 bytes) is ~1700 > the old 512 slot.
_g115comp=$(printf 'a%.0s' $(seq 1 250))
_g115d="$_g115w"
_i=0; while [ "$_i" -lt 3 ]; do _g115d="$_g115d/$_g115comp"; _i=$(( _i + 1 )); done
_g115home="$_g115d"
_crun "$P6_8" "$_g115home" "$_g115c" "$CSTUB"
_g115cut=0
grep -qF "would not fit the slot" "$tmp/cout" && _g115cut=1
if [ "$_crc" -eq 0 ] \
    && [ -e "$_g115home/ci-$P6_8" ] \
    && [ "$_g115cut" -eq 0 ]; then
    t_ok "115: SV:1059 the ci stage's clone+checkout slot is NOT cut (the deep ~800-char gate WORK dir makes the clone command ~1700 bytes > the old 512 slot; the fixed slot runs the full command: the clone home ci-$P6_8 is CREATED, the ci stage is green, and the record carries no cut message)"
else
    t_fail "115: SV:1059 the ci stage's clone+checkout slot cut (home: $([ -e "$_g115home/ci-$P6_8" ] && echo PRESENT || echo absent); cut-record: $([ "$_g115cut" -eq 1 ] && echo yes || echo no); crc=$_crc; record tail: $(tail -2 "$tmp/cout" 2>/dev/null | tr '\n' '|' | head_bytes 200); cerr: $(head -1 "$tmp/cerr" 2>/dev/null | head_bytes 120))"
fi

t_done
