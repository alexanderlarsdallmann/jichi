# Annai Appendix A — Reading without a bench

*[案内（あんない）*Annai* — the guided tour](ANNAI.md) · appendix*

The chapters assume a built jichi (Module 0). If you are reading from a
repository browser or a locked-down machine, this appendix gives every
*Prove it to yourself* a read-only twin: a committed artifact to read
instead of an experiment to run, and a note on what the live run would
have added. It routes to files already in the repository wherever one
exists — with one exception, below, kept here on purpose.

## The one inline artifact: a captured turn (chapters 1–2)

This is a real, unedited event stream from the built binary — produced
against the test suite's scripted mock model (chapter 9 explains
`tests/tools/mm_core.h`), so it is deterministic and involves no network.
The prompt was *"read note.txt and count the words"* over a file
containing `three mice ran`, with `--auto --no-session --output jsonl`:

```json
{"v":1,"type":"message_start","model":"mock","mode":"auto"}
{"v":1,"type":"tool_call","name":"read_file","args":"{\"path\":\"note.txt\"}"}
{"v":1,"type":"tool_result","name":"read_file","is_error":false,"preview":"     1\tthree mice ran\n"}
{"v":1,"type":"message_start","model":"mock","mode":"auto"}
{"v":1,"type":"text","delta":"The note contains three words."}
{"v":1,"type":"usage","input":20,"output":5,"cost":0}
{"v":1,"type":"done","text":"The note contains three words.","model":"mock","tokens":{"input":20,"output":5},"cost":0,"tool_calls":1,"aborted":false,"stop_reason":"done","work_kept":true,"starved":false,"peak_input":20,"cache":{"read":0,"write":0},"tools":{"read":1,"write":0,"shell":0,"other":0}}
```

Annotate it against chapter 2's sequence diagram: two `message_start`
events (two model calls — one per round), the `tool_call`/`tool_result`
pair between them, the numbered-gutter preview (chapter 5's
`jc_format_numbered` at work), and the terminal `done` object carrying a
structured `stop_reason`. What the live run adds: real token counts from
a real model, and many small `text` deltas where the mock sends one —
the chunking of chapter 6, which a canned transcript cannot show
honestly. *(Why inline and not a fixture file? A transcript in prose can
be annotated; the lint that guards this guide checks paths and
functions, not JSON — so the appendix says plainly: this block was
captured on 2026-08-01 from the M223 build, and rot here is possible.
The `"v":1` on every line is the version field that makes such rot
detectable.)*

## Chapter-by-chapter twins

- **Ch. 1 (build & poke).** Read `CLAUDE.md` top to bottom — it is the
  orientation `doctor`/`describe` would give you, in prose. The repo map
  the chapter has you generate is described (with its size bounds and
  what it skips) in `docs/REPOMAP.md`.
- **Ch. 2 (a turn outside).** The transcript above. The full event
  vocabulary — every `type`, every field, the exit codes — is
  `docs/SCRIPTING.md`; the builders are
  `src/util/jc_agentjson.c:jc_agentjson_event`, and their unit tests
  assert the exact shapes.
- **Ch. 3 (startup).** In place of `--verbose`: read
  `src/main.c:run_headless` for the dispatch, and `docs/DOCTOR.md` for
  what a healthy startup checks. The fan-in claim (one loop, three
  front-ends) is verifiable read-only: search `src/tui/jc_tui.c`,
  `src/main.c`, and `src/acp/jc_acp.c` for `jc_agent_run_turn` — three
  callers, one callee.
- **Ch. 4 (the gate).** In place of the `--readonly` refusal run: the
  verdict table lives in `docs/AGENT_MODES.md`, and the pure
  `src/chat/jc_perm.c:jc_perm_for_tool` is short enough to execute in
  your head against it. The self-healing catalog's motivations are in
  the comments at `src/tools/jc_tool.c:jc_toolcall_scan` and
  `docs/ANECDOTES.md` #15.
- **Ch. 5 (guards).** The ambiguity experiment's outcome is specified by
  its unit tests: `tests/test_patch.c` walks exact, whitespace-drifted,
  and ambiguous matches — read the assertions as the transcript you
  did not produce. The measured motivation for line numbers in the
  error is the M208 entry in `CHANGELOG.md`.
- **Ch. 6 (the wire).** `tests/test_provider.c` builds both dialects'
  requests and feeds synthetic SSE events — the whole pipeline, socket
  amputated. Read one streaming test as the packet capture you did not
  take.
- **Ch. 7 (memory).** In place of a live `/context`: the measured tables
  in `docs/analysis/2026-07-29-tool-arena.md` (the 55 MB → 15 MB peak)
  and `docs/analysis/2026-08-01-telemetry-memory.md` (the post-fix A/B
  rows) are the same gauges, at incident scale. The RAM map itself is
  `docs/LOW_MEMORY.md`.
- **Ch. 8 (compaction).** The marker formats are visible in the source
  constants around `src/chat/jc_compact.c:jc_compact_midturn`, and
  `docs/COMPACTION.md` shows the summarized-prefix shape a session file
  would contain.
- **Ch. 9 (tests).** Already a reading chapter; the run you skip is
  summarized honestly by the latest milestone's "Gates:" lines in
  `docs/ROADMAP.md` — check counts, driver counts, and what was observed
  red.
- **Ch. 10.** Needs no bench — though its contribution walk does, and
  that is the appendix's honest limit: reading gets you oriented;
  rule 1 of chapter 9 ("shown to fail") cannot be performed by reading.
  When you get a machine, start there.
