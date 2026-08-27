# shellcheck shell=sh
# _rig_ship.sh -- the ONE implementation of "put the tree on the target". Source it.
#
# WHY THIS FILE EXISTS (M466). Every rig shipped `git archive --format=tar HEAD`,
# which is right for a published row -- it names a commit, so the row is
# reproducible -- and makes one loop IMPOSSIBLE:
#
#     find a portability defect on the target -> fix it -> verify the fix there
#
# `git archive HEAD` cannot see an uncommitted change, so the only way to test a
# fix was to COMMIT IT UNTESTED. That is how M466's `\b` fix would have shipped:
# a GNU-only regex escape in a lint, found on OpenBSD, whose repair could not be
# checked on the platform that found it without first claiming in a commit message
# that it worked.
#
# So there are two modes, and the reason they are one file rather than ten lines
# copied into each rig is M464's lesson about the multiplier: four rigs computed
# one value four ways and two of them were wrong. A second rig is where drift
# starts, not the fourth.
#
# THE PROPERTY THAT MATTERS MORE THAN EITHER MODE: a dirty row must never be
# mistaken for a reproducible one. jc_rig_ship_stamp writes a line that says
# outright it is NOT a commit, with the count of differing paths, and the caller
# is expected to put it in the results file. A row whose provenance is unclear is
# worse than no row -- it will be quoted later by someone who was not here.

# jc_rig_ship_tar REPO DIRTY -- write a tar of the tree to stdout.
#
# DIRTY=1 ships TRACKED files with WORKING-TREE content, which is what
# `make smoke` on the target would actually run. Deliberately not `git stash` +
# archive (mutates the developer's tree, and a rig that can dirty the tree it
# tests has already been three separate mistakes in this campaign) and not a
# blanket `tar .` (that would carry build artifacts, .git, and every cache).
#
# The two tarballs have different ENTRY COUNTS and that is not a lost file:
# `git archive` emits an explicit entry per directory, a file-list tar does not
# (1918 vs 1633 on this tree, the difference being directories). `tar xf -`
# creates parents as needed in both GNU and BSD tar, and the dirty path is
# empirically proven -- the M466 fix was built and its full suite run on OpenBSD
# through it. Counting entries to compare the modes will mislead; compare files.
jc_rig_ship_tar() {
    if [ "${2:-0}" = 1 ]; then
        git -C "$1" ls-files -z | tar -C "$1" --null -T - -cf -
    else
        git -C "$1" archive --format=tar HEAD
    fi
}

# jc_rig_ship_stamp REPO DIRTY -- one line of provenance for the results file.
jc_rig_ship_stamp() {
    _rs_head=$(git -C "$1" rev-parse --short HEAD 2>/dev/null || echo unknown)
    if [ "${2:-0}" = 1 ]; then
        _rs_n=$(git -C "$1" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
        printf 'tree: WORKING TREE (--dirty), %s path(s) differ from %s -- NOT a commit\n' \
               "${_rs_n:-?}" "$_rs_head"
    else
        printf 'tree: HEAD %s (clean archive)\n' "$_rs_head"
    fi
}

# jc_rig_ship_label DIRTY -- what to print to the operator as the stage banner.
jc_rig_ship_label() {
    if [ "${1:-0}" = 1 ]; then
        printf 'ship the WORKING TREE (--dirty: uncommitted changes included)\n'
    else
        printf 'ship the tree at HEAD\n'
    fi
}
