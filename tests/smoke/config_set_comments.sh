#!/bin/sh
# M924 (review R07-F1, fix-order item 3): `config set` must not wipe a config
# the reader accepts. The writer's loader strict-JSON-parsed the target while
# the reader strips // and block comments and trailing commas (M459) -- so a
# commented config failed to parse for the writer, was mapped to an EMPTY
# object, and the save overwrote the file with just the one key being set.
# The reviewer measured the consequence: a trusted project config lost its
# model, pathFence, permissions.deny and configEditable, and STAYED TRUSTED in
# its wiped form. The fix: the writer runs the same strip the reader does, and
# a non-empty file that is not a parseable JSON object is REFUSED (its bytes
# are left untouched) -- and a commented file's edit says so.
# Model-free: `config set` is a pre-flight subcommand, no provider needed.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

cfgset() {
    ( cd "$ws" && with_deadline 30 "$BIN" --config "$1" --config-editable \
        config set "$2" "$3" < /dev/null > "$tmp/out.txt" 2>&1 )
}

# 1. A commented config's VALUES survive an edit (the R07-F1 wipe).
c1="$tmp/c1.json"
cat > "$c1" <<'CFG'
{
  // the operator's note
  "model": "jlu/strong-model",
  "pathFence": 1,
  "permissions": { "deny": ["run_terminal_command"], },
  "logging": "off"
}
CFG
cfgset "$c1" logging full; rc=$?
[ "$rc" = 0 ] && grep -q '"model": *"jlu/strong-model"' "$c1" \
    && grep -q '"pathFence": *1' "$c1" \
    && grep -q '"deny"' "$c1" \
    && t_ok "a commented config's values survive the config-set edit (no wipe)" \
    || t_fail "commented config edit: rc=$rc; model kept=$(grep -c jlu/strong-model "$c1") pathFence kept=$(grep -c pathFence "$c1") deny kept=$(grep -c deny "$c1")"

# 2. the edit of a commented file says it re-serialised the file.
grep -q "re-serialised without" "$tmp/out.txt" \
    && t_ok "editing a commented file notes the re-serialisation (original in .bak)" \
    || t_fail "no re-serialisation note: $(head_bytes 120 "$tmp/out.txt")"

# 3. a truncated (unparseable) config is REFUSED and left byte-unchanged.
c3="$tmp/c3.json"
printf '{"model": "jlu/strong", "pathFence": 1' > "$c3"
cp "$c3" "$tmp/c3.before"
cfgset "$c3" snapshots false; rc=$?
[ "$rc" = 1 ] && cmp -s "$c3" "$tmp/c3.before" \
    && t_ok "an unparseable config is refused and byte-unchanged" \
    || t_fail "unparseable config: rc=$rc changed=$(cmp -s "$c3" "$tmp/c3.before" && echo no || echo YES)"

# 4. an array-root config is refused the same way.
c4="$tmp/c4.json"
printf '[1, 2, 3]' > "$c4"
cp "$c4" "$tmp/c4.before"
cfgset "$c4" snapshots false; rc=$?
[ "$rc" = 1 ] && cmp -s "$c4" "$tmp/c4.before" \
    && t_ok "an array-root config is refused and byte-unchanged" \
    || t_fail "array-root config: rc=$rc changed=$(cmp -s "$c4" "$tmp/c4.before" && echo no || echo YES)"

# 5. a plain (no-comment) config edits cleanly, without the re-serialise note.
c5="$tmp/c5.json"
printf '{"model":"jlu/fast-model","logging":"off"}\n' > "$c5"
cfgset "$c5" logging full; rc=$?
[ "$rc" = 0 ] && ! grep -q "re-serialised" "$tmp/out.txt" \
    && grep -q '"logging"' "$c5" \
    && t_ok "a plain config edits without the re-serialisation note" \
    || t_fail "plain config: rc=$rc; out=$(head_bytes 120 "$tmp/out.txt")"

# 6. the DEFAULT target (local/config.json, no --config) starts from scratch
#    when absent -- the one case the load's empty-object fallback is FOR.
( cd "$ws" && with_deadline 30 "$BIN" --config-editable \
    config set snapshots true < /dev/null > "$tmp/out6.txt" 2>&1 )
rc=$?
[ "$rc" = 0 ] && [ -f "$ws/local/config.json" ] && grep -q '"snapshots"' "$ws/local/config.json" \
    && t_ok "an absent default config starts from scratch" \
    || t_fail "absent default config: rc=$rc; created=$([ -f "$ws/local/config.json" ] && echo yes || echo no); out=$(head_bytes 120 "$tmp/out6.txt")"

t_done
