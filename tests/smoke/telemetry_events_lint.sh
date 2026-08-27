#!/bin/sh
# lint: the JSONL event vocabularies stay registered (M366). Two growing
# vocabularies -- telemetry events (telem()/jc_eventlog_begin) and the
# envelope journal's events (jc_env_journal_begin) -- are written by many
# sites, read by consumers that IGNORE unknown events by design, and
# documented in two canonical tables (TELEMETRY.md "Event types",
# OBSERVABILITY.md "Journal event reference"). That tolerance means drift is
# invisible at runtime: one audit found nine emitted telemetry events with no
# documentation row. This lint closes the loop BOTH WAYS: an event emitted in
# src/ but absent from its table fails, and a table row for an event nothing
# emits fails. Extraction floors keep the ground truth honest (M295): if a
# changed call shape makes extraction find too few names, the lint fails
# loudly instead of checking nothing -- and a missed emit shape still
# surfaces, because its (documented) row then reads as never-emitted.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
SRC="$SMOKE_ROOT/src"

tmp=$(smoke_tmp)

# --- extraction: telemetry events ---------------------------------------------
# Shapes: telem(app, "x") | jc_app_telem_begin(app, "x") (M583, the shared
# helper) | jc_eventlog_begin(<anything>, "x") on one line | a continuation line
# `"x");` directly after a line ending `jc_eventlog_begin(...`.
#
# COMMENTS ARE STRIPPED FIRST, and that is not tidiness. Writing M583's own
# comment -- 'seventeen call sites read better as telem(app, "x")' -- made this
# lint report an emitted event named `x`. Third occurrence of one shape in this
# tree: mutant-sweep.sh selected a driver by a binary name inside a comment
# (M579), group_sep_lint had it before that. An extractor that reads comments is
# reading prose as if it were code. Dropping block-comment continuation lines
# (`^ *\*`) and comment openers is enough here, because no real call site starts
# with an asterisk -- and the extraction FLOORS below are what keep this
# conservative strip from silently hiding a real emitter.
strip_c_comments() {
    sed -e 's|/\*.*\*/||' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\*/d' "$@"
}
find "$SRC" -type f -name '*.c' | sort > "$tmp/csrc"
while IFS= read -r f; do strip_c_comments "$f"; done < "$tmp/csrc" > "$tmp/nocomment.c"
{
    grep -hoE 'telem\(app, "[a-z_]+"'                     "$tmp/nocomment.c"
    grep -hoE 'jc_app_telem_begin\([^,)]*, *"[a-z_]+"'    "$tmp/nocomment.c"
    grep -hoE 'jc_eventlog_begin\([^,)]*, *"[a-z_]+"'     "$tmp/nocomment.c"
    grep -h -A1 'jc_eventlog_begin([^"]*$' "$tmp/nocomment.c" \
        | grep -E '^[[:space:]]*"[a-z_]+"\);?'
} | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u > "$tmp/telem.src"

# --- extraction: journal events -------------------------------------------------
{
    find "$SRC" -type f -name '*.c' -exec grep -hoE 'jc_env_journal_begin\([^,)]*, *"[a-z_]+"' {} +
    find "$SRC" -type f -name '*.c' -exec grep -h -A1 'jc_env_journal_begin([^"]*$' {} + \
        | grep -E '^[[:space:]]*"[a-z_]+"\);?'
} | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u > "$tmp/journal.src"

# --- documented rows -------------------------------------------------------------
# TELEMETRY.md: rows of the "Event types" table (up to the next heading).
sed -n '/^\*\*Event types and their extra fields:\*\*/,/^### /p' \
    "$SMOKE_ROOT/docs/TELEMETRY.md" \
    | grep -oE '^\| `[a-z_]+`' | grep -oE '[a-z_]+' | grep -v '^event$' \
    | sort -u > "$tmp/telem.doc"
# OBSERVABILITY.md: rows of the "Journal event reference" table.
sed -n '/^## Journal event reference/,/^## /p' \
    "$SMOKE_ROOT/docs/OBSERVABILITY.md" \
    | grep -oE '^\| `[a-z_]+`' | grep -oE '[a-z_]+' | grep -v '^event$' \
    | sort -u > "$tmp/journal.doc"

