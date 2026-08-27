#!/bin/sh
# Startup footprint of CLI coding agents (M181). A measurement, never a CI
# gate. For each tool present: peak RSS and wall time of a trivial cold
# invocation (--version, then --help), three runs each keeping the best
# (cold-cache noise goes one way), plus the on-disk footprint of the
# executable itself. Prints a markdown table.
#
# Usage: sh tests/measure/startup.sh [path-to-jichi]
# Honest caveat printed with the numbers: the three tools do different
# amounts of work at startup (update checks, config discovery, runtimes).

JICHI="${1:-./jichi}"
TIME=/usr/bin/time

measure() { # $1 label, rest = command
    label="$1"; shift
    best_rss=""; best_ms=""
    for i in 1 2 3; do
        out=$($TIME -v "$@" 2>&1 >/dev/null)
        rss=$(printf '%s\n' "$out" | sed -n 's/.*Maximum resident set size (kbytes): //p')
        el=$(printf '%s\n' "$out" | sed -n 's/.*Elapsed (wall clock) time (h:mm:ss or m:ss): //p')
        # m:ss.cc -> milliseconds
        ms=$(printf '%s' "$el" | awk -F: '{ if (NF==2) print int(($1*60+$2)*1000); else print int(($1*3600+$2*60+$3)*1000) }')
        if [ -z "$best_rss" ] || [ "$rss" -lt "$best_rss" ]; then best_rss=$rss; fi
        if [ -z "$best_ms" ] || [ "$ms" -lt "$best_ms" ]; then best_ms=$ms; fi
    done
    printf '| %s | %s KB | %s ms |\n' "$label" "$best_rss" "$best_ms"
}

disk() { # $1 command name/path
    p=$(command -v "$1" 2>/dev/null) || { echo "n/a"; return; }
    # Follow one level of symlink; report the real file's size.
    rp=$(readlink -f "$p")
    du -h "$rp" | cut -f1
}

echo "Host: $(uname -srm); $(nproc) cores"
echo "Date: $(date -u +%Y-%m-%d)"
echo
echo "Versions:"
"$JICHI" --version 2>/dev/null | head -1
opencode --version 2>/dev/null | head -1 | sed 's/^/opencode /'
claude --version 2>/dev/null | head -1 | sed 's/^/claude /'
echo
echo "| Invocation | peak RSS | wall |"
echo "|---|---|---|"
measure "jichi --version"  "$JICHI" --version
measure "jichi --help"     "$JICHI" --help
if command -v opencode >/dev/null 2>&1; then
    measure "opencode --version" opencode --version
    measure "opencode --help"    opencode --help
fi
if command -v claude >/dev/null 2>&1; then
    measure "claude --version" claude --version
    measure "claude --help"    claude --help
fi
echo
echo "| Executable | on disk |"
echo "|---|---|"
printf '| jichi | %s (+ system libcurl) |\n' "$(du -h "$(readlink -f "$JICHI")" | cut -f1)"
printf '| opencode | %s |\n' "$(disk opencode)"
printf '| claude | %s |\n' "$(disk claude)"
echo
echo "Caveat: --version/--help exercise each tool's full startup path"
echo "(runtime init, config discovery, update checks where applicable) but"
echo "no model traffic; the tools do different amounts of startup work by"
echo "design. Treat as environment snapshots, not benchmarks of record."
