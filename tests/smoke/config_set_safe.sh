#!/bin/sh
# M1107 (D-25): `config set`'s write is SAFE. The old sequence renamed the live
# config to .bak FIRST, wrote a temp at a FIXED name (<path>.tmp), then renamed
# it over the config. Three measured shapes broke (LOG-3 Run 0111):
#   expA: a 0600 config came back 0644 (the rename carried the mode to the
#         backup, not the successor, which took the umask);
#   expB: a DIRECTORY planted at <path>.tmp made the write fail with the
#         config ALREADY MOVED AWAY -- `config show` then said "No model is
#         configured";
#   expC: a symlinked config was FOLLOWED and replaced by a regular file, the
#         link's target edited through the rename-over-link.
# The fix (jc_config_edit_save) reorders to: refuse a symlink -> back up by
# COPY (the original is never moved, so a failed write leaves it in place) ->
# temp-first mode-preserving write (unique <path>.tmp<pid> suffix, O_EXCL,
# fchmod to the original mode before the rename).
#
# Model-free and offline: `config set` is a local pre-flight subcommand, driven
# with --config-editable (which passes the edit gate on its own). The FAULT half
# (a failed save leaves the original byte-identical -- the expB/D-25 "config
# lost" shape) lives in fault_config_loss.sh, gated on a FAULT=1 binary.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Portable permission string (stat -c is banned by posix_utils_lint): ls -l's
# field 1 is "<type><perms>" (e.g. -rw-------); strip the type char. 0600 =
# rw------- (the relay_baton_written idiom).
mode_of() { ls -l "$1" 2>/dev/null | awk '{ print substr($1, 2) }'; }
# Portable "is this a symlink" (ls -l's leading char is 'l' for a link).
is_link() { ls -l "$1" 2>/dev/null | cut -c1-1 | "$G" -q '^l'; }

cfgset() {   # cfgset <wsdir> <key> <value> <outfile>  (no --config: local/config.json)
    ( cd "$1" && with_deadline 30 "$BIN" --config-editable \
        config set "$2" "$3" < /dev/null > "$4" 2>&1 )
}

seed() {   # seed <wsdir> : a 0600 config with a model + configEditable:true
    _s="$1"
    mkdir -p "$_s/local"
    printf '{"model":"jlu/strong-model","configEditable":true,"snapshots":true}\n' \
        > "$_s/local/config.json"
    chmod 600 "$_s/local/config.json"
}

# ---- 1. a 0600 config keeps its mode, and .bak is a byte-identical COPY ----
ws1=$(smoke_tmp)
seed "$ws1"
cp "$ws1/local/config.json" "$tmp/c1.before"
cfgset "$ws1" snapshots false "$tmp/o1"
rc=$?
m1=$(mode_of "$ws1/local/config.json")
b1=$(mode_of "$ws1/local/config.json.bak")
if [ "$rc" = 0 ] && [ "$m1" = "rw-------" ] && [ "$b1" = "rw-------" ] \
    && cmp -s "$ws1/local/config.json.bak" "$tmp/c1.before"; then
    t_ok "a 0600 config keeps its mode and its .bak is a byte-identical copy"
else
    t_fail "mode kept: rc=$rc mode=$m1 bak_mode=$b1 bak_identical=$([ -f "$ws1/local/config.json.bak" ] && cmp -s "$ws1/local/config.json.bak" "$tmp/c1.before" && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o1")"
fi

# ---- 2. a normal edit still lands the set key ----
if [ -f "$ws1/local/config.json" ] \
    && "$G" -q '"snapshots": *false' "$ws1/local/config.json"; then
    t_ok "the edited key is present in the saved config"
else
    t_fail "normal edit: $(head_bytes 160 "$ws1/local/config.json" 2>/dev/null || echo missing); out=$(head_bytes 160 "$tmp/o1")"
fi

