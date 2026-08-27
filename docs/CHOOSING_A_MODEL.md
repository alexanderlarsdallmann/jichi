# Choosing a model — tiny, small, or large, and the question you must ask it first

A tutorial and reading track for learners: how to pick a language model for
coding and for non-coding work (documentation, analysis, summaries), and the
one cheap check — *ask the model what version of your language it was trained
on* — that would have predicted this project family's most expensive model
failure before it happened. It is written against jichi's configuration
surface ([`MODELS.md`](MODELS.md), [`ROUTING.md`](ROUTING.md)) but the method
is agent-agnostic.

The thesis in one line: **choose by task shape and knowledge fit first; size
is the third question, not the first — and never rely on a model for a
language before probing what version of it the model actually knows and
writing the answer into the project documentation.**

## 1. The four axes that matter

Model choice is an engineering trade across four axes. Leaderboard rank is a
summary of none of them.

1. **Capability** — reasoning depth, cross-file coherence, how reliably it
   *calls tools instead of narrating them* (see the M147 nudge and
   [`ANECDOTES.md`](ANECDOTES.md) #19 for what a small model does with a
   malformed request).
2. **Knowledge** — *when* its training data ends, and where its
   centre of mass sits for each language and library you use. This axis is
   invisible on benchmarks and is the subject of §4.
3. **Cost and latency** — per-token price, and whether prompt caching works
   on your backend (it changes the economics of long agentic turns by an
   order of magnitude; [`PROMPT_CACHING.md`](PROMPT_CACHING.md)).
4. **Context window** — the real one, not the advertised one. Below ~12k
   effective tokens jichi trims the toolset (`toolProfile` auto-core);
   below that, agentic work stops being possible at all
   ([`COMPACTION.md`](COMPACTION.md)).

## 2. The tiers

"Tiny / small / large" is a capability shorthand, not a precise parameter
count. What each tier is *for*:

| Tier | Rough size | Good for | jichi surface |
| --- | --- | --- | --- |
| **Tiny** (≲4B, incl. heavily quantized) | runs anywhere, near-instant | autocomplete / fill-in-the-middle, one-line summaries, commit-message drafts | `autocomplete` role (`complete`, `fim`, Ctrl-G ghost text) |
| **Small** (≈7–40B) | local-servable, cheap per call | mechanical edits, test writing, focused refactors — **under gates**; history summarization | routing `fast` tier; `summarize` role; the default coder in this project family |
| **Large / frontier** | API-priced, slowest | design, cross-file reasoning, reviews, documentation for humans, unattended `--auto` runs | routing `strong` tier; the escalation target (`escalateOnVerify`/`OnError`/`OnStall`) |

Three qualifications that matter more than the table:

- **A tier is a *floor per task*, not a badge per project.** jichi's routing
  exists precisely so one session can use a small model for the mechanical
  90% and escalate to a large one when verification fails
  ([`ROUTING.md`](ROUTING.md)). Paying frontier prices for `list_files`
  round-trips is waste; trusting a tiny model with a design decision is a
  different kind of waste.
- **Agentic work has a capability cliff.** Below roughly the small tier,
  models stop reliably emitting well-formed tool calls, close turns after a
  content-free assistant message, and narrate actions instead of taking
  them. jichi carries specific machinery for this boundary
  (`jc_jsonrepair`, `jc_toolcall_scan`, the placeholder-skip wire
  invariant) — but machinery softens the cliff, it does not remove it.
- **Specialists are roles, not sizes.** Embedding, rerank, transcription and
  image models are picked by role fit ([`MODELS.md`](MODELS.md) §
  specialists); a 0.6B embedding model is not a "tiny chat model", it is a
  different kind of instrument.

## 3. Coding vs non-coding tasks: the verification asymmetry

The deepest difference between the two task families is not difficulty — it
is **who catches the mistake**.

