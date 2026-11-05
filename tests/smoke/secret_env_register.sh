#!/bin/sh
# smoke: the secretEnv register (M1043-a, plan 2026-10-03 4.2, card 79, def-329's
# 4.2 half). A configured "secretEnv" name is removed from EVERY child jichi
# forks, four built-in CI/forge names are dropped even when unconfigured, and
# `jichi doctor` warns for a credential-looking env var in NEITHER list.
#
# RED-FIRST. On the tree before the fix, legs 1, 2 and 4 are the red half: the
# model's shell still sees a registered secretEnv name (leg 1, count 1) and a
# built-in CI name (leg 2, count 1), and doctor emits no 'environment secret
# not scrubbed' line at all (leg 4). The fix makes leg 1 and leg 2 count 0 and
# turns on the doctor warning leg 4 asserts. Legs 3 and 5 are green both sides
# (a control, and the doctor warning's absent shape for a non-credential name).
# The shell child greps `env | grep -c -e '^NAME='` into files the driver reads;
# the jichi call always takes < /dev/null.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# --- 1 (RED on unfixed): a CONFIGURED secretEnv name is scrubbed -------------
# The model runs one shell command that counts two vars: MY_REG_TOKEN (named in
# secretEnv) and CI_JOB_TOKEN (a built-in). Before the fix MY_REG_TOKEN is not
# registered (the config key did not exist), so the child sees it (count 1);
# after, it is (count 0).
cat > "$tmp/m1.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '^MY_REG_TOKEN=' > '$tmp/n1a'; env | grep -c -e '^CI_JOB_TOKEN=' > '$tmp/n1b'; exit 0"}
rule
  text REG_DONE
EOF
mm_start_unbounded "$tmp/m1.mm" "$tmp/cap1"
write_config "$tmp/c1.json" "$MM_PORT" '"secretEnv":["MY_REG_TOKEN"]' \
    '"apiKeyEnv":"MOCK_KEY"'
(cd "$ws" && with_deadline 90 env MY_REG_TOKEN=canary CI_JOB_TOKEN=canary MOCK_KEY=x \
    "$BIN" --config "$tmp/c1.json" -q --no-session --auto \
    -p "check the env" < /dev/null > "$tmp/out1" 2>&1); rc=$?
mm_stop
n1a=$(cat "$tmp/n1a" 2>/dev/null || echo unread)
if [ "$rc" = "0" ] && [ "$n1a" = "0" ]; then
    t_ok "the configured secretEnv name MY_REG_TOKEN is scrubbed from the shell tool's child"
else
    t_fail "secretEnv name not scrubbed (count=$n1a, rc=$rc): $(head_bytes 160 "$tmp/out1")"
fi

# --- 2 (RED on unfixed): a BUILT-IN CI/forge name is dropped, unconfigured ----
# GITHUB_TOKEN is set but NOT in secretEnv; only the built-in list can drop it.
# (Leg 1's CI_JOB_TOKEN count is the same assertion from the other side -- also
# built-in -- so it must be 0 too.)
n1b=$(cat "$tmp/n1b" 2>/dev/null || echo unread)
cat > "$tmp/m2.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '^GITHUB_TOKEN=' > '$tmp/n2a'; exit 0"}
rule
  text BUILTIN_DONE
EOF
mm_start_unbounded "$tmp/m2.mm" "$tmp/cap2"
write_config "$tmp/c2.json" "$MM_PORT" '' '"apiKeyEnv":"MOCK_KEY"'
(cd "$ws" && with_deadline 90 env GITHUB_TOKEN=canary MOCK_KEY=x \
    "$BIN" --config "$tmp/c2.json" -q --no-session --auto \
    -p "check the env" < /dev/null > "$tmp/out2" 2>&1); rc=$?
mm_stop
n2a=$(cat "$tmp/n2a" 2>/dev/null || echo unread)
if [ "$rc" = "0" ] && [ "$n2a" = "0" ] && [ "$n1b" = "0" ]; then
    t_ok "the built-in CI/forge names GITHUB_TOKEN and CI_JOB_TOKEN are dropped even when unconfigured"
else
    t_fail "a built-in CI/forge name reached the child (GITHUB_TOKEN=$n2a, CI_JOB_TOKEN=$n1b, rc=$rc): $(head_bytes 160 "$tmp/out2")"
fi