# ---- 3. a symlinked config is refused: name + target untouched ----
ws2=$(smoke_tmp)
mkdir -p "$ws2/local" "$ws2/real"
printf '{"model":"jlu/strong-model","configEditable":true}\n' > "$ws2/real/config.json"
chmod 600 "$ws2/real/config.json"
ln -s ../real/config.json "$ws2/local/config.json"
cp "$ws2/real/config.json" "$tmp/c3.real.before"
cfgset "$ws2" logging full "$tmp/o3"
rc=$?
if [ "$rc" != 0 ] && "$G" -qi symlink "$tmp/o3" \
    && is_link "$ws2/local/config.json" \
    && cmp -s "$ws2/real/config.json" "$tmp/c3.real.before"; then
    t_ok "a symlinked config is refused; the link and its target are untouched"
else
    t_fail "symlink refused: rc=$rc still_link=$(is_link "$ws2/local/config.json" && echo yes || echo NO) target_unchanged=$(cmp -s "$ws2/real/config.json" "$tmp/c3.real.before" && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o3")"
fi

# ---- 4. a DIRECTORY at the old fixed temp name no longer breaks the save ----
ws3=$(smoke_tmp)
seed "$ws3"
mkdir "$ws3/local/config.json.tmp"   # planted where the old writer's temp lived
cp "$ws3/local/config.json" "$tmp/c4.before"
cfgset "$ws3" snapshots true "$tmp/o4"
rc=$?
m4=$(mode_of "$ws3/local/config.json")
b4=$(mode_of "$ws3/local/config.json.bak")
if [ "$rc" = 0 ] && [ -d "$ws3/local/config.json.tmp" ] \
    && [ "$m4" = "rw-------" ] && [ "$b4" = "rw-------" ] \
    && cmp -s "$ws3/local/config.json.bak" "$tmp/c4.before"; then
    t_ok "a planted directory at the old fixed temp leaves the save working"
else
    t_fail "planted temp: rc=$rc tmp_is_dir=$([ -d "$ws3/local/config.json.tmp" ] && echo yes || echo NO) mode=$m4 bak_mode=$b4 bak_identical=$([ -f "$ws3/local/config.json.bak" ] && cmp -s "$ws3/local/config.json.bak" "$tmp/c4.before" && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o4")"
fi

# ---- 5. the writer leaves no stale <config>.tmp<pid> behind ----
# In ws1 the save succeeded (leg 1); the only names it may leave are config.json
# and config.json.bak -- never a <config>.tmp<digits> the atomic writer forgot.
ls1=$( (cd "$ws1/local" && ls -A) | sort )
if [ "$(printf '%s\n' "$ls1" | tr '\n' ' ')" = "config.json config.json.bak " ]; then
    t_ok "no stale <config>.tmp<pid> temp is left behind (only config.json + .bak)"
else
    t_fail "unexpected names in local/: $(printf '%s\n' "$ls1")"
fi

# ---- 6. a SYMLINK planted at <path>.bak is replaced, not followed (SV:1725 #1) ----
# M1107's copy backup (jc_write_file = fopen "wb") FOLLOWS the name, so a symlinked
# .bak clobbered the canary it pointed at (and chmod'd it 0600). The .bak is now
# written by jc_write_file_atomic_mode: a unique <path>.bak.tmp<pid> is fchmod'd
# then renamed over the name -- the symlink is REPLACED, never opened.
ws6=$(smoke_tmp)
seed "$ws6"
cp "$ws6/local/config.json" "$tmp/c6.before"
printf 'CANARY-SYM\n' > "$tmp/c6.canary"
printf 'CANARY-SYM\n' > "$tmp/symcanary"
ln -s "$tmp/symcanary" "$ws6/local/config.json.bak"
cfgset "$ws6" snapshots false "$tmp/o6"
rc=$?
if [ "$rc" = 0 ] \
    && cmp -s "$tmp/symcanary" "$tmp/c6.canary" \
    && [ ! -L "$ws6/local/config.json.bak" ] \
    && [ -f "$ws6/local/config.json.bak" ] \
    && cmp -s "$ws6/local/config.json.bak" "$tmp/c6.before" \
    && "$G" -q '"snapshots": *false' "$ws6/local/config.json"; then
    t_ok "a symlink planted at <path>.bak is replaced, not followed (canary untouched, edit lands)"
