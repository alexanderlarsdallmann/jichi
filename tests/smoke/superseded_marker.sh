#!/bin/sh
# smoke: a superseded read's marker tells the truth and points below (M354).
#
# The M93/M94 dedup elides an old read_file result when a NEWER read of the
# same file exists later in the conversation -- zero loss by construction.
# But its marker was borrowed from the pressure pass: "elided to fit the
# context window", false twice over (the eager dedup runs at budget 0, under
# no window pressure) and silent about the one fact that stops the re-read
# loop: the newer copy is still IN the conversation. M354 gives the dedup its
# own marker: "elided: superseded -- a newer read ... appears LATER in this
# conversation; use that instead of re-reading the file".
#
# No contextLimit is set, so the pressure pass is a no-op (limit unknown) and
# every marker in this run can only come from the dedup -- the flavours are
# isolated by construction. The sentinel sits mid-file (inside the elided
# span), so its count across the request tells elision AND survival at once:
# exactly 2 before the dedup (both reads intact), exactly 1 after (old copy
# elided, newest kept verbatim).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

i=1
while [ "$i" -le 100 ]; do
    if [ "$i" -eq 50 ]; then
        printf 'DEDUP_SENTINEL_517 sits mid-file\n'
    else
        printf 'line %03d pad pad pad pad pad pad\n' "$i"
    fi
    i=$((i + 1))
done > "$ws/f.txt"
printf 'small\n' > "$ws/s.txt"

cat > "$tmp/d.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"f.txt"}
rule
  count 2
  tool read_file {"path":"f.txt"}
rule
  count 3
  tool read_file {"path":"s.txt"}
rule
  count 4
  tool read_file {"path":"s.txt"}
rule
  text DEDUP_DONE
EOF
mm_start "$tmp/d.mm" "$tmp/cap" 7
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session -p "read around" < /dev/null) >/dev/null 2>&1
mm_stop

# --- 1: before aging, both copies are intact (artifact + pair) -----------------
n=$(grep -o "DEDUP_SENTINEL_517" "$tmp/cap/req.3" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "2" ] && ! grep -q "elided" "$tmp/cap/req.3" 2>/dev/null; then
    t_ok "request 3 carries both reads verbatim, nothing elided yet"
else
    t_fail "request 3 wrong shape (sentinels=$n)"
fi

# --- 2: the aged duplicate is elided with the TRUE reason ----------------------
if grep -q "elided: superseded" "$tmp/cap/req.5" 2>/dev/null && \
   grep -q "LATER in this conversation" "$tmp/cap/req.5" 2>/dev/null; then
    t_ok "the marker names supersession and points below"
else
    t_fail "no superseded marker in request 5: $(head_bytes 200 "$tmp/cap/req.5" 2>/dev/null)"
fi

# --- 3: ...and no longer claims window pressure that never existed -------------
if grep -q "to fit the context window" "$tmp/cap/req.5" 2>/dev/null; then
    t_fail "the dedup still borrows the pressure pass's marker"
else
    t_ok "no false window-pressure claim (no contextLimit is even set)"
fi

# --- 4: the newest copy survives verbatim -- elision AND survival in one count -
n=$(grep -o "DEDUP_SENTINEL_517" "$tmp/cap/req.5" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "exactly one sentinel left: old copy elided, newest kept"
else
    t_fail "expected 1 sentinel in request 5, found '$n'"
fi

t_done
