#!/bin/sh
# smoke: the repo-map build arena peaks at ONE scanned file, not their sum
# (M1081, F-01; finding docs/internal/reviews/2026-10-17-hardening/F-resources.md
# '### F-01', confirmation 2026-10-29-F01-confirm.md).
#
# MECHANISM: render_listing (src/index/jc_repomap.c) walked the workspace,
# jc_read_file'd EVERY candidate file into one build-local arena, and freed it
# only after the 12 KiB-bounded listing was built -- so the arena's peak was the
# SUM of the scanned files (measured 2,297,920 B for 40 x ~57 KB, against a
# 776 B rendered map). The 12 KiB stop bounds the OUTPUT, not the retention.
# The fix reads each file's text into a per-file arena that is reset right
# after the scan, so the peak is one file (<= the 512 KiB jc_read_file cap) +
# the path list. No output change: `jichi map` on the same tree is
# byte-identical before/after (proven in the M1081 report).
#
# THE RIG: `jichi map` runs exactly that build (run_map -> jc_repomap_render ->
# render_listing), needs no provider, and the repoMap config knob does not gate
# the subcommand -- so the BASELINE is the same binary in an EMPTY workspace
# (files.len == 0: the walk and the loop run, nothing is retained), and the
# PEAK is the same binary in a workspace of 40 x 64 KB symbol-sparse .c files.
# A tiny C wrapper fork+exec's $BIN, reaps it with wait4, and prints
# ru_maxrss -- the PROCESS high-water resident set. M1186: the wrapper's
# maxrss reading no longer carries the F-01 assertion (retired -- see the
# M1186 note below); it STAYS as the rig-intact denominator for checks 1-3
# (the wrapper builds, the baseline run is alive (>0), the peak run rendered
# a real 40-row map), so a hollow $BIN cannot make the peak check vacuous.
# The F-01 assertion is now on the arena's OWN high-water mark, read through
# the TEST-ONLY JC_MAP_ARENA_PEAK door in run_map (the unit gate
# test_repomap_peak_is_one_file, via the same jc_repomap_last_arena_peak
# accessor, asserts the bound on the arena directly; this driver proves it
# end to end on the built binary).
#
# M1183-b: ru_maxrss reads in 192 KB steps and an in-suite round can sit quiet
# on one side and loud on the other (the 2026-11-13 22:43 gate red'd at a 576 KB
# diff, in-suite only, with the repo-map code unchanged: base at its lowest
# 10176, peak at its highest 10752 -- a single quiet-vs-loud cross-window mix).
# M1183 took 3 runs per side and the MINIMUM of each side; that still MIXES
# across the base->peak window, so a SUSTAINED step straddling it sits high on
# the peak readings while the min base can come from a quiet moment -- it would
# reproduce 576 with near certainty, not 1/27. M1183-b pairs INSTEAD: 3 rounds,
# each one base run + one peak run, and diff = the MINIMUM of the 3 (peak-base)
# pair-diffs from the same round. A sustained load step lands on BOTH readings
# of a round and cancels in that round's diff; pairing removes the quiet-vs-loud
# mix. Noise only inflates ru_maxrss, so the min pair-diff is the stable
# statistic. The F-01 signal (~2600 KB, the sum of the 40 files) is 6x above
# the CAP, so the min pair-diff still catches it (tooth-proven in the M1183-b
# report).
#
# M1186 (REL4): the ru_maxrss DIFF above is RETIRED as the F-01 assertion.
# SV:0251: ru_maxrss wandered 10368->12480 KB under concurrent builds and red'd
# the 2026-11-13 22:43 gate at a 576 KB diff with the repo-map code unchanged --
# process RSS is the wrong instrument for an arena's peak. CLAUDE.md (the three
# arenas) names the right one; the driver now reads the SCAN ARENA'S OWN
# high-water mark: the peak leg runs `jichi map` with the TEST-ONLY env var
# JC_MAP_ARENA_PEAK set (the door lives in run_map, src/main.c), which prints
# `map arena peak: <N>` to stderr after the map. Check 4 greps N out of the
# SAME run's output check 3 uses (no mix) and asserts N <= 256 KiB -- one 64 KB
# file + arena-block overhead + the 40-path list + the rendered map. F-01 (the
# sum of 40 files) reads ~2.6 MB: ten times over. N is LOAD-INDEPENDENT (a
# property of the code path, not the machine), so the 22:43 incident class is
# gone. The M1183-b pairing comment above is KEPT as history: it still explains
# checks 1-3's structure (wrapper build, baseline liveness, the rendered-map
# denominator). The F-01 tooth now lives in test_repomap_peak_is_one_file
# (unit, load-independent) + this check 4; the same M1081 revert reds both.
#
# ru_maxrss is KB on Linux, BYTES on macOS (Darwin) -- converted to KB below.
#
# NOTE: registered in run.sh by the wave supervisor at merge (M1081 tasking);
# run standalone meanwhile: TMPDIR=/tmp/jdw-1081 sh tests/smoke/repomap_peak.sh
. "$(dirname "$0")/_smoke.sh"

