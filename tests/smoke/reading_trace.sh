#!/bin/sh
# smoke: the traces quoted by docs/reading/tsuiseki-*.md are re-taken and
# diffed, so a chapter cannot keep describing a run the code no longer does
# (M508; all four traces M509).
#
# WHAT THIS EXISTS FOR. docs/reading/ is prose about code, the fastest-rotting
# kind, and reading_refs_lint.sh already holds its anchors to the tree. A
# TRACE chapter goes further and quotes values -- the event stream, the bytes
# on the wire, the file left on disk -- so it can rot in a way no anchor check
# would notice: every function it names still exists, and every number it
# prints is wrong.
#
# So the trace is not described in the chapter, it is REPLAYED here.
# docs/reading/traces/capture.sh runs the fixture (against
# tests/tools/mockmodel, or with no model at all) and normalizes the seven
# per-run values named in its header; this driver runs that same script and
# diffs the result against the artifacts committed under expected/. One
# implementation, so what a reader re-takes by hand is what the gate checks.
#
# THE TRACE LIST IS THE DIRECTORY, not a list kept here: every subdirectory of
# docs/reading/traces/ with a trace.sh in it is replayed. Adding a trace and
# forgetting to register it is therefore not a way to lose coverage.
#
# WHAT IS NOT CHECKED HERE: that the chapters' PROSE agrees with the
# artifacts. Quoted blocks are held by reading_quotes_lint.sh; the argument
# around them is a reader's job (docs/TEST_INTEGRITY.md: some defects only a
# reader can find).
. "$(dirname "$0")/_smoke.sh"

TDIR_ROOT="$SMOKE_ROOT/docs/reading/traces"
CAP="$TDIR_ROOT/capture.sh"
TRACES=$(cd "$TDIR_ROOT" 2>/dev/null && for d in */; do
             [ -f "$d/trace.sh" ] && printf '%s ' "${d%/}"
         done)
ntr=$(printf '%s\n' $TRACES | grep -c .)

# Two suite-wide checks, then three per trace.
t_plan $((2 + ntr * 3))
smoke_home
tmp=$(smoke_tmp)

# --- 1: the traces are there, each with a committed record ------------------
# The floor. A trace whose expected/ is empty would pass every comparison
# below over nothing, which is the M295 lesson: assert the ground truth exists
# before comparing against it.
if [ "$ntr" -ge 4 ]; then
    thin=""
    for t in $TRACES; do
        n=$(ls "$TDIR_ROOT/$t/expected" 2>/dev/null | grep -c .)
        [ "$n" -ge 4 ] || thin="$thin $t($n)"
    done
    if [ -z "$thin" ]; then
        t_ok "$ntr traces, each with a committed record of 4+ artifacts"
    else
        t_fail "trace(s) with too few committed artifacts:$thin"
    fi
else
    t_fail "found only $ntr trace(s) under docs/reading/traces (floor 4) -- layout moved?"
fi

# --- 2: the capturer pins the machine profile -------------------------------
# capture.sh lives under docs/, so smoke_lint's checks 7-8 (which scan
# tests/smoke/*.sh) cannot see its inline config. Without a pinned toolProfile
# and lowResource:false the artifacts would depend on how much RAM the machine
# has, and the diffs below would fail on a small box for a reason that has
# nothing to do with any chapter.
if grep -q '"toolProfile":"full"' "$CAP" && grep -q '"lowResource":false' "$CAP"; then
    t_ok "capture.sh pins toolProfile and lowResource (auto-lite cannot reshape a trace)"
else
    t_fail "capture.sh no longer pins the machine profile -- the traces are machine-dependent"
fi

# --- the replays -------------------------------------------------------------
# JC_TRACE_BIN is exported rather than prefixed: a var assignment in front of
# with_deadline is not exported by FreeBSD's sh (smoke_lint check 13).
JC_TRACE_BIN="$BIN"
export JC_TRACE_BIN

for t in $TRACES; do
    exp="$TDIR_ROOT/$t/expected"
    got="$tmp/$t"
    with_deadline 90 sh "$CAP" "$t" "$got" > "$tmp/out.$t" 2> "$tmp/err.$t"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        t_ok "$t: replayed"
    else
        t_fail "$t: capture.sh rc=$rc: $(head_bytes 200 < "$tmp/err.$t")"
    fi

    # The artifact SET, both directions: a run that stopped producing a file
    # (or started producing an extra one) is a change in behaviour even when
    # every file they have in common still matches.
    ls "$exp" 2>/dev/null | sort > "$tmp/want.$t"
    ls "$got" 2>/dev/null | sort > "$tmp/have.$t"
    if cmp -s "$tmp/want.$t" "$tmp/have.$t"; then
        t_ok "$t: produced exactly the $(grep -c . "$tmp/want.$t") committed artifacts"
    else
        t_fail "$t: artifact set changed: $(diff "$tmp/want.$t" "$tmp/have.$t" | tr '\n' ' ' | head_bytes 200)"
    fi

    # Byte-for-byte. Everything that legitimately varies between two runs is
    # already a <PLACEHOLDER> by the time the files are written, so a
    # difference here is a real change in what jichi does -- either a chapter
    # is now wrong, or expected/ needs re-taking on purpose.
    bad=""
    for f in $(cat "$tmp/want.$t"); do
        [ -f "$got/$f" ] || continue
        cmp -s "$exp/$f" "$got/$f" || bad="$bad $f"
    done
    if [ -z "$bad" ]; then
        t_ok "$t: every artifact matches byte-for-byte"
    else
        t_fail "$t: drifted:$bad -- re-take with capture.sh and read the diff before committing it"
    fi
done

t_done
