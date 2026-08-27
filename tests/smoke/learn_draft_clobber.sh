#!/bin/sh
# smoke: the M79 output fallback must not DESTROY an existing artifact (M423).
#
# THE DEFECT THIS EXISTS FOR. A `subtask:` command declaring `output:` snapshots
# that file's mtime, and if the subtask leaves it unchanged -- because the model
# narrated its result instead of calling write_file -- jichi persists the returned
# ANSWER there "so the work isn't lost" (M79). It wrote unconditionally, so it
# could not tell the case it was built for (nothing there; save the work) from its
# exact inverse (a good artifact is there; overwrite it with whatever the model
# happened to say last).
#
# Measured on a real run: learn-on-stop fires automatically after any completed
# --auto run, and the mentor of a run about listing .zig files replaced an 85-line
# reviewed lessons draft -- one whose `## Corrections` section cited ANECDOTES #6
# and a memory.md line number -- with FOUR lines of mid-thought narration ending
# "Let me check if there were multiple commits:". Recoverable only because that
# workspace happened to have the draft committed to git.
#
# The fix keeps M79's purpose and removes the destructive case: an existing
# non-empty target is left alone and the answer goes to a sibling `<path>.answer`,
# so neither artifact is lost and the human diffs them.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The mentor turn NARRATES -- no write_file. That is the whole point: it is what
# the real model did.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "lessons.draft.md"
  text Let me check if there were multiple commits:
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"learnOnStop":true'

(cd "$ws" && "$BIN" --config "$tmp/config.json" init \
    < /dev/null > /dev/null 2>&1)

# A prior, reviewed draft is already in place.
PRIOR='# Lessons learned

## Corrections

- replace: a stub returning a plausible default => prefer @panic("TODO")

## New lessons

### The fix/break/fix loop
Substantive prior content a human curated and has not applied yet.
'
mkdir -p "$ws/.jichi"
printf '%s' "$PRIOR" > "$ws/.jichi/lessons.draft.md"
prior_lines=$(wc -l < "$ws/.jichi/lessons.draft.md" | tr -d ' ')

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop

# --- 1: the run completed, so learn-on-stop actually fired -------------------
if [ $rc -eq 0 ]; then
    t_ok "the --auto run completed, so the mentor ran"
else
    t_fail "run rc=$rc: $(tail -c 200 "$tmp/err")"
fi

# --- 2: THE DEFECT -- the prior draft survives -------------------------------
if grep -q 'Substantive prior content' "$ws/.jichi/lessons.draft.md" 2>/dev/null
then
    t_ok "the prior draft survived the mentor's narration"
else
    t_fail "the prior draft was clobbered: now $(wc -l < "$ws/.jichi/lessons.draft.md" | tr -d ' ') lines (was $prior_lines): $(head_bytes 120 "$ws/.jichi/lessons.draft.md")"
fi

# --- 3: and M79's PURPOSE survives too -- the answer is not thrown away ------
# Preserving the old file by simply refusing to write would trade one loss for
# another. The narration has to land somewhere the human can see.
if [ -s "$ws/.jichi/lessons.draft.md.answer" ]; then
    t_ok "the narrated answer was kept beside it (.answer)"
else
    t_fail "the answer was discarded: no .answer file"
fi

# --- 4: and the operator is TOLD, not left to find it ------------------------
if grep -q 'lessons.draft.md.answer' "$tmp/err" 2>/dev/null; then
    t_ok "stderr names where the answer went"
else
    t_fail "nothing on stderr about the .answer file: $(tail -c 200 "$tmp/err")"
fi

# --- 5: the ORIGINAL M79 behaviour is intact ---------------------------------
# With no prior artifact there is nothing to protect, and the answer must land at
# the declared path itself -- that is the case M79 was built for. Asserted here
# because after the fix above, nothing else pins it: a refactor that sent every
# answer to `.answer` would pass checks 1-4 and silently break the feature.
ws2=$(smoke_tmp)
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" '"learnOnStop":true'
(cd "$ws2" && "$BIN" --config "$tmp/config2.json" init \
    < /dev/null > /dev/null 2>&1)
rm -f "$ws2/.jichi/lessons.draft.md"
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --no-session --auto -p "do the task" \
    < /dev/null > /dev/null 2>"$tmp/err2")
mm_stop

if grep -q 'multiple commits' "$ws2/.jichi/lessons.draft.md" 2>/dev/null &&
   [ ! -e "$ws2/.jichi/lessons.draft.md.answer" ]; then
    t_ok "with no prior draft the answer lands at the declared path (M79 intact)"
else
    t_fail "M79 regressed: draft=$(head_bytes 60 "$ws2/.jichi/lessons.draft.md" 2>/dev/null) answer_exists=$([ -e "$ws2/.jichi/lessons.draft.md.answer" ] && echo yes || echo no)"
fi

t_done