# The rss wrapper needs a C compiler (it is the whole point of this driver);
# decide before t_plan, like snprintf_fallback.sh.
_cc=""
for _c in "${CC:-}" cc gcc clang; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    _cc="$_c"; break
done
[ -n "$_cc" ] || t_skip "no C compiler on PATH (tried \$CC, cc, gcc, clang)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
empty=$(smoke_tmp)

# --- the rss wrapper (written to $tmp, never to the repo tree) --------------
cat > "$tmp/rss.c" <<'EOF'
#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>

/* fork+exec argv, wait4, print the child's ru_maxrss then exit with its
 * status. maxrss is the process high-water resident set -- the arena peak
 * is in it whether or not the allocator returned the blocks on free. */
int main(int argc, char **argv)
{
    pid_t pid;
    int status;
    struct rusage ru;

    if (argc < 2) {
        fprintf(stderr, "usage: rss <cmd>...\n");
        return 2;
    }
    pid = fork();
    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(127);
    }
    if (wait4(pid, &status, 0, &ru) < 0) {
        perror("wait4");
        return 2;
    }
    printf("%ld\n", (long)ru.ru_maxrss);
    if (WIFEXITED(status)) exit(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) exit(128 + WTERMSIG(status));
    exit(1);
}
EOF
if "$_cc" -std=c89 -Wall -Wextra -O2 -o "$tmp/rss" "$tmp/rss.c" 2> "$tmp/rss.err"; then
    t_ok "rss wrapper built ($_cc)"
else
    t_fail "rss wrapper failed to build: $(head_bytes 200 "$tmp/rss.err")"
    t_done
fi

# ru_maxrss is bytes on macOS, KB on Linux; normalise to KB.
case $(uname -s) in
    Darwin) DIV=1024 ;;
    *)      DIV=1 ;;
esac

CFG='{"snapshots":false,"references":false,"lowResource":false}'

# run_map_ws DIR -> the maxrss (KB) of `$BIN --config-json CFG map` run from
# DIR (0 on failure). The rss wrapper prints the map and, as its LAST line,
# the maxrss in native units; normalised by $DIV below. M1186: the reading no
# longer carries the F-01 assertion (retired ru_maxrss diff) -- it is the
# rig-intact denominator for checks 1-3 only (a >0 reading means the run was
# alive and rendered something). Normalisation stays: check 2/3 print the KB.
run_map_ws() {
    (cd "$1" && with_deadline 120 "$tmp/rss" \
        "$BIN" --config-json "$CFG" map < /dev/null > "$tmp/map.out" 2>&1) \
        || { echo 0; return 0; }
    _v=$(tail -1 "$tmp/map.out")
    case $_v in ''|*[!0-9]*) echo 0; return 0 ;; esac
    echo $((_v / $DIV))
}

