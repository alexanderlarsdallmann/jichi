# Small language models: what they are built for, how to test them for that, and what follows for jichi

*An analysis and a test procedure, written 2026-10-23 for self-learners and for agents that will run the
procedure. **The procedure was run the same day: results in §8a.** Apart from §8a, nothing here was measured; it re-reads E0 (§8a of
[`2026-10-23-edge-ai.md`](2026-10-23-edge-ai.md)) against what each model's own makers say it is for.
Every claim carries a mark (§0.2).*

**The question, in the operator's words (2026-10-23):** *"What are these small models aimed at, and good at?
We have to test them accordingly. So, please, document the test procedure, and the implications for the
various models for self-learners, and other agents, too."*

**What was read:**

- the raw model cards and generation configurations of the three models E0 used, fetched with `curl` from
  Hugging Face's raw file URLs (not through a summarising tool). They are kept with checksums in the
  workstation's `Downloads/edge-ai-e0/model-cards/`.
  - Meta's own Llama 3.2 repository is gated (HTTP 401), so its card is read from an ungated mirror
    (unsloth), which reproduces the "Model Information" section but not the "Intended Use" section.
- the option list of the `llama-server` used in E0 (release `b11836`, its `--help`);
- jichi `fc638450`: what a model entry can carry, and how replies are read.

---

## 0. How to read this

### 0.1 A reading path

1. Read §1, the answer in seven points.
2. Read §2 if "sampling", "thinking mode" or "chat template" are new.
3. Read §3 (each model's stated purpose and settings) and §4 (why E0 was the right test for one question and
   the wrong one for another).
4. Follow §5, the procedure, step by step. §8 is the same procedure as a compact checklist for an agent.
5. Read §6 (implications per model) and §7 (decisions).

### 0.2 The marks

| Mark | Meaning |
|---|---|
| **[C]** | Card: quoted from the model's own card or generation configuration, as fetched. |
| **[V]** | Verified in jichi's source or in `llama-server --help`, by the author. |
| **[M]** | Measured in E0 (edge-AI analysis §8a). |
| **[I]** | Inferred: argued, not observed. |
| **[GK]** | General knowledge: not from the fetched files; unverified. |

---

## 1. The answer, up front

1. **The three models were built for different things, and only one of them claims agentic tool use** [C]:

   | Model | What its card says it is for | Tool calling claimed? |
   |---|---|---|
   | **Qwen3 0.6B** | the whole Qwen3 family's card: reasoning, instruction following, multi-turn dialogue, "agent capabilities … integration with external tools", 100+ languages; a thinking and a non-thinking mode; 32,768-token context | yes, for the family |
   | **Llama 3.2 1B Instruct** | "multilingual dialogue use cases, including agentic retrieval and summarization tasks"; 8 officially supported languages; up to 131,072 tokens | not stated in the section fetched |
   | **SmolLM2 360M Instruct** | small enough "to run on-device"; instruction following, knowledge, reasoning; the instruct models add "text rewriting, summarization and function calling (for the 1.7B)"; English | **no**: function calling is the 1.7B's |

2. **E0 tested all three on jichi's agentic coding bench**: multi-step file edits through tools. That is a
   valid test of one question: can a model this small be jichi's local coding agent? The answer was no (0 or
   1 of 11 tasks [M]). It is not a test of what two of the three are *for*. SmolLM2 360M was never meant to
   call tools. Llama 3.2 1B is meant for dialogue, retrieval and summarisation.
3. **E0 also ran every model off its makers' settings** [M][C]:
   - Temperature 0.2 for all three, with the server's defaults for top-k (40), top-p (0.95) and min-p (0.05)
     [V: `llama-server --help`].
   - Qwen3's card says: "**DO NOT use greedy decoding**, as it can lead to performance degradation and endless
     repetitions". It asks for temperature 0.6, top-p 0.95, top-k 20 and min-p 0 in thinking mode, 0.7, 0.8,
     20 and 0 without thinking, and a presence penalty up to 1.5 against repetition.
   - Llama's configuration says temperature 0.6 and top-p 0.9.
   - SmolLM2's card example uses temperature 0.2 and top-p 0.9.
   - Qwen3 ran in its default thinking mode, with output limits far below the card's recommended 32,768
     tokens.
4. **jichi can set only one of these per model: temperature** [V: `struct jc_model_cfg` has `temperature`
   and no `top_p`, `top_k`, `min_p`, `presence_penalty` or thinking switch; none of those fields is sent].
   The vendor settings must therefore be set on the server today. That is a gap (G15 and G16, §6.4).