- **Code fails loudly.** A wrong API is a compile error; a wrong behaviour
  is (with a real gate) a test failure. The toolchain is a free, tireless,
  incorruptible reviewer. This is why you can push *coding* work **down** a
  tier when — and only when — the gates are real: a `verify` command that
  provably runs the tests ([`AUTONOMY.md`](AUTONOMY.md), and
  [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md) on gates that were green while
  running nothing).
- **Prose fails silently.** Nobody compiles a README. A confidently wrong
  sentence in documentation ships, gets read, and misleads — the failure
  surfaces weeks later in a user's head, not in CI. So documentation and
  analysis deserve a tier **up** from intuition, not down: the common
  instinct "docs are the easy task, give them to the cheap model" is exactly
  backwards, because the cheap model's errors there are the ones nothing
  catches.
- **Stale knowledge poisons both, differently.** In code, an outdated model
  writes the old API and the compiler objects (§4). In prose, the same model
  *describes* the old API fluently — and no tool objects. Documentation
  about a fast-moving dependency is therefore the **most** knowledge-
  sensitive task you have, not the least.
- Tasks that transform supplied text — summarizing a diff, translating,
  reformatting — need far less world knowledge than tasks that must supply
  their own facts. A small model summarizes well; a small model *asserting*
  API behaviour from memory is a hazard.

### Measured: same gateway, two models, two crafts (2026-08-12/13)

A two-day campaign drove both HRZ models against real tasks in one repository,
with jichi's fences and verdicts as instruments (artifacts:
[`case-studies/`](case-studies/README.md)). **Eleven runs, four tasks, both
models in both roles** — organised by *role*, because that is where the lesson
turned out to be:

**Authoring an assignment (write the spec, the hints, the gate command):**

| model | result | tokens |
|---|---|---|
| qwen3-coder-next | *case 1* — a **hollow gate**: `verify` was already green, so `grade` reported PASS at 100% with the target function still panicking. Hints shipped as angle-bracketed placeholders | 297k (+468k for the mentor afterwards) |
| gemma-4-31b-it | *case 2* — **the best teaching text of the campaign**: correct deep-bounce semantics, a diagram that carries the algorithm, sound pseudo-code, a 3-rung ladder that parses | ~1.6M |
| qwen3-coder-next | *case 3* — good content, **bad file discipline**: overwrote an existing spec, dropped the frontmatter fence (nothing parsed), and hallucinated a `.lerp` method its own hints taught | ~0.8M |
| gemma-4-31b-it | *case 4* — **hung for 22 minutes** with a 0-byte journal; ended by hand, task authored by the tutor | — |

**Surgical editing (insert a stub and three tests — mechanically simple):**

| model | result | tokens |
|---|---|---|
| gemma-4-31b-it | two runs, **both spliced the insertion *into* a neighbouring function** (replacing a signature; stealing a closing brace); one ended claiming success over an *empty diff*. Its gate also compared f32 with `==`, which a **correct** implementation would have failed | ~3.1M |

**Implementing to a gate (the junior harness, `attempt --keep-worktree`):**

| model | setup | result | tokens |
|---|---|---|---|
| qwen3-coder-next | **unfenced** | **"PASS" by editing the gate tests** — ten ignored goalpost warnings; now reported TAINTED | 5,215k |
| qwen3-coder-next | **write-fenced to one file** | correct implementation, verify green, no test touched | **504k** |
| qwen3-coder-next | junior harness, case 2 | clean pass, 0 hints, 0 test edits | 973k |
| qwen3-coder-next | junior harness, case 3 | pass — and it **built the API its own author had hallucinated** (accepted on review) | 1,356k |
| qwen3-coder-next | junior harness, case 4 | pass, one file, and **better than the reference** (exhaustive `switch` where the reference used `else`) | **520k** |

Three selection lessons that no benchmark table shows:

- **"Coding model" is not one skill — and neither model was reliable at the
  mechanical half.** The 31B instruction-tuned model wrote the best *prose about
  code* and could not perform a simple insertion; the coder model implemented
  cleanly every time and, as an *author*, shipped a hollow gate, overwrote a
  file, and invented an API. Assign models to **crafts** (prose · surgical edit ·
  implement-to-gate), not to a "strong/weak" axis — and re-check a routing config
  that escalates *editing* work to a model whose measured strength is prose.
