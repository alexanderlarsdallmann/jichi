#!/bin/sh
# smoke: warrant tags on drafted lessons -- a note says HOW it knows, the apply
# summary counts the three classes, and analyze reviews unchecked notes first
# (M632).
#
# THE GAP. M326b's trichotomy (judgement / evidence / unchecked) existed for
# deferrals and nowhere else. The mentor drafted memory notes with an optional
# `[evidence: ...]` trailer -- a POINTER to where a lesson came from, not a
# CLASSIFICATION of how much it should be trusted. A gotcha noticed once and a
# number measured over forty runs landed in memory.md as the same kind of line
# and were re-injected into every turn with the same authority.
#
# Nothing is refused: an unchecked note IS what memory is for. It is labelled
# where it is read, counted where it is committed, and reviewed first.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

mkdir -p "$ws/.jichi"
cat > "$ws/.jichi/lessons.draft.md" <<'DRAFT'
## Memory notes
- apply_patch fails on 31 of 43 CRLF files; read first [evidence: run r-1] [warrant: measured]
- the retry loop likely masks a stall in jc_http.c [warrant: judgement]
- run_terminal_command once hung on a here-doc [warrant: unchecked]
- prefer small commits
DRAFT

# --- 1-2: apply counts the classes and keeps the pinned sentence -----------------
(cd "$ws" && with_deadline 30 "$BIN" learn apply < /dev/null > "$tmp/ap" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "Warrants: 1 measured, 1 judgement, 1 unchecked (labelled -- check before trusting), 1 untagged" "$tmp/ap"; then
    t_ok "apply reports the four warrant counts of the notes it committed"
else
    t_fail "apply rc=$rc: $(head_bytes 300 "$tmp/ap")"
fi
if grep -q "^Applied 4 memory note(s), 0 skill(s), 0 correction(s), and 0 rule(s) from" "$tmp/ap"; then
    t_ok "the sentence learn.sh pins is unchanged; the warrant line is appended"
else
    t_fail "pinned sentence changed: $(head_bytes 300 "$tmp/ap")"
fi

# --- 3: the tag travels into memory.md with the note ----------------------------
if grep -q "here-doc \[warrant: unchecked\]" "$ws/.jichi/memory.md"; then
    t_ok "the warrant trailer is kept on the committed note (it is read there)"
else
    t_fail "trailer lost: $(head_bytes 300 "$ws/.jichi/memory.md")"
fi

# --- 4: analyze reviews the unchecked note FIRST, and names it ------------------
tdir="$HOME/.jichi.d/telemetry"
mkdir -p "$tdir"
printf '{"v":1,"event":"tool_call","name":"run_terminal_command","ok":false}\n' > "$tdir/run.jsonl"
(cd "$ws" && with_deadline 30 "$BIN" learn analyze "$tdir/run.jsonl" \
    --workspace "$ws" < /dev/null > "$tmp/an" 2>&1); rc=$?
u=$(grep -n "labelled \[warrant: unchecked\]" "$tmp/an" | head -1 | cut -d: -f1)
m=$(grep -n "remembered note(s) -- review against current code" "$tmp/an" | head -1 | cut -d: -f1)
if [ $rc -eq 0 ] && [ -n "$u" ] && [ -n "$m" ] && [ "$u" -lt "$m" ] && \
   grep -q "here-doc" "$tmp/an"; then
    t_ok "analyze lists the 1 unchecked note before the general review, by name"
else
    t_fail "unchecked review missing or misplaced (rc=$rc u=$u m=$m): $(head_bytes 400 "$tmp/an")"
fi

# --- 5: the scaffolded mentor asks for the tag ------------------------------------
ws2=$(smoke_tmp)
(cd "$ws2" && "$BIN" init < /dev/null > /dev/null 2>&1)
if grep -q "\[warrant: measured\]" "$ws2/.jichi/agents/mentor.md" && \
   grep -q "\[warrant: unchecked\]" "$ws2/.jichi/agents/mentor.md"; then
    t_ok "init's mentor.md asks for the warrant tag on every note"
else
    t_fail "mentor prompt does not ask for the tag: $(grep -c warrant "$ws2/.jichi/agents/mentor.md" 2>/dev/null) mention(s)"
fi
t_done