nts=$(grep -c . "$tmp/telem.src"); ntd=$(grep -c . "$tmp/telem.doc")
njs=$(grep -c . "$tmp/journal.src"); njd=$(grep -c . "$tmp/journal.doc")

# --- 1+2: extraction floors -------------------------------------------------------
if [ "$nts" -ge 14 ] && [ "$ntd" -ge 14 ]; then
    t_ok "telemetry ground truth: $nts emitted, $ntd documented (floor 14)"
else
    t_fail "telemetry extraction too thin: src=$nts doc=$ntd -- the scrape broke"
fi
if [ "$njs" -ge 18 ] && [ "$njd" -ge 18 ]; then
    t_ok "journal ground truth: $njs emitted, $njd documented (floor 18)"
else
    t_fail "journal extraction too thin: src=$njs doc=$njd -- the scrape broke"
fi

# --- 3: every emitted telemetry event has a row -----------------------------------
miss=$(comm -23 "$tmp/telem.src" "$tmp/telem.doc" | tr '\n' ' ')
if [ -z "$miss" ]; then
    t_ok "every emitted telemetry event is documented"
else
    t_fail "telemetry events emitted but undocumented:$miss"
fi

# --- 4: every documented telemetry row is emitted ----------------------------------
ghost=$(comm -13 "$tmp/telem.src" "$tmp/telem.doc" | tr '\n' ' ')
if [ -z "$ghost" ]; then
    t_ok "no telemetry row documents an event nothing emits"
else
    t_fail "telemetry rows with no emitter:$ghost"
fi

# --- 5: every emitted journal event has a row --------------------------------------
missj=$(comm -23 "$tmp/journal.src" "$tmp/journal.doc" | tr '\n' ' ')
if [ -z "$missj" ]; then
    t_ok "every emitted journal event is documented"
else
    t_fail "journal events emitted but undocumented:$missj"
fi

# --- 6: every documented journal row is emitted -------------------------------------
ghostj=$(comm -13 "$tmp/journal.src" "$tmp/journal.doc" | tr '\n' ' ')
if [ -z "$ghostj" ]; then
    t_ok "no journal row documents an event nothing emits"
else
    t_fail "journal rows with no emitter:$ghostj"
fi

# --- 7/8: the `runs` reader's journal-EXCLUSIVE list (M421) --------------------------
# Both sinks write objects keyed "event", and since M420 both carry `run`, so
# `runs <dir>` (which globs *.jsonl) could no longer tell them apart: pointing
# --journal and --log at one campaign directory rendered every run TWICE, the
# phantom wearing the real run id and carrying telemetry's `constraint` count.
# jc_runsview.c now decides with a POSITIVE test -- a journal is a file carrying
# a journal-exclusive event name. That list is only correct while it stays
# disjoint from the telemetry vocabulary, and NOTHING at runtime would notice if
# telemetry gained an event called `verify`: the phantom would simply return.
# Extracted from the NAMES[] array in journal_exclusive(), floor-checked.
sed -n '/^static int journal_exclusive/,/^}/p' "$SRC/util/jc_runsview.c" \
    | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u > "$tmp/excl.src"
nexcl=$(wc -l < "$tmp/excl.src" | tr -d ' ')
if [ "$nexcl" -ge 12 ]; then
    t_ok "extracted $nexcl journal-exclusive names from the runs reader"
else
    t_fail "only $nexcl names extracted from journal_exclusive() -- did the array shape change?"
fi

# The two failure modes, one check: a name that telemetry ALSO emits (the
# phantom returns) or a name no journal site emits (a dead entry that quietly
# stops qualifying real journals).
bad_shared=$(comm -12 "$tmp/excl.src" "$tmp/telem.src" | tr '\n' ' ')
bad_dead=$(comm -23 "$tmp/excl.src" "$tmp/journal.src" | tr '\n' ' ')
if [ -z "$bad_shared" ] && [ -z "$bad_dead" ]; then
    t_ok "the reader's journal-exclusive list is disjoint from telemetry and fully emitted"
else
    t_fail "shared with telemetry:$bad_shared | emitted by no journal site:$bad_dead"
fi

