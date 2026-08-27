# Auto-compaction

Long sessions grow an ever-larger message history. Eventually the history no
longer fits the model's context window (or the cost per turn becomes silly).
**Auto-compaction** keeps a session running indefinitely by summarizing the
older part of the conversation into a single compact note and discarding the
raw messages it replaces, while preserving the most recent turns verbatim.

This mirrors what the original Continue CLI (and Claude Code) do: when the
history gets large, fold the old part into a summary and carry on.

## When it fires

Auto-compaction runs **between turns** — at the start of `jc_agent_run_turn`,
after the new user message is appended but before any model call. It applies to
the top-level agent only (subagents are short-lived and bounded).

The trigger is a token estimate of the current history (plus a fixed allowance
for the system prompt and tool schemas). We have no real tokenizer, so the
estimate is a byte heuristic: roughly `bytes / 4` tokens per message, with a
small per-message overhead. When the estimate exceeds
`COMPACT_TRIGGER` (0.8) × the effective context limit, compaction runs.

The effective context limit is resolved as:

1. top-level `"contextLimit"` (if > 0), else
2. the active model's `"contextLength"` (if > 0), else
3. a built-in default (`JC_COMPACT_DEFAULT_LIMIT`, 32000 tokens).

Set `"autoCompact": false` to disable it entirely.

## What it does

1. Pick a **cut point**: walk back from the end, keeping the most recent turns
   that fit within `COMPACT_KEEP` (0.35) × the context limit, and snap the cut
   to a **user-message boundary**. This guarantees the kept tail starts with a
   user message and never splits an assistant→tool-result group, so the
   provider request stays well-formed (Anthropic in particular requires the
   first message to be `user` and every `tool_use` to be answered).
2. **Render** messages `[0, cut)` as a plain-text transcript (per-message
   content is truncated so a few huge tool outputs can't blow up the summary
   request).
3. **Summarize** the prefix, **chunked to the summarizer's own context**. The
   summarizer is the model with the `summarize` role if configured, else the
   active chat model. Because that model may have a *much smaller* context than
   the active model (e.g. a cheap local summarizer for a 128k coding model), the
   prefix is split into windows that each fit the summarizer's `contextLength`
   (`summarizer_input_tokens`, ≈ 3/5 of its context; falling back to
   `JC_COMPACT_SUMMARIZER_DEFAULT` = 8192 when undeclared), each window is
   summarized (with an explicit `max_tokens` for the output), and the partial
   summaries are folded into one. A context-overflow **HTTP 400** from the
   endpoint triggers a halving retry rather than aborting. This is the M30 fix
   for the failure where a ~50k-token prefix (sized to the big active model) was
   sent whole to an 8k-context summarizer and 400'd.
4. **Rewrite** the history: drop messages `[0, cut)` and prepend the summary
   (wrapped in a clear marker) to the content of the first kept message (the
   user message at the cut). No extra message is inserted, so role alternation
   is preserved.

If the summarization call fails for any reason, compaction is abandoned and the
turn proceeds with the full, uncompacted history — auto-compaction is an
optimization, never a correctness dependency.

## The compacted history

After compaction the first user message looks like:

```
[Earlier conversation summarized to save context]
<summary text>

[Most recent request follows]
<original text of the first kept user message>
```

The summary persists with the session, so a `--resume`d session stays compact.

## Manual compaction

