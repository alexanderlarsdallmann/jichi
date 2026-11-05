#!/bin/sh
# M923 (review R02-F1/F3/F4): a numeric CLI argument that atoi/atol once read
# as 0 (or a truncated value) with no message must now be a USAGE ERROR (exit 2),
# not a silently-removed fence or a silent mass delete. The wounds, measured by
# the 2026-10-08 review and reproduced by this driver:
#   --max-tool-calls abc  -> 0 = no cap at all (the fence vanished)
#   --max-tool-calls 1k   -> 1 (a unit suffix read as its digits)
#   --context-limit 32k   -> 32 (a 32k-token window read as 32)
#   prune --keep abc      -> 0 = keep nothing = delete EVERY session
#   undo / rewind abc     -> 0 -> clamped to 1, a workspace/history change
# A M923 incident is what this whole milestone is: an unfixed `prune --keep`
# probe deleted a real 605-session store, so the prune probe here runs under
# smoke_home's sandboxed HOME and asserts the seeded session file SURVIVES.
# All probes are model-free (brief-check is a pre-flight that spends no tokens).
# Set NUM_PARSE_RES to keep the artifacts.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
RES="${NUM_PARSE_RES:-$tmp}"

# brief-check prints "  max tool calls : N" and no model is configured, so it
# reaches the parse without a provider. A bad number must exit 2 BEFORE it gets
# there; a good number must be echoed back exactly.
bc() {
    ( cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/c.json" brief-check \
        /dev/null --max-tool-calls "$1" \
        < /dev/null > "$tmp/bc.out" 2>&1 )
}
printf '{}' > "$tmp/c.json"

# 1. abc -> usage error, no "max tool calls" line at all.
bc abc; rc=$?
[ "$rc" = 2 ] && ! grep -q "max tool calls" "$tmp/bc.out" \
    && t_ok "--max-tool-calls abc is a usage error (rc 2, no fence line)" \
    || t_fail "--max-tool-calls abc: rc=$rc, out=$(head_bytes 80 "$tmp/bc.out")"

# 2. 1k -> usage error (atoi read it as 1, a fence of one call).
bc 1k; rc=$?
[ "$rc" = 2 ] && ! grep -q "max tool calls" "$tmp/bc.out" \
    && t_ok "--max-tool-calls 1k is a usage error (no silent cap of 1)" \
    || t_fail "--max-tool-calls 1k: rc=$rc, out=$(head_bytes 80 "$tmp/bc.out")"

# 3. 3 -> echoed back exactly.
bc 3; rc=$?
[ "$rc" = 0 ] && grep -q "max tool calls : 3" "$tmp/bc.out" \
    && t_ok "--max-tool-calls 3 parses to 3" \
    || t_fail "--max-tool-calls 3: rc=$rc, out=$(head_bytes 80 "$tmp/bc.out")"

# 4. 0 -> a valid explicit value (no cap), echoed back, not an error.
bc 0; rc=$?
[ "$rc" = 0 ] && grep -q "max tool calls : 0" "$tmp/bc.out" \
    && t_ok "--max-tool-calls 0 stays a valid explicit value" \
    || t_fail "--max-tool-calls 0: rc=$rc, out=$(head_bytes 80 "$tmp/bc.out")"

# 5. a number that would overflow -> usage error, not a wrapped value.
bc 99999999999999999999; rc=$?
[ "$rc" = 2 ] && ! grep -q "max tool calls" "$tmp/bc.out" \
    && t_ok "an overflowing --max-tool-calls is a usage error (no wrap)" \
    || t_fail "overflow --max-tool-calls: rc=$rc, out=$(head_bytes 80 "$tmp/bc.out")"

# 6. THE incident, contained: prune --keep abc under a sandboxed HOME must exit
#    2 and leave a seeded session file in place (keep=0 would have deleted it).
sandbox=$(smoke_tmp)
mkdir -p "$sandbox/.jichi.d/sessions"
printf '{"messages":[{"role":"user","content":"x"}]}' \
    > "$sandbox/.jichi.d/sessions/seed00000000000000000000000000001.json"
# env NAME=value, not a prefixed assignment: with_deadline's child sh must see
# it (FreeBSD's /bin/sh does not export shell assignments; smoke_lint 13).
( cd "$ws" && with_deadline 30 env HOME="$sandbox" "$BIN" prune --keep abc \
    < /dev/null > "$tmp/prune.out" 2>&1 )
prc=$?
[ "$prc" = 2 ] && [ -f "$sandbox/.jichi.d/sessions/seed00000000000000000000000000001.json" ] \
    && t_ok "prune --keep abc exits 2 and the seeded session survives" \
    || t_fail "prune --keep abc: rc=$prc, seeded file present=$([ -f "$sandbox/.jichi.d/sessions/seed00000000000000000000000000001.json" ] && echo yes || echo NO)"

# 7. rewind abc -> usage error (was clamped to 1, truncating history by one).
( cd "$ws" && with_deadline 30 "$BIN" rewind abc < /dev/null > "$tmp/rw.out" 2>&1 )
rwc=$?
[ "$rwc" = 2 ] && t_ok "rewind abc is a usage error (no silent one-turn cut)" \
    || t_fail "rewind abc: rc=$rwc, out=$(head_bytes 80 "$tmp/rw.out")"

# 8. undo abc -> usage error (was clamped to 1, reverting the latest checkpoint).
# The parse happens before any snapshot work, but on a <1 GB machine the LITE
# profile is the default and its snapshots:0 answers "snapshots unavailable"
# (rc 1) before the number is read -- V2f (492 MB Debian 9), 2026-10-16. Pin
# the full profile, like every other call here, so this tests the parse.
printf '{"lowResource":false}' > "$tmp/undo-c.json"
( cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/undo-c.json" undo abc \
    < /dev/null > "$tmp/ud.out" 2>&1 )
udc=$?
[ "$udc" = 2 ] && t_ok "undo abc is a usage error (no silent checkpoint revert)" \
    || t_fail "undo abc: rc=$udc, out=$(head_bytes 80 "$tmp/ud.out")"

t_done