# --- 9: ONE stamping path, so a tenth emitter cannot be born unstamped ---------
# THE DEFECT (M583, seams D4). telem() was `static` in jc_agent.c, so only that
# file's emitters carried `depth`, `turn` and (M420) `run`. NINE call sites in
# four other files reached jc_eventlog_begin() directly and carried none of them:
# prefix_churn, hook, retrieve, test_edit, args_truncated and four args_repair
# variants. M420 built the join between behaviour and outcome, and it was partial
# by exactly those events -- "which turn did the argument repairs happen on?" had
# no answer at all.
#
# The rule, not the list: an event sourced from a `struct jc_app` goes through
# jc_app_telem_begin(). A per-site fix would have been correct today and silently
# wrong the next time somebody typed the raw call, which is how the nine
# accumulated. jc_eventlog_begin() stays public for sinks that have no app (the
# unit tests drive it with no application at all) -- the rule binds only calls
# whose sink comes from one.
raw=$(grep -nE 'jc_eventlog_begin\([^)]*app' "$tmp/nocomment.c" | grep -c . || true)
# the single legitimate site is the helper's own body, in src/chat/jc_app.c
own=$(strip_c_comments "$SRC/chat/jc_app.c" | grep -cE 'jc_eventlog_begin\([^)]*app' || true)
if [ "$own" -eq 1 ] && [ "$raw" -eq 1 ]; then
    t_ok "every app-sourced telemetry event goes through jc_app_telem_begin()"
else
    t_fail "an app-sourced event bypassing the stamping helper carries no depth,
   turn or run, so it cannot be joined to the run that produced it -- which is
   the M420 seam, reopened one call site at a time. Found $raw such call(s) in
   src/ (want exactly 1, the helper's own body in jc_app.c, which contributes
   $own). Offenders:
$(grep -nE 'jc_eventlog_begin\([^)]*app' "$tmp/nocomment.c" | head -12)"
fi

# --- 10: every emitted telemetry event has a READER ---------------------------
# THE SEAM (M584, proposal S2/D6). Checks 3 and 4 above guarantee every event is
# DOCUMENTED. Nothing guaranteed any event was ever READ, and the gap was not
# hypothetical: eight of eighteen telemetry event types were written to disk on
# every run and displayed by no command -- `args_truncated`, `constraint`,
# `constraint_exempt`, `history_check`, `hook`, `kinetic`, `prefix_churn`,
# `retrieve`. A signal nobody looks at is the same as no signal, and it costs
# disk to keep the illusion.
#
# WHAT THE MEASUREMENT SHOWED, because it argues against overclaiming: on the
# only real corpus available (42,652 events, one workload) just two of the eight
# had ever fired -- `hook` 15 times and `privileged` twice. The other six had
# never occurred, because the features behind them are off by default
# (auto-context), need a violation (history_check) or need hardware (kinetic).
# So this check is NOT evidence that six readers were valuable; it is a
# structural guarantee that the ninth event type cannot be born unreadable.
#
# The reader side is extracted from `strcmp(ev, "...")` only -- `ev` is the
# variable both summarisers bind the event name to. A looser pattern would count
# a FIELD VALUE (`strcmp(outcome, "timeout")`) as if it read an event of that
# name, which would make the check pass for an event nobody reads.
READERS="$SRC/util/jc_telemetry.c $SRC/util/jc_runsview.c"
for r in $READERS; do strip_c_comments "$r"; done \
    | grep -oE 'strcmp\(ev, *"[a-z_]+"' | grep -oE '"[a-z_]+"' | tr -d '"' \
    | sort -u > "$tmp/telem.read"
nread=$(grep -c . "$tmp/telem.read")
unread=$(comm -23 "$tmp/telem.src" "$tmp/telem.read" | tr '\n' ' ')
if [ "$nread" -ge 20 ] && [ -z "$unread" ]; then
    t_ok "all $nts emitted telemetry events are read by a summariser ($nread arms)"
else
    t_fail "emitted every run, displayed by no command:$unread
   (reader arms extracted: $nread, floor 20 -- a thin extraction would make this
   check pass by finding no readers AND no events to miss.) Add an arm to
   src/util/jc_telemetry.c or src/util/jc_runsview.c. An event that reaches disk
   and no reader is a cost with no benefit; see
   docs/proposals/2026-08-observability-seams.md S2."
fi

t_done
