# Design note: closing the edge and small-model gaps (G5, G9 to G18)

*Written 2026-10-23 at the operator's request ("Then design to address the gaps, please"), under the standing
rule of the same day: design now, build when needed. **Nothing here is built.** Each section names the gap,
the evidence, the design, what it rejects, the red-first test that would prove it, and when to build it.*

**Where the gaps come from:**

- G1 to G9: [`docs/analysis/2026-10-23-edge-ai.md`](../analysis/2026-10-23-edge-ai.md) §6;
- G10 to G14: that analysis's E0 measurement, §8a.5;
- G15 to G17: [`docs/analysis/2026-10-23-small-models-purpose-and-tests.md`](../analysis/2026-10-23-small-models-purpose-and-tests.md) §6.4.

Line references are to jichi `fc638450`.

---

## 0. Summary

| Gap | Design | Size | Build when |
|---|---|---|---|
| G5 | An `edge` preset: a short system prompt, the `core` tools, an explicit output limit | small | before rung A or B on a board |
| G9 | Local-only routing: a project or session pinned to local routes, with a path taint for file tools | medium | when a deployment must keep files on the device |
| G10 | Show where the output limit comes from; an `auto-remaining` limit | small | now: it already distorted one measurement |
| G11 | A `model-format` failure class with one nudged retry | small | before any llama.cpp route carries an agent |
| G12 | `doctor --live`: name the text-recovery flag, split cached and uncached tokens, detect a template that drops the tools | small | with G11 |
| G13 | `strictness.toolErrorLoop`: off, warn or park; default warn, provisional | small | after its threshold is measured from telemetry |
| G14 | The bench runner: `--uncapped` and a minimal environment by default | small | now: the next measurement needs it |
| G15 | A per-model `sampling` object, passed through per dialect | small to medium | before small models get a role |
| G16 | A per-model `reasoning` object, mapped per dialect | small to medium | with G15 |
| G17 | A summary grader and the summary text in full-level telemetry | small | before rung A is claimed |
| G18 | A verify claimed green that never ran: report green only from a run, and run the verifier without a snapshot | small | **now** (HIGH) |

Not designed here, because another note owns them:

- G1, G6, G7 and G8 need the board or a measurement: the edge analysis, plan E1 to E6;
- G2 is [`2026-10-23-measure-stream-silence.md`](2026-10-23-measure-stream-silence.md);
- G3 and G4 are the route table, D-S11 of the scale analysis.

Every design follows jichi's standing rules:

- a value a human writes in a config is read leniently;
- a refusal names what to do instead;
- every new event has a reader;
- strictness is configurable (off, warn, refuse), with telemetry in every mode, and defaults stay provisional
  until long-run data decides them.

---

## G5. An `edge` preset

**Gap.** jichi's fixed prompt is about 2,300 to 2,560 tokens with the `core` tools [measured, E0]: 1,054 of
system prompt and 1,122 of tool schemas by jichi's own estimate. A 4,096-token window leaves little for
history and answer.

**Design:**

- A config value `preset: "edge"` that only sets defaults the user did not set:
  - `toolProfile: core`;
  - a short system prompt variant (the same rules, without the long examples);
  - `autoContext: false`, `repoMap: false`;
  - `maxTokens` per G10's `auto-remaining`;
  - `maxParallelAgents: 1`, `maxSubagentDepth: 0`.
- `doctor` prints the preset, and which keys it set.
- `jichi context` shows the resulting fixed prefix.

**Rejected:**

- a separate binary: the one-binary rule;
- shrinking the default prompt for everyone: the rules in it were each earned;
- an automatic switch by RAM: lite mode already does that, and a 4 GB board is not low on RAM; its limit is
  the model's window.

**Red-first test.** A driver asserts that the edge preset's fixed prefix is at most a stated budget (set from
`jichi context` on the first build), and that an explicit user key overrides the preset. Remove the preset
mapping and the first check fails.

**Build when:** before rung A or B runs on a board.

---

## G9. Local-only routing (privacy)

**Gap.** "Sensitive files never leave the device" is not something jichi implements. The session chooses a
model; the file does not [edge analysis G9].

**The difficulty, stated first.** File content can enter the history through many doors:

- `read_file`, `search_code` and `codebase_search` results;
- a `@`-reference;
- **the shell**: `cat`, `grep`, a build log.

jichi can see the first three. It cannot reliably see what a shell command printed. A per-file taint that
ignores the shell is a leak, documented as a guarantee.

