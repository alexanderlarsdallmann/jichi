#!/bin/sh
# smoke: def-350 (an H3c follow-up on M1056) -- the committed SAN boundary cell
# for list_pushed's argv sizing (the +5 fixed at 29df6d33).
#
# A REGRESSION cell, not a new feature: it is GREEN on the current tree and its
# value is that it is PROVEN red when the argv sizing in list_pushed is reverted
# from `+5` to `+4` (the pre-fix bug). It is run by its own `make ci` stage,
# `smoke-push-boundary`, which lowers PUSH_MAX_TIPS/PUSH_MAX_EXCL to 1/1 and
# builds jichi-harness with SAN=1; it SKIPS on every other build.
#
# WHY SAN, NOT FAULT: the defect is an out-of-bounds NULL-terminator write
# caught by UBSan (`index 6 out of bounds for type char *[6]`), not an
# injected fault. WHY LOWERED CAPS: at the real 24/48 the boundary is
# unreachable through a git fixture (git_child refuses nargs>40, git_m2
# refuses nargs>8, so a 76-argument rev-list never runs); lowered to 1/1 the
# fixture lands ntips=1, excl->n=1, an==6==T+E+4 (the legal max). At +5 the
# NULL terminator fits; at +4 it is one past the end and UBSan aborts.
#
# Design note (the contract):
#   docs/internal/drive/2026-10-23-harness/notes/def-350.md
# The original teeth (run 0010, 09:00):
#   docs/internal/drive/2026-10-23-harness/LOG.md
#
# GATES (t_skip outside the stage, t_fail inside it; not planned checks):
#   1. the binary must carry BOTH the ASan and the UBSan runtimes (`__asan_`
#      AND `__ubsan_handle` in its strings; the ASan discriminator uses the
#      one-trailing-underscore form -- the double-underscore spelling matches
#      nothing in this toolchain, gcc 15.2, which the report documents) -- the
#      OOB report is UBSan's, so an ASan-only build could not catch it; a
#      plain non-sanitizer build cannot catch it either, so skip. smoke_lint
#      check 16 keeps this driver wired to its Makefile stage by the GATE it
#      greps for -- M1088 rekeyed the detector from the skip wording to the
#      build-marker probe (`__asan_` / `LeakSanitizer` / `FAULT=1`), so a
#      reworded skip can no longer make it invisible. Inside the
#      smoke-push-boundary stage (JC_PUSH_BOUNDARY_STAGE set) the same gate
#      t_fails: a skip there is a vacuous pass.
#   2. the binary must be the LOWERED-caps PUSH_BOUNDARY build with the caps
#      at 1/1; a plain SAN build at 24/48 is NOT at the boundary (the
#      rev-list is an==8-11), so it must skip, not run -- else it would pass
#      vacuously. Inside the stage it t_fails, like gate 1.
#
# WHAT IS CHECKED (t_plan 3, one t_ok/t_fail each):
#   1. the boundary push's rev-list child argv is EXACTLY the 6-argument
#      boundary shape `rev-list --objects <40-hex> ^<40-hex> --not
#      --remotes=origin` (both the key-scan and the size-check rev-list; any
#      other argument count means the cell is NOT at the boundary -> t_fail).
#   2. the boundary push exits 0.
#   3. the boundary push's stderr carries NO `runtime error` and NO
#      `out of bounds` (the +5 green state; at +4 UBSan writes exactly those
#      and aborts, so checks 2 and 3 go red together).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

# Gate 1: the binary must carry BOTH the ASan and the UBSan runtimes (the OOB
# is UBSan's to catch, not a plain build's). The discriminators are `__asan_`
# (one trailing underscore) AND `__ubsan_handle`: a SAN=1 build carries the
# __asan_* and __ubsan_handle runtime symbols and a plain build carries none.
# (The double-underscore spelling `__asan__` matches nothing in this toolchain,
# gcc 15.2 -- the report documents the measurement.) This skip wording is
# what smoke_lint check 16 scans for to keep the driver wired into the
# Makefile gate by lint. Inside the smoke-push-boundary stage (JC_PUSH_
# BOUNDARY_STAGE set) the same miss is a FAILURE, not a skip: a gate that
# skips in its own stage is a vacuous pass (the tooth has silently become a
# permanent skip), so the stage must go red there, while run.sh and ordinary
# smoke keep the skip.
HBIN="${JC_SMOKE_HBIN:-$(cd "$(dirname "$0")/../.." && pwd)/jichi-harness}"
if strings "$HBIN" 2>/dev/null | grep -q __asan_ \
    && strings "$HBIN" 2>/dev/null | grep -q __ubsan_handle; then
    :
