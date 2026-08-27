#!/bin/sh
# smoke: `prune` applies its retention selectors to ~/.jichi.d/dreams/ (M611).
#
# THE SEAM. `prune` (M219) is "session-store hygiene" and scoped ONLY sessions;
# ~/.jichi.d/dreams/ had no retention at all, while the daemon's `--idle-dream`
# writes one draft per idle stretch -- unbounded growth with no way to trim it.
# M611 gives prune a dreams pass over the SAME --keep/--older-than selectors
# (reusing the tested jc_session_prune_select). Born red: before M611 `prune`
# deleted zero dreams and reported only sessions.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
dd="$HOME/.jichi.d/dreams"
mkdir -p "$dd"

# Three dreams, mtimes oldest -> newest by fixed timestamps (deterministic
# keep-newest; no reliance on creation order or the clock).
printf 'oldest\n'  > "$dd/dream-1000.md"; touch -t 202601010101 "$dd/dream-1000.md"
printf 'middle\n'  > "$dd/dream-2000.md"; touch -t 202601020101 "$dd/dream-2000.md"
printf 'newest\n'  > "$dd/dream-3000.md"; touch -t 202601030101 "$dd/dream-3000.md"

nd() { ls "$dd"/dream-*.md 2>/dev/null | grep -c . ; }

# --- 1: --dry-run names dreams and deletes nothing --------------------------------
out=$(with_deadline 20 "$BIN" prune --keep 1 --dry-run < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'dream' && [ "$(nd)" -eq 3 ]; then
    t_ok "dry-run reports dreams and deletes none (still 3)"
else
    t_fail "dry-run rc=$rc, dreams=$(nd): $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 2: prune --keep 1 deletes the two oldest dreams ------------------------------
out=$(with_deadline 20 "$BIN" prune --keep 1 < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && [ "$(nd)" -eq 1 ]; then
    t_ok "prune --keep 1 left exactly 1 dream (was 3)"
else
    t_fail "prune rc=$rc left $(nd) dream(s): $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 3: the KEPT dream is the newest ----------------------------------------------
if [ -f "$dd/dream-3000.md" ] && [ ! -f "$dd/dream-1000.md" ] \
   && [ ! -f "$dd/dream-2000.md" ]; then
    t_ok "keep-newest kept dream-3000.md and removed the two older ones"
else
    t_fail "wrong dream kept: $(ls "$dd" 2>/dev/null | tr '\n' ' ')"
fi

# --- 4: the report counts dreams distinctly ---------------------------------------
# Re-seed and prune by age so the count line must mention dreams by name.
printf 'x\n' > "$dd/dream-1000.md"; touch -t 202601010101 "$dd/dream-1000.md"
out=$(with_deadline 20 "$BIN" prune --older-than 1d < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'dream(s)'; then
    t_ok "the prune report counts dreams distinctly from sessions"
else
    t_fail "no dream count in the report: $(printf '%s' "$out" | head_bytes 160)"
fi

t_done