5. **The procedure (§5) tests each model for its stated purpose, at its makers' settings, graded
   mechanically, with repetitions**, before any test through jichi. Only a model that passes the single
   tool-call probe is sent to jichi's agentic bench.
6. **Expected implications** [I]. *The measured results are in §8a (added the same day); where they differ, §8a
   supersedes this item:*
   - SmolLM2 360M is a candidate for jichi's `summarize` role and for rewriting, in English, and never for
     tool roles. For tools from this family, test the 1.7B.
   - Llama 3.2 1B is a candidate for `summarize` and for retrieval-style single tool calls. llama.cpp's parser
     problem must be characterised first, since it ended 7 of 11 tasks in E0 [M].
   - Qwen3 0.6B is a candidate for single tool calls in non-thinking mode, which is also the mode a CPU board
     can afford.
7. **For jichi this means role by role, not model by model.** jichi already selects a model per role (chat,
   summarize, autocomplete and others) [V]. An edge deployment can give the roles that suit a small model to
   the local model, and keep the coding agent on a network model.

---

## 2. Vocabulary

- **Model card.** The maker's description of a model: what it is for, how it was trained and evaluated,
  its limits, and its recommended use. A card often describes a whole *family*; a claim for the family is
  not a claim for its smallest member.
- **Generation configuration.** A small file with the maker's default sampling settings
  (`generation_config.json`).
- **Sampling settings.** How the next token is chosen from the model's probabilities:
  - **temperature** flattens (high) or sharpens (low) the distribution; 0 is "greedy", always the most
    likely token;
  - **top-k** keeps only the k most likely tokens;
  - **top-p** keeps the smallest set whose probabilities sum to p;
  - **min-p** drops tokens below a fraction of the top token's probability;
  - **presence penalty** discourages repeating tokens already used.

  Small models are sensitive to these. A setting far from the maker's can produce loops or nonsense that
  are not the model's real ability [GK; Qwen3's card says so, C].
- **Thinking mode.** Some models (Qwen3) first write a reasoning section (`<think>…</think>`) and then the
  answer. Thinking improves hard tasks and costs output tokens, which is decode time on a CPU [C; I for the
  CPU cost].
- **Chat template.** How a conversation and the tool definitions are turned into the model's input text,
  and how its tool calls are read back. llama.cpp uses the template stored in the model file (`--jinja`).
  The server's *parser* for tool calls is part of the result: in E0 it rejected Llama 3.2 1B's output in 7
  of 11 tasks [M].
- **Purpose-fit test.** A test of a model on what its makers say it is for, with a grader a program can run.
- **Mechanical grading.** A check that needs no judgement: an exact value, a JSON comparison, a word count,
  a required fact present, a number not invented.
- **k of N.** With sampling on, one run is a draw. Report successes out of N repetitions per item, never a
  single run as the verdict.

---

## 3. What each model is built for

### 3.1 Qwen3 0.6B

- **Purpose** [C: card "Qwen3 Highlights", written for the family]:
  - thinking mode "for complex logical reasoning, math, and coding";
  - non-thinking mode "for efficient, general-purpose dialogue";
  - reasoning, instruction following, multi-turn dialogue and creative writing;
  - "Expertise in agent capabilities, enabling precise integration with external tools in both thinking and
    unthinking modes";
  - "100+ languages and dialects".
- **The model itself** [C]: 0.6B parameters (0.44B without embeddings), 28 layers, a 32,768-token context.
- **Settings** [C: "Best Practices" and `generation_config.json`]:
  - thinking: temperature 0.6, top-p 0.95, top-k 20, min-p 0;
  - non-thinking: temperature 0.7, top-p 0.8, top-k 20, min-p 0;
  - a presence penalty between 0 and 2 against repetition; 1.5 if "endless repetitions" appear;
  - an output length of 32,768 tokens "for most queries";
  - no thinking content kept in the conversation history.
- **What the card does not say:** how the 0.6B in particular performs as an agent. The family's claims are
  not split by size in the fetched card.
- **What E0 showed, off-spec** [M]: native tool calls, well formed; content errors, including a false success
  claim. That was in thinking mode at temperature 0.2, which the card warns against.

### 3.2 Llama 3.2 1B Instruct