elif [ -n "${JC_PUSH_BOUNDARY_STAGE:-}" ]; then
    t_fail "the driver SKIPPED inside its own stage (vacuous) -- gate 1: the SAN=1 build check (needs a SAN=1 build (make smoke-push-boundary))"
    exit 1
else
    t_skip "needs a SAN=1 build (make smoke-push-boundary)"
fi

# Gate 2: the LOWERED-caps build at 1/1. A plain SAN build at 24/48 is not at
# the boundary (its rev-list is an==8-11), so it must skip, not pass
# vacuously. The grep pins the VALUES the binary reports, not just the marker
# (a hand-built -DJC_PUSH_BOUNDARY at 24/48 would print the marker and pass
# vacuously otherwise). Inside the stage the same miss is a failure, as at
# gate 1.
if "$HBIN" --version 2>/dev/null | grep -q 'PUSH_BOUNDARY=1 (PUSH_MAX_TIPS=1 PUSH_MAX_EXCL=1'; then
    :
elif [ -n "${JC_PUSH_BOUNDARY_STAGE:-}" ]; then
    t_fail "the driver SKIPPED inside its own stage (vacuous) -- gate 2: the lowered-caps PUSH_BOUNDARY build check (needs the lowered-caps PUSH_BOUNDARY build (make smoke-push-boundary), not a plain SAN build)"
    exit 1
else
    t_skip "needs the lowered-caps PUSH_BOUNDARY build (make smoke-push-boundary), not a plain SAN build"
fi

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: a non-lint driver must run the tree's "$BIN" -- that
# requirement is satisfied at SOURCE level (the lint greps for the quoted
# "$BIN"). At stage runtime $BIN may not exist (only jichi-harness is built
# there), so the line is a no-op inside the stage; in ordinary smoke it runs.
"$BIN" --version > "$tmp/binv" 2>/dev/null

unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
# Neutralise the operator's ambient git config for the fixtures.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

# (If we passed both gates, $HBIN exists and runs -- gate 1 ran `strings` on
# it and gate 2 ran `$HBIN --version` -- so no separate built-check is needed.)
BR=harness-plan

# --- fixtures: the design note's verified shape -----------------------------
# A local source repo carrying $BR/plan (init clones --branch $BR/plan and
# pushes it into the rehearsal stand-in).
src="$tmp/src"
git init -q "$src"
git -C "$src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: plan base"
git -C "$src" branch "$BR/plan"

# A FAKE pinned jichi (init only needs it to exist and answer --version).
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$tmp/jichi"
chmod 755 "$tmp/jichi"