# peak_ws DIR -> the maxrss (KB) of the PEAK leg: `$BIN --config-json CFG map`
# run from DIR with JC_MAP_ARENA_PEAK set in its environment (the M1186
# TEST-ONLY door in run_map). Same rig as run_map_ws (the rss wrapper
# fork+execvp's $BIN, so the exported var reaches the child; maxrss is still
# the LAST line of map.out, so liveness reads it exactly as before) EXCEPT the
# door adds one line to the child's stderr (merged into map.out, AFTER the map
# body and BEFORE the wrapper's maxrss line): `map arena peak: <N>`. That is
# check 4's F-01 input, and it comes from the SAME run check 3 uses (map.peak.out
# is the first successful peak_ws run) -- no mix.
peak_ws() {
    (cd "$1" && with_deadline 120 env JC_MAP_ARENA_PEAK=1 "$tmp/rss" \
        "$BIN" --config-json "$CFG" map < /dev/null > "$tmp/map.out" 2>&1) \
        || { echo 0; return 0; }
    _v=$(tail -1 "$tmp/map.out")
    case $_v in ''|*[!0-9]*) echo 0; return 0 ;; esac
    echo $((_v / $DIV))
}

# Stage the 40 x 64 KB fixture workspace first: the interleaved loop below runs
# a base (empty) and a peak (full) in the SAME round, so $ws must be ready
# before round 1 (one top-level function per file).
FILLER="$tmp/filler"
if dd if=/dev/zero bs=65536 count=1 2>/dev/null | tr '\0' 'a' > "$FILLER"; then
    :
else
    t_fail "could not make the 64 KB filler (dd/tr unavailable)"
    t_done
fi
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19; do
    printf 'int f_%s(void)\n{\n' "$i" > "$ws/mod$i.c"
    cat "$FILLER" >> "$ws/mod$i.c"
    printf '\nreturn 0;\n}\n' >> "$ws/mod$i.c"
done
for i in 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39; do
    printf 'int f_%s(void)\n{\n' "$i" > "$ws/mod$i.c"
    cat "$FILLER" >> "$ws/mod$i.c"
    printf '\nreturn 0;\n}\n' >> "$ws/mod$i.c"
done
nfiles=$(ls "$ws" | grep -c '^mod')

# THREE interleaved rounds (M1183-b pairing KEPT for liveness): round i runs
# one base (run_map_ws "$empty") then one peak. M1186: the peak leg is
# peak_ws "$ws" -- the SAME rig as run_map_ws "$ws" but with JC_MAP_ARENA_PEAK
# set, so the run's captured output carries the door's `map arena peak: <N>`
# line alongside the maxrss. The ru_maxrss readings no longer feed an
# assertion -- they keep the wrapper readings HONEST (a hollow or dead $BIN
# yields 0 and checks 2/3 red). $base = the first successful base reading
# (check 2's >0 guard); $peak = the first successful peak reading, whose
# map.out is copied to map.peak.out -- check 3's rendered-map denominator AND
# check 4's peak-line input, the SAME run (no mix). One run is enough for
# check 4: N is load-independent (the arena's high-water mark, a property of
# the code path), so pairing/min-of-3 is unnecessary -- that was the whole
# ru_maxrss problem (the 22:43 false red, base 10176 the lowest, peak 10752
# the highest, a quiet-vs-loud cross-window mix).
base=0
peak=0
i=0
while [ $i -lt 3 ]; do
    _b=$(run_map_ws "$empty")
    _p=$(peak_ws "$ws")
    if [ "$_b" -gt 0 ] && [ "$base" -eq 0 ]; then
        base=$_b
    fi
    if [ "$_p" -gt 0 ]; then
        if [ "$peak" -eq 0 ]; then
            peak=$_p
            cp "$tmp/map.out" "$tmp/map.peak.out"
        fi
    fi
    i=$((i + 1))
done

# Check 2's "rig intact" guard (the >0 guard on $base, unchanged message): the
# baseline run must have produced a real reading and the fixture must be staged.
if [ "$nfiles" -ne 40 ] || [ "$base" -le 0 ]; then
    t_fail "rig broken: $nfiles fixture files, baseline maxrss=$base KB"