- **Purpose** [C: "Model Information"]: "optimized for multilingual dialogue use cases, including agentic
  retrieval and summarization tasks".
- **Languages** [C]: English, German, French, Italian, Portuguese, Hindi, Spanish and Thai are officially
  supported.
- **Settings** [C: `generation_config.json` via the ungated mirror]: temperature 0.6, top-p 0.9, a maximum
  length of 131,072 tokens.
- **Not in what was fetched:** Meta's "Intended Use" section and its tool-calling prompt guidance. From
  general knowledge, Meta describes the 1B and 3B models for assistant-like chat, knowledge retrieval,
  summarisation and query or prompt rewriting, with tool calls in a JSON format the user defines [GK,
  unverified here].
- **What E0 showed** [M]: native tool calls according to `doctor --live`, but llama.cpp `b11836`'s parser
  rejected the output in 7 of 11 tasks, six of them on the first call. It also looped on failing reads (175
  and 194 calls).

### 3.3 SmolLM2 360M Instruct

- **Purpose** [C: "Model Summary"]:
  - a family "lightweight enough to run on-device";
  - advances "in instruction following, knowledge, reasoning";
  - the instruct models support "text rewriting, summarization and function calling (for the 1.7B)".
- **Language** [C: "Limitations"]: "primarily understand and generate content in English".
- **Settings** [C]: no sampling defaults in `generation_config.json`; the card's own example uses
  temperature 0.2 and top-p 0.9.
- **The card's own numbers for the 360M Instruct** [C]: IFEval 41.0, MT-Bench 3.66, GSM8K (5-shot) 7.43.
  Instruction following is its relative strength; arithmetic reasoning is weak.
- **What E0 showed** [M]: "text" tool calling. It described calls instead of making them, which is exactly
  what its card predicts: function calling is the 1.7B's.

---

## 4. Why E0 was the right test for one question and the wrong one for another

- **The right test for "can a model this small be jichi's local coding agent?"** The bench is jichi's own,
  with fixed fixtures, graded by each task's verifier. At 0 or 1 of 11 tasks [M], the answer for these three
  is no, and the edge-AI analysis records that rungs B and C are not the honest claim for this size class.
  Better settings will not turn 0 of 11 into 11 of 11 [I], but they could change 0 into a few, so the exact
  number should be re-measured at the makers' settings before it is quoted as a model property.
- **The wrong test for "what is each model good at?"**
  - SmolLM2 360M was built for rewriting and summarising in English. A tool-driven coding bench measures
    something it was never trained for.
  - Llama 3.2 1B was built for dialogue, retrieval and summarisation. Seven of its eleven results were
    decided by a server parser before jichi saw a tool call.
  - Qwen3 0.6B was the only one tested at something its card claims (agentic tool use), and it ran at
    settings its card warns against.
- **The lesson for any small model:** first ask its card what it is for, then test that at its makers'
  settings. Only then test it at what *you* need, and keep the two verdicts apart.

---

## 5. The test procedure

The procedure has six steps. Each names its inputs, how it is graded, and what is recorded. It runs on the
workstation first and later on a board (step 5). Caps stay off for every measurement, and fences stay on.
That means:

- no deadline and no token budget;
- an output limit set explicitly and generously, with every call that reaches it counted;
- the tool-call fence kept, and every run that reaches it counted;
- a minimal environment for every process the test starts.

### Step 0: pin the stack

Record, before anything runs:

1. the model file, its source repository and its sha256;
2. the quantisation;
3. the server's release and commit;
4. the jichi commit;
5. the hardware, and what else was loading it at the time.

A result without these cannot be compared with anything. E0's pins are in the edge-AI analysis §8a.1.

### Step 1: read the card, write down the settings and the claimed tasks

For each model, from the raw card and generation configuration (fetched as files, not summarised):

1. the tasks the card claims, quoted, and whether a claim is for the model or for its family;
2. the languages;
3. the context window;
4. the sampling settings per mode, and any thinking switch;
5. what the card does *not* claim.

Keep the fetched files and their checksums. §3 is this step done for E0's three models.

### Step 2: serve the model at its makers' settings, and check that they took effect

1. Start the server with the makers' sampling as its defaults. `llama-server` has `--temp`, `--top-k`,
   `--top-p`, `--min-p` and `--presence-penalty` [V].
2. For Qwen3, serve twice, once per mode. Thinking is the template's default. Without thinking, set the
   template's `enable_thinking` to false through `--chat-template-kwargs`, or set `--reasoning-budget 0` [V:
   both options exist; which of them the template honours is to be checked in this step].
