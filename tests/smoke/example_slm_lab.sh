#!/bin/sh
# smoke: the examples/slm-lab bench stays valid, and its program trains (M764).
#
# The operator's decision on the twelve questions, item 3: the SLM lab at level
# (b), a character-level model trained from scratch on a CPU, as an ungraded
# bench in examples/. Checks 1-5 are every bench's contract, as the other
# example_*.sh drivers hold it: the assets, the config, jichi's own asset
# validator, and a negative check that the validator has teeth. Checks 6-9 hold
# the program the lab is about -- charlm.c -- to what its header promises:
# strict C89, the baselines, a model that learns, and a seed that reproduces.
. "$(dirname "$0")/_smoke.sh"

JQ="$SMOKE_TOOLS/jsonq"
EX="$SMOKE_ROOT/examples/slm-lab"
CC_LAB="${CC:-cc}"

t_plan 9

# 1. the expected assets are all present (2 agents, 4 commands, 3 skills).
na=$(ls "$EX/agents"/*.md 2>/dev/null | grep -c .)
ncmd=$(ls "$EX/commands"/*.md 2>/dev/null | grep -c .)
ns=$(ls "$EX/skills"/*/SKILL.md 2>/dev/null | grep -c .)
if [ "$na" -eq 2 ] && [ "$ncmd" -eq 4 ] && [ "$ns" -eq 3 ] \
   && [ -f "$EX/START_HERE.md" ] && [ -f "$EX/AGENTS.md" ]; then
    t_ok "assets present (2 agents, 4 commands, 3 skills, START_HERE, AGENTS)"
else
    t_fail "asset count off (agents=$na commands=$ncmd skills=$ns)"
fi

# 2. the example config is valid JSON with the documented shape, and no key.
if [ "$("$JQ" '.models[0].roles[0]' "$EX/config.example.json" 2>/dev/null)" = "chat" ] \
   && ! grep -q '"apiKey"' "$EX/config.example.json"; then
    t_ok "config.example.json is valid JSON with a chat model and no literal key"
else
    t_fail "config.example.json is not the expected valid, secret-free JSON"
fi

# stage the assets as a real .jichi/ project.
ws=$(smoke_tmp)
mkdir -p "$ws/.jichi"
cp -r "$EX/agents" "$ws/.jichi/agents"
cp -r "$EX/commands" "$ws/.jichi/commands"
cp -r "$EX/skills" "$ws/.jichi/skills"
cp "$EX/AGENTS.md" "$ws/"
out=$(cd "$ws" && "$BIN" doctor 2>&1)

# 3. jichi's own validator passes the staged assets.
if printf '%s\n' "$out" | grep -qi 'asset frontmatter valid'; then
    t_ok "doctor: asset frontmatter valid"
else
    t_fail "doctor did not report the assets valid"
    printf '%s\n' "$out" | grep -i asset | sed 's/^/# /'
fi

# 4. doctor sees the project assets.
if printf '%s\n' "$out" | grep -qi 'project assets'; then
    t_ok "doctor: project assets discovered"
else
    t_fail "doctor did not discover the project assets"
fi

# 5. teeth: a broken asset (an unknown frontmatter key) must be REJECTED.
bad=$(smoke_tmp)
mkdir -p "$bad/.jichi/agents"
printf -- '---\ndescription: broken\nbogus_key: nope\n---\nbody\n' \
    > "$bad/.jichi/agents/broken.md"
badout=$(cd "$bad" && "$BIN" doctor 2>&1)
if printf '%s\n' "$badout" | grep -qi 'asset frontmatter valid'; then
    t_fail "validator did NOT catch an unknown frontmatter key -- no teeth"
else
    t_ok "validator rejects a broken asset (has teeth)"
fi

# --- the program ------------------------------------------------------------------------
command -v "$CC_LAB" > /dev/null 2>&1 || {
    for _k in 6 7 8 9; do t_skip_one "no C compiler ($CC_LAB) to build charlm.c"; done
    t_done
}
tmp=$(smoke_tmp)

# 6. strict C89, zero warnings, as the lab's /build asks of the learner.
if "$CC_LAB" -std=c89 -pedantic -Wall -Wextra -Werror -O2 -o "$tmp/charlm" \
       "$EX/charlm.c" -lm > "$tmp/cc.out" 2>&1; then
    t_ok "charlm.c compiles under -std=c89 -pedantic -Wall -Wextra -Werror"
else
    t_fail "charlm.c does not compile cleanly: $(head_bytes 300 "$tmp/cc.out")"
fi

# A short, fixed run on two of this project's documents.
cat "$SMOKE_ROOT/docs/PHILOSOPHY.md" "$SMOKE_ROOT/docs/GLOSSARY.md" > "$tmp/corpus.txt"
run_lab() {
    "$tmp/charlm" --steps 300 --hidden 32 --every 100 --sample 60 --seed 3 \
        "$tmp/corpus.txt" > "$1" 2>&1
}
run_lab "$tmp/a.txt"; rca=$?
run_lab "$tmp/b.txt"
num() {   # the number after a label on the line that starts with it
    sed -n "s/^ *$1 *\([0-9.]*\).*/\1/p" "$2" | head -1
}
u=$(num uniform "$tmp/a.txt")
g=$(num unigram "$tmp/a.txt")
b=$(num bigram "$tmp/a.txt")
v0=$(sed -n 's/^step 0 .*validation \([0-9.]*\).*/\1/p' "$tmp/a.txt")
vN=$(sed -n 's/^step 300 .*validation \([0-9.]*\).*/\1/p' "$tmp/a.txt")

# 7. the baselines are ordered (uniform > unigram > bigram on real text), step 0
# sits at the uniform baseline (an untrained model knows nothing), and training
# brought the validation loss below it.
if [ "$rca" -eq 0 ] && [ -n "$u" ] && [ -n "$vN" ] \
   && awk -v u="$u" -v g="$g" -v b="$b" -v v0="$v0" -v vn="$vN" \
        'BEGIN { d = v0 - u; if (d < 0) d = -d;
                 exit !(u > g && g > b && d < 0.3 && vn < v0 && vn < u) }'; then
    t_ok "baselines ordered ($u > $g > $b); validation $v0 at step 0 -> $vN at step 300"
else
    t_fail "run (rc=$rca): uniform '$u' unigram '$g' bigram '$b', validation '$v0' -> '$vN'"
fi

# 8. a seed reproduces a run byte for byte, sample included
if [ -s "$tmp/a.txt" ] && cmp -s "$tmp/a.txt" "$tmp/b.txt"; then
    t_ok "the same seed reproduces the run byte for byte"
else
    t_fail "two runs with seed 3 differ: $(diff "$tmp/a.txt" "$tmp/b.txt" | head -3 | tr '\n' ' ')"
fi

# 9. the split: the held-out tail is announced, and train + validation = corpus
total=$(wc -c < "$tmp/corpus.txt" | tr -d ' ')
st=$(sed -n 's/^split: train \([0-9]*\), validation \([0-9]*\) .*never trained on.*/\1 \2/p' "$tmp/a.txt")
if [ -n "$st" ] && [ "$(echo "$st" | awk '{print $1 + $2}')" = "$total" ]; then
    t_ok "the split holds out the tail, and train + validation = $total characters"
else
    t_fail "split line '$st' against $total characters"
fi

t_done