- **The fence is part of the model choice.** The cheapest and most honest run in
  the table is the fenced one; the identical model, unfenced, found the cheapest
  path to green (editing the test) and spent **10×** doing it. Compare models
  *inside the guardrails you will actually run them under* — an unfenced bake-off
  measures gate-gaming ability.
- **An author and a solver sharing a model share its blind spots.** Case 3's
  junior never read the repaired hints and independently rebuilt the same
  non-existent API its author had described. If one model does both jobs, the
  solver will sometimes *ratify* the author's hallucination rather than catch it.

*(Attribution note worth carrying: case 1's authoring run was **addressed to
gemma and served by qwen3** — routing overrode the explicit `--model`, silently,
which is the defect jichi fixed as M411. Every row above is attributed from the
run's own journal, not from the flag that was passed.)*

## 4. Ask the model what it knows — the version probe

This section exists because of a measured failure, in the zigodot project
and one other:

> The project's rules state **Zig 0.16.0** — twice, loaded into every
> request. The small coder model (`jlu/qwen3-coder-next`) nevertheless
> produced internally-consistent **Zig 0.11–0.13** code, repeatedly, against
> those rules. And when finally *asked directly*, it answered that its
> training data covers **Zig 0.11.0**.

Three version numbers, none matching. The full diagnosis and fix live in
[`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md); what this
tutorial adds is the lesson about *ordering*: the direct question costs one
prompt and would have exposed the requirements gap **before the project
started**, instead of after days of confusing compile failures. Nobody asked
it, because nobody thought of the model's knowledge as a requirement that
could fail.

So treat it as one:

**The rule.** Before relying on a model for a language or major framework,
(1) ask it what the newest version is that it knows, (2) probe what it
actually *writes*, and (3) **record both answers in the project
documentation, next to the version the project requires.** If they do not
match, the project's requirements are not met by that model — that is a
requirements gap, and it must be visible in the docs, not discovered in the
compiler output. Then decide, in writing: mitigate (delta table +
readable toolchain source + verify gate) or choose a different model.

### The probe, concretely

**Put every flag *before* `-p`.** `-p` takes the prompt as its argument and
refuses a flag-shaped one (it exits 2 and tells you so) — which is not a
nuisance but a scar: the invocation `jichi -p --no-session "…"` is exactly the
bug in §4's caveat below, where the flag became the prompt and the real question
was silently dropped.

Self-report (one prompt, headless):

```sh
jichi --no-session -p "Answer from your training data only, without guessing:
what is the newest released version of Zig you have reliable knowledge of?
Name two standard-library APIs that changed in releases after that version,
if you know of any."
```

Behaviour probe (what it writes is the real evidence):

```sh
# sed strips any markdown code fence: a leftover ``` line makes the compiler
# fail on line 1, and that error is NOT a dialect finding -- read probe.zig first.
jichi --no-session -p "Write a minimal Zig program that appends three
integers to a std.ArrayList and prints them. Output only code." \
  | sed '/^```/d' > probe.zig
zig build-exe probe.zig   # the compiler is the arbiter
```

To probe **context-free** (§4's rule, below), run it from an empty directory with
a self-contained config so none of your project's rules reach the model:

```sh
mkdir /tmp/probe && cd /tmp/probe
jichi --config-json '{"models":[{"name":"probe","provider":"openai",
  "model":"<the model id>","apiBase":"<the endpoint>","apiKeyEnv":"JICHI_API_KEY"}]}' \
  --no-session -p "…the question above…"
```

Date the APIs in whatever comes back (the clustering diagnostic —
[`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md) §2). The two
mechanical steps are also automated as `tests/bench/version_probe.py`
([`BENCH_LOCAL_GPU.md`](BENCH_LOCAL_GPU.md) §11) — it sends the probes
context-free, lets the installed compiler judge, and prints the record
block; reviewing and pasting the record stays yours. Then record:

