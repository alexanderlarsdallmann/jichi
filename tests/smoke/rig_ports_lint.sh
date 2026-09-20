#!/bin/sh
# smoke lint: no two rigs default to the same guest ssh port (M678).
#
# THE DEFECT, measured rather than imagined. `tier-v-vm.sh` and
# `tier-v-openbsd.sh` both defaulted to **2222**. An OpenBSD VM left up by an
# earlier run was still holding it eight hours later, so when the Debian ladder
# row booted, qemu exited immediately with
#
#   Could not set up host forwarding rule 'tcp:127.0.0.1:2222-:22'
#
# and the rig -- seeing no kernel banner after 120 s -- wrote this into the
# results file that `PLATFORMS.md` quotes:
#
#   FINDING: the kernel never STARTED at -m 1024. ... a property of the IMAGE,
#   not of jichi.
#
# Nothing about the image was true in that sentence, and nothing about jichi was
# measured. **An infrastructure failure recorded as a capability verdict** is the
# one outcome every rig in this tree exists to avoid, and it took one shared
# default to produce it.
#
# Two repairs, and this is the cheap half: the ports are distinct, and a check
# says so. The expensive half is in tier-v-vm.sh, which now asks whether qemu is
# alive before attributing silence to the guest.
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 2
tmp=$(smoke_tmp)
SC="$SMOKE_ROOT/scripts"

# Every rig's default guest ssh port, from its own PORT= line. Extraction by
# sed rather than `grep -o`: illumos returns only the first match per line.
: > "$tmp/ports"
n=0
for f in "$SC"/tier-*.sh "$SC"/jhub-*.sh; do
    [ -f "$f" ] || continue
    _p=$(sed -n 's/^PORT="\${[A-Z_]*:-\([0-9][0-9]*\)}".*/\1/p' "$f" | head -1)
    [ -n "$_p" ] || continue
    n=$((n+1))
    printf '%s %s\n' "$_p" "$(basename "$f")" >> "$tmp/ports"
done

# --- 1: the extraction found the rigs -------------------------------------
if [ "$n" -ge 4 ]; then
    t_ok "$n rigs declare a default guest ssh port"
else
    t_fail "only $n rig(s) declare a default port (want >= 4).
A floor of zero cannot validate: with an empty list, check 2 finds no
duplicates because it compares nothing. Fix the sed before the ports."
fi

# --- 2: no two share one --------------------------------------------------
_dup=$(awk '{ if (seen[$1] != "") { print $1 ": " seen[$1] " and " $2 }
              seen[$1] = $2 }' "$tmp/ports")
if [ -z "$_dup" ]; then
    t_ok "all $n default ports are distinct ($(awk '{printf "%s ", $1}' "$tmp/ports" | sed 's/ $//'))"
else
    t_fail "rigs sharing a default guest ssh port:
$_dup
A leftover VM from one then makes the other report a FALSE finding rather than
an error -- measured 2026-09-20, when a stale OpenBSD guest on 2222 produced
'the kernel never STARTED ... a property of the IMAGE' in a Debian row's
results file. Give each rig its own port."
fi

t_done
