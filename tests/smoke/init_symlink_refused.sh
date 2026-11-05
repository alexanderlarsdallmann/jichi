#!/bin/sh
# smoke: D-23 (M1105) `init` / `setup --import` must not create or overwrite
# files through symlinks planted in the target project.
#
# THE DEFECT. `jc_file_exists` is a stat (follows a link), `scaffold_mkparent`
# follows a symlinked parent, and `jc_write_file` is fopen("wb") (follows a
# symlink leaf). So a symlink leaf or a symlinked parent directory in the
# project redirected scaffold writes OUTSIDE the project:
#   (a) dangling  ws/AGENTS.md -> outside/created.md   wrote outside, rc=0
#   (b) symlinked ws/.jichi    -> outside/dir           wrote all 25 pack files outside
#   (c) --force   ws/AGENTS.md -> outside/victim.md     overwrote a 0600 outside file
# The existing setup_draft_write guard (M993/D-21) already resolves the parent
# under the workspace root and lstats the leaf; this driver proves the scaffold
# config/script/import writes get the same protection.
#
# CHECKS (one t_ok/t_fail each):
#   1: dangling leaf -- nothing created outside, the run names the refusal
#   2: symlinked parent (.jichi) -- no pack file created outside
#   3: --force onto a link leaf -- the outside victim is byte- AND mode-unchanged
#   4: --force onto a REGULAR existing file -- IS overwritten (no over-refusal)
#   5: setup --import with a symlinked local/config.json -- nothing outside, refusal printed
#   6: setup --import --config-target global -- $HOME/.jichi IS written (no over-refusal)
#   7: same, with a PLANTED symlink at $HOME/.jichi -- still refused (leaf lstat teeth)
#
# No mock model: `init` and `setup --import` (flag mode) are offline.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# outside_snapshot DIR -> print a deterministic listing (files + mode + size) so a
# later compare can tell whether anything was created, clobbered, or re- chmod'd.
outside_snapshot() {
    _os="$1"
    # stat -c is banned by posix_utils_lint (BSD stat uses -f): read the mode
    # with the tier's `ls -l | cut -c1-10` idiom and the size with `wc -c < f`
    # (the tier's standard portable size read -- ls -l's size column is
    # locale/spacing-fragile). Sorted, so a before/after diff catches a created,
    # clobbered, or re-chmod'd file exactly as the old listing did.
    (cd "$_os" 2>/dev/null && find . -type f 2>/dev/null) \
        | sort \
        | while IFS= read -r _osf; do
              [ -n "$_osf" ] || continue
              printf '%s %s %s\n' \
                  "$(ls -l "$_os/$_osf" 2>/dev/null | cut -c1-10)" \
                  "$(wc -c < "$_os/$_osf" 2>/dev/null | tr -d ' ')" \
                  "$_osf"
          done
}

# ---------------------------------------------------------------- LEG 1
# Dangling leaf at the project root. The 25 .jichi/... files are unaffected
# (their parent is the real ws); only the AGENTS.md leaf is a planted link.
ws1=$(smoke_tmp); out1=$(smoke_tmp)
ln -s "$out1/created.md" "$ws1/AGENTS.md"
snap1_before=$(outside_snapshot "$out1")
(cd "$ws1" && unset JC_CONFIG && with_deadline 30 "$BIN" init default </dev/null) > "$tmp/out1" 2>&1
rc1=$?
snap1_after=$(outside_snapshot "$out1")
if [ "$snap1_before" = "$snap1_after" ] && [ ! -e "$out1/created.md" ] \
   && grep -q 'refused' "$tmp/out1"; then
    t_ok "leg1 dangling leaf: nothing created outside + refusal named (rc=$rc1)"
else
    t_fail "leg1 dangling leaf: outside changed or no refusal (rc=$rc1, before=[$snap1_before] after=[$snap1_after]): $(head_bytes 200 "$tmp/out1")"
fi

# ---------------------------------------------------------------- LEG 2
# Symlinked PARENT directory: .jichi -> outside/dir. Every .jichi/* pack file
# must be refused; none may land outside.
ws2=$(smoke_tmp); out2=$(smoke_tmp); mkdir -p "$out2/dir"
ln -s "$out2/dir" "$ws2/.jichi"
snap2_before=$(outside_snapshot "$out2")
(cd "$ws2" && unset JC_CONFIG && with_deadline 30 "$BIN" init default </dev/null) > "$tmp/out2" 2>&1
rc2=$?
snap2_after=$(outside_snapshot "$out2")
# (review-0109: also require the refusal to be NAMED -- an unchanged outside
# tree alone could pass for the wrong reason, e.g. a crash before writing.)
if [ "$snap2_before" = "$snap2_after" ] && [ -z "$(outside_snapshot "$out2")" ] \
   && grep -q 'refused' "$tmp/out2"; then
    t_ok "leg2 symlinked parent .jichi: no pack file created outside + refusal named (rc=$rc2)"
else
    t_fail "leg2 symlinked parent .jichi: files escaped or no refusal (rc=$rc2): $(outside_snapshot "$out2" | head -3)"
fi

# ---------------------------------------------------------------- LEG 3
# --force onto a LINK leaf. The victim is an existing 0600 file OUTSIDE. After
# the run it must be byte-identical to the backup AND still mode 0600.
ws3=$(smoke_tmp); out3=$(smoke_tmp)
printf 'SECRET_M1105_victim\n' > "$out3/victim.md"
chmod 600 "$out3/victim.md"
cp "$out3/victim.md" "$out3/victim.md.pristine"
ln -s "$out3/victim.md" "$ws3/AGENTS.md"
snap3_before=$(outside_snapshot "$out3")
(cd "$ws3" && unset JC_CONFIG && with_deadline 30 "$BIN" init default --force </dev/null) > "$tmp/out3" 2>&1
rc3=$?
snap3_after=$(outside_snapshot "$out3")
# the pristine backup is inside out3; compare the victim to it, then the whole
# tree (minus the backup) before/after
if cmp -s "$out3/victim.md" "$out3/victim.md.pristine" \
   && [ "$(ls -l "$out3/victim.md" 2>/dev/null | cut -c1-10)" = "-rw-------" ] \
   && [ "$snap3_before" = "$snap3_after" ]; then
    t_ok "leg3 --force onto link leaf: outside victim byte- and mode-unchanged (rc=$rc3)"
