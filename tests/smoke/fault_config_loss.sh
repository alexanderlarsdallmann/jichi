#!/bin/sh
# M1107 (D-25), the FAULT half: a FAILED `config set` save must leave the
# original config in place, byte- and mode-identical -- the "config lost" shape
# the finding measured (expB / Run 0111). Requires a FAULT=1 binary; SKIPS
# otherwise.
#
# How the fault reaches the write. JICHI_FAULT_WRITE_AFTER=0 faults every WRITE
# site (the monotone threshold-0 model, the ENOSPC production case). In the
# FIXED save (M1107-e) BOTH the .bak copy and the primary are
# jc_write_file_atomic_mode, so BOTH fail -- but the backup failure is
# non-fatal (the original is still in place) and the primary fails before
# touching the target (temp-first), so the config is left byte- and
# mode-identical. That is exactly the property this driver asserts, and it is
# the one that was RED on the pre-fix tree: the old code renamed the live
# config to .bak FIRST, so by the time the fixed-name <path>.tmp write faulted,
# the config was already GONE (the D-25 "config lost" measurement).
#
# The driver asserts on local/config.json -- and, since M1107-c, on the ABSENCE
# of a partial .bak. Under a blanket fault the .bak copy itself is expected to
# fail (the fix makes that non-fatal). Since M1107-e the copy is
# jc_write_file_atomic_mode, whose monotone-knob fault returns BEFORE opening
# any temp -- so a faulted backup leaves the .bak ABSENT (never a 0-byte file);
# the accepted post-fault shapes are ABSENT or NON-EMPTY, and ABSENT is now the
# one that occurs. The live config surviving (legs 2-3) remains the invariant;
# the .bak leg pins what a failed backup may NOT look like.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- the reliable probe
# (the write_fail_restores.sh idiom). Must precede t_plan (M672).
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

# Permission string minus the type char (stat -c is banned by posix_utils_lint;
# the relay_baton_written idiom). 0600 = rw-------.
mode_of() { ls -l "$1" 2>/dev/null | awk '{ print substr($1, 2) }'; }

ws=$(smoke_tmp)
mkdir -p "$ws/local"
printf '{"model":"jlu/strong-model","configEditable":true,"snapshots":true}\n' \
    > "$ws/local/config.json"
chmod 600 "$ws/local/config.json"
cp "$ws/local/config.json" "$tmp/c.before"

# A FAILED save: the primary atomic write is faulted at threshold 0.
( cd "$ws" && with_deadline 30 env JICHI_FAULT_WRITE_AFTER=0 "$BIN" \
    --config-editable config set snapshots false \
    < /dev/null > "$tmp/o" 2>&1 )
rc=$?

# ---- 1. the save reported a failure (no false success) ----
if [ "$rc" != 0 ]; then
    t_ok "a faulted config-set save reports a failure (exit $rc)"
else
    t_fail "a faulted save reported SUCCESS -- the write path did not fault: $(head_bytes 160 "$tmp/o")"
fi

# ---- 2. the original config STILL EXISTS ----
if [ -f "$ws/local/config.json" ]; then
    t_ok "a failed save leaves the original config in place"
else
    t_fail "the original config is GONE after a failed save (the D-25 config-loss shape); out=$(head_bytes 160 "$tmp/o")"
fi

# ---- 3. ... and it is byte- and mode-identical to the pre-edit content ----
if cmp -s "$ws/local/config.json" "$tmp/c.before" \
    && [ "$(mode_of "$ws/local/config.json")" = "rw-------" ]; then
    t_ok "the original config is byte- and mode-identical after a failed save"
else
    t_fail "config changed on failed save: byte_same=$(cmp -s "$ws/local/config.json" "$tmp/c.before" && echo yes || echo NO) mode=$(mode_of "$ws/local/config.json")"
fi

# ---- 4. no partial (0-byte) .bak survives the faulted backup copy ----
# jc_write_file opens <path>.bak truncating BEFORE its fault check (D-04/M979),
# so a faulted copy leaves the file present but EMPTY -- wider in danger than
# absent, because a 0-byte <path>.bak is useless as a restore and a later
# "the original is in <path>.bak" message would affirm it. Accepted post-fault
# shapes: the .bak is ABSENT, or NON-EMPTY (a real copy). On the unfixed tree
# this is a 0-byte file -> RED.
bakf="$ws/local/config.json.bak"
if [ ! -e "$bakf" ] || [ -s "$bakf" ]; then
    t_ok "a faulted backup copy leaves no partial (0-byte) <path>.bak"
else
    t_fail "a faulted backup copy LEFT a 0-byte <path>.bak (useless as a restore; absent-or-non-empty are the accepted shapes): size=$(wc -c < "$bakf") mode=$(mode_of "$bakf")"
fi

t_done
