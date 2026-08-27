#!/bin/sh
# smoke: doctor reports a `style:` that names no output style (M302).
#
# The feature and its lint ship together on purpose. M285's lesson is that a
# declared-but-dead name is worse than an absent one, because it looks like it is
# doing something: a `style:` naming a missing style falls back silently to the
# session's tone, so nothing breaks and nothing works either. That is precisely the
# failure that needs a check rather than a user's patience.
#
# WARN, not FAIL, matching M285's asymmetry: a specialist with the wrong tone is
# degraded, not broken.
#
# Asserted differentially (the M284 pattern): this fixture has an unreachable model
# server, so doctor's exit code and total warning count are not stable ground to
# stand on. Compare the finding's presence with and without the defect instead.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

write_config "$tmp/config.json" 9

mkdir -p "$ws/.jichi/output-styles" "$ws/.jichi/agents" "$ws/.jichi/skills/tidy"

cat > "$ws/.jichi/output-styles/blunt.md" <<'EOF'
---
description: terse and direct
---
Be blunt. No hedging.
EOF

# M389: output styles are frontmatter-VALIDATED too. They were the one
# frontmatter-bearing project asset doctor never checked -- the very kind M302
# pointed at when agents and skills gained `style:`. Two real defects here: a
# `name:` that does nothing (the filename is the style's name, so this is the
# M285 declared-but-dead shape) and a typo'd `descrption:` leaving no usable
# description for `/output-style` to show.
cat > "$ws/.jichi/output-styles/sloppy.md" <<'EOF'
---
name: sloppy
descrption: typo'd key, so no real description
---
Whatever you like.
EOF

# A profile whose style EXISTS, and one whose style does not.
cat > "$ws/.jichi/agents/reviewer.md" <<'EOF'
---
description: reviews a change
style: blunt
---
You review changes.
EOF
cat > "$ws/.jichi/agents/tutor.md" <<'EOF'
---
description: explains patiently
style: patient
---
You explain things.
EOF
cat > "$ws/.jichi/skills/tidy/SKILL.md" <<'EOF'
---
name: tidy
description: tidy up a file
style: nonexistent-style
---
1. read it
2. tidy it
EOF

(cd "$ws" && with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/bad.out" 2>&1)

if grep -q "style" "$tmp/bad.out"; then
    t_ok "doctor mentions asset styles at all"
else
    t_fail "doctor says nothing about styles -- the check did not run"
    sed 's/^/    | /' "$tmp/bad.out" | head -20
fi

if grep -q "names no such style" "$tmp/bad.out"; then
    t_ok "the dead style names are reported"
else
    t_fail "a style naming nothing was not reported"
    grep -i "style" "$tmp/bad.out" | sed 's/^/    | /'
fi

# It must name WHICH asset and WHICH style, or the finding is not actionable --
# "some style is wrong" sends the reader to grep the tree themselves.
if grep -q "patient" "$tmp/bad.out" && grep -q "nonexistent-style" "$tmp/bad.out"
then
    t_ok "the finding names the offending asset and style"
else
    t_fail "the finding does not name the asset/style pair"
    grep -i "style" "$tmp/bad.out" | sed 's/^/    | /'
fi

if grep -q "unknown frontmatter key 'descrption'" "$tmp/bad.out" &&
   grep -q "output-styles/sloppy.md" "$tmp/bad.out"; then
    t_ok "an output style's bad frontmatter is reported, naming the file"
else
    t_fail "output-style frontmatter went unvalidated (M389)"
    grep -i "frontmatter" "$tmp/bad.out" | sed 's/^/    | /'
fi

# Now fix both and confirm the finding DISAPPEARS -- the differential half. Without
# this, a check that always fires would look identical to a working one.
cat > "$ws/.jichi/output-styles/sloppy.md" <<'EOF'
---
description: repaired
---
Whatever you like.
EOF
cat > "$ws/.jichi/output-styles/patient.md" <<'EOF'
---
description: gentle
---
Explain gently, one step at a time.
EOF
sed 's/nonexistent-style/blunt/' "$ws/.jichi/skills/tidy/SKILL.md" \
    > "$ws/.jichi/skills/tidy/SKILL.md.new"
mv "$ws/.jichi/skills/tidy/SKILL.md.new" "$ws/.jichi/skills/tidy/SKILL.md"

(cd "$ws" && with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/good.out" 2>&1)

if ! grep -q "names no such style" "$tmp/good.out" &&
   grep -q "asset styles resolve" "$tmp/good.out"; then
    t_ok "with every style present the finding is gone and doctor says so"
else
    t_fail "the finding survived fixing the styles (it fires unconditionally)"
    grep -i "style" "$tmp/good.out" | sed 's/^/    | /'
fi

if grep -q "asset frontmatter valid" "$tmp/good.out"; then
    t_ok "repaired: the output-style frontmatter finding is gone"
else
    t_fail "asset frontmatter still reported as invalid after the repair"
    grep -i "frontmatter" "$tmp/good.out" | sed 's/^/    | /'
fi

t_done