In the TUI, `/compact` forces a compaction immediately, ignoring the threshold
(but still a no-op if there's nothing old enough to fold away). Useful before a
big request when you know the history is bloated.

## Seeing the budget (`/context`, M41)

`/context` (TUI) and the `context` subcommand print where the context budget is
going, so a surprise compaction or prompt bloat is diagnosable:

```
Context window: model claude-opus-4-8, limit ~200000 tokens

  system prompt     ~3200
    repo map        ~1500
    rules            ~800
    skills           ~200
    craft            ~419
    memory           ~120
    glossary          ~40
  tool definitions  ~4500  (28 tools)
  history           ~12000  (46 messages)
  ----
  total             ~19700  (9% of limit)

Auto-compaction: on; triggers when the conversation reaches ~80% of the limit (~160000 tokens).
```

**The system prompt's sections account for all of it (M312).** They are listed
**largest first** (the report answers *what do I cut*, not *what is in here*) and
**non-zero only** — a section that contributed nothing prints no line, because fourteen
zeroes would bury the two lines that matter. The counts come from
`jc_sysmsg_build_parts`, which records each section's size **as it appends**, so they are
the same bytes the model receives, M73 fit caps included.

That last point was a second, quieter bug in the old line: it measured the raw
`app->rules`, so whenever the fit cap bit it described text that was not in the prompt.
The old line also named only six sub-parts against a stated total, leaving "the base
persona + section headers" unaccounted — which explained nothing at all on a graded
`attempt`, where rules and repo map are both absent and the remainder *is* the prompt.

Summing the printed lines can land a few tokens under the stated total: each line is an
independent integer division, so it loses at most one token per line. The invariant is
exact in bytes.

The sixteen possible sections are `persona`, `craft`, `safety`, `environment`,
`extra prompt`, `language`, `output style`, `rules`, `design`, `constraints`, `memory`,
`glossary`, `board`, `repo map`, `assignment`, `skills`.
[What the breakdown found when it was first switched on.](analysis/2026-08-06-sysmsg-breakdown.md)

It's built by `jc_context_report` (`src/chat/jc_context.c`), which sizes the
built system prompt (and its individually-measurable parts), the serialized tool
definitions, and the history using **the same `jc_compact_estimate_text` /
`jc_compact_estimate_tokens` byte heuristic** that drives the trigger — so the
numbers line up with when compaction actually fires. The `context` subcommand has
no live conversation, so history reads 0 and tools reflect the **built-in** set
(the TUI shows the full live registry + running history). Read-only; no model
call.

**The tool line is what will actually be sent (M310).** It applies the resolved tool
profile's fence — the same `jc_config_tool_profile_core` call, with the same limit, that
`jc_agent_run_turn` makes — and says so when the lean set is in play:

```
  tool definitions  ~1193  (7 tools, core profile)
```

### Per-tool sizes: `context tools` (M313)

The one tools line stays one line. For the breakdown, ask:

```
$ jichi context tools
Tool definitions: 16 advertised, ~2915 tokens (full profile)
  (~5 of that is JSON array framing, charged to no tool)

  tokens   share   cum.   core  tool
     381     13%    13%        spawn_subagent
     345     11%    25%        spawn_parallel
     343     11%    36%    *   apply_patch
     262      9%    45%        todowrite
     202      6%    52%    *   run_terminal_command
     ...
* = kept by the core profile: 7 of 16 tools, ~1161 tokens (39% of the above).
```

**Why a separate view and not more lines in `/context`** — deliberately the opposite of
what M312 did for the system prompt, because the measured shape differs. There, six lines
explained a 15,196-token block whose top item was 10,441: the lines *were* the answer.
Here sixteen lines would explain a ~2,900-token block whose largest item is ~380 —
**top 5 is 49%, largest 12%**, death by a thousand cuts — and printing them every time
would push the system-prompt lines off the screen.

The **cumulative** column exists because that flatness is the finding: without it a reader
sees `13%` at the top and goes looking for one fat tool to delete. The **`core` marker and
footer** answer the question that actually follows — *what would `--tool-profile core` save
me* — instead of leaving it to be cross-referenced. The view respects the resolved fence,
so under `core` it lists the seven that are sent and says so.

It states the **same total** as `/context`, and names the difference from the per-tool
column (the JSON array's own brackets and commas, which belong to no single tool). Two
reports on one thing must not show two totals.

`jichi context tools` needs no model and no network.

**The subcommand now advertises what a real turn does (M325b).** It used to build only the
*unconditional* built-ins — 16 tools where a live session in a git repo had 26 — so the report
under-stated the very cost it exists to measure, and every conditional tool added since M41 had
widened the gap silently. `jc_tool_register_configured` is now shared between `main()`, the
`context` subcommand and `doctor`, so all three advertise the same set.

The one exception is **MCP**: listing those tools means connecting to every configured server,
and a read-only report must not. So the report *says so* rather than letting the count look
complete:

```
Not counted: tools from 2 configured MCP server(s) -- listing them would require
connecting. A live turn advertises those too.
```

`tests/smoke/context_tools_live.sh` holds the report against a captured request in **both**
directions, plus a raw-vs-distinct count so a duplicate registration cannot hide.

#### Paid-for vs called (M314)

When a telemetry log for this workspace exists, the listing gains a `calls` column and a
footer:

```
  tokens   share   cum.   core  calls  tool
     381     13%    13%           0   spawn_subagent
     262      9%    45%           2   todowrite
     ...
Use: telemetry-2026-08-06.jsonl, this workspace, 34 turns, 210 tool calls.
9 advertised tools were never called in it, costing ~1240 tokens on every model call.
A tool can be rare and still right, and this cannot see a tool the model was never
able to call -- treat it as a question, not a verdict.
```

A tool definition is **not** billed per use: it sits in the request prefix and is paid on
every model call, used or not. So the number that matters is *what you pay every call*
against *how often it earned it* — which is why there is no per-call price column, since
dividing one by the other would invent a figure.

The join is automatic, and its **absence is stated** rather than shown as zeroes:

- **No log** (telemetry is off by default) → "which of these were actually called is
  unknown", with the flag that starts recording. A column of zeroes would tell most users
  they use none of their tools.
- **A log with no events for this workspace** → the same stated absence. Events are stamped
  with the workspace root (M56), and "I never call this *here*" is a per-project claim.
- **A short log** → the turn and tool-call counts are printed next to the conclusion, so
  one turn's worth of evidence can be discounted by the reader.
- **Rare is not useless**, and a tool the model was never *able* to call (denied by
  permissions, or a schema it could not use — cf. M285's dead fence entries) looks
  identical here to one nobody wants. The report says so and gives no advice; advice is
  [`doctor`](DOCTOR.md)'s job.

---

Before M310 the report sized the *unfenced* array, so under `toolProfile: core` (which
`auto` reaches by itself under `--lite` or below a ~12,000-token effective context) it
over-reported the one line a user opens the report to read. The unsent definitions are
deliberately **not** also shown: the report answers *where is my window going*, and the
window is spent on what is sent. What `core` withholds is `doctor`'s job — it names the
resolved profile and warns about configured MCP/user/LSP tools that `core` drops.
[Measured cost of the profile](analysis/2026-08-06-tool-profile-cost.md).

**The subcommand loads the prompt's assets too (M311).** It used to print `rules ~0`
always — it is dispatched before `main()`'s asset load and did not do its own — so on
this repository it reported a **~750**-token system prompt against a real **~15,200**
(rules ~10,400, repo map ~4,000): 11% of a 32k window instead of 57%, before a single
message. It now loads rules, memory, glossary, repo map, skills, styles and the design
spec through the same helper `sysmsg` uses, so **the report and the prompt cannot
describe different prompts** — `tests/smoke/context_assets.sh` asserts that the
subcommand's system-prompt figure *is* the byte estimate of what `sysmsg` prints.

One consequence worth knowing: with `repoMap` on, `jichi context` now walks the
workspace to build the map, exactly as a real turn does. It is slower on a large repo
than it was when it was wrong. Still read-only, still no model call.

### Where the history went: `context history` (M315)

The history is the part of the window that **grows** — the part compaction exists for, and
the part that dominates a long `--auto` run. It used to be one number and a message count.

```
$ jichi context history            # or: context history <session-id|prefix>
History: session 99dda8d4-…, 46 messages, ~12000 tokens

  by role
    tool results    ~9100     (76%, 21 messages)
    assistant       ~2100     (17%, 18 messages)
    user             ~800      (7%, 7 messages)

  tool output, largest first (of ~9100 tokens of tool results)
    search_code         ~5200     (57%, 6 calls)
    read_file           ~2600     (28%, 9 calls)

  largest single messages
    ~3100     tool result  search_code          (message 22)
    ~1400     tool result  read_file            (message 14)
```

It answers three questions, in the order they are useful: **which tool's output is filling
the window**, how the total splits between you / the model / tool results, and whether
**one enormous message** is the whole story (it often is, and then the fix is that message
rather than a policy).

- **The `context` subcommand has no live conversation**, so this reads a **saved session** —
  the most recent for this workspace, or an explicit id/prefix, resolved exactly as
  `export` and `--continue` do. That is the better surface anyway: "what filled my window
  on that run?" is asked *after* the run, often about an unattended one, and the session
  file is the artifact that survives it. Sessions are saved every turn, so a live session
  lags by one.
- **The role block sums exactly to the total.** Per-message sizes come from
  `jc_compact_estimate_message` — the exact term the compaction trigger sums — so the
  breakdown cannot drift from the line it explains.
- **The per-tool block states its own base**, because it covers only the tool results. A
  percentage that silently changes denominator between blocks is how a report stops being
  trusted.
- **Tool results are attributed by name** via `tool_call_id`, searched *backwards* from the
  result. A `tool_call_id` is only unique within one provider response, and a local backend
  that numbers calls per response reuses `c1` on every turn — a forward scan charges the
  whole session to whichever tool the first such call was. (That is not hypothetical: it is
  what the first run of `tests/smoke/context_history.sh` reported.) A result whose call is
  no longer in the history — compaction dropped the prefix — is charged to `(unknown)`, a
  visible bucket rather than a silent shortfall.
- **Message indices are printed**, so a size can be located in `export --output json`.

**In the TUI it is `/context history`** (M317), and there it is *better* than the
subcommand: the TUI holds the **live** history, so there is no one-turn save lag and it works
before the first save. An empty history says `is empty` rather than printing a block of
nothing, and an unknown view (`/context nosuchview`) is refused rather than silently falling
back to the budget report — a typo must not look like it worked.

No advice, and no content: sizes and names only. `export` prints transcripts.

### When mid-turn compaction cannot reach its target (M323)

Mid-turn compaction's lever is **large tool results**: it elides the oldest big ones down to
`MIDTURN_TARGET_PCT` of the limit. That works when the window is filled by a few fat outputs.
It does **not** work when the window is filled by *thousands of small* ones — there is simply
nothing large left to elide, and the request goes out over budget anyway.

This is measured, not hypothetical. In one 34,216-event workload
([analysis](analysis/2026-08-06-large-workload-telemetry.md)) the pass ran **1,038 times** and
**3.1% of calls still exceeded the configured `contextLimit`** — the largest by **1.36× the
model's declared window** — with tool output at p99 = 14 KB and exactly **one** result above
100 KB in 13,783 calls. 70% of the over-budget calls came within six events of a compaction
that had just run.

**None of that was visible.** The `compact` event recorded only `elided`/`dup`/`age`, and was
emitted *only when something was elided* — so the case that matters most produced **no event at
all**. Since M323 the event carries the whole decision, in the same calibrated real-token terms
the trigger uses:

```json
{"event":"compact","phase":"midturn","elided":0,
 "before":1661,"after":1661,"target":540,"limit":900,"short":true}
```

- **`short: true`** — the pass ran under pressure and did not get under `target`.
- **an event is emitted whenever the trigger fired**, elisions or not.
- **once per turn** jichi also warns on stderr, and streams an `on_status` line — the condition
  persists for every remaining round, so warning per round would bury the one fact that matters.
- `jichi telemetry` reports the total: *"Compaction SHORT: N mid-turn pass(es) could not reach
  the target"*.

**What to do about it, today.** This is deliberately the observability half only — what jichi
*should* do when it cannot reach the target (drop whole messages? summarize mid-turn? refuse the
call?) is an open design question, recorded in [DEFERRED.md](DEFERRED.md) rather than guessed
at. What helps now:

- **Treat `contextLimit` as a target, not a guarantee**, and leave headroom below the model's
  real window rather than setting it at the edge.
- **Find the number rather than guessing it, where the server publishes one.** A
  **LiteLLM** gateway serves `GET /v1/model/info`, whose per-model `model_info` block
  carries `max_input_tokens`; the standard `/v1/models` carries nothing of the sort.
  `jichi doctor` now reads it for the active model and compares it against what your
  config declares, so a wrong `contextLength` is something you are **told at setup**
  instead of something you infer afterwards from a run that compacted for no reason.
  Declare `max_input_tokens`, **not** `max_input + max_output`: this budget is the
  prompt side only, and the 0.8 trigger is already the headroom. Servers without that
  endpoint (LM Studio, plain vLLM, the cloud providers) are reported as lacking it
  rather than as a problem -- including the one that answers HTTP **200** with an
  error object, which is why the check reads the body's shape and not the status. See
  [analysis/2026-08-19-gateway-published-context-windows.md](analysis/2026-08-19-gateway-published-context-windows.md).
- **`setup` can record it for you.** `jichi setup --context-length <tokens>` writes
  the per-model `contextLength` key, and stays entirely offline while doing it --
  which is the mode to use from a script or from an agent driving another jichi,
  because the run is then reproducible and needs no egress.
  `--context-length auto` is an explicit opt-in to one request: it reads what the
  server publishes and says which number it used, or says plainly that the server
  publishes none and that jichi will therefore assume 32000. Note the near-twin
  flags mirror the two config keys exactly: `--context-length` is the per-model
  window, `--context-limit` the top-level budget that overrides it.
- **Declare `contextLength` honestly.** In the measured workload the declared 150,000 was
  conservative, which is the only reason 148 over-budget calls did not become HTTP 400s.
- **Reduce the number of results, not just their size.** A history of thousands of small tool
  results is usually a workload leaning on the shell for everything (that one was 56%
  `run_terminal_command`); a first-class tool call that returns one answer costs one result.
- **Watch for the warning in a long `--auto` run.** It means the budget you set is no longer
  being enforced, which is worth knowing before a provider enforces it for you.

## Configuration

```json
{
  "autoCompact": true,
  "contextLimit": 32000,
  "models": [
    { "provider": "openai", "model": "gpt-x", "contextLength": 128000 },
    { "provider": "openai", "model": "summarizer", "roles": ["summarize"] }
  ]
}
```

- **autoCompact** — master switch (default `true`).
- **contextLimit** — top-level override of the token budget used for the
  trigger; `0`/absent falls back to the model's `contextLength`, then the
  built-in default.
- **contextLength** (per model) — the model's context window in tokens.
- A model with the **summarize** role is used for the summary call; otherwise
  the active model summarizes itself.

## Pure core / orchestration split

Following the repo convention, the testable logic is pure:

- `jc_compact_estimate_tokens(hist)` — byte-heuristic token estimate.
- `jc_compact_find_cut(hist, keep_tokens)` — the user-boundary cut index (0 if
  there's nothing worth compacting).
- `jc_compact_render_transcript(hist, end, arena)` — transcript text.

`jc_compact_run` orchestrates these plus the model call and the history rewrite,
and is exercised end-to-end against a real model.

## Limitations

- **Between-turn summarization; mid-turn elision.** The model-summarizing
  compaction above runs only *between* turns. A single turn whose own tool churn
  overflows the window can't take a summary prefix cleanly (the tail ends on tool
  results awaiting a reply), so mid-turn it is handled by **elision** instead of
  summarization — see "Mid-turn compaction" below.
- The token estimate is a heuristic, not a real tokenizer; `COMPACT_TRIGGER`
  leaves headroom to absorb the error.
- Compaction fires one summary call per prefix window (plus a fold pass when
  there are several), adding latency/cost at the moment it fires. A summarize
  model with a small `contextLength` produces more windows; if it is also slow,
  prefer giving the `summarize` role to a faster or larger-context model.

## System-prompt fitting (M73)

Compaction trims *history*, never the *system prompt*. So on a model whose real
context window is smaller than `rules + repo map + tool definitions`, every turn
overflows before history even matters — a big `CLAUDE.md`/`AGENTS.md` plus the
repo map can blow a small window on its own.

`jc_sysmsg_build` guards against this: the pure `jc_sysmsg_fit_caps` bounds the
two largest shrinkable sections (the instruction files, then the repo map) to a
fraction of the effective context budget (`jc_compact_context_limit`), truncating
each with a `[... truncated to fit the model context window ...]` note. It is a
no-op when they already fit (the common case is unchanged) and when the budget is
unknown.

The fit only engages if jichi *knows* the real window:

- Declare `"contextLength"` on the model (best), **or**
- set the top-level `"contextLimit"` config key, **or**
- pass `--context-limit <tokens>`.

**Prefer declaring `contextLength` per model and leaving `contextLimit` unset.**
The advice that used to sit here — set the limit to roughly *half* the server's
real window, because the byte heuristic runs optimistic — was written before M286
fixed the calibration basis. The estimate is now scaled by a measured per-model
ratio within the first turn, so halving the window on top of that is
double-counting: it throws away context you paid for and triggers compaction that
is not needed. Two further reasons to avoid the global key: it **overrides every
model**, so it flattens a routing tier design (a 256k `strong` next to a 150k
`fast` becomes 128k/128k and context-pressure escalation can never fire — `doctor`
warns about exactly this, M288); and a per-model `contextLength` is a *fact* about
the model, which jichi can reason about, where `contextLimit` is a *budget*, which
it must simply obey. Use it when you deliberately want a tighter budget than the
model allows — not as a safety margin. `doctor` warns when the active model declares no
`contextLength` (jichi then assumes the default and can overflow silently) and when
the instruction files are large for the effective window. When a server returns
its overflow message as completion content (HTTP 200, so it can't be caught on
the wire), the CLI recognizes the signature and prints a one-line fix hint to
stderr.

## Tool profile (M74)

The fit handles rules + repo map, but the **tool definitions** are also a large,
fixed cost (~16 tools). On a small window they crowd out room for the
conversation. The `toolProfile` config (`--tool-profile`, TUI consults the same
resolver) selects how many tools the top-level turn advertises:

- `full` — every registered tool (the default behavior on a large model).
- `core` — the lean set: `read_file`, `write_file`, `edit_file`, `apply_patch`,
  `list_files`, `search_code`, `run_terminal_command` (plus `load_skill`, always
  exempt). The heavy/rare tools (git_*, LSP nav/refactor, subagents, fetch/web,
  media, todo, MCP resources, run_tests) are dropped — `run_terminal_command`
  still covers running tests and git by hand.
- `auto` (default) — `core` when lite mode is on **or** the effective context is
  known and below `JC_TOOL_PROFILE_AUTO_BELOW` (12000 tokens); else `full`.

So setting `contextLimit` (or `--context-limit`) below ~12k on a small server
*also* trims the toolset automatically. It reuses the same per-run tool
allow-list the subagent profile fence uses (`jc_tool_core_allow` +
`jc_tool_allowed`); the pure resolver is `jc_config_tool_profile_core`. `doctor`
reports the resolved profile. Note: in `core` mode `spawn_subagent`/
`spawn_parallel` are dropped, so there is no sub-agent fan-out on a small-context
model (which is the right call — they'd each carry the same heavy prompt).

## Mid-turn compaction (M76)

Between-turn compaction can't help a *single* turn whose own tool churn overflows
the context window — a long agentic turn (dozens of tool calls, each with verbose
output like a build log) grows the in-flight history mid-turn, and a model server
rejects the next request with a context-overflow error (the recurring "HTTP 400"
seen on long `--auto` runs; ANECDOTE-style, the conversation climbed to ~150k on a
150k-window model before erroring).

`jc_compact_midturn` (called from the agent loop after each round of tool results,
at any depth) handles this **without a model call**: when the in-flight history +
a system/tools allowance crosses `MIDTURN_HIGH_PCT` (80%) of the effective context
limit, `jc_compact_trim_tool_output` **elides the content of the oldest large
tool-result messages** — replacing the middle with a `head + "[N bytes … elided
to fit the context window] …" + tail` marker — until the estimate drops to
`MIDTURN_TARGET_PCT` (60%) or only the last `MIDTURN_KEEP_RECENT` (6) messages
remain untouched.

Key properties:
- **Structure-preserving.** It only *shrinks* tool-result content; it never drops
  messages, so every `tool_call` keeps its paired `tool_result` and the request
  stays well-formed (unlike dropping a message mid-turn).
- **Lossy but bounded.** Old verbose tool output (which the agent has already
  acted on) loses its middle; recent results and all message *structure* survive.
- **The marker is a claim ticket (M348).** Each lossy elision writes the FULL
  original content to the M339 preservation store (`~/.jichi.d/tool-output/`,
  0600, fence-readable, cleaned at teardown — tickets are turn-lived) and the
  marker names the path: *"the complete result is preserved at PATH — read or
  search THAT path instead of re-running the call"*. Before M348 the marker was a
  receipt with no ticket, and the model that still needed the bytes re-ran the
  original call at full price — the measured re-read loop (72% of reads were
  re-reads, one path 216×, and 82 of 142 advisory-firing re-reads immediately
  followed another `read_file`). Preservation failure falls back to the old
  ticketless marker, byte for byte — the model is never given a path that is not
  there. The superseded-read dedup below stays ticketless on purpose: it is
  zero-loss by construction, so a ticket would duplicate the store. The `compact`
  telemetry event carries `preserved:N`.
- **No latency.** Pure string work — no summarizer call mid-turn (important on a
  slow endpoint, and it must be fast since it runs every round once large).
- **Observable.** Logs `[compact] mid-turn: elided N …`, fires `on_status`, and
  emits a `compact` telemetry event with `phase:"midturn"`.

**Argument-side elision (M218).** The result-side trims never touch the *other*
half of a marathon turn's history: the assistant messages' own
`tool_calls[].arguments_json`, which for the mutating tools
(`write_file`/`apply_patch`/`edit_file`) carries a **full file body per call**.
A telemetry pass over a long unattended workload (single turns with 200–340
tool calls, where between-turn compaction never runs) showed that side growing
monotonically for the whole turn. When the result-side passes leave the
estimate above target, `jc_compact_trim_tool_args` replaces an old call's
oversized arguments (over the same `ELIDE_MIN_BYTES` threshold, same
keep-recent window) with a compact marker that **must be a valid JSON object**
— the Anthropic serializer re-parses `arguments_json` (invalid JSON degrades to
`_unparsed_arguments`) while the OpenAI one emits it verbatim:

```json
{"elided":"PLACEHOLDER, not arguments: 52341 bytes of arguments were dropped from your context to save space and cannot be recovered. To repeat this call, send its real arguments.","path":"src/foo.c"}
```

**Why the note is a directive (M289).** It used to read
`"arguments (52341 bytes) elided mid-turn to fit the context window"` — a
description. But this object sits in the **arguments slot** of history, which the
model reads as an example of what a call to this tool looks like, and it copied the
shape straight back: on one measured run **18 of 19 argument-shape failures were
this marker arriving as real tool arguments**, across `edit_file`, `write_file`,
`todo_write` and `run_terminal_command`. One came back paraphrased
(`"elided mid-turn to request"`), a wording that exists nowhere in jichi — which is
what proves the model retyped it rather than jichi mis-replaying it. Each cost a
full uncached round-trip and was answered with a generic
`error: 'path', 'old_string', and 'new_string' are required`, which explained
nothing and invited the same mistake.

So the note now says plainly that it is not arguments and what to do instead, and
the tool layer **detects the placeholder** and answers with that instead of the
generic shape complaint (naming the file, when the marker carried one). There is
nothing to repair — the real arguments are gone by construction — so a precise
error is the whole fix. It is counted as an `args_repair` with
`kind:"elided_placeholder"` and `ok:false`, so the rate stays visible rather than
hiding inside the generic failures. `JC_COMPACT_ELIDED_KEY` is shared between the
writer and the detector so the two cannot drift.

The original `path` (or an `apply_patch`'s first edit path) is preserved so the
model still knows *what* it wrote; `id`/`name` and the paired tool result are
untouched, so pairing stays intact on both wires. The size threshold doubles as
the idempotence guard and inherently protects `read_file`/`search_code`
arguments (tiny), which the superseded-read dedup parses for paths. Elided
markers persist into the saved session — a resumed session keeps them (and the
smaller file). The `compact` event gains an `args` count beside `dup`/`age`
(`dup + age + args == elided`).
- **Measurable (M192).** The `compact` event splits `elided` into `dup` (this
  zero-loss pass) and `age` (the lossy fallback), and `telemetry` renders a
  `Compaction reclaim` line. Previously the dedup's effect reached only a `jc_logf`
  line on stderr, so a log could not say whether the zero-loss mechanism was doing the
  work or the lossy one was — which is a difference worth knowing.
- **Cuts on character boundaries (M191).** The head and tail cuts go through
  `jc_utf8_trunc_len` / `jc_utf8_resync`, never a raw byte offset. Byte 400 of a
  tool result can fall inside a multi-byte character, and keeping only its first
  byte makes the whole request body ill-formed UTF-8 — which a strict server
  rejects with a 500 *in 40 ms*, and keeps rejecting for the rest of the run,
  because the broken byte now lives in the history. That is not hypothetical: it
  wedged a zigodot drive on an em-dash in a design document
  (docs/ANECDOTES.md #22). Two chokepoints back this up regardless of producer —
  `jc_history_add`/`jc_msg_set_content` sanitize message content on ingest, and
  `jc_prov_print_body` sanitizes the finished request body — replacing an
  ill-formed byte with U+FFFD and warning, rather than sending a rejected request.

**When the eager dedup runs (M218).** A read becomes superseded only when a
*later* read of the same path lands, so the eager pass now runs only on the
first mid-turn call of a run (a resumed history may carry pre-existing
duplicates) and after a round that appended a `read_file` result — the
outcome is bit-identical to the old every-round pass, without its cost (the
per-message argument parses summed to ~165k JSON parses across one analyzed
marathon turn). The originating-call lookup also scans backward from the
result to its adjacent assistant message instead of forward from the start
of history.

**Superseded-read elision first (M93).** Before the age-based pass,
`jc_compact_midturn` runs `jc_compact_trim_superseded_reads`, which elides a
`read_file` result whose path **and requested range** is read **again later** in
the history — that later read carries this copy's content, so the earlier copy is
pure duplication. Its marker names that reason (M354): *"elided: superseded — a
newer read of this same file appears LATER in this conversation; use that instead
of re-reading the file"*. Until M354 it borrowed the pressure pass's "elided to
fit the context window" text — false twice over for the eager dedup (which runs at
budget 0, under no pressure) and silent about the one fact that stops the re-read
loop: the newer copy is still in context. This is
targeted at the biggest observed waste on a cacheless backend: a telemetry pass
found **84% of `read_file` calls were repeat reads** of a file already read (one
file read 93×), each re-injecting ~20k tokens and lingering until eviction. (Read
that figure with care: it counted repeats **by path**, so it includes a model
paging through one large file, which is not waste. A later log measured 55% by
path but only **14% by path-and-range** — see M287 below. The pass is still worth
having; the headline number overstated its target.) Unlike
the age-based pass it is **zero information loss** — the newest read of every file
is kept — so duplicates are dropped *before* any unique output is touched. It uses
the same head+tail+marker format (shared `elide_tool_msg` helper) and idempotence
guard, respects `keep_recent`, and stops at the budget target. Read-after-read only
(edit/write supersession is deferred: an `edit_file` result is a diff, not the full
post-edit file, so eliding the pre-edit read there would lose content).

**Path identity is spelling-independent (M192).** The match used to be a raw `strcmp`
on the `path` argument, and the model reads the same file both ways within one session
— a dogfood log has `src/gdscript/vm.zig` (91×) *and*
`/home/…/src/gdscript/vm.zig` (232×). Each spelling therefore deduped separately and
kept its own full copy: measured, 338 distinct path strings for 334 distinct files, so
4 files held a redundant resident copy (~94 KB, ~24k tokens re-billed per later call).
Paths are now compared after the pure `jc_path_normalize` (join a relative path against
`app->cwd`, collapse `//` and `.`). Two deliberate choices: it is **not**
`jc_path_resolve`, because that calls `realpath()` and this pass is pure, runs every
tool round, and is unit-tested offline; and `..` is **refused rather than collapsed**,
because `a/link/../b` is not `a/b` when `link` is a symlink and a false identity match
would elide a read that was never superseded — losing information in the one pass whose
whole claim is that it loses none. A refused path keeps its raw spelling, so the worst
case is the pre-M192 missed dedup.

**A read's identity includes its RANGE (M287).** The paragraph above worries at
length about a false identity match from path *spelling* — and the same pass had a
much larger one hiding in plain sight: identity was the path **alone**, so a model
paging a large file (`limit:100`, then `offset:100,limit:150`, then
`offset:250,limit:100`) had its first page elided the moment the second landed, on
the stated grounds that "the later read carries the current content". For a paged
read it does not; it carries different lines. The pass whose entire justification is
zero information loss was discarding content the model still needed, and one
project's log holds **909 paged reads**. The identity key is now
`<normalized path>\n<offset>:<limit>`, so only a re-read of the *same slice*
supersedes; the M93 whole-file case is unchanged. Range values go through the shared
`jc_json_get_num_lenient`, so `{"limit": "100.0"}` keys identically to
`{"limit": 100}` and cannot drift from what `read_file` itself resolved.

This very likely fed the re-read behaviour it was meant to reduce: **82 of 142**
advisory-firing re-reads in that log immediately followed *another* `read_file`,
which is the shape of a model re-fetching pages that had been silently deleted
behind it.

**M94 runs it eagerly.** Because the pass is zero-loss, `jc_compact_midturn` runs
it **every tool round, before the 80% high-water check** (with `budget_tokens = 0`,
so it elides *all* superseded reads, not just enough to hit a target). On a
cacheless backend the context ramp happens well below 80%, so waiting for the
high-water would re-bill every duplicate for many turns first; eager elision keeps
duplicates from ever accumulating. The eager pass is quiet (an INFO log, no
`on_status`, since it runs every round); only the pressure-triggered age-based pass
emits the "compacting" banner. This was chosen over a read-time cache (returning a
"[unchanged since your read above]" stub instead of the file) which risks dangling
references — a stub pointing at a copy a later compaction may elide.

It requires a *known* budget (`contextLength`/`contextLimit`/`--context-limit`);
with an unknown limit it's a no-op (like the fit). The pure
`jc_compact_trim_tool_output` and `jc_compact_trim_superseded_reads` are
unit-tested (`tests/test_compact.c`).

## The exhaustion latch (M361)

Reclaim decays to zero after ~2 lossy passes in a turn (measured medians:
10,324 → 1,306 → ~0 tokens), yet the pass used to re-run every pressured
round — 174 of 593 pressured passes in the M321 workload were the 26th or
later in their turn, scanning everything to reclaim nothing. Now a pressed
pass whose lossy trims elide **nothing** sets a latch: the pure
`jc_compact_rearm_len` records the exact history length at which the sliding
keep-recent window next releases a candidate (oldest protected candidate
index + keep + 1; with no candidate, `len + keep + 1`), and the lossy scans
are skipped until then. Bounded by construction — every latch expires within
`keep + 1` appends, so a conservative candidate detector can only delay one
scan, never skip one forever (the subtlety that kept this deferred), and a
history that *shrank* re-arms immediately (stale index math after a
truncation). The skip is CPU-only: the estimate, `pressed`, and the `compact`
telemetry event are unchanged, with `latched:true` marking skipped passes so
a reader can split thrash from effort. The latch is owned by the agent loop
(one per (sub)turn, `struct jc_midturn_latch`); passing NULL disables it. On
the driver fixture (`tests/smoke/compact_latch.sh`), 12 pressed passes
collapsed to 4 full scans.

## The context gauge (M358)

The window was the one resource the model-facing notice family never metered:
the operator has `/context`, the TUI ctx%, and the once-per-turn
`warned_short` log line, while the model saw only per-message elision markers
*after* the fact — which explain what happened to one output and never say
"change how you read". Two halves close that:

- **Static** (`jc_sysmsg_append_context_window`): one Environment line stating
  the **configured** context window and the reading habit that fits it
  (offset/limit + `search_code` over whole-file reads). Emitted only when
  `contextLimit`/`contextLength` is explicitly set — never the built-in
  default, because a number nobody set is not a fact about this model
  (`jc_compact_context_limit_explicit`). Both prompt builders, so subagents
  get it too; byte-stable within a model, so the cached prefix is unaffected.
- **Dynamic** (`jc_compact_pressure_note`): the first **pressed** mid-turn
  pass of each agent loop injects ONE `[context]` user-role note at the
  history tail (the cache-safe control-inject shape), built from the pass's
  own calibrated numbers so the note and the trigger cannot disagree.
  `reached` picks the honest message: elision is coping ("being elided to
  make room — prefer targeted reads") vs elision cannot cope ("could NOT
  bring it back under the target — write durable results now"). Once per
  loop at any depth (each (sub)agent flies its own window); the pass's
  `compact` telemetry event gains `noticed:true` when it fired, so a reader
  can join "the model was told" to what it did next.

The `[context]` tag is registered in [NOTICES.md](NOTICES.md) (the
bracketed-tag registry). Pure cores unit-tested; the wire behavior is pinned by
`tests/smoke/context_gauge.sh` (note absent before pressure, present exactly
once after, guidance text on the wire, and the no-configured-limit absence
pair).

## Token-estimate calibration (M77)

Every decision above is driven by a **byte heuristic** — `~bytes/4` plus a small
per-message overhead (`jc_compact_estimate_tokens`). Real tokenizers pack more,
so the estimate runs *optimistic* — but **by how much is a property of the
model's tokenizer, not a constant**. Measured ratios of true `prompt_tokens` to
the byte estimate:

| Model | Ratio (v1 basis) | Samples |
|---|---|---|
| `anthropic/claude-haiku-4-5` | 2.53× | 19 |
| `jlu/gemma-4-26b-it` (HRZ) | 1.68× | 200 |
| `jlu/qwen3-coder-next` (HRZ) | 1.57× | 2191 |
| `hosted_vllm/qwen3-coder-next` | 1.42× | 899 |
| `gwdg/qwen3-coder-30b` | 1.35× | 49 |
| `google/gemma-4-e4b` (local, LM Studio) | **0.69×** | 168 |

> **These figures are on the v1 basis and are not comparable to what jichi
> measures now.** M286 changed the basis (see below); the numbers above absorb an
> additive error that inflated them by an amount depending on how large the
> histories in each model's samples happened to be. `jlu/qwen3-coder-next` later
> reached 2.72× on this basis and reads **1.19×** on the corrected one. The table
> is kept because its *qualitative* lesson survives — see the next paragraph — but
> re-measure your own before planning against a number.

The spread is real and it is the point: `gemma-4-e4b` sits *below 1.0*, meaning for
that tokenizer the byte heuristic **over**-states the token count, and the v1
error could only push a ratio up, so its true ratio is lower still. Any fixed
correction would be wrong for most of this list, and wrong in both directions.

It is also a lesson in sample size. The M166 bench measured 1.30× for
`gemma-4-e4b` after **two** calls and wrote it into this file; the converged value
over 168 calls is 0.69×, on the other side of 1.0. Two samples told you the wrong
sign. The window-capped average is doing its job — just do not read it early.

Do not plan against a single figure. **Read your own**:
`~/.jichi.d/calibration.json` holds what jichi has learned per model, and
`doctor --live` reports the real `prompt_tokens` of its probe request (M167). A
ratio near 1.3 leaves far more usable window than one near 2.0, and on a small
model that difference decides whether a task fits at all.

Left uncorrected, the gap makes the compaction trigger fire late,
the mid-turn high-water mark read low, the system-prompt fit budget too generous,
and the `/context` breakdown and TUI context% understate reality — the root cause
behind the conservative "set `contextLimit` to ~half the real window" guidance and
the recurring context-overflow HTTP-400 on long runs.

M77 learns the real ratio per model and applies it everywhere:

- **Learn.** Each successful model call already reports `prompt_tokens` (the full
  input, cache included). `jc_calib_observe` compares that against the byte
  estimate of the *same* request — **system prompt + tool schemas + history, all
  three measured** (M286; see "The basis" below) — and
  folds `real / estimate` into a running, window-capped average for that model
  (`jc_calib_blend`, clamped to `[0.5, 8.0]`; a wildly out-of-band sample from a
  truncated/failed request is rejected outright). This runs at every agent depth,
  so subagent calls calibrate too.
- **Persist.** The table is a small JSON file keyed by model id at
  `~/.jichi.d/calibration.json` — **outside any workspace** (the
  observability-blast-radius lesson), loaded at startup and saved at teardown. So
  a model that's been run before starts already calibrated.
- **Apply.** `jc_compact_calibration(app)` returns the active model's ratio (1.0
  when uncalibrated — no correction). The compaction trigger and mid-turn
  high-water/target scale the estimate up by it; system-prompt fitting and the
  auto-context budget deflate their limit by it (equivalent, since they measure
  raw byte lengths); the `/context` breakdown and TUI context% multiply their
  displayed numbers by it so what you see matches what triggers.

The clamp/blend math is pure and unit-tested (`tests/test_calib.c`), as is the
save/load round-trip. Because the correction is self-learned from real usage, a
freshly-configured model self-tunes within its first turn instead of needing a
hand-set `contextLimit`. `jc_compact_calibration` returning 1.0 for an unknown
model means the pre-M77 behaviour is exactly preserved until data arrives.

### The basis: what the ratio is measured *against* (M286)

A ratio is only meaningful relative to the estimate it corrects, so the estimate
is part of the definition. Until M286 the basis was **`history + 2000`**, where
2000 (`SYS_TOOLS_OVERHEAD`) was a flat allowance for the system prompt and tool
schemas — chosen so the calibration and the compaction trigger agreed with each
other. Measured on one project's 34 MB log, the real non-history part was:

| Tool profile | system prompt | tool schemas | true total | assumed |
|---|---|---|---|---|
| `core` | 6423 | 998 | **7421** | 2000 |
| `full` | 6340 | 4827 | **11167** | 2000 |

So a **multiplicative** correction was absorbing a 5–9k **additive** error. The
consequence is not that the ratio was merely too high — it is that the learned
"per-model constant" became a function of how large the history happened to be:

| History in the request | Ratio on `history + 2000` | Ratio on `sys + tools + history` |
|---|---|---|
| < 2k | **3.98×** | 1.07× |
| 2–10k | **1.96×** | 1.14× |
| 10–30k | **1.54×** | 1.19× |
| 30–80k | **1.37×** | 1.19× |
| all 1173 calls | **1.50×** | **1.19×** |

On the corrected basis the ratio is flat — an actual property of the tokenizer,
which is what M77 set out to learn. On the old one it varied 2.9×. Turn-starts and
subagent calls carry small histories and pushed the average up; it was then applied
to large-history calls, where it over-stated. That model persisted **2.717×**.

How it surfaced: jichi's own telemetry summarizer had always computed the honest
figure (it sums the three M192 attribution fields) and reported **1.17×** for the
same model on the same events. Two instruments in one program, disagreeing by 2.3×
about the same quantity, with the wrong one driving every context decision — and
nothing compared them. A unit test now does
(`tests/test_telemetry.c`, the cross-instrument lint), which is cheaper than an
audit and does not depend on anyone thinking to look.

**Migration.** `calibration.json` now carries a top-level `"v"`
(`JC_CALIB_SCHEMA`). A file without it, or with an older version, is **discarded
on load** rather than trusted or slowly averaged down — a v1 ratio is not a v1
estimate of the v2 quantity, it is a different measurement. Each model re-learns
within its first turn, which is the same property that made M77 self-tuning in the
first place. Bump the version whenever the basis changes again.

`SYS_TOOLS_OVERHEAD` survives only as the fallback for the moment before any model
call has been made, and as a fixed offset inside the pure trim passes — where it is
a stop-condition offset rather than a scaling basis, so being off by a few thousand
tokens on a ~100k budget only shifts slightly where trimming stops.

**Two samples are enough to be useful, not enough to be trusted** — and the
bench proved it on itself. It measured 1.30× for `gemma-4-e4b` after two calls;
168 calls later the same model sits at 0.69×. The early figure was not merely
imprecise, it had the wrong sign: it said "the estimate under-counts, scale it up"
when the truth for that tokenizer is the opposite. If you are sizing
`contextLimit` by hand, read `calibration.json` after a few dozen real turns, not
after the first one — and prefer setting `contextLength` correctly and letting M77
do the correcting.

## Reading a `compact` event (M326x)

A mid-turn pass does **two** different jobs, and the telemetry now says which ran:

| field | meaning |
|---|---|
| `pressed` | the 80% high-water trigger fired — this pass was the last thing between the request and the limit |
| `short` | it was pressed **and** still could not get under `target`; the request went out over the configured limit |
| `unrelieved` | it was pressed and ended still **above the high-water**, so it re-triggers next round. Not the same as `short`: a pass can miss the 60% target and still drop under the 80% trigger, which buys quiet rounds |
| `dup` | elisions by the eager **zero-loss** pass (superseded `read_file` results) |
| `age` / `args` | elisions by the **lossy** age-based fallback, which runs only when pressed |
| `before` / `after` / `limit` / `target` | calibrated real-token terms, so a reader can re-check the decision |

**Most mid-turn events are not emergencies.** The eager dedup runs on every round
that saw a new read result, well below the high-water, because on a cacheless
backend a superseded read is re-billed every turn until it is dropped. In one
measured workload **44% of 1,057 mid-turn events were that housekeeping**, and
only 56% were under real pressure — so a raw compaction count reads as alarm at
nearly twice the true rate. `jichi telemetry` states the split.

Before M326x, `short` was written without consulting `pressed`, so every eager
dedup reported "could not reach the target" (and `target: 0`). All 19 such events
in that workload were false positives. Logs written before the fix are still read
correctly: the summarizer infers pressure from `age`/`args` when `pressed` is
absent.

### Why a lower threshold does not fix a thrashing turn (M326y)

Measured across 593 pressured passes, reclaim decays *within* a turn:

| pass # in turn | median tokens reclaimed |
|---|---|
| 1st | 10,324 |
| 2nd | 1,306 |
| 3rd onward | ~0 |

The first pass harvests everything eligible. Elided content shrinks to ~660 bytes
— under `ELIDE_MIN_BYTES`, and the idempotence guard never re-elides it — while
newer results are protected by `MIDTURN_KEEP_RECENT`. Material is *not* the
constraint: 86% of tool-output bytes are already above the floor.

So a lower high-water fires the first (useful) pass slightly earlier and the later
(useless) ones more often, and a lower target asks for headroom that no longer
exists. **The lever is smaller tool output** — fewer, tighter reads and commands —
not a smaller threshold. `unrelieved` identifies the turns where eliding has
stopped helping at all.

**The cheaper fix is upstream.** Compaction can only remove a tool result *after*
it has been sent and billed — usually many times, since it rides along in the
history from the moment it is produced.
[TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) measures where that output comes from
and which levers actually reduce it.