**Design, two levels:**

1. **Project level (sound, simple).** `privacy: { "localOnly": true }`:
   - every model call of the run must go to a route marked `local: true`, meaning loopback or the LAN
     addresses the config declares;
   - routing, escalation and fallback are filtered to local routes, so an escalation to a network model is
     refused and named;
   - if no local route is reachable, the run parks (outage design D3) rather than sending anything;
   - `doctor` lists the local routes, and refuses to call a config local-only when its chat model is not.
2. **Path level (narrower, with a stated hole closed).** `privacy: { "localOnlyPaths": ["secrets/**", "*.pem"] }`:
   - when a file tool, a search or an `@`-reference puts content from a matching path into the history, the
     **session** is pinned local from then on, because the history now contains the content;
   - the shell is **refused** in such a project, as `--strict-scope` refuses it, because shell output cannot be
     attributed to paths;
   - a `privacy_pin` telemetry event records the path glob (never the content) and the turn.

**Model entries** gain `local: true|false`. The default is inferred from the `apiBase` host (loopback or a
private address means local) and shown by `doctor`. An explicit value wins. It is the same "locality" field
the route table needs (D-S11).

**Rejected:**

- **per-file taint without refusing the shell:** it would claim a guarantee it cannot keep;
- **redacting protected content before sending it:** it fails on paraphrase, and partial redaction is worse
  than a clear refusal;
- **trusting the model to avoid sending:** the model is not a fence.

**Red-first tests:**

1. `localOnly` with a network chat model: the run refuses before the first call. Remove the check and the
   mock network endpoint receives a request.
2. `localOnlyPaths`: after reading a protected file, an escalation is refused and the network mock receives
   nothing. Remove the pin and it does.
3. `localOnlyPaths`: the shell is refused, and the refusal names the reason.

**Build when:** a deployment must keep files on the device. Until then, document "use a local-only config"
as the honest answer.

---

## G10. The derived output limit

**Gap.** Without `maxTokens`, jichi asks for one fifth of the declared window, at least 512 and at most
16,384 (`jc_config_effective_max_tokens`, `src/config/jc_config.c:2371-2392`). E0's first pass hit it in 2 of
35 and in 10 of 17 calls. At a board's 4,096-token window it is 819 tokens: thinking models will be cut there.

**Design:**