else
    t_fail "leg3 --force onto link leaf: victim changed (before=[$snap3_before] after=[$snap3_after])"
fi

# ---------------------------------------------------------------- LEG 4
# CONTROL: --force onto a REGULAR existing file must still overwrite (the guard
# must not block legitimate force-overwrite of a real file).
ws4=$(smoke_tmp)
printf 'OLD_M1105_regular\n' > "$ws4/AGENTS.md"
(cd "$ws4" && unset JC_CONFIG && with_deadline 30 "$BIN" init default --force </dev/null) > "$tmp/out4" 2>&1
rc4=$?
if grep -q 'OLD_M1105_regular' "$ws4/AGENTS.md"; then
    t_fail "leg4 --force on regular file: NOT overwritten (guard over-refuses, rc=$rc4)"
elif ! grep -qiE 'project rules|agents' "$ws4/AGENTS.md"; then
    t_fail "leg4 --force on regular file: overwritten with wrong content (rc=$rc4): $(head_bytes 80 "$ws4/AGENTS.md")"
else
    t_ok "leg4 --force on regular file: overwritten (no over-refusal, rc=$rc4)"
fi

# ---------------------------------------------------------------- LEG 5
# setup --import with a SYMLINKED local/config.json. Importing a minimal
# continue config writes only the config (no assets), so the leaf link at
# local/config.json -> outside/cfg.json is the planted redirection.
ws5=$(smoke_tmp); out5=$(smoke_tmp)
cat > "$out5/continue.json" <<'EOF'
{
  "models": [
    { "model": "gpt-4o", "provider": "openai", "title": "Chat" }
  ],
  "lowResource": false
}
EOF
mkdir -p "$ws5/local"
ln -s "$out5/cfg.json" "$ws5/local/config.json"
snap5_before=$(outside_snapshot "$out5")
(cd "$ws5" && unset JC_CONFIG && with_deadline 30 "$BIN" setup --import "$out5/continue.json" </dev/null) > "$tmp/out5" 2>&1
rc5=$?
snap5_after=$(outside_snapshot "$out5")
if [ "$snap5_before" = "$snap5_after" ] && [ ! -e "$out5/cfg.json" ] \
   && grep -q 'refused' "$tmp/out5"; then
    t_ok "leg5 setup --import symlinked config: nothing outside + refusal named (rc=$rc5)"
else
    t_fail "leg5 setup --import symlinked config: outside changed or no refusal (rc=$rc5, before=[$snap5_before] after=[$snap5_after]): $(head_bytes 200 "$tmp/out5")"
fi

# ---------------------------------------------------------------- LEG 6
# M1105-c: setup --import --config-target global must WRITE $HOME/.jichi.
# smoke_home (per driver) pointed $HOME at a scratch dir, so $HOME/.jichi is
# isolated; the pre-fix tree refuses it as "outside the project" (over-refusal).
ws6=$(smoke_tmp); out6=$(smoke_tmp)
cat > "$out6/continue.json" <<'EOF'
{
  "models": [
    { "model": "gpt-4o", "provider": "openai", "title": "Chat" }
  ],
  "lowResource": false
}
EOF
(cd "$ws6" && unset JC_CONFIG && with_deadline 30 "$BIN" setup --import "$out6/continue.json" --config-target global </dev/null) > "$tmp/out6a" 2>&1
rc6a=$?
if [ "$rc6a" -eq 0 ] && [ -f "$HOME/.jichi" ] \
   && ! grep -q 'refused' "$tmp/out6a" \
   && "$JQ" -q '.' "$HOME/.jichi"; then
    t_ok "leg6 global import writes \$HOME/.jichi as valid JSON (rc=$rc6a)"
else
    t_fail "leg6 global import: refused, missing, or invalid JSON (rc=$rc6a, exists=$([ -f "$HOME/.jichi" ] && echo yes || echo no)): $(head_bytes 200 "$tmp/out6a")"
fi

# ---------------------------------------------------------------- LEG 7
# M1105-c control: a PLANTED dangling symlink at $HOME/.jichi must STILL be
# refused (the guard's leaf lstat runs even with the parent fence lifted), and
# nothing may be created outside.
ws7=$(smoke_tmp); out7=$(smoke_tmp)
rm -f "$HOME/.jichi"   # leg 6's success left a real file; the link cannot
                       # sit on top of a non-empty file
ln -s "$out7/cfg.json" "$HOME/.jichi"
(cd "$ws7" && unset JC_CONFIG && with_deadline 30 "$BIN" setup --import "$out6/continue.json" --config-target global </dev/null) > "$tmp/out7" 2>&1
rc7=$?
rm -f "$HOME/.jichi"   # restore a sane $HOME before any later driver
if [ "$rc7" -ne 0 ] && [ ! -e "$out7/cfg.json" ] && grep -q 'refused' "$tmp/out7"; then
    t_ok "leg7 global import via planted symlink refused, nothing outside (rc=$rc7)"
else
    t_fail "leg7 global import via planted symlink: escaped or not refused (rc=$rc7, outside=$([ -e "$out7/cfg.json" ] && echo made || echo none)): $(head_bytes 200 "$tmp/out7")"
fi

t_done
