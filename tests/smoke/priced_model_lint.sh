#!/bin/sh
# smoke lint: no harness spends money by default (M544).
#
# THE DEFECT. tests/bench/craft_ab/craft_ab.py declared
#
#     r.add_argument("--model", default="anthropic/claude-opus-4-5")
#     r.add_argument("--api-base", default="https://api.hrz.uni-giessen.de/v1")
#
# The gateway exposes 353 model ids, priced ones included, so that pair means any
# `craft_ab.py run` that forgot --model billed a SHARED institutional key. It did:
# roughly $10 went on anthropic/claude-opus-4-5 for routine dogfooding the free
# jlu/* models then did better (docs/ANECDOTES.md #63).
#
# CLAUDE.md already carried the rule -- "a default in a test harness ... is a record
# of a past decision, not a standing permission" -- naming this very file as the
# example. Writing it down did not prevent it, which is the whole argument for
# preferring a lint: the rule was known, quotable, and violated by the line it was
# written about.
#
# WHAT IS AND IS NOT A VIOLATION, because the difference is most of this file.
# A priced vendor's model id is legitimate in three places and this lint must not
# flag them:
#
#   1. CONVERTER FIXTURES -- tests/test_convert.c, tests/test_jsonc.c and
#      examples/opencode.jsonc are foreign configs whose whole purpose is to name
#      foreign models. Nothing calls a model; the code under test is a parser.
#   2. A PURE PREDICATE -- tests/test_promptcache.c passes model NAMES to
#      jc_promptcache_min_tokens, which returns a number. No network.
#   3. PROVIDER EXAMPLES -- examples/config.anthropic.json and config.openai.json
#      document pointing jichi at those vendors' own endpoints with the reader's own
#      key. jichi supports those providers; hiding that would be dishonest, and a
#      reader who copies one is spending their own money knowingly.
#
# The harm is the combination the lint actually checks: a priced id **reached
# through the institutional gateway**, or a priced id **as a default**. Comments are
# stripped first, so a file may cite the incident -- craft_ab.py's new comment names
# the exact model that cost the $10, and must be able to.
#
# UNIVERSE, measured 2026-08-22: 5 files, 13 references outside docs/. Small enough
# to enumerate, which is why a lint fits here and did not fit the vacuous-check
# problem (scripts/mutant-sweep.sh records that negative result).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1

# Priced vendors: the prefixes the gateway resells. `jlu/` is the free institutional
# namespace and is deliberately absent.
PRICED='anthropic/|openai/|vertex_ai/|azure/|gemini/|bedrock/'
GATEWAY='api\.hrz\.uni-giessen\.de'

# Files that may reach a model: harnesses and scripts we run. NOT src/ -- jichi's
# own code names no model id -- and not docs/, whose subject is history.
FILES=$(ls tests/bench/craft_ab/*.py tests/bench/*.py scripts/*.sh 2>/dev/null)

# ---- 1: the file list is non-empty and the pattern matches something ------
# Both floors. An empty FILES makes checks 2-3 vacuous; a PRICED pattern that
# matches nothing anywhere would make them vacuous too, so it is proved against a
# file known to contain one.
nf=$(printf '%s\n' $FILES | "$G" -c .)
canfire=$("$G" -cE "$PRICED" tests/test_convert.c 2>/dev/null || echo 0)
if [ "$nf" -ge 4 ] && [ "$canfire" -ge 1 ]; then
    t_ok "$nf harness/script files in scope, and the pattern matches a known id"
else
    t_fail "scope=$nf files (want >= 4), pattern self-test=$canfire (want >= 1) \
-- the extraction is broken and checks 2-3 would pass on nothing"
fi

# ---- 2: no priced model as a DEFAULT ------------------------------------
# The defect's exact shape. Comments stripped first so a file may cite the
# incident that produced the rule.
bad=""
for f in $FILES; do
    sed -e 's/[[:space:]]*#.*$//' "$f" \
        | "$G" -qE "default[[:space:]]*=[[:space:]]*[\"']($PRICED)" \
        && bad="$bad $f"
done
if [ -z "$bad" ]; then
    t_ok "no harness declares a priced model as a default"
else
    t_fail "priced model as a DEFAULT in:$bad -- a run that forgets to pass \
--model spends real money, and on a shared key that is someone else's money"
fi

# ---- 3: no priced model reached through the INSTITUTIONAL gateway -------
# The sharper condition, and the one that made the craft_ab pair dangerous rather
# than merely unwise. A priced id against a vendor's own endpoint spends the
# reader's key; against this gateway it spends a shared one.
bad=""
for f in $FILES; do
    stripped="$tmp/$(basename "$f").s"
    sed -e 's/[[:space:]]*#.*$//' "$f" > "$stripped"
    if "$G" -qE "$GATEWAY" "$stripped" && "$G" -qE "($PRICED)[a-z0-9.-]" "$stripped"
    then
        bad="$bad $f"
    fi
done
if [ -z "$bad" ]; then
    t_ok "no file routes a priced model through the institutional gateway"
else
    t_fail "priced model + the shared gateway in:$bad -- this is the \$10 \
combination from ANECDOTES #63"
fi

# ---- 4: the legitimate uses stay exactly four files -------------------
# A fence around the exemptions rather than a fix. These four are converter
# fixtures and a pure predicate (see the header); a FIFTH file acquiring a priced
# id is a thing to look at, not to wave through. Fix the file or extend this list
# with a reason -- never silently.
known='tests/test_convert.c tests/test_jsonc.c tests/test_promptcache.c examples/opencode.jsonc'
found=$("$G" -rlE "$PRICED" --include='*.c' --include='*.h' --include='*.jsonc' \
        tests examples 2>/dev/null | sort | tr '\n' ' ')
want=$(printf '%s\n' $known | sort | tr '\n' ' ')
if [ "$found" = "$want" ]; then
    t_ok "the 4 fixture/predicate files naming priced models are unchanged"
else
    t_fail "the set of C/jsonc files naming a priced model changed.
  found: ${found:-none}
  known: $want
Each entry must be a parser fixture or a pure predicate -- something that names a
model without ever calling one. If a new file calls one, that is the defect."
fi

t_done