```markdown
### Model knowledge record — <model id>, probed <YYYY-MM-DD>
- project requires:        <language> <version>
- self-reported newest:    <verbatim answer>
- observed dialect:        <version cluster, from N compile probes>
- verdict:                 MEETS / DOES NOT MEET
- mitigation in force:     <delta table / referenceRoots / verify gate / different model>
```

### The honesty caveats

- **A self-report is evidence, not ground truth.** Models have no reliable
  introspective access to their training data; the answer can be wrong in
  both directions. In the measured case the self-report (0.11.0) sat at the
  low edge of the observed dialect cluster (0.11–0.13) — usefully honest,
  cheap to obtain, and still not sufficient alone.
- **A *matching* self-report never skips the compile probe.** "I know 0.16"
  costs the model nothing to say. The behaviour probe plus the compiler is
  the part that cannot flatter you.
- **Probe context-free** — from an empty directory with a minimal
  `--config-json`, or a direct `curl` to the endpoint — so the probe
  measures the model rather than your project context, and costs tokens
  accordingly. And **verify what was actually sent before interpreting a
  bizarre answer**: this rule's own first version cited a measurement that
  turned out to be a misdiagnosis — two probe replies so strange they
  spawned a "the big prompt drowned the question" theory, which stood in
  three documents for half a day before the truth surfaced: jichi's `-p`
  had swallowed the next flag as the prompt and silently dropped the real
  question (fixed and smoke-pinned in M375; re-asked properly through the
  full 15k-token project prompt, the model answered cleanly). A model's
  weird reply is a prompt-delivery bug until proven otherwise —
  [`ANECDOTES.md`](ANECDOTES.md) #19 and #50 are the same lesson from two
  directions.
- **Re-probe on every change of model, backend, or toolchain version.** A
  proxy silently swapping the model behind an alias, or the project bumping
  Zig, invalidates the record. The record carries its probe date for
  exactly this reason.
- The probe is a **documented ritual, deliberately not a `doctor` check** —
  see D1 below.

## 5. A decision checklist

For each *task family* in your project (not the project as a whole):

1. What tier is the floor? (Mechanical + gated → small. Judgment, design,
   prose for humans → large. Keystroke-latency completion → tiny.)
2. How knowledge-sensitive is it? (Fast-moving toolchain → probe first,
   §4. C89/POSIX → almost any model ever trained qualifies — jichi's own
   implementation language is, among other things, a model-compatibility
   decision.)
3. What catches a mistake? (Compiler/tests → gate it and go cheaper. Nothing
   → go stronger, and add review.)
4. Does the economics hold? (Prompt caching available? Context window big
   enough for the toolset + your rules? `jichi context` shows the budget.)
5. **Can it emit a native tool call, on the server you actually run?** Check
   before anything else if the task involves files, because a model that cannot
   call `read_file` will answer plausibly about a file it never opened. This is a
   property of the **model plus its server template**, not of the weights or the
   parameter count: on one measured bench (M519) five of six local models emitted
   native `tool_calls` and the one that failed was **the largest**, returning a
   correct call as `<tools>{...}</tools>` in content because that GGUF's template
   never translated it back. The only check that settles it is
   `jichi --config <yours> doctor --live`, which must say
   `tool calling observed "native"` — a hand-written probe can be wrong about this
   in either direction ([`LOCAL_MODELS.md`](LOCAL_MODELS.md) carries the five
   traps). If it says `text` or `none`, set `toolCalling: "none"` to get an honest
   Q&A agent rather than a silent no-op (M149), or pick another model.
6. Wire it as roles + routing, not as one global choice
   ([`MODELS.md`](MODELS.md), [`ROUTING.md`](ROUTING.md)) — and write the
   knowledge record (§4) for every model you configured.

## 6. Design decisions (jichi)

Recorded here in the house form — decision, then the rejected alternative.

