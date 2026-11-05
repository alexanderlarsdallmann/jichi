#!/bin/sh
# smoke: a prompt-asset file read past its cap no longer stays resident whole
# (M1109, D-35; finding docs/internal/2026-10-17-hardening-review/
# 05-files-and-persistence-internal.md D-35 row, the "read at most the asset's
# cap plus one byte into a transient buffer" line).
#
# MECHANISM: jc_rules' add_file, jc_glossary's append_file and the command
# @-reference append_file each jc_read_file'd the WHOLE prompt asset (up to the
# 64 MB JC_READ_FILE_MAX) into the SESSION arena and only then cut it to the
# asset's cap (32 KB / 8 KB). The cut bounds the OUTPUT, not the RETENTION:
# the full file stayed on the session arena for the process lifetime.
# Measured (the D-35 note): seven sparse 60 MB prompt-asset files (16 KB on
# disk) raised peak RSS from 12.9 MB to 442 MB. The fix is a bounded read at
# each cut site (jc_read_file_bounded: the leading cap+1 for the head keepers,
# the trailing cap for the tail keeper) -- no output change.
#
# THE RIG: a one-turn headless mock run (`-q --no-session --no-stdin -p go`) in
# an EMPTY workspace (baseline) and in a workspace whose CLAUDE.md is 4 MB
# (over the 32 KB rules cap, under the 64 MB read cap). A tiny C wrapper
# fork+exec's $BIN, reaps it with wait4, and prints ru_maxrss -- the PROCESS
# high-water resident set, which includes the arena peak whether or not the OS
# returns the blocks on free. The assertion is on the DIFF, so the binary's
# own ~MB of baseline cancels:
#
#   peak(maxrss) - baseline(maxrss) <= one bounded read + margin
#
# WITHOUT the fix the diff is ~4096 KB (the whole 4 MB file resident) and
# check 4 goes red; with the fix it is ~32 KB (one bounded head read).
#
# Check 3 is the ANTI-VACUOUS denominator (the M579/SV:1201 rule): the
# captured request req.1 must contain the file's first-line marker, proving
# the 4 MB CLAUDE.md was ACTUALLY read into the system prompt. A hollow
# binary that never reads the file would read low and pass check 4 vacuously.
#
# ru_maxrss is KB on Linux, BYTES on macOS (Darwin) -- converted to KB below.
#
# NOTE: registered in run.sh by the wave supervisor at merge (M1109 tasking,
# with the count-page carry); run standalone meanwhile:
#   mkdir -p /tmp/jdw-d35 && TMPDIR=/tmp/jdw-d35 sh tests/smoke/prompt_asset_peak.sh
. "$(dirname "$0")/_smoke.sh"

# The rss wrapper needs a C compiler (it is the whole point of this driver);
# decide before t_plan, like repomap_peak.sh.
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
 * status. maxrss is the process high-water resident set -- the session-arena
 * peak is in it whether or not the allocator returned the blocks on free. */
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

# run_turn DIR [CAPDIR] -> the maxrss (KB) of a one-turn headless mock run from
# DIR (0 on failure; CAPDIR, when given, is the request capture dir). The rss
# wrapper prints the run's output and, as its LAST line, the maxrss in native
# units; normalised by $DIV below.
run_turn() {
    (
        mm_start "$tmp/reply.mm" "${2:-$tmp/cap}"
        write_config "$tmp/cfg.json" "$MM_PORT"
        ( cd "$1" && with_deadline 120 "$tmp/rss" "$BIN" \
            --config "$tmp/cfg.json" -q --no-session --no-stdin -p go \
            < /dev/null > "$tmp/turn.out" 2>&1 )
        mm_stop
    ) || { echo 0; return 0; }
    _v=$(tail -1 "$tmp/turn.out")
    case $_v in ''|*[!0-9]*) echo 0; return 0 ;; esac
    echo $((_v / $DIV))
}

# One-turn mock: a single text reply, no tools.
cat > "$tmp/reply.mm" <<'EOF'
wire openai
rule
  count 1
  text READY
EOF

# baseline: the same binary, an empty workspace -- no prompt asset is read.
base=$(run_turn "$empty")
if [ "$base" -gt 0 ]; then
    t_ok "baseline turn: maxrss $base KB"
else
    t_fail "baseline turn produced no maxrss ($base KB) -- the binary did not run (a hollow build yields 0; every later check is vacuous)"
fi

# peak fixture: a 4 MB CLAUDE.md whose FIRST line is a distinctive marker
# (over the 32 KB JC_RULES_MAX, under the 64 MB JC_READ_FILE_MAX -- the shape
# D-35 measured).
printf 'D35PEAKMARKER\n' > "$ws/CLAUDE.md"
dd if=/dev/zero bs=1024 count=4096 2>/dev/null | tr '\0' 'x' >> "$ws/CLAUDE.md"
cbytes=$(wc -c < "$ws/CLAUDE.md" | tr -d ' ')

# peak: the same turn in the workspace with the 4 MB CLAUDE.md.
peak=$(run_turn "$ws" "$tmp/capbig")
if grep -F 'D35PEAKMARKER' "$tmp/capbig/req.1" 2>/dev/null; then
    t_ok "4 MB CLAUDE.md was read (marker in the system prompt of req.1)"
else
    t_fail "the file was not read (no marker in req.1) -- every later check is vacuous"
fi

# THE D-35 assertion: the resident high-water difference is ONE bounded read
# (one 32 KB head + margin), not the whole 4 MB file. Without the fix the diff
# is ~4096 KB and this goes red.
diff_kb=$((peak - base))
CAP=1024
if [ "$diff_kb" -le "$CAP" ]; then
    t_ok "peak-retained is one bounded read: maxrss diff $diff_kb KB <= $CAP KB"
else
    t_fail "maxrss diff $diff_kb KB > $CAP KB -- the whole 4 MB prompt asset is resident (D-35 unfixed; expect ~4096 KB)"
fi

t_done
