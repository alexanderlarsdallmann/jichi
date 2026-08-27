#!/bin/sh
# smoke lint: the output caps docs/TOOL_OUTPUT_COST.md advertises are the ones
# the tools actually compile in (M326z).
#
# That page recommends setting `readMaxBytes` and tells the reader what the
# built-in is (256 KB) and what `--lite` uses (64 KB). Advice keyed to a number
# is only as good as the number: if a #define moves and the table does not, the
# page recommends a change from a value that no longer exists, which is worse
# than recommending nothing -- the reader has no way to tell.
#
# Ground truth is the source, in both directions: the `#define`s and the `--lite`
# fallbacks in jc_config.c's parse. Decoded here rather than compared to
# hand-written constants, the portability_lint idiom.
#
# M440 moved the four `#define`s out of the individual tools into
# include/jc_toolcaps.h, because the PROMPT now reports them too and two copies of
# a number that must agree is the drift M296 forbids. This lint broke on that move
# and said so loudly ("unreadable"), which is the behaviour an extraction floor is
# for -- it read four files and now reads one.
#
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 4

DOC="$SMOKE_ROOT/docs/TOOL_OUTPUT_COST.md"
CFG="$SMOKE_ROOT/src/config/jc_config.c"
tmp=$(smoke_tmp)

if [ ! -f "$DOC" ]; then
    t_fail "docs/TOOL_OUTPUT_COST.md is missing"
    t_fail "(no page: --lite caps not checked)"
    t_fail "(no page: config keys not checked)"
    t_fail "(no page: KB arithmetic not checked)"
    t_done
fi

# tool : the macro in include/jc_toolcaps.h : the config key that overrides it
CAPS="read_file:JC_CAP_READ_DEFAULT:readMaxBytes
run_terminal_command:JC_CAP_RUN_DEFAULT:runMaxBytes
search_code:JC_CAP_SEARCH_DEFAULT:searchMaxBytes
fetch_url:JC_CAP_FETCH_DEFAULT:fetchMaxBytes"
CAPHDR="$SMOKE_ROOT/include/jc_toolcaps.h"

# --- 1: every built-in cap in the doc matches its #define --------------------
bad=""
n=0
for row in $CAPS; do
    def=$(echo "$row" | cut -d: -f2)
    key=$(echo "$row" | cut -d: -f3)
    # "#define JC_CAP_READ_DEFAULT   (256 * 1024)" -> 256
    kb=$(sed -n "s/.*#define $def *( *\([0-9][0-9]*\) \* 1024 *).*/\1/p" \
         "$CAPHDR" | head -1)
    [ -n "$kb" ] || { bad="$bad $def(unreadable)"; continue; }
    n=$((n + 1))
    # Anchored on the TABLE ROW for this tool, not on the config key anywhere in
    # the page. Matching the key was the first attempt and had no teeth: the
    # recommendation prose ("Set `readMaxBytes`. 256 KB is a safety bound...")
    # also contains the key AND the value, so a wrong table row still found the
    # right number on a different line -- docs/TEST_INTEGRITY.md fm. 9, the
    # assertion matching something other than the thing it named.
    tool=$(echo "$row" | cut -d: -f1)
    got=$(sed -n "s/^| \`$tool\` | \**\([0-9][0-9]*\) KB\** |.*/\1/p" "$DOC" | head -1)
    if [ "$got" != "$kb" ]; then
        bad="$bad $tool(doc=${got:-none}KB src=${kb}KB)"
    fi
done
if [ "$n" -ge 4 ] && [ -z "$bad" ]; then
    t_ok "all $n built-in caps in the doc match their #define"
else
    t_fail "cap mismatch or unreadable ($n read):$bad"
fi

# --- 2: the --lite column matches jc_config.c's fallbacks --------------------
# Those are the numbers the page tells an operator to consider adopting without
# --lite, so a drift here misdirects the recommendation specifically.
bad=""
for row in $CAPS; do
    key=$(echo "$row" | cut -d: -f3)
    tool=$(echo "$row" | cut -d: -f1)
    raw=$(sed -n "s/.*\"$key\", lite ? \([0-9][0-9]*\)\.0.*/\1/p" "$CFG" | head -1)
    [ -n "$raw" ] || { bad="$bad $key(no-lite-default)"; continue; }
    kb=$((raw / 1024))
    # Second column of the same anchored row, for the same reason as check 1.
    got=$(sed -n "s/^| \`$tool\` | [^|]* | \([0-9][0-9]*\) KB |.*/\1/p" "$DOC" | head -1)
    [ "$got" = "$kb" ] || bad="$bad $tool(doc=${got:-none}KB lite=${kb}KB)"
done
if [ -z "$bad" ]; then
    t_ok "the --lite column matches jc_config.c's fallbacks"
else
    t_fail "--lite drift:$bad"
fi

# --- 3: every config key the doc names is one jichi parses -------------------
# The page's whole recommendation is "set these keys"; a key jichi does not read
# is advice that silently does nothing.
bad=""
for key in readMaxBytes runMaxBytes searchMaxBytes fetchMaxBytes gitMaxBytes; do
    if grep -q "$key" "$DOC"; then
        grep -q "\"$key\"" "$CFG" || bad="$bad $key"
    fi
done
if [ -z "$bad" ]; then
    t_ok "every cap key the page recommends is parsed by jc_config.c"
else
    t_fail "page names key(s) jichi does not parse:$bad"
fi

# --- 4: the page still states the two facts its advice rests on --------------
# Both are load-bearing: without the multiplier the advice has no motive, and
# without the "measure your own" step the reader cannot tell if it applies.
if grep -q 'prompt cache hit-rate' "$DOC" && grep -q 'cache-audit' "$DOC"; then
    t_ok "the page states the multiplier's precondition and how to check it"
else
    t_fail "the page no longer tells the reader to check whether their backend caches"
fi

t_done