# --- 3 (control, green both sides): the configured apiKeyEnv scrub still works -
cat > "$tmp/m3.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '^MOCK_KEY=' > '$tmp/n3'; exit 0"}
rule
  text CTRL_DONE
EOF
mm_start_unbounded "$tmp/m3.mm" "$tmp/cap3"
write_config "$tmp/c3.json" "$MM_PORT" '' '"apiKeyEnv":"MOCK_KEY"'
(cd "$ws" && with_deadline 90 env MOCK_KEY=x \
    "$BIN" --config "$tmp/c3.json" -q --no-session --auto \
    -p "check the env" < /dev/null > "$tmp/out3" 2>&1); rc=$?
mm_stop
n3=$(cat "$tmp/n3" 2>/dev/null || echo unread)
if [ "$rc" = "0" ] && [ "$n3" = "0" ]; then
    t_ok "control: the configured apiKeyEnv (MOCK_KEY) is still scrubbed from the child"
else
    t_fail "control failed: the configured MOCK_KEY reached the child (count=$n3, rc=$rc)"
fi

# --- 4 (green-shape on the fix): doctor WARNS for a secret-looking name in no list
# SOME_TOKEN is set, ends in _TOKEN, and is in neither the built-in list nor the
# configured secretEnv. On the unfixed tree there is no such doctor line (the
# warning does not exist yet), so this leg's assertion fails there; with the fix
# doctor names SOME_TOKEN. The model server is unreachable (offline doctor is
# fine -- the env scan does not call a model).
write_config "$tmp/c4.json" 9 '' '"apiKeyEnv":"MOCK_KEY"'
(cd "$ws" && with_deadline 40 env SOME_TOKEN=canary MOCK_KEY=x \
    "$BIN" --config "$tmp/c4.json" doctor < /dev/null > "$tmp/doc4" 2>&1)
if grep -q "environment secret not scrubbed: SOME_TOKEN" "$tmp/doc4"; then
    t_ok "doctor warns for the secret-looking SOME_TOKEN, which is in neither list"
else
    t_fail "doctor did not warn for SOME_TOKEN (no 'environment secret not scrubbed: SOME_TOKEN'):"
    head_bytes 400 "$tmp/doc4"
fi

# --- 5 (green both sides): doctor does NOT warn for a name without the suffixes
# HOME is set but does not end in _TOKEN/_KEY/_SECRET/_PASSWORD, so the scan must
# skip it. There must be no 'environment secret not scrubbed' line naming HOME.
(cd "$ws" && with_deadline 40 env MOCK_KEY=x \
    "$BIN" --config "$tmp/c4.json" doctor < /dev/null > "$tmp/doc5" 2>&1)
if grep -q "environment secret not scrubbed: HOME" "$tmp/doc5"; then
    t_fail "doctor wrongly warned for HOME, a name without the credential suffixes"
else
    t_ok "doctor does not warn for HOME (no credential suffix): the scan is suffix-gated"
fi

# --- 6 (pin for the fix): doctor warns for the unregistered name, NOT the registered one
# Set BOTH a name that IS in secretEnv (REGISTERED_TOKEN) and one that is not
# (SOME_TOKEN); both end in _TOKEN, so both are suffix-gated the same way. The
# scan must warn for SOME_TOKEN and suppress the line for REGISTERED_TOKEN --
# that is jc_proc.c:211's is_secret skip, pinned end to end in BOTH directions
# in one leg. On the unfixed tree this leg is red (no warning exists at all);
# it is a pin for the fix, not the red-first (legs 1/2/4 already recorded).
write_config "$tmp/c6.json" 9 '"secretEnv":["REGISTERED_TOKEN"]' '"apiKeyEnv":"MOCK_KEY"'
(cd "$ws" && with_deadline 40 env SOME_TOKEN=canary REGISTERED_TOKEN=canary MOCK_KEY=x \
    "$BIN" --config "$tmp/c6.json" doctor < /dev/null > "$tmp/doc6" 2>&1)
if grep -q "environment secret not scrubbed: SOME_TOKEN" "$tmp/doc6" && \
   ! grep -q "environment secret not scrubbed: REGISTERED_TOKEN" "$tmp/doc6"; then
    t_ok "doctor warns for the unregistered SOME_TOKEN and suppresses the registered REGISTERED_TOKEN"
else
    t_fail "the registered-name suppression is not end to end (doc6:)"
    head_bytes 400 "$tmp/doc6"
fi
t_done