1. **Visibility first.**
   - `doctor` prints each model's effective output limit and its source (`configured` or `derived:
     window/5`).
   - `model_call` telemetry gains `max_tokens` and `max_tokens_source`.
   - The existing "OUTPUT CEILING" note (`src/provider/jc_provider_openai.c:352-356`) names the source and the
     key to set.
2. **An opt-in `maxTokens: "auto-remaining"`.** Per call, ask for the declared window minus jichi's estimate
   of the prompt, minus a margin, never below 512. A small window then gives the answer everything that is
   left, instead of a fifth.
3. **No change to the default** until telemetry shows how often the derived limit is hit in real runs. That
   is a count jichi can make once the source is recorded.

**Rejected:**

- **raising the fixed fraction:** a different guess, still a guess;
- **omitting `max_tokens`:** M84 sends it on purpose, because some servers default to a tiny value.

**Red-first test.** A mock that returns `finish_reason: "length"` whenever `max_tokens` is below N. With
`auto-remaining`, a short prompt in a window of 4N gets a full answer. Revert to the derivation and it is cut.
A second check asserts `max_tokens_source` in telemetry.

**Build when:** now. It already made one measurement's first pass a capped one.

---

## G11. A server-side tool-call parse error ends the turn

**Gap.** llama.cpp's server answered Llama 3.2 1B's malformed tool calls with an error inside an HTTP 200
stream ("The model produced output that does not match the expected peg-native format"). jichi ended the
turn as a provider error, with no retry, in 7 of 11 tasks [measured, E0].

**Design:**

- **A tenth failure class, `model-format`,** for provider errors whose message matches known
  model-output-format patterns: llama.cpp's "does not match the expected … format", and the equivalents
  of other servers as they are observed. The patterns live in one table, and each entry cites the server
  version where it was seen.
- **On `model-format`, retry once with a nudge**, as M147 does for a described call. The nudge asks for
  exactly one tool call with valid JSON arguments. A second `model-format` in the same round ends the turn,
  and the end record names the class.
- **Telemetry:** `model_call.fail_class = "model-format"`, and a retry counter on the turn.
- **Route profiles** (D-S11) record the server version a model's tool calls were tested on. A version change
  marks the profile as needing re-measurement.

**Rejected:**

- **retrying every provider error:** it would hide outages, which the fallback and park logic own;
- **asking the server not to parse tool calls:** no portable request field exists, and jichi's text recovery
  would then carry what the server's template already does.

**Red-first test.** `mockmodel` replies with the llama.cpp error text in the stream once, then with a valid
call. The turn completes with exactly one retry. Remove the class and the turn ends with `provider error`. A
second check: two errors in a row end the turn, with `model-format` in the end record.

**Build when:** before any llama.cpp route carries an agent role.

---

## G12. `doctor --live` wording, and a template that drops the tools

**Gaps** [measured, E0 and the trial of 2026-10-23]:

- with `toolCalling: "text"`, the probe says "configured native" (the text mode keeps the native form and
  sets a separate recovery flag);
- "probe prefix was 1 real prompt token" when the server answered from its cache;
- SmolLM2's chat template drops the tool definitions entirely: a request with tools was 39 prompt tokens. The
  model never sees the tools, and today that looks the same as a model that declines to use them.

**Design:**

1. Print "configured: native, text recovery on" when `textToolCalls` is on.
2. Print the probe's prompt tokens as "N in total, M of them cached".
3. **A template check.** Send the probe once with the tools and once without. If the two prompt-token counts
   differ by less than the tools' own estimated size, report "the server's chat template did not include the
   tools: this model cannot see them". That is a stack verdict, not a model verdict, and it costs one extra
   short request.

**Rejected:** inferring the template from the model's name: names lie, and templates change between quantised
copies.

**Red-first test.** A mock server whose prompt-token count ignores the tools array makes the check fire. A
mock that counts the tools does not. Remove the comparison and the first check stays silent.

**Build when:** with G11. Both are about telling the stack's failures from the model's.

---

## G13. A tool-error loop that nothing stops

**Gap.** Llama 3.2 1B made 175 and 194 calls in two tasks, mostly the same failing `read_file` [measured, E0].
jichi's detector warns, journals a `tool_loop` record and appends a note to the tool result
(`src/chat/jc_agent.c:4845-4890`), but the turn goes on until the tool-call fence. On a board that decodes a
few tokens a second, such a loop costs hours.

**Design:**

- `strictness.toolErrorLoop`, with the same shape as M1006's guard:
  - `off`: no detection;
  - `warn`: today's behaviour, and the default;
  - `park`: after N identical failures in one turn, end the turn with outcome `stopped: tool_error_loop`.
    That is an *unknown* result, never a success and never a model failure, so a measurement records it
    as a fence that fired.
- N comes from data. The drives' telemetry already records every `tool_loop` journal line and every
  `tool_call` with its failure class. The threshold is the smallest N that no completed, verified run ever
  reached. Measure it first, then set it.
- A typo in the mode is loud, as for M1006.

**Rejected:**

- **a fixed small N now:** it would be a cap chosen before the distribution is known;
- **stopping on any repeated call:** repeated successful reads are normal.

**Red-first test.** A mock that always calls `read_file` on a missing path. Under `park`, the turn stops at N
with the named outcome. Under `warn`, it continues to the fence. Remove the park branch and the first check
fails.

**Build when:** after the threshold is measured, which needs no new code: it is a telemetry query.

---

## G14. The bench runner's caps and environment

**Gap.** `tests/bench/run_bench.py` bounds every task with a token budget, a 10-minute deadline and a hard
900 s timeout. It also passes the whole caller environment to jichi [V]. During E0, `doctor` flagged a
caller's token variable that would have reached jichi's children. A measurement must run uncapped.

**Design:**

- `--uncapped`:
  - no `--budget-tokens`, no `--deadline` and no subprocess timeouts;
  - `--max-tool-calls` kept as a fence, raised to a stated value;
  - per task, the record says whether the fence or the output limit was reached;
  - each task's first prompt size and its system and tool estimates go into the record.
- **A minimal environment by default:** `PATH`, `HOME`, `LANG` and `LC_ALL`. Anything else must be named
  with `--pass-env`.
- `--max-tokens` writes an explicit `maxTokens` into the run's config copy, and the summary prints it (G10).

E0's modified runner is the reference implementation. Its diff against `fc638450` is kept with the E0
evidence.

**Rejected:** removing the capped mode. The bounded mode is the right tool for a quick smoke run, as long as
its results are never quoted as measurements.

**Red-first test.**

- A task whose mock model answers after the deadline would have expired: it completes under `--uncapped` and
  is cut without it.
- A dummy secret variable in the caller's environment is absent from the child's environment by default.
  Remove the filter and it appears.

**Build when:** now. The re-run of E0's bench at the makers' settings (D-SM5) needs it.

---

## G15. The makers' sampling settings

**Gap.** A model entry carries `temperature` only (`struct jc_model_cfg`). The makers of all three E0 models
prescribe more: top-p, top-k, min-p, and for Qwen3 a presence penalty against repetition. jichi sends none of
them [V].

**Design:**

- **A per-model `sampling` object** with known keys only: `topP`, `topK`, `minP`, `presencePenalty`,
  `frequencyPenalty`, `repeatPenalty`, `seed`.
  - Each key is optional and read leniently: a number written as a string counts, and prose falls back to
    "unset".
  - An unset key is not sent, so the server's default applies, as today.
- **Mapped per dialect:**

  | Dialect | Sent as standard fields | Sent only to servers that declare them | Not supported |
  |---|---|---|---|
  | OpenAI-compatible | `top_p`, `presence_penalty`, `frequency_penalty`, `seed` | `top_k`, `min_p`, `repeat_penalty` (llama.cpp, vLLM, LM Studio), switched on by a per-model `extendedSampling: true` | — |
  | Anthropic | `top_p`, `top_k` | — | the other keys; `doctor` warns when the config sets them |

- **`doctor`** prints each model's effective sampling, and warns when a key will not be sent.
- **Telemetry:** `run_start` records each model's sampling (numbers only).
- **The preview rule** (approval integrity): no preview is involved; these are request fields only.

**Rejected:**

- **an arbitrary passthrough object** (`extraBody`): it could override `model`, `tools` or `messages`, and
  would make the request a user-editable surface. Known keys only.
- **setting sampling on the server only:** that works for one model per server process, and it is today's
  workaround. But a route serving several jichi roles cannot have different settings per role.

**Red-first test.** A mock captures the request. With `sampling.topK` set and `extendedSampling` on, the
capture contains `top_k`. With it off, it does not. With nothing set, the request is byte-identical to
today's. Remove the mapping and the first check fails.

**Build when:** before a small model is given a role. Until then, the makers' settings go on the server,
recorded in the run's notes.

---

## G16. Thinking on or off, per model

**Gap.** jichi sends no thinking switch [V]. Qwen3 thinks by default, and on a CPU board every thinking token
is decode time. jichi already keeps the reasoning text out of the history: it records only that reasoning
happened (`src/provider/jc_provider_openai.c:447-456`). That already follows Qwen's "no thinking content in
history".

**Design:**

- **A per-model `reasoning` object:** `enabled` (true or false), `budget` (tokens), `effort` (low, medium or
  high).
- **Mapped per dialect:**

  | Dialect | Mapping |
  |---|---|
  | OpenAI-compatible, llama.cpp-style | `chat_template_kwargs: {"enable_thinking": false}` when disabled (the request-level form of the server's `--chat-template-kwargs`; whether a given server honours it per request is checked by G12's probe) |
  | OpenAI | `reasoning_effort` |
  | Anthropic | `thinking: {type, budget_tokens}` |

- **Per role,** by giving one route two aliases (D-S11): `summarize` can use the non-thinking alias while
  `chat` thinks.
- **`doctor --live`** sends one probe with the switch and reports whether reasoning content still appeared,
  so a switch that the server ignored is visible.

**Rejected:** the in-prompt soft switch (`/no_think`). It is model-specific text in the user's prompt, and
Qwen's own card says it is ignored when thinking is hard-disabled.

**Red-first test.** A mock captures the request. A model with `reasoning.enabled: false` sends the dialect's
field, and one without sends nothing. A second check uses a mock that streams `reasoning_content` despite
the switch: `doctor --live` reports "the server ignored the thinking switch".

**Build when:** with G15.

---

## G17. Grading summaries

**Gap.** Nothing in the tree grades a summary [V]. Rung A, local compaction, cannot be claimed without it.
jichi's `compact` event records counts (`before`, `after`, `target`, `limit`), not the summary text
(`src/chat/jc_agent.c:5308`, `:6121`).

**Design:**

1. **`tests/bench/summary_probe.py`:** the 3a probe of the small-model procedure, stdlib only.
   - Fixed texts, each with its required facts.
   - A pass needs every fact, no invented number, the instruction's own length limit, and **no copy**
     (word-sequence similarity below 0.8).
   - The copy check was found necessary on 2026-10-23: a model that echoed the input passed the fact checks.
2. **Through jichi:** at `--log-level full`, the `compact` event also records the summary text, redacted
   as every full-level text is, with its length and the summarising model's id. A driver forces a compaction
   with a mock `summarize` model and asserts that the text is there. At `metrics` level it is absent, as
   today.
3. **A reader:** `jichi telemetry` shows compactions per summarising model and the median summary length,
   satisfying the new-event-needs-a-reader lint's requirement.

**Rejected:**

- **a model as the judge:** it moves the question to another model's judgement. A model may draft, and a
  program decides, as JCP P1 says.
- **storing summaries at metrics level:** content stays at full level only.

**Red-first test.**

- The probe's grader fails a verbatim copy and a summary with an invented number, and passes a hand-written
  summary. Remove the copy check and the copy passes.
- The driver's text check fails at `metrics` level, and passes at `full`.

**Build when:** before rung A is claimed for any model.

---

## G18. A verify claimed green that never ran (added 2026-10-23, 17:40)

**Gap** [measured: the small-model procedure §8a.5; reproduced on `fc638450` with a real local model]. The
end-of-turn verifier runs only when a pre-edit snapshot was taken (`src/chat/jc_agent.c:2877`). The run
report then infers green from "a verifier is armed and the outcome is OK" (`src/main.c:2475`). With
`--verify false`, a verifier that always fails:

| Case | jichi reports | Exit code | Verify records in the journal |
|---|---|---|---|
| snapshots off | "checked: verify green", "[envelope] verified ok" | 0 | 0 |
| snapshots on | "verify RED", "verify_failed" | 1 | 1 |

A run that changes nothing gets the same false green with snapshots on. That case is known in the code
(`jc_agent.c:6527-6530`, the journal's `no_changes`), but the human-facing lines still claim green. Lite
mode (below 1 GB of RAM) has snapshots off, so every `--verify` run on a small board is affected. Routing's
escalate-on-verify cannot fire in either case.

**Design:**

1. **Green only from a run.** The envelope records `verifier_ran` and its exit code.
   - The report says "verify green" only when the verifier ran and exited 0.
   - Otherwise it reports "not checked: verifier did not run (no snapshot)" or "(no change made)".
   - The `[envelope]` footer says "ok, not verified (…)", never "verified ok".
   - The journal's `end` record gains `verified: true | false | "not_run"`.
2. **Run the verifier even without a snapshot.** A snapshot is needed to *roll back*, not to *check*. Without
   one, a red verify is still `verify_failed`, with exit 1, and the work is kept as it is, because there is
   nothing to roll back to. The report says so.
3. **Run it for a run that changed nothing too.** A "did nothing" run with a failing check then ends RED,
   which is what it is. A passing check stays green: the task may have needed no change.
4. **`doctor`** warns when `--verify` or a configured verifier meets snapshots that are off: "failures cannot
   be rolled back".

**Rejected:**

- refusing `--verify` without snapshots: small boards need the check most;
- keeping the inference: a claim of green must come from a check that ran, never from the absence of red.

**Red-first tests.** A driver with `mockmodel` covers three cases:

1. snapshots off, `--verify false`: must exit 1, with one journal `verify` record;
2. a run that changes nothing, `--verify false`: must exit 1;
3. `--verify true` with no change: green, with a `verify` record.

Cases 1 and 2 are red on `fc638450`, which proves the driver can fire.

**Build when:** now. Registered with the hardening drive as a HIGH finding (inbox, 2026-10-23 17:31, with an
addendum for the no-change case).

**Decided (the operator, 2026-10-23, about 20:25):** "Yes, fix G18 before P3, as you recommended." The
hardening drive registered it as M1048 (run 0076) and paused at 20:05. The jichi-harness drive builds it under
that number, as its own batch right after its batch H0 and before phase P3, whose `verify` would otherwise
trust the false claim.

---

## Order, if all were wanted

1. G18 first: a false verification claim, HIGH. Then G14 and G10, the measurement tools.
2. G15 and G16: without them a small model cannot run at its makers' settings through jichi.
3. G17, then rung A on the workstation and later on the board.
4. G11 and G12, before llama.cpp routes carry agent roles.
5. G13, after its threshold is measured.
6. G5, before a board runs rung A or B.
7. G9, when a deployment asks for it.

Each step is a milestone with its own red-first tests, and each changed default is provisional until
telemetry decides it.
