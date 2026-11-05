#!/bin/sh
# smoke: the upgrade that re-keyed the checkpoint store (M1097) orphans the old
# djb2-keyed numeric store silently -- M1097-c (N2, LOW) makes that silence loud.
#
# THE DEFECT. Pre-M1097 the shadow repo lived at ~/.jichi.d/checkpoints/<key>
# where <key> was djb2 of the workspace path, rendered as a DECIIMAL number
# (h = 5381u; h = h*33 + (unsigned char)c). M1097 re-keyed every per-workspace
# store to the 64-hex SHA-256 of the raw path, so the numeric store is never
# reached again -- but nothing told the user. Their undo history is sitting in
# a directory jichi will not open, and the silence is the data-loss shape (the
# M326v lesson: a store that is orphaned without a word is a store that is lost
# without a word).
#
# THE FIX. At start-up (the same place warn_legacy_paths runs, and BEFORE
# ~/.jichi.d is created per the M1098 ordering constraint), if
# ~/.jichi.d/checkpoints/<djb2(cwd)> exists, print ONE warn naming that path and
# saying it is no longer read. djb2 is re-implemented locally in main.c to match
# the OLD key byte-for-byte (5381, h*33+c, unsigned char); this driver carries
# the same djb2 so it can plant the exact store the warn looks for.
#
# WHAT THIS ASSERTS. (1) a planted numeric store for the current workspace makes
# the warn fire; (2) it names the EXACT path (so the user can find/inspect it
# before deleting); (3) with NO old store the warn is silent (a warning that
# always fires is noise, and would say a store exists that does not).
#
# Git-free and model-free: the warn is keyed off the store directory existing,
# which the driver plants by hand (a manifest file makes it a plausible store),
# and it needs no network (the `models` command, like migration_paths.sh, is
# enough to reach start-up).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

ws=$(smoke_tmp); mkdir -p "$ws"
KP="$SMOKE_TOOLS/keyprobe"
# The OLD key, byte-exact: keyprobe djb2 links the in-tree core (h = 5381u,
# h = h*33 + (unsigned char)c) -- the same 64-bit wrap main.c's warn uses, so
# the driver plants exactly the store the warn looks for. A shell reimplementation
# cannot hold the 2^64 wrap portably (dash rejects the modulus literal), which
# is why this goes through keyprobe.
key=$("$KP" djb2 "$ws")
[ -n "$key" ] || { echo "not ok - keyprobe djb2 failed for $ws"; exit 1; }

run_in_ws() { # $1 = workspace, $2 = home
    (cd "$1" && with_deadline 30 env HOME="$2" "$BIN" models \
        < /dev/null > "$2/out" 2> "$2/err")
}

# --- 1. a planted numeric store for THIS workspace fires the warn -----------
h1=$(smoke_tmp); mkdir -p "$h1/.jichi.d/checkpoints/$key"
printf '{\n  "version": 1,\n  "worktree": "%s"\n}\n' "$ws" \
    > "$h1/.jichi.d/checkpoints/$key/manifest.json"
run_in_ws "$ws" "$h1"
if grep -q 'checkpoint store from an older jichi' "$h1/err"; then
    t_ok "a planted djb2 checkpoint store for the workspace fires the warn"
else
    t_fail "no warn for a present old store (key=$key): $(head_bytes 300 "$h1/err")"
fi

# --- 2. the warn NAMES the exact path ---------------------------------------
if grep -q "$h1/.jichi.d/checkpoints/$key" "$h1/err"; then
    t_ok "the warn names the exact orphaned store path"
else
    t_fail "warn did not name the store path: $(head_bytes 300 "$h1/err")"
fi

# --- 3. no old store -> silent ----------------------------------------------
h2=$(smoke_tmp); mkdir -p "$h2"
run_in_ws "$ws" "$h2"
if ! grep -q 'checkpoint store from an older jichi' "$h2/err"; then
    t_ok "no warn when there is no old store (silent for everyone else)"
else
    t_fail "warn fired with no old store present (noise): $(head_bytes 300 "$h2/err")"
fi

t_done
