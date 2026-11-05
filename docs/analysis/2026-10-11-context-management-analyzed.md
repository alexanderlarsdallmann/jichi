# jichi context management, analyzed: what actually held this week

*2026-10-11. A measurement, not a change -- nothing in `src/` or `tests/`
moves on this page. Answering the operator's question: the agent ran long
this week without "seemingly" autocompacting; does jichi already have the
feature to store a plan and start a fresh session, or does it need one?*

## The short answer

The feature already exists and it fired. But the thing that kept these runs
alive was NOT the model-summarizing autocompact -- it was the mid-turn
**elision** pass, which runs every agent round and costs no model call. The
autocompact (the visible `[Earlier conversation summarized to save context]`
block) can only run **between turns**, and this week's runs were single long
headless turns: it had no opportunity. That is precisely the gap the context
relay proposal (`docs/proposals/2026-10-context-relay.md`) measures, and the
relay is the "store a plan, start a fresh session" feature -- built (M917-
M921), and idle until its 12 operator decisions (HANDOFF S4) are answered.

## The mechanisms, in firing order

All in `src/chat/jc_compact.c` unless noted.

**1. Mid-turn elision (M76) -- the workhorse.** At EVERY agent round (line
4770 of jc_agent.c), when the calibrated estimate exceeds `MIDTURN_HIGH_PCT`
(80%) of the effective limit, `jc_compact_midturn` elides old tool outputs
and oversized tool-call arguments down to `MIDTURN_TARGET_PCT` (60%),
keeping the last `MIDTURN_KEEP_RECENT` (6) messages intact. Elided text is
NOT destroyed: it is spilled to `~/.jichi.d/tool-output/<sid>/elided-N.txt`
and the in-history copy becomes a marker with that path ("re-read THAT path
instead of re-running the call"). No model call; structure preserved.
Latching (M712) avoids re-scanning the whole history each round when nothing
new left the protected window.

**2. The pressure note (M358).** Rendered into the next request once per
pressed pass: `[context] this turn has used ~N% of the ~L-token context
budget.` with two honest variants -- "old tool output is being elided"
(coping) vs "eliding could NOT bring it back under the target, so upcoming
requests may exceed the window. Finish the current step and write durable
results now" (not coping). THIS is the line the operator saw repeated this
week; it is the model being told, by jichi, to write durable results.

**3. Between-turn autocompact (M77/M30/M536).** At the turn boundary (line
5565), if `auto_compact` is on and the calibrated estimate exceeds 80% of
the limit, `do_compact` finds a cut point, summarizes the prefix in
summarizer-context-sized windows via `SUMMARIZE_SYS`, and replaces it with
the `[Earlier conversation summarized to save context]` block. A real model
call. Calibrated against measured usage (M536: byte estimate scaled to real
tokens).

**4. Session persistence.** History is saved continuously (`jc_session_save`,
dirty-tracked at M218), so every session -- compressed or not -- resumes
with `--continue`. The summary block travels with it.

**5. The context relay (M917-M921, shipped, idle).** The "store a plan and
start a fresh session" design, in two parts: (a) a **baton** -- mechanical
notes written on EVERY exit (including crash) plus a model-written account
in one fixed-template call while the model still can; (b) **legs** -- a
fresh session starts from the baton and continues the same task. The
decision core (five triggers of proposal 6.2) is a pure function already
called at every mid-turn compaction point (jc_agent.c:4804, relay_decide);
it returns
CONTINUE on every signal while `config.relay` is 0, the default. Evidence
that the idea works: a hand-driven 80-step task finished in four legs from
the agent's own notes where one un-relayed session stopped at 39. It is the
answer to the operator's question as a *forced* mechanism; what it lacks is
an operator decision, not code.

## What actually happened this week (measured)

Telemetry: `~/.jichi.d/telemetry/jichi-5620300187187864985.jsonl`, this
workstation, 10 sessions (drives, subagent runs, and the self-dev runs that
contain the sessions the operator is reading).

- **705 mid-turn elision passes; 2,309 tool outputs elided to disk.** Every
  heavy self-dev turn ran the sawtooth: estimate climbs past 80%, a pass
  elides to 60%, the turn continues. The pressure note appeared from ~80% up
  to ~96% of the ~196,608-token window on this session's own turns -- the
  repeated `~80%`/`~88%`/`~96%` lines the operator saw.
- **4 between-turn compacts, on OTHER sessions** (turn 3 x2, turn 19, turn
  2) -- none on the long headless self-dev turns, because they were single
  turns.
- **The summarizing autocompact ran zero times on any long headless turn
  this week.** Same shape as the 366-run drive corpus the relay proposal
  measured.
- The `[Earlier conversation summarized to save context]` block that opens
  this conversation was produced by mechanism 3 in a PRIOR (shorter,
  multi-turn) session and resumed into this one by mechanism 4. It is not
  proof the long runs autocompacted; it is proof the short ones did, and
  that the summary persists.

## Honest gaps

1. **Single long headless turn = no autocompact window.** The between-turn
   mechanism is structurally absent from `-p`/drive runs. Elision copes, but
   its sawtooth re-billings (the proposal estimates about seven fresh starts
   in uncached tokens per long loop) and its lossiness (elided text must be
   re-read from disk) are the cost. The relay is the designed fix; it needs
   S4's decisions, and #9 (the telemetric trigger) names exactly this RSS/
   token-pressure shape.
2. **"Write durable results now" is a nudge, not a guarantee.** The pressure
   note asks the model to persist state; this week the model did (PLAN.md,
   memory.md, handoff notes) but that is compliance, not mechanism. A forced
   version -- the relay's baton call with its fixed template -- is what
   converts the nudge into a fence.
3. **No model-side "I should hand over" reflex.** The proposal measured the
   model *imagining* the pressure signal in two of three legs; the trigger
   must be jichi's, from measured numbers. That part is built; it is off.

## Recommendation

Answer S4 (HANDOFF, relay section) -- the load-bearing decisions are #3
(default trigger), #4 (budget variant), #7 (salvage policy), #9 (telemetric
trigger), #10 (baton cap). With the relay armed, the weekly pattern this
page describes (sawtooth at 60-80%, durable-result nudges, resumable
sessions) upgrades to: legs hand over at a clean point with a measured
baton, and the fresh session starts from notes instead of a compressed
summary. The `rss_after_n_turns` driver (the open item from the 09-22 RSS
analysis) is the measurement base for #9.
