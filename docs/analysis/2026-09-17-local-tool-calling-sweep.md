# Nine local models, probed three times each: how many actually emit tool calls

*2026-09-17 (M648). The operator asked how jichi can support small models at tool
use, and support models that were never trained for it. Before designing
anything, this measures the population — because this project's own rule is to
**measure the population before building a gate**, and the last time that rule
was applied it killed a planned lint (1 verbatim quote in 110 blocks).*

**The headline: four of nine local models consistently *describe* a tool call in
prose instead of emitting one.** That is not a rounding error, and it is the
population that `toolCalling: "text"` — reserved since M149 and still unbuilt —
would serve.

---

## 1. Method

- **Instrument:** jichi's own tool-use probe, the one `doctor --live` runs.
  `jc_toolprobe_classify` is pure and returns one of four verdicts:
  `NATIVE` (emitted a call naming the probe tool), `TEXT` (described the call in
  prose — the M147 shape), `NONE` (answered, neither called nor described), and
  `UNKNOWN` (nothing to classify). The fourth exists because M519 shipped a
  classifier whose else-branch reported a *positive claim* for five capable
  models; since M628 the fallback says *unknown*.
- **Backend:** LM Studio on `127.0.0.1:1234`, OpenAI-compatible, context 8192,
  `maxTokens` 512, costs declared 0. Every model is local; no priced model was
  contacted.
- **Isolation, and this mattered:** **one model loaded at a time**, everything
  else unloaded between runs, each probe under a fresh throwaway `HOME`.
- **Repeats:** three probes per model, so a verdict is a repeated observation
  rather than one sample.

**The first sweep was thrown away, and why is worth recording.** It probed all
models against a warm server without unloading, and six of ten returned no
verdict at all. The cause was not the models: the endpoint answered
`HTTP 400 — "Failed to load model"`, a VRAM-contention failure. Had those six
been scored as "no tool calling", the conclusion would have been exactly
backwards for models that turn out to be **native**. *An infrastructure failure
must never be read as a capability verdict* — which is the distinction
`JC_TOOLPROBE_UNKNOWN` was added for, and here it was the operator's own first
sweep that needed it.

---

## 2. Results

Three probes per model, one model resident at a time.

| Model | Probe 1 | Probe 2 | Probe 3 | Stable? | Probe prefix (real prompt tokens) |
|---|---|---|---|---|--:|
| `qwen/qwen3.5-9b` | native | native | native | yes | 308 |
| `google/gemma-4-12b` | native | native | native | yes | 104 |
| `google/gemma-4-12b-qat` | native | native | native | yes | 104 |
| `google/gemma-4-e4b` | native | native | native | yes | 104 |
| `prism-ml/bonsai-27b` | *probe error* | *probe error* | **native** | see below | 308 |
| `qwen/qwen2.5-coder-14b` | **text** | **text** | **text** | yes | 203 |
| `llama-3-elyza-jp-8b` | **text** | **text** | **text** | yes | 33 |
| `llm-jp-4-8b-thinking` | **text** | **text** | **text** | yes | 50 |
| `rakutenai-7b` | **text** | **text** | **text** | yes | 33 |

**Counts: 5 native, 4 text, 0 none, 0 unknown**, out of nine models probed
(the tenth served id is an embedding model and has no chat surface).

### What the table supports

1. **The verdict is stable.** Eight of nine models returned the same verdict
   three times out of three. A one-shot probe is not the coin-flip it could have
   been, which is what makes `doctor --live`'s single probe defensible.
2. **`bonsai-27b` is native, and its two errors are infrastructure.** At 27B it
   was still settling when the first two probes fired; the third succeeded and
   returned native. It is reported here as **native with a caveat**, not as a
   2-in-3 failure, because the failures were transport errors rather than
   answers. **This is exactly the trap §1 describes, met a second time in the
   same session.**
3. **"Small" is not the predictor.** The smallest model in the set,
   `gemma-4-e4b`, is native three times out of three. The *largest* text-only
   model is a **14B coder model** — `qwen2.5-coder-14b`, a model specifically
   trained for code, which consistently describes the call it was asked to make.
   Any advice of the shape *"below N billion parameters, expect no tool calls"*
   is refuted by this table in both directions.

### What the table does NOT support