else
    t_ok "baseline (empty workspace) map run: maxrss $base KB, 40 x 64 KB fixture staged"
fi

mapbytes=$(wc -c < "$tmp/map.peak.out" 2>/dev/null | tr -d ' ')
nrows=$(grep -c '^mod' "$tmp/map.peak.out" 2>/dev/null)
# THE DENOMINATOR (the M579/SV:1201 rule: a driver that passes with a hollow
# binary measures its own fixtures). A hollow $BIN prints no map, so peak is
# small and the diff check below would be trivially green -- unless we first
# assert the run actually rendered the product's map: all 40 fixture files
# listed. Zero rows means the binary did nothing and every later check is
# vacuous.
# F2 (M1183-b): if the peak run succeeded but the cp to map.peak.out failed,
# $nrows is empty and [ '' -ne 40 ] evaluates false, so this check could emit
# t_ok with a blank row count. Guard it the way run_map_ws guards its output
# (the case ''|*[!0-9]* idiom): a blank or non-numeric $nrows is the failure
# itself, named for the missing map.peak.out / cp failure. It runs BEFORE the
# [ -ne 40 ] test so a blank value is never fed to [ -ne ].
case "$nrows" in
    ''|*[!0-9]*)
        t_fail "peak map run rendered no real map (maxrss=$peak KB, no numeric row count -- map.peak.out missing or the cp failed) -- the binary did not run the product"
        ;;
    *)
        if [ "$peak" -le 0 ] || [ "$nrows" -ne 40 ]; then
            t_fail "peak map run rendered no real map (maxrss=$peak KB, $nrows of 40 fixture files listed) -- the binary did not run the product"
        else
            # The rendered map must still list all 40 files (the no-output-change
            # promise); counted above as the denominator.
            t_ok "peak (40 x 64 KB workspace) map run: maxrss $peak KB, rendered map lists $nrows files"
        fi
        ;;
esac

# THE F-01 assertion (M1186): the scan arena's OWN high-water mark -- N, read
# from the peak run's `map arena peak: <N>` line (the SAME run check 3 used,
# map.peak.out) -- is at most 256 KiB (262144 bytes): one 64 KB file +
# arena-block overhead + the 40-path walk list + the rendered map. Without the
# M1081 fix the arena holds the SUM of all 40 scanned files (~2.6 MB): ten
# times over the bound, so the tooth is preserved with margin. N is
# LOAD-INDEPENDENT (a property of the code path, not the machine), which is
# why the retired ru_maxrss diff could false-red under concurrent builds
# (22:43) while this cannot.
#
# A pre-M1186 $BIN has no door, so the line is ABSENT: that is the red-first
# leg, and the failure is named for the missing line (not for a number).
# map.peak.out is the first successful peak_ws run (same run as check 3).
# The unit gate test_repomap_peak_is_one_file holds the load-independent F-01
# tooth; the same M1081 revert reds it AND this check.
peak_line=$(grep 'map arena peak:' "$tmp/map.peak.out" 2>/dev/null | head -1)
peak_n=$(printf '%s' "$peak_line" | sed 's/^map arena peak: //')
case "$peak_n" in
    ''|*[!0-9]*)
        t_fail "no 'map arena peak: <N>' line in the peak run's output -- the \$BIN has no M1186 door (pre-M1186 binary) -- check 4 cannot read the arena HWM"
        ;;
    *)
        if [ "$peak_n" -le 262144 ]; then
            t_ok "arena peak is one file: scan arena HWM $peak_n bytes <= 262144 (256 KiB) (the peak run's own 'map arena peak' line, load-independent)"
        else
            t_fail "scan arena HWM $peak_n bytes > 262144 (256 KiB) -- the build retained the SUM of the scanned files (F-01 unfixed; expect ~2.6 MB)"
        fi
        ;;
esac

t_done
