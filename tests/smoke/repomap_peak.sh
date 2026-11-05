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
# ru_maxrss -- the PROCESS high-water resident set, which includes the arena
# peak whether or not the OS returns the blocks on free. The assertion is on
# the DIFF, so the binary's own ~MB of baseline cancels:
#
#   peak(maxrss) - baseline(maxrss) <= one file + margin
#
# WITHOUT the fix the diff is ~the sum of the 40 files (~2.6 MB) and check 4
# goes red; with the fix it is ~one 64 KB file plus the path list. The unit
# gate (tests/test_repomap.c test_repomap_peak_is_one_file, via the
# jc_repomap_last_arena_peak accessor) asserts the same bound on the arena
# itself; this driver proves it end to end on the built binary.
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
# the maxrss in native units; normalised by $DIV below.
run_map_ws() {
    (cd "$1" && with_deadline 120 "$tmp/rss" \
        "$BIN" --config-json "$CFG" map < /dev/null > "$tmp/map.out" 2>&1) \
        || { echo 0; return 0; }
    _v=$(tail -1 "$tmp/map.out")
    case $_v in ''|*[!0-9]*) echo 0; return 0 ;; esac
    echo $((_v / $DIV))
}

# baseline: the same binary, an empty workspace -- the walk and the loop run,
# no file is ever retained.
base=$(run_map_ws "$empty")
# peak: 40 x 64 KB symbol-sparse .c files (one top-level function each).
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
if [ "$nfiles" -ne 40 ] || [ "$base" -le 0 ]; then
    t_fail "rig broken: $nfiles fixture files, baseline maxrss=$base KB"
else
    t_ok "baseline (empty workspace) map run: maxrss $base KB, 40 x 64 KB fixture staged"
fi

peak=$(run_map_ws "$ws")
mapbytes=$(wc -c < "$tmp/map.out" | tr -d ' ')
nrows=$(grep -c '^mod' "$tmp/map.out")
# THE DENOMINATOR (the M579/SV:1201 rule: a driver that passes with a hollow
# binary measures its own fixtures). A hollow $BIN prints no map, so peak is
# small and the diff check below would be trivially green -- unless we first
# assert the run actually rendered the product's map: all 40 fixture files
# listed. Zero rows means the binary did nothing and every later check is
# vacuous.
if [ "$peak" -le 0 ] || [ "$nrows" -ne 40 ]; then
    t_fail "peak map run rendered no real map (maxrss=$peak KB, $nrows of 40 fixture files listed) -- the binary did not run the product"
else
    # The rendered map must still list all 40 files (the no-output-change
    # promise); counted above as the denominator.
    t_ok "peak (40 x 64 KB workspace) map run: maxrss $peak KB, rendered map lists $nrows files"
fi

# THE F-01 assertion: the map-on process's resident high-water is within ONE
# file (64 KB) + a 384 KB margin (one 64 KB malloc block is at most 128 KB
# resident under mmap, plus the path list, the rendering, and allocator
# slack) of the map-off baseline. Without the fix the diff is ~2.6 MB (the
# sum of the 40 files) and this goes red.
diff_kb=$((peak - base))
CAP=$((64 + 384))
if [ "$diff_kb" -le "$CAP" ]; then
    t_ok "arena peak is one file: maxrss diff $diff_kb KB <= $CAP KB (one 64 KB file + margin)"
else
    t_fail "maxrss diff $diff_kb KB > $CAP KB -- the build retained the SUM of the scanned files (F-01 unfixed; expect ~2600 KB)"
fi

t_done