- **The `probe prefix` column is suggestive and is not a finding.** The text
  models span 33–203 tokens and the natives 104–308, so the two ranges
  **overlap** and no threshold separates them: `gemma-4-12b` is **native at
  104**, well below `qwen2.5-coder-14b`, which is **text at 203**. Prefix size
  does not predict the verdict. The column is
  published because a low prefix *can* indicate that a tool schema never reached
  the model, which would make the verdict a statement about the server's chat
  template rather than the weights — the finding `LOCAL_MODELS.md` already
  records, that "tool calls" is a claim about **this GGUF's template**, not
  about the weights. **Which of the four it is has not been measured here**, and
  §4 says what would settle it.
- **Nine models on one backend on one box.** Not a claim about model families,
  quantisations, or other servers.
- **The probe asks for one trivial call.** It does not measure whether a model
  that *can* call a tool calls the *right* one, with the right arguments, twenty
  turns in. That is what `tests/bench/` measures, and it is a different question.

---

## 3. What this means for the reserved `"text"` mode

jichi's ladder for a model that will not call a tool is thorough and ordered
(`LOCAL_MODELS.md`, "When the model calls no tool at all"): check the request,
check the bare endpoint, check persisted constraints, check whether reasoning ate
the budget, **and only then** set `toolCalling: "none"`. The ladder's last rung
is a **degradation**: `"none"` stops advertising tools at all and leaves an
honest Q&A/plan agent. There is no rung where a model that cannot emit native
calls still *uses tools*.

That rung is `toolCalling: "text"`, reserved at M149, parsed-as-native with a
warning ever since, and still unbuilt. `jc_toolprobe_suggested_setting` says so
in a comment: a TEXT verdict recommends `"none"` today, "the text protocol is
unbuilt".

**This measurement is the argument for building it, and it is a stronger
argument than the design ever was:** the population is not hypothetical and it
is not tiny. It is **four of nine models on this box**, including every Japanese
model available here and one 14B coding model, and every one of them **already
gets far enough to describe the call it wants** — which is to say the gap is
transport, not comprehension.

**Two things already in the tree do most of the work:**

- `jc_toolcall_scan` (`src/tools/jc_tool.c`) is already a text-to-call parser.
  It uses three high-precision patterns and **requires the name to resolve in
  the live registry**, so it cannot invent a tool.
- The M147 nudge already detects the TEXT case at runtime and asks for a retry,
  with telemetry recording `fired` and `recovered`.

**The difference between those and a `"text"` mode is precision versus recall,
and it is the whole design risk.** The scanner is *recovery*: opportunistic,
fired after the fact, tuned so that a false positive is nearly impossible —
because inventing a call the model did not make is far worse than missing one. A
`"text"` mode inverts that: the model would be **told** the syntax, so recall
becomes the target and the syntax is ours to pick. **A protocol jichi specifies
can be made unambiguous in a way free prose cannot**, and that is why the mode
is worth building rather than simply loosening the scanner.

---

## 4. Recommendation

1. **Build `toolCalling: "text"`**, on the measured population above, reusing
   `jc_toolcall_scan`'s registry-resolution rule as the safety floor: *a parsed
   call whose name does not resolve is not a call*. Every existing fence —
   edit-scope, path fence, approval, `is_error` results — applies unchanged,
   because a text-parsed call must enter `jc_tool_execute` by the same door as a
   native one. It must never be a second execution path; that is how M535's
   three unknown write tools happened.
2. **Before writing the parser, settle the template question** (§2). For one
   text-verdict model, capture the request jichi sends and replay it with and
   without a tool-aware chat template. If the verdict flips, that model is a
   packaging problem and belongs on the ladder, not in the new mode — and the
   population shrinks. This is cheap and it bounds the feature's real audience.
3. **Report the sweep, not a rule of thumb.** Any documentation that says
   "small models cannot call tools" should be corrected against this table: a
   4B-class model here is native and a 14B coder model is not.
4. **Keep the probe's four verdicts and its failure semantics.** Two separate
   times in this one session an infrastructure failure looked exactly like a
   capability verdict. The distinction is the instrument's most valuable
   property.

---

## 5. Reproducing this

LM Studio serving on `127.0.0.1:1234`; for each model, unload everything, load
that model alone, then run `jichi doctor --live` against a config naming it,
three times, each under a throwaway `HOME`. Read the
`--live: tool calling observed "<verdict>"` line. **Unloading between models is
not optional** — §1 explains what a warm, contended server produces instead.

*State note:* this sweep loaded and unloaded models on a shared LM Studio. The
resident set was recorded before it started and restored afterwards, with two
deviations worth naming: the identifier `ja-elyza` came back under its model
name `llama-3-elyza-jp-8b`, and the embedding model reloaded with an 8192
context where it had been 2048.