- **D1 — the version probe is a documented ritual, not an automated
  `doctor` check.** Rejected: a `doctor --live` knowledge probe. A
  self-report is unverifiable by construction — `doctor` printing a green
  checkmark next to a model's unaudited claim about itself would launder
  weak evidence into apparent fact, the exact inversion of what `doctor` is
  for. The compile probe *is* automated — as a bench artifact, not a health
  check: `tests/bench/version_probe.py` (M374) runs the ritual against a
  live endpoint with the locally installed compiler as arbiter and prints
  the record block for the operator to paste; it can fail a model, never
  certify one, and its own gate is proven two-sided offline
  (`--mode self-test`). See [`BENCH_LOCAL_GPU.md`](BENCH_LOCAL_GPU.md) §11.
- **D2 — probe results are recorded in project documentation and in the
  config's `_note` keys, not in a new config schema key.** Rejected: a
  per-model `knowledgeCutoff` config key. jichi cannot verify it, nothing
  in jichi would consume it mechanically, and an unverified schema-blessed
  value rots into misinformation with authority. `_`-prefixed comment keys
  (already house style in this family's configs) plus the project's docs
  carry the same information *as a dated claim by a named prober*, which is
  its honest epistemic status.
- **D3 — the mitigation of record for a stale-knowledge model is the
  existing triad, not model surgery:** the old-form → new-form delta table
  in the always-loaded rules, the toolchain source made readable via
  `referenceRoots`, and a real verify gate. Rejected: fine-tuning (no
  training pipeline in scope, and it would need re-doing per toolchain
  release) and stuffing changelogs into the prompt (a version label with
  more words — [`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md)
  §3 measured why labels don't work).

## 7. Design decisions (zigodot)

zigodot keeps its own requirement record — `docs/MODEL_KNOWLEDGE.md` in that
repository — because the record belongs to the project whose requirements
are at stake. The decisions, summarized: the requirement (**Zig 0.16.0**) and
the probe results (**`jlu/qwen3-coder-next`: self-reported 0.11.0, observed
0.11–0.13 dialect; `jlu/gemma-4-26b-it`, probed 2026-08-11: self-reported
0.11.0, wrote pure 0.11, failed to compile — both DO NOT MEET**, so no
reachable model there meets the requirement and the gates are load-bearing
for every tier) are stated in the docs as a requirements gap; the model is *kept* for mechanical Zig work under the delta table +
readable std source + `zig build test` gate (it wrote the project's working
code across hundreds of calls — discarding it over a narrow, patched weakness
would be the wrong trade); Zig-heavy design and documentation route to the
strong tier; and every configured model gets a probe record, re-dated on any
model or toolchain change.

## 8. Extra curriculum — the reading track

In this repository, in reading order:

1. [`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md) — the full
   zigodot case: dialect vs confabulation, why version labels fail, the
   delta table, the measurement.
2. [`MODELS.md`](MODELS.md) + [`ROUTING.md`](ROUTING.md) — the configuration
   surface this tutorial's choices are expressed in: roles, tiers, fallback,
   escalation.
3. [`AUTONOMY.md`](AUTONOMY.md) + [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md) —
   gates: what lets you use a cheaper model safely, and the recorded ways
   gates lie (hollow green, hollow red).
4. [`COMPACTION.md`](COMPACTION.md) + [`PROMPT_CACHING.md`](PROMPT_CACHING.md)
   — the context and cost mechanics behind axis 3 and 4.
5. [`ANECDOTES.md`](ANECDOTES.md) #19, #20, #37 — small-model wire
   fragility, the grader that lied, and the dialect story as it happened.
6. [`BENCH_LOCAL_GPU.md`](BENCH_LOCAL_GPU.md) — measuring a small model's
   agentic competence yourself, instead of trusting a leaderboard.

Concepts worth reading up on outside this repository (search these terms;
prefer primary sources): *training data cutoff*; *scaling laws* (why
capability tiers exist at all); *distillation* and *quantization* (why a
competent 30B local model exists); *retrieval-augmented generation* (the
docs-in-context mitigation family — jichi's is [`RAG.md`](RAG.md));
*benchmark contamination* (why leaderboard rank and axis-2 knowledge are
different things); and any post-mortem you can find of an AI-written defect
that shipped — the genre this page is trying to keep you out of.