3. Give the context window the model supports, or the largest the target device can hold, and say which.
4. Set jichi's per-model `temperature` to the maker's value. jichi sends temperature and nothing else, so a
   mismatch would override the server's default [V].
5. Confirm with one request that the settings and the mode took effect, from the server's response or its
   log. A setting that was not applied is a common silent error.

### Step 3: purpose probes, straight to the server

Small fixed item sets, each graded by a program, each item repeated N = 5 times at the makers' settings.
Report k of 5 per item and the totals. Record separately: output limits reached, server-side parse errors,
and timeouts. Those are failures of the stack, not of the model.

| Probe | For which models | Items | Graded by |
|---|---|---|---|
| **3a Summarise** | all three | 5 fixed texts, including one excerpt of a real jichi transcript (the input jichi's compaction would see) | every required fact present (a list per text); **no number in the summary that is not in the text**; within a stated length |
| **3b Follow verifiable instructions, rewrite** | all three | 10 instructions with checkable constraints: an exact bullet count, a word limit, a required and a forbidden word, valid JSON of a given shape | each constraint checked by a program |
| **3c One tool call** | Qwen3 0.6B, Llama 3.2 1B (SmolLM2 1.7B if added) | 10 requests, each answerable by exactly one call, with 1, 3 and then 8 tool schemas advertised | the call's name and arguments compared as JSON; a described call (text) and a server parse error counted apart |
| **3d A retrieval step** | Llama 3.2 1B, Qwen3 0.6B | 5 questions answerable by "search, then read, then answer" with two tools | the final answer's fact |
| **3e The same in another language** | Qwen3 and Llama (German is officially supported); SmolLM2 as a negative control | 5 instructions from 3b, in German | the constraint, and the answer's language |
| **3f Short reasoning** | Qwen3, in both modes | 10 arithmetic word problems, in the card's own format ("put your final answer within \boxed{}") | the boxed value |

The repository already has an instrument for part of 3c: `tests/bench/schema_probe.py` replays a captured
request while varying the advertised tool array [V: `tests/bench/README.md`]. Nothing in the tree grades summaries yet.
The 3a grader is new work (G17, §6.4).

### Step 4: through jichi, role by role

Only after step 3, and only for the roles a model passed:

1. **`summarize` (rung A).** The model serves jichi's `summarize` role and a free network model serves
   `chat`. Run a scripted session long enough to compact, then grade the compaction summaries with 3a's fact
   lists. A person reads at least one.
2. **`autocomplete`.** jichi's `complete`/`fim` paths on 10 fixed code prefixes, graded by exact or prefix
   match. Instruction-tuned models may answer instead of continuing; that is a finding, not a bug.
3. **Local first with escalation (rung B).** Only for a model that passed 3c with 1 and 3 tools at least 4
   of 5 times per item. Use a set of tool-light tasks, `routing.fast` local, `routing.strong` a free network
   model, and count local completions against escalations from jichi's `route` telemetry.
4. **The agentic bench (`tests/bench`).** Only for a model that passed 3c with 8 tools. Run it uncapped, as
   E0 did, at the maker's settings.

### Step 5: on the board

Repeat 3a, 3c and step 4.1 on the target board. Add what only the board can measure:

- prefill and decode speed at thermal steady state;
- the memory of the server, jichi and a turn in flight together;
- thinking's cost in seconds (Qwen3).

jichi's stall timeout is off, with `deadStream` as the fence, and `maxTokens` is set explicitly.

### Step 6: record and decide

One row per model and mode:

- the step 0 pins and the step 2 settings;
- k of N per probe;
- limits reached, parse errors, timeouts;
- a verdict **per role**: *not suited*, or *suited for role R*, with the scale vocabulary of the
  orchestration analysis: *Designed*, *Mechanics-tested*, *Driven at tier N*.

The thresholds for "suited" are decided **before** the run, not fitted after it (D-SM2).

---

## 6. Implications

### 6.1 Per model

These are predictions until the procedure runs [I].

| Role in jichi | Qwen3 0.6B | Llama 3.2 1B | SmolLM2 360M |
|---|---|---|---|
| `summarize` (compaction, query rewriting) | candidate; non-thinking mode | **best candidate**: its stated purpose | candidate in English: its stated purpose |
| Rewriting, short instructions | candidate | candidate | candidate: its relative strength (IFEval 41.0 [C]) |
| `autocomplete` | untested; instruct models may answer instead of continuing | as left | as left |
| One tool call | **candidate**: the card claims it; native in E0 [M] | candidate, **after** the server-parser question is settled | not suited: its card gives function calling to the 1.7B [C] |
| A multi-step coding agent | not suited at this size [M], to re-measure at the card's settings | not suited [M], with the parser caveat | not suited |
| Languages other than English | 100+ claimed [C] | 8 official [C] | English only [C] |

### 6.2 For the edge deployments in the edge-AI analysis

- **Rung A is where small models belong**, and it needs no tool calling. That makes rung A, not rung B, the
  likely honest claim for the UNO Q at this size.
- **Thinking mode is expensive at the edge.** Each thinking token is decode time on a CPU [I]. Qwen3's card
  recommends up to 32,768 output tokens for thinking [C]; on an A53 that is not a budget a board can spend
  per call [I]. Non-thinking mode is the edge setting to test first.
- **The serving stack is part of the verdict.** A model's tool calls depend on the template and the server's
  parser version [M]. A row records the server version beside the model, and a change of either needs a
  re-run of 3c.
- **For tool use at the edge, the next models to test** are the ones whose cards claim it at a size the board
  can hold: SmolLM2 1.7B [C: function calling], and a Qwen3 or Llama model between 1.5B and 3B that fits the
  board's free memory [I].

### 6.3 For self-learners

- **Read the card before you test.** The quickest wrong conclusion about a model is to grade it on a task it
  was never meant for, at settings its makers warn against. E0 did both for two of three models, and this
  page corrects the reading, not the measurement.
- **Separate three kinds of failure:** the model's (wrong content), the stack's (a parser error, a template
  mismatch, an output limit), and the harness's (a cap, a timeout, a wrong setting). Only the first is a
  statement about the model.
- **Report k of N.** One sampled run is one draw. In E0, Qwen3's single pass did not repeat in the second
  pass [M].

### 6.4 Gaps for jichi

| # | Gap | Evidence | Proposed fix |
|---|---|---|---|
| G15 | A model entry can set `temperature` only; the makers' top-k, top-p, min-p and presence penalty cannot be set per model | [V: `struct jc_model_cfg`; no such field is sent] | Design note: an optional per-model `sampling` object passed through to OpenAI-compatible servers, read leniently. Until then, server-side defaults |
| G16 | No per-model thinking switch or reasoning budget | [V: no `enable_thinking` or reasoning field is sent] | The same design note: a per-model reasoning setting, mapped per dialect |
| G17 | Nothing in the tree grades a summary, so rung A has no instrument | [V: no summary grader under `tests/`] | A small fact-retention grader (required facts present; no invented numbers), the 3a probe |

These join G10 to G14 of the edge-AI analysis.

---

## 7. Decisions for the operator

| # | Decision | Recommendation |
|---|---|---|
| D-SM1 | Adopt this procedure for every local model before it is given a role | Yes |
| D-SM2 | Thresholds for "suited for a role" | Decide them before the first run. Proposed: `summarize`, all required facts in at least 4 of 5 runs per text and no invented number ever; one tool call, the exact call in at least 4 of 5 runs per item with 1 and 3 tools |
| D-SM3 | Which models to add | SmolLM2 1.7B (its card claims function calling), and one 1.5B to 3B model that fits the board's free memory |
| D-SM4 | Build G15 and G16 | Design note now; until it is built, set the makers' settings on the server and record them |
| D-SM5 | Re-run E0's bench at the makers' settings | Yes, for Qwen3 0.6B in both modes and Llama 3.2 1B, so the bench number quoted is the model's, not the harness's |

---

## 8. The procedure as a checklist for an agent

1. Pin: model file and sha256, quantisation, server release and commit, jichi commit, hardware, and other
   load. Write them down before running.
2. Fetch the raw card and generation configuration as files; keep them with checksums. List the claimed
   tasks (quoted; model or family), languages, context, sampling per mode, thinking switch, and what is not
   claimed.
3. Serve at the makers' settings. Qwen-style models: one server per thinking mode. Set jichi's `temperature`
   to the same value. Prove with one request that the settings took effect.
4. Run the purpose probes 3a to 3f that match the claimed tasks, N = 5 each, uncapped (explicit generous
   output limit), with fences on and a minimal environment. Count model failures, stack failures (parse
   errors, limits) and harness failures apart.
5. Through jichi, only for roles passed in step 4: `summarize`, then `autocomplete`, then local-first
   routing, then the agentic bench.
6. On the board: repeat the summary and single-call probes, add speed and memory, stall off, `maxTokens`
   explicit.
7. Record one row per model and mode, with a verdict per role, using thresholds decided before step 4. Never
   quote one run as a verdict.

## 8a. The procedure, run on the three models (2026-10-23, 16:00 to 17:36)

*Added the same day, at the operator's go ("Yes, run the procedure on the three models at their settings").
Measured. Where these results differ from the predictions in §6, the results supersede them. Evidence is
kept outside the tree, on the workstation.*

### 8a.1 Setup, and what had to be corrected on the way

- **Stack:**
  - jichi `fc638450`;
  - `llama-server` `b11836` on the CPU, 10 threads, run at the lowest CPU priority from 16:26 (the operator's
    choice);
  - the three Q4_K_M models of E0.
- **The makers' settings were proven applied.** After a warm-up request, each server's slot reported:

  | Configuration | Temperature | top-k | top-p | min-p |
  |---|---|---|---|---|
  | SmolLM2 360M | 0.2 | 50 | 0.9 | 0 |
  | Llama 3.2 1B | 0.6 | 50 | 0.9 | 0 |
  | Qwen3 0.6B, no thinking | 0.7 | 20 | 0.8 | 0 |
  | Qwen3 0.6B, thinking | 0.6 | 20 | 0.95 | 0 |

  Where a maker's configuration leaves a setting unspecified, the default of its reference framework was used
  (Hugging Face transformers: top-k 50, min-p 0).
- **The thresholds were D-SM2's proposal,** written to a timestamped file before the run.
- **Uncapped.** Each reply had an explicit, generous output limit (4,096 tokens; 16,384 for thinking). Every
  reply that reached it was counted. The retrieval probe's six-step loop was a fence, and no run reached it.
- **Three corrections to my own instruments,** all made openly. Both gradings are kept where a grading
  changed:
  1. **Before the run:** a trial showed that a verbatim copy of the input passed the summary grader. The
     instruction's own 60-word limit and a copy check were added before the measured run.
  2. **After the run:** the invented-number check had two false positives. It counted the instruction's own
     "60", and it split "16.4s" and "0.5-0.9" into stray digits. Fixed and re-graded offline. After the fix,
     no reply from any model invented a number.
  3. **After the run:** one autocomplete pattern was too narrow. `{ return a + b; }` is a correct JavaScript
     continuation. Widened and re-graded.

### 8a.2 Step 3: purpose probes, straight to the server (k of N; N = 5 per item)

| Probe | SmolLM2 360M | Llama 3.2 1B | Qwen3 0.6B, no thinking | Qwen3 0.6B, thinking |
|---|---|---|---|---|
| 3a Summarise (25) | 2 (22 copies) | 0 | 5 | 3 |
| … required facts kept (mean) | — (copies) | 54% | 81% | 79% |
| 3b Instructions (50) | 33 | 37 | 37 | **46** |
| 3c One tool call, 1 / 3 / 8 tools (50 each) | 0 / 0 / 0 | 39 / 38 / 26 (strict types) | **50 / 50 / 50** | **49 / 50 / 50** |
| … reading numbers as jichi's own tools do | 0 / 0 / 0 | 49 / 47 / 35 | 50 / 50 / 50 | 49 / 50 / 50 |
| 3d Retrieval, search then read (25) | not run | 1 (21 server parse errors) | 0 | 1 |
| 3e German (25) | 1 (control) | 13 | 10 | 15 |
| 3f Reasoning (50) | not run | not run | 44 | **50** |
| Seconds per attempt (median, workstation CPU) | 0.5 | 0.7 | 0.5 | 2.8 |

What the failures were:

- **SmolLM2 cannot see tools.** Its chat template drops them: a request with tools came to 39 prompt tokens.
  Its card gives function calling to the 1.7B. Its summaries were mostly copies of the input.
- **Llama 3.2 1B:**
  - It sends numbers as strings (`"minutes": "15"`). jichi's own tools accept those (`tu_arg_int`, M168,
    `src/tools/tool_util.c:107-120`); a strict schema does not.
  - llama.cpp's tool-call parser rejected its output in multi-step use: 21 of 25 retrieval attempts and 13 of
    50 calls with 8 tools ended with "does not match the expected peg-native format".