else
    t_fail "sym planted .bak: rc=$rc canary_untouched=$(cmp -s "$tmp/symcanary" "$tmp/c6.canary" && echo yes || echo CLOBBERED) bak_is_link=$([ -L "$ws6/local/config.json.bak" ] && echo yes || echo no) bak_is_copy=$([ -f "$ws6/local/config.json.bak" ] && cmp -s "$ws6/local/config.json.bak" "$tmp/c6.before" && echo yes || echo NO) edit_landed=$("$G" -q '"snapshots": *false' "$ws6/local/config.json" 2>/dev/null && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o6")"
fi

# ---- 7. a HARDLINK planted at <path>.bak is replaced, not written through ----
# Two names share one inode; the unfixed fopen "wb" truncates the SHARED inode
# (so outside_hard loses CANARY-HARD). The atomic writer renames a fresh inode
# over the .bak NAME, leaving the other name on the untouched old inode.
ws7=$(smoke_tmp)
seed "$ws7"
cp "$ws7/local/config.json" "$tmp/c7.before"
printf 'CANARY-HARD\n' > "$tmp/c7.canary"
printf 'CANARY-HARD\n' > "$ws7/outside_hard"
ln "$ws7/outside_hard" "$ws7/local/config.json.bak"
cfgset "$ws7" snapshots false "$tmp/o7"
rc=$?
if [ "$rc" = 0 ] \
    && cmp -s "$ws7/outside_hard" "$tmp/c7.canary" \
    && [ -f "$ws7/local/config.json.bak" ] \
    && [ ! -L "$ws7/local/config.json.bak" ] \
    && "$G" -q '"snapshots": *false' "$ws7/local/config.json"; then
    t_ok "a hardlink planted at <path>.bak is replaced, not written through (other name untouched, edit lands)"
else
    t_fail "hard planted .bak: rc=$rc other_name_untouched=$(cmp -s "$ws7/outside_hard" "$tmp/c7.canary" && echo yes || echo CLOBBERED) bak_is_file=$([ -f "$ws7/local/config.json.bak" ] && echo yes || echo NO) edit_landed=$("$G" -q '"snapshots": *false' "$ws7/local/config.json" 2>/dev/null && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o7")"
fi

# ---- 8. a FIFO planted at <path>.bak does not HANG the save (SV:1725 #1) ----
# fopen "wb" on a FIFO blocks in open() until a reader arrives -- a NEW hang the
# unfixed M1107 copy backup introduced. with_deadline 30 makes a hang a red
# (rc >= 124). The atomic writer's rename replaces the FIFO name without opening
# it, so the save returns rc=0.
ws8=$(smoke_tmp)
seed "$ws8"
cp "$ws8/local/config.json" "$tmp/c8.before"
mkfifo "$ws8/local/config.json.bak"
cfgset "$ws8" snapshots false "$tmp/o8"
rc=$?
if [ "$rc" = 0 ] \
    && [ -f "$ws8/local/config.json.bak" ] \
    && [ ! -L "$ws8/local/config.json.bak" ] \
    && "$G" -q '"snapshots": *false' "$ws8/local/config.json"; then
    t_ok "a FIFO planted at <path>.bak does not hang the save (rc=0, .bak is a regular file, edit lands)"
else
    t_fail "fifo planted .bak: rc=$rc (>=124 means the save BLOCKED in open) bak_is_file=$([ -f "$ws8/local/config.json.bak" ] && echo yes || echo NO) edit_landed=$("$G" -q '"snapshots": *false' "$ws8/local/config.json" 2>/dev/null && echo yes || echo NO) out=$(head_bytes 160 "$tmp/o8")"
fi

t_done
