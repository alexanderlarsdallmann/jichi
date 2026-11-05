#!/bin/sh
# smoke: the lease's documented degradation is pinned (B-07, M1115).
#
# THE DEFECT, from the 2026-10-17 process review (finding B-07): the
# per-workspace lease is taken read-decide-write under a lock, but where the
# lock cannot be taken -- a filesystem without file locking (some network
# mounts), or a lock whose open fails -- the take proceeds anyway, by
# deliberate choice ("a diagnostic must not become an outage"). So `--lease
# fail` is advisory, not a guarantee, in those cases. Where the lock IS held
# past its wait, the take also proceeds -- loudly, naming the holder.
#
# STRICT MODE WAS CONSIDERED AND REJECTED: refusing to start where locking is
# unavailable would turn a convenience into an outage. The docs carry the
# degradation instead; this driver pins them, by class, in the two pages a
# security-minded reader opens: SECURITY.md and docs/HARDENING.md.
#
# CHECK 1 is the mechanism's own lines -- a src regression that deleted the
# degradation (or rewrote its log lines) turns this red. CHECK 2 is the
# documentation: red on the tree that carries the behaviour without saying so.
#
# Reads src/ and docs/ only, so it runs without a built binary -- that is why
# it is exempt from smoke_lint check 5 by name (the case at smoke_lint.sh's
# check 5; the docs_flags.sh shape -- a driver whose name keeps the docs_*
# convention rather than *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 2
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# --- 1: the mechanism still exists in src --------------------------------------
if "$G" -q 'another process has held' "$SMOKE_ROOT/src/util/jc_lease.c" \
    && "$G" -q 'no lock on %s here' "$SMOKE_ROOT/src/util/jc_lease.c"; then
    t_ok "the lease's two degradation paths still exist in src (busy-holds warn; no-locking debug)"
else
    t_fail "the lease degradation lines are missing from src/util/jc_lease.c -- the documented behaviour is gone (or renamed); re-check the docs against it"
fi

# --- 2: the degradation is documented as advisory ------------------------------
# The RED-ON-HEAD half: before M1115 neither page contained the word, so a
# tree that carried the behaviour without saying it is caught here.
m_sec=$("$G" -qi 'advisory' "$SMOKE_ROOT/SECURITY.md" && echo found || echo missing)
m_hrd=$("$G" -qi 'advisory' "$SMOKE_ROOT/docs/HARDENING.md" && echo found || echo missing)
if [ "$m_sec" = found ] && [ "$m_hrd" = found ]; then
    t_ok "SECURITY.md and docs/HARDENING.md both carry the lease's advisory degradation"
else
    t_fail "the lease's advisory degradation is not documented: SECURITY.md=$m_sec docs/HARDENING.md=$m_hrd (both must say 'advisory'; the finding is B-07, M1115)"
fi

t_done