- **Qwen3 0.6B:**
  - Its single tool calls were near-perfect in both modes.
  - In retrieval it called `search`, received titles only, and then answered **without reading**, with
    invented values ("approximately 1.8 A" for a document that says 3 A).
  - Its summaries kept about 80% of the required facts but rarely all five.

### 8a.3 Role verdicts against the thresholds fixed before the run

| Role | SmolLM2 360M | Llama 3.2 1B | Qwen3 0.6B, no thinking | Qwen3 0.6B, thinking |
|---|---|---|---|---|
| `summarize` (all facts in at least 4 of 5 runs per text, no invented number) | not suited | not suited | not suited | not suited |
| One tool call (at least 4 of 5 per item with 1 and with 3 tools) | not suited | not suited by schema types; **suited as jichi's own tools read numbers** | **suited** (8 tools too) | **suited** (8 tools too) |

### 8a.4 Step 4: through jichi

- **4.1 `summarize`: not run.** No model passed 3a.
- **4.2 Autocomplete (`jichi complete`, 10 code prefixes, 50 attempts each):** SmolLM2 21, Llama 3.2 1B 16,
  Qwen3 0.6B (no thinking) 15. None is usable for ghost text.
  - The failures: restating the line instead of continuing it; answering with jichi's own autocomplete
    instruction (SmolLM2); a different but valid continuation (counted once the pattern was widened).
  - Instruction-tuned models are the wrong kind for this role. A base model trained for fill-in-the-middle is
    the right next candidate [GK].
