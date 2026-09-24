#!/bin/sh
# smoke: a driver's temp dirs are gone when it exits (M739).
#
# THE DEFECT, and it is as old as the tier. Every driver writes `tmp=$(smoke_tmp)`,
# and smoke_tmp recorded the new directory in a shell variable for the EXIT trap to
# remove. A command substitution runs in a subshell; what it assigns is lost when it
# returns; so the trap removed nothing, in every driver, from M209 (2026-07-31) on.
# /tmp being a tmpfs that a reboot empties hid it -- until 2026-09-24, when threadwork
# had 20,284 of them since its last boot, the tmpfs's 1,048,576 inodes were all in
# use with 102 GB of its space free, and every tool that made a temp file, this
# session's own shell included, failed with "No space left on device".
#
# THE CHECKS run a driver in miniature -- it sources the real _smoke.sh and makes its
# temp dirs the way every driver does -- with TMPDIR inside this driver's own temp
# dir, so what it leaves is countable: that it made them at all (the instrument);
# that nothing is left after a clean exit, or after a failing one; and that the
# registry, a file on a shared /tmp, cannot be used to remove anything smoke_tmp did
# not make. One of the two dirs holds a fixture left mode 000, as several drivers
# build, because rm -rf alone cannot remove what is under it.
# Runs no jichi, which in this tier is what *_lint.sh means.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)

cat > "$tmp/child.sh" <<EOF
. "$SMOKE_DIR/_smoke.sh"
a=\$(smoke_tmp)
b=\$(smoke_tmp)
mkdir "\$a/locked" && : > "\$a/locked/f" && chmod 000 "\$a/locked"
[ -d "\$a" ] && [ -d "\$b" ] && printf '%s\n%s\n' "\$a" "\$b" > "\$MADE"
# A line smoke_tmp did not write, naming a directory it did not make.
printf '%s\n' "\$TMPDIR/victim" >> "\$SMOKE_TMPREG"
[ "\${1:-}" = fail ] && exit 1
exit 0
EOF

run_child() {  # MODE -> runs the miniature driver; leaves $tmp/root to inspect
    chmod -R u+rwx "$tmp/root" 2>/dev/null
    rm -rf "$tmp/root"
    mkdir -p "$tmp/root/victim"
    : > "$tmp/made"
    TMPDIR="$tmp/root" MADE="$tmp/made" sh "$tmp/child.sh" "$1" > "$tmp/child.out" 2>&1
}
left() {       # what the miniature driver left in its TMPDIR, the planted victim aside
    find "$tmp/root" -mindepth 1 ! -path "$tmp/root/victim" 2>/dev/null
}

# --- 1: the instrument -- the child made two real temp dirs ------------------
run_child ok
_made=$(grep -c '/jichi_smoke\.' "$tmp/made" 2>/dev/null)
if [ "${_made:-0}" -eq 2 ]; then
    t_ok "the miniature driver made its two temp dirs (so the checks below can fail)"
else
    t_fail "the miniature driver did not report two temp dirs (got ${_made:-0}):
$(cat "$tmp/child.out")
Without them every check below passes on an empty directory."
fi

# --- 2: nothing is left after a clean exit ------------------------------------
if [ -z "$(left)" ]; then
    t_ok "after a clean exit, nothing is left -- the mode-000 fixture and the registry included"
else
    t_fail "a driver's exit left these behind:
$(left | sed "s#^$tmp/root/##" | head -n 10)
smoke_tmp runs in a command substitution, so what it records must survive the
subshell: the registry file, read by the EXIT trap. And chmod before rm -rf, or a
mode-000 fixture keeps everything beneath it."
fi

# --- 3: nor after a failing one ------------------------------------------------
run_child fail
if [ -z "$(left)" ]; then
    t_ok "after a failing exit, nothing is left either"
else
    t_fail "a driver that exited 1 left these behind:
$(left | sed "s#^$tmp/root/##" | head -n 10)"
fi

# --- 4: the registry removes only what smoke_tmp made --------------------------
if [ -d "$tmp/root/victim" ]; then
    t_ok "a registry line naming a directory smoke_tmp did not make removes nothing"
else
    t_fail "the cleanup removed a directory smoke_tmp did not make, because a line in
its registry named it. The registry is a file on a shared /tmp: check the name."
fi

t_done
