#!/bin/sh
# smoke: setup with JSONC config - preserve comments in backup and refuse to replace
# unparsable config without --force (D-22/M980)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# (1) JSONC with comments merges and creates backup
ws=$(smoke_tmp)
mkdir -p "$ws/local"
# The fixture is the CONFIG INPUT DATA for `setup` (a non-model operation), but
# it is a complete config and pins lowResource like every other inline driver
# config (smoke_lint check 7 is a file-level grep and cannot tell fixture from
# live config). The trailing comma is deliberate: it is JSONC, which is the
# whole point of the test.
cat > "$ws/local/config.json" << 'EOFCONFIG'
{
  // this is a comment
  "pathFence": true,
  "lowResource": false,
  "apiKeyEnv": "TEST_API_KEY",
  /* model settings */
  "models": [
    {
      "provider": "openai",
      "model": "gpt-4o",
      "roles": ["chat"]
    }
  ],
}
EOFCONFIG

(cd "$ws" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out1" 2>&1); rc1=$?
# Check config is valid JSON and pathFence is true
if [ $rc1 -eq 0 ] && "$JQ" -q '.pathFence' "$ws/local/config.json" 2>/dev/null && \
   [ "$("$JQ" '.pathFence' "$ws/local/config.json" 2>/dev/null)" = "true" ] && \
   [ -f "$ws/local/config.json.bak" ]; then
    # Check .bak contains original with pathFence (the raw text has comments too)
    if grep -q 'pathFence' "$ws/local/config.json.bak"; then
        t_ok "JSONC config: merge OK, backup exists with original comments"
    else
        t_fail "JSONC config: backup missing pathFence reference"
    fi
else
    t_fail "JSONC config: merge rc=$rc1 or backup missing/invalid or pathFence check failed"
fi

# (2) Unparsable config without --force: refuse to replace
ws2=$(smoke_tmp)
mkdir -p "$ws2/local"
echo '{ oops' > "$ws2/local/config.json"
orig_content=$(cat "$ws2/local/config.json")

(cd "$ws2" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out2" 2>&1); rc2=$?
new_content=$(cat "$ws2/local/config.json")
if [ $rc2 -ne 0 ] && [ "$orig_content" = "$new_content" ] && \
   grep -qi 'could not be parsed' "$tmp/out2"; then
    t_ok "Unparsable config: refused without --force, file unchanged, error message"
else
    t_fail "Unparsable config: rc=$rc2, content changed=$([ "$orig_content" != "$new_content" ] && echo yes || echo no), missing error: $(grep -c 'could not be parsed' "$tmp/out2" 2>/dev/null || echo 0)"
fi

# (3) With --force: replace even broken config, backup holds original broken bytes
ws3=$(smoke_tmp)
mkdir -p "$ws3/local"
echo '{ broken' > "$ws3/local/config.json"
orig_content3=$(cat "$ws3/local/config.json")

(cd "$ws3" && with_deadline 30 "$BIN" setup --force --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out3" 2>&1); rc3=$?
if [ $rc3 -eq 0 ] && [ -f "$ws3/local/config.json.bak" ] && \
   [ "$orig_content3" = "$(cat "$ws3/local/config.json.bak" 2>/dev/null)" ]; then
    t_ok "With --force: successful, backup holds original broken bytes"
else
    t_fail "With --force: rc=$rc3, .bak exists=$([ -f "$ws3/local/config.json.bak" ] && echo yes || echo no), bak matches=$([ "$orig_content3" = "$(cat "$ws3/local/config.json.bak" 2>/dev/null)" ] && echo yes || echo no)"
fi

# (4) --from-global seeds the project config from the GLOBAL config, which is
# the FILE $HOME/.jichi (the W5 convention; jc_config.c:1201 reads it the same
# way). The success path reads it into the scratch arena (jc_read_file) and
# merges -- the review of bb393e79 flagged free(raw) on that arena-allocated
# buffer (an invalid pointer, heap damage at teardown). Teeth: on the unfixed
# code this arm dies mid-exit (rc != 0); on the fixed code it exits 0 and the
# merged project config carries the inherited keys.
ws4=$(smoke_tmp)
mkdir -p "$ws4"
cat > "$HOME/.jichi" <<'EOFGLOBAL'
{
  "provider": "openai",
  "models": [
    { "name": "g", "provider": "openai", "model": "gpt-4o", "roles": ["chat"] }
  ],
  "pathFence": true
}
EOFGLOBAL
(cd "$ws4" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local --from-global < /dev/null > "$tmp/out4" 2>&1); rc4=$?
if [ $rc4 -eq 0 ] && [ -f "$ws4/local/config.json" ] && \
   "$JQ" -q '.pathFence' "$ws4/local/config.json" >/dev/null 2>&1; then
    t_ok "--from-global success path: clean exit, inherited keys in the config"
else
    t_fail "--from-global: rc=$rc4 (the unfixed free(raw) dies here)"
fi
# (5) NUL-byte config: the .bak must hold the original BYTES, NUL included
# (supervisor 05:20: strlen(raw) truncated at the first NUL; jc_read_file's
# &len out-param carries the true byte count). Red with main.c from 43f22501.
ws5=$(smoke_tmp)
mkdir -p "$ws5/local"
{ printf '%s\0%s' '{ "models": [' ']' ; printf '
' ; } > "$ws5/local/config.json"
cp "$ws5/local/config.json" "$tmp/nul5.orig"
(cd "$ws5" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local --force < /dev/null > "$tmp/out5" 2>&1); rc5=$?
if [ $rc5 -eq 0 ] && cmp -s "$tmp/nul5.orig" "$ws5/local/config.json.bak"; then
    t_ok "NUL-byte config: the backup is byte-identical to the original"
else
    t_fail "NUL-byte config: rc=$rc5, bak byte-identical=$(cmp -s "$tmp/nul5.orig" "$ws5/local/config.json.bak" && echo yes || echo no)"
fi

t_done