- **4.4 The agentic bench at the makers' settings** (Qwen3 0.6B, uncapped; no fence or output limit reached):

  | Mode | Bench | E0, off-spec |
  |---|---|---|
  | No thinking | 2 of 11 tasks, 2 of 21 points | 0 to 1 of 11 |
  | Thinking | 1 of 11, 1 of 21 | |

  Not an agent at this size, now at the right settings.
- **4.3 Local first with escalation (rung B):** Qwen3 0.6B without thinking as the fast tier, and the free
  `jlu/qwen3.8-27b` on the HRZ gateway as the strong tier. jichi escalates when the task's own verify fails.
  - **The first run was invalid, through my mistake.** The bench runner passes `--model`, and jichi said so
    in its own output: "`--model pins 'local-small' for this run; tiered routing disabled`". It also ran with
    snapshots off (see 8a.5). It is kept as evidence and not counted.
  - **The valid run** (routing given as `--route-fast`/`--route-strong`, snapshots on):

    | Measure | Result |
    |---|---|
    | Tasks | **10 of 11 (18 of 21 points)** |
    | Passed on the local model alone | 4 (tasks 01, 03, 07, 10) |
    | Escalated on a failed verify, then passed on the 27B | 6 (02, 04, 05, 06, 08, 09) |
    | Failed without escalating | 1 (task 11; see 8a.5) |
    | Model calls | 28 local, 21 to the gateway |

  - This is the first measurement of rung B. It runs on the workstation, not yet on a board.