# The canary key + token (>=12 bytes; rehearsal never reads them, but the
# config references must exist to pass the loader).
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
printf '%s\n' "$_kc" > "$tmp/kreal"; chmod 600 "$tmp/kreal"
printf 'CANARY-TOKEN-DO-NOT-PRINT\n' > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"drive":{"branchPrefix":"$BR","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF

# init rehearsal -> $BR/plan + stand-in; mirror fetch -> mirror gets $BR/main.
"$HBIN" init --config "$tmp/cfg.json" rehearsal "$tmp/dir" \
    --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i1" 2> "$tmp/i1.err"
if [ $? -ne 0 ]; then
    t_fail "fixture: init exited nonzero: $(head_bytes 200 "$tmp/i1.err")"
    t_fail "fixture: boundary push argv shape (unrun)"
    t_fail "fixture: boundary push exit 0 (unrun)"
    t_done
fi
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/f1" 2> "$tmp/f1.err"
if [ $? -ne 0 ]; then
    t_fail "fixture: mirror fetch exited nonzero: $(head_bytes 200 "$tmp/f1.err")"
    t_fail "fixture: boundary push argv shape (unrun)"
    t_fail "fixture: boundary push exit 0 (unrun)"
    t_done
fi

# The initial push populates the stand-in's $BR/main (NOT wrapped; the boundary
# push below is the one whose rev-list argv the cell asserts).
"$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p0" 2> "$tmp/p0.err"
if [ $? -ne 0 ]; then
    t_fail "fixture: initial mirror push exited nonzero: $(head_bytes 200 "$tmp/p0.err")"
    t_fail "fixture: boundary push argv shape (unrun)"
    t_fail "fixture: boundary push exit 0 (unrun)"
    t_done
fi

# One new commit on the team's $BR/main, then the BOUNDARY push -- wrapped in a
# git argv-dump shim on PATH (the design note's step 4).
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: boundary commit"

# A PATH entry holding a `git` script that appends the child's argv (one arg
# per line, between ARGV: and ENDARGV:) to DIR/dump, then execs the REAL git
# (resolved at runtime -- the driver never assumes /usr/bin).
_realgit=$(command -v git)
mkdir -p "$tmp/wrap"
printf '#!/bin/sh\n{ printf "ARGV:\\n"; for _a in "$@"; do printf "%%s\\n" "$_a"; done; printf "ENDARGV:\\n"; } >> %s/dump\nexec %s "$@"\n' \
    "$tmp/wrap" "$_realgit" > "$tmp/wrap/git"
chmod 755 "$tmp/wrap/git"

PATH="$tmp/wrap:$PATH" "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/pb" 2> "$tmp/pb.err"
rc=$?

# revlist_argv DIR -- each rev-list child's argv from the dump, starting at the
# `rev-list` word (the -C <mirror> prefix is dropped; a rev-list child is
# `-c ... -C <mirror> rev-list --objects <tips> ^<excl> --not --remotes=origin`).
revlist_argv() {
    awk '
        /^ARGV:$/          { acc = ""; inb = 1; next }
        inb && /^ENDARGV:$/ {
            inb = 0
            if (index(acc, " rev-list ") > 0)
                print substr(acc, index(acc, "rev-list"))
            next
        }
        inb                { acc = acc " " $0 }
    ' "$1/dump" 2>/dev/null
}

# --- 1: the rev-list child argv is EXACTLY the 6-argument boundary shape -----
_shape_ok=1
if [ ! -f "$tmp/wrap/dump" ]; then
    _shape_ok=0
    _shape_why="the git wrapper captured no rev-list argv at all"
else
    _revs=$(revlist_argv "$tmp/wrap")
    _nrev=$(printf '%s\n' "$_revs" | grep -c .)
    # The boundary push runs list_pushed_bc TWICE (the key scan and the size
    # check), each building the SAME an==T+E+4 rev-list -> exactly 2. Any other
    # count means the cell is not at the boundary.
    if [ "$_nrev" -ne 2 ]; then
        _shape_ok=0
        _shape_why="expected exactly 2 boundary rev-list children, got $_nrev"
    else
        # One rev-list child per line; check each line (NOT word-split) against
        # the exact 6-argument boundary shape:
        #   rev-list --objects <40-hex> ^<40-hex> --not --remotes=origin
        while IFS= read -r _r; do
            [ -n "$_r" ] || continue
            if ! printf '%s\n' "$_r" | grep -qE '^rev-list --objects [0-9a-f]{40} \^[0-9a-f]{40} --not --remotes=origin$'; then
                _shape_ok=0
                _shape_why="rev-list argv not the 6-argument boundary shape: $_r"
            fi
        done <<EOF
$_revs
EOF
    fi
fi
if [ "$_shape_ok" -eq 1 ]; then
    t_ok "1: the boundary push's rev-list child argv is the exact 6-argument boundary shape (rev-list --objects <tip> ^<excl> --not --remotes=origin), so an==T+E+4"
else
    t_fail "1: boundary push NOT at the argv boundary -- $_shape_why"
fi

# --- 2: the boundary push exits 0 -------------------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "2: the boundary push exits 0 at an==T+E+4"
else
    t_fail "2: the boundary push exited $rc (want 0) at an==T+E+4 -- $(tr '\n' ' ' < "$tmp/pb.err" | head_bytes 200)"
fi

# --- 3: the boundary push's stderr carries no UBSan report -------------------
_oob=$(grep -cE 'runtime error|out of bounds' "$tmp/pb.err" 2>/dev/null)
if [ "$_oob" -eq 0 ]; then
    t_ok "3: the boundary push's stderr carries no 'runtime error'/'out of bounds' (the +5 green state)"
else
    t_fail "3: the boundary push's stderr carries a UBSan report (the +4 regression): $(grep -E 'runtime error|out of bounds' "$tmp/pb.err" | head_bytes 200)"
fi

t_done
