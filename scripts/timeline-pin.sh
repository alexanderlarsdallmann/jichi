#!/bin/sh
# scripts/timeline-pin.sh -- PROJECT_TIMELINE's counting universes, written down.
#
# M907's doctrine (docs/PROJECT_TIMELINE.md) requires a re-count to first
# REPRODUCE the previously published column against the pre-commit tree of that
# milestone. Until now the pin's thirteen figures were known to "reproduce"
# but the expressions were never written down -- this script is them.
#
# Usage:  scripts/timeline-pin.sh [commit]   (default: HEAD)
#         scripts/timeline-pin.sh a9cc36f3   # the M865 pin (the check below)
#
# The M865 pin targets live in the M907 footnote of docs/PROJECT_TIMELINE.md:
# 1587 commits, 340 source files, 137 unit files, 398 drivers, 10 e2e modules,
# 23 fuzz targets, 576 EN pages, 628 doc files, 48 proposals, 104 analysis
# notes, 32 reading guides, 856 ROADMAP entries, and the three line totals
# (122,748 / 117,647 / 181,975).
#
# The two traps that cost M907 and M948 are named here because the expressions
# look self-evidently wrong until you try them:
#   * drivers: do NOT exclude with 'run\.sh$' -- it also matches
#     grade_cannot_run.sh. The exclusion is the two files run.sh and _smoke.sh.
#   * reading guides: do NOT use ls docs/reading/*.md -- it misses the
#     docs/reading/traces/ subdirectory (31 instead of 32).
set -u
rev=${1:-HEAD}
cd "$(git rev-parse --show-toplevel)"
# files tracked at $rev (one per line)
list() { git ls-tree -r --name-only "$rev" | /usr/bin/grep -E "$1" | /usr/bin/grep -vE "$2"; }
lines() { : > /tmp/_pinfiles.$$; list "$1" "$2" > /tmp/_pinfiles.$$
  i=0; while IFS= read -r f; do i=$((i+1)); done < /tmp/_pinfiles.$$
  total=0; while IFS= read -r f; do
    n=$(git show "$rev:$f" 2>/dev/null | wc -l); total=$((total+n))
  done < /tmp/_pinfiles.$$
  rm -f /tmp/_pinfiles.$$; echo "$total"
}
commits=$(git rev-list --count "$rev"^)
src_files=$(list '^(src|include)/.*\.(c|h)$' '^$' | wc -l)
unit_files=$(list '^tests/test_.*\.c$' '^$' | wc -l)
drivers=$(list '^tests/smoke/.*\.sh$' '^(tests/smoke/(run|_smoke))\.sh$' | wc -l)
e2e=$(list '^tests/e2e/.*\.py$' '^$' | wc -l)  # ALL .py: the published figure includes the _e2e.py helper
pages=$(list '^docs/.*\.md$' '^docs/(i18n|internal)/' | wc -l)
docfiles=$(list '^docs/.*\.md$' '^docs/internal/' | wc -l)
proposals=$(list '^docs/proposals/.*\.md$' '^$' | wc -l)
analysis=$(list '^docs/analysis/.*\.md$' '^$' | wc -l)
guides=$(list '^docs/reading/.*\.md$' '^$' | wc -l)
entries=$(git show "$rev:docs/ROADMAP.md" | grep -cE '^### ')            # ALL ### headings
entries_mnamed=$(git show "$rev:docs/ROADMAP.md" | grep -cE '^### M[0-9]') # milestone-named only
fuzz=$(git show "$rev:tests/fuzz/jc_fuzz_targets.c" | awk '/JC_FUZZ_TARGETS\[\] = \{/,/^};/' | grep -cE '^[[:space:]]*\{ *"')  # check 17's expression, anchor relaxed (pin file has none)
echo "commit $rev: commits=$commits src_files=$src_files unit_files=$unit_files drivers=$drivers e2e=$e2e pages=$pages docfiles=$docfiles proposals=$proposals analysis=$analysis guides=$guides fuzz=$fuzz roadmap_entries=$entries (all-###) / $entries_mnamed (M-named)"
if [ "${2:-}" = "--pin" ]; then
  echo "--pin: checking the thirteen M865 targets (M907 footnote) at $rev"
  check(){ [ "$1" = "$2" ] && echo "  ok $3 ($1)" || echo "  FAIL $3: got $1 want $2"; }
  check $commits 1587 commits; check $src_files 340 src_files; check $unit_files 137 unit_files
  check $drivers 398 drivers; check $e2e 10 e2e; check $fuzz 23 fuzz
  check $pages 576 pages; check $docfiles 628 docfiles; check $proposals 48 proposals
  check $analysis 104 analysis; check $guides 32 guides; check $entries 856 entries_all###  # M865 published all-###; the LIVE table uses M-named (see footnote)
fi
echo "  line totals (src, tests, docs-EN): $(lines '^(src|include)/.*\.(c|h)$' '^$') $(lines '^tests/' '^$') $(lines '^docs/.*\.md$' '^docs/(i18n|internal)/')"