### 8a.5 Findings about jichi (not about the models)

1. **A false "verify green" (registered with the hardening drive, severity HIGH).** jichi's end-of-turn
   verifier runs only when a pre-edit snapshot was taken (`src/chat/jc_agent.c:2877`). The report then infers
   green from an armed verifier and an OK outcome (`src/main.c:2475`). So, in two cases, a run whose
   verify command fails prints "checked: verify green" and "[envelope] verified ok", and exits 0:
   - **snapshots off** (`snapshots: false`, no git, lite mode below 1 GB of RAM). This was reproduced with
     `--verify false`: snapshots off gave "verify green", rc 0 and no verify record; snapshots on gave
     "verify RED", rc 1.
   - **a run that changed nothing** (task 11 above).

   The second case was known in the code (`src/chat/jc_agent.c:6527-6530`, the journal's `no_changes`). The
   human-facing claim was not. It also means routing's escalate-on-verify cannot fire in either case. This is
   gap G18 in the design note.
2. **`--model` turns routing off.** That is documented behaviour, and jichi prints it. But a harness that
   always passes `--model` cannot test routing, which belongs to the bench runner's design (G14).
3. **A chat template that drops the tools** (SmolLM2) looks, from outside, like a model that declines to call
   them. G12's template check would tell them apart.
4. **The server's parser decides multi-step tool use for Llama 3.2 1B** in this llama.cpp version (G11).

### 8a.6 What this means

- **Qwen3 0.6B without thinking is the edge candidate for tool roles.** Its single calls are reliable even
  with 8 tools. With a strong network model behind it, local-first routing completed 10 of 11 tasks, 4 of them
  without leaving the device. For the edge-AI analysis, rung B is now measured as viable **with escalation**
  on the workstation. Whether it stays viable at a board's speed is E2 to E4.
- **Thinking mode buys reasoning and instruction following,** not tool calls. It went from 44 to 50 of 50 on
  reasoning and from 37 to 46 of 50 on instructions, at about 5 times the time per attempt on this CPU. For
  edge tool roles, non-thinking is the setting.
- **No model here is suited to `summarize` by the criteria fixed beforehand.** Qwen3 keeps about 80% of the
  required facts. A gentler criterion would let it pass, but changing the criterion after seeing the results
  would be fitting the test to them. The next step is a larger model under the same criterion (D-SM3).
- **Llama 3.2 1B's tool calls are usable for jichi's own tools,** whose numbers are read leniently, but its
  multi-step use is blocked by the server's parser. Re-test it with another llama.cpp version before judging
  the model.
- **SmolLM2 360M has no tool role.** Its best result is continuing code (21 of 50), which is still not
  usable.

---

## 9. What this analysis did not do, and what to doubt

- **Nothing was measured for this document.** It re-reads E0 against the cards. The predictions in §6 are
  [I] until §5 runs.
- **Cards are their makers' claims.** They describe the intended use and the makers' own evaluations, often
  for a whole family. A card claim is a reason to test, not a result.
- **Meta's "Intended Use" section was not fetched** (the repository is gated). Statements about it are [GK].
- **The probes' item sets are not written yet.** §5 states their shape, size and grading. Writing them is the
  first piece of work, and their quality decides the results.
- **The thresholds in D-SM2 are proposals.** The operator's rule is that limits come from measured
  distributions. These are pass criteria, not caps, but they are still judgements to be decided before the
  run.

---

## 10. Exercises for a self-learner

1. **Read a card.** Open SmolLM2's card and find the sentence that decides whether the 360M should be tested
   on tool calls. *Check:* "function calling (for the 1.7B)".
2. **Find the warning.** In Qwen3's card, what does "Best Practices" say about greedy decoding, and what did
   E0 use? *Check:* "DO NOT use greedy decoding"; E0 used temperature 0.2 with the server's top-k 40.
3. **Classify a failure.** "The server reported: does not match the expected peg-native format." Is that the
   model, the stack or the harness? *Check:* the stack: the server's parser rejected the output before jichi
   saw a call. The model's output may still have been malformed, so the follow-up is to try another server
   version or parser.
4. **Write a grader.** For the text "The cache holds 128 pages of 64 KB; eviction is LRU." write two required
   facts and one forbidden-number check. *Check:* "128", "LRU" required; any number other than 128 and 64 in
   the summary is invented.
5. **Choose a role.** A board has 2.3 GB free and a CPU decoding at a few tokens per second. Which jichi role
   would you give a 1B model first, and why not the coding agent? *Check:* `summarize`. It needs no tool
   calling, is the model's stated purpose, and its output is short; the agent needs reliable multi-step tool
   use, which E0 did not find at this size.

---

## 11. Sources

- **Model cards and configurations**, fetched 2026-10-23 as raw files and kept with checksums:
  - Qwen/Qwen3-0.6B: `README.md`, `generation_config.json`;
  - HuggingFaceTB/SmolLM2-360M-Instruct: `README.md`, `generation_config.json`;
  - unsloth/Llama-3.2-1B-Instruct (an ungated mirror of Meta's repository): `README.md`,
    `generation_config.json`;
  - bartowski/Llama-3.2-1B-Instruct-GGUF: `README.md` (the prompt format and the licence text).
- **`llama-server --help`**, release `b11836`: the sampling and reasoning options.
- **jichi `fc638450`:** `include/jc_config.h` (`struct jc_model_cfg`), `src/provider/jc_provider_openai.c`
  (what is sent; `reasoning_content` read apart from the answer), `tests/bench/README.md` (`schema_probe.py`).
- **Companions:** `docs/analysis/2026-10-23-edge-ai.md` (E0, §8a), `docs/analysis/2026-10-23-orchestration-at-scale.md`
  (the scale vocabulary).
