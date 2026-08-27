# Can a local model drive jichi's tools? — a measured answer, and three broken instruments

*Measured 2026-08-21 on one bench: a 16 GB ROCm card (gfx1100-class), LM Studio
serving eight installed models on `:1234`, jichi 0.9.0 at M519. Every number here
was read off the running server, `lms ps`, the server log, or jichi's own output.
Where I state what something means, that is an inference and is marked as one.*

**Read this if** you are pointing jichi at a local model and something says "this
model cannot call tools" — or if you want to watch a measurement go wrong four
times in a row and see how each wrong answer was found.

---

## 1. The question, and why the honest answer took five attempts

jichi's file tools are the whole product: a reviewer that cannot call `read_file`
answers plausibly about a diff it never opened. So the one thing that matters when
choosing a local model is **does it emit native tool calls** — a `tool_calls`
array the agent loop can execute — rather than *describing* a call in prose that
jichi has no way to run.

The answer, finally, is **yes, five of six models on this bench do**. Getting
there produced four wrong answers first, each of them plausible, each published
nowhere only because the next layer down was checked:

| Attempt | What I concluded | What was actually true |
|---|---|---|
| 1 | "the gateway is too slow to review" | my own `--deadline 15m` fired, and a missing `< /dev/null` had made jichi read stdin as prompt context; with stdin closed the same call took **0.937 s** |
| 2 | "no local model here can call tools" | true of the *resident* model only; nobody had asked the others |
| 3 | "five of eight models fail to load — reinstall them" | the card was full. The resident 14B held **11.7 GB** with no TTL, so nothing could load beside it |
| 4 | "the server cannot translate tool calls at all" | my probe's `max_tokens: 96` truncated the reply *inside the model's reasoning block*, before it ever reached its tool call |
| 5 | **five of six models emit native tool calls; one genuinely cannot** | this document |

Between 4 and 5 the probe found a *fifth* fault — in itself. More on that in §4,
because it is the most instructive one.

**The lesson is not "check twice."** It is that **a symptom reported by the layer
you are standing on always looks like a finding.** Each of those four answers was
a faithful report of what one layer said. The way down is to ask the thing itself:
the server instead of the client, the log instead of the server, `lms ps` instead
of the log.

---

## 2. What I changed on the machine, and what I put back

The operator granted a model-swap experiment on the condition that it be
announced in advance and written up. So, plainly — **this is the whole protocol,
and it is worth copying if you do this on a shared box:**

**Recorded first, before touching anything** (you cannot restore a state you did
not write down):

```
$ lms ps
IDENTIFIER              SIZE     CONTEXT  PARALLEL  TTL
qwen/qwen2.5-coder-14b  8.99 GB  16384    4         (none)

VRAM used: 12,909 MB of 16,368        8 models installed, 59.55 GB on disk
```

**Then, one model at a time:** `lms unload --all`, `lms load <id> -c 8192
--parallel 1 -y`, probe, next. Single slot and a small window on purpose — the
question was *can it call a tool*, not *how big a context fits*, and a smaller
footprint means the load either works or the card is genuinely too small.

**Restored at the end,** to the recorded numbers exactly:

```
$ lms load qwen/qwen2.5-coder-14b -c 16384 --parallel 4
$ lms ps
qwen/qwen2.5-coder-14b  8.99 GB  16384    4         (none)      VRAM 12,712 MB
```

Nothing was installed, deleted, or re-quantized; the only mutation was which
weights sat in VRAM, and that is now what it was. **The unload is also the
measurement that killed attempt 3:** with the 14B gone, VRAM fell from 12,909 MB
to **1,209 MB**, which is how you know the 14B was holding 11.7 GB — 8.99 GB of
weights plus roughly 2.7 GB of KV cache for four slots at 16k.

---

## 3. The measurements

**Loading, on a free card, `-c 8192 --parallel 1`:**

| model | loads | seconds | VRAM after load |
|---|---|---|---|
| `qwen/qwen3.5-9b` | yes | 3 | 7,689 MB |
| `prism-ml/bonsai-27b` | yes | 4 | 6,577 MB |
| `google/gemma-4-12b-qat` | yes | 5 | 3,959 MB |
| `google/gemma-4-e4b` | yes | 8 | 5,511 MB |
| `google/gemma-4-12b` | yes | 11 | 9,293 MB |

**All five load in eleven seconds or less.** Every one of them had "failed to
load" an hour earlier. Nothing was repaired in between — the card was simply
empty. A full GPU reports exactly the way a broken model does, and the server log
is where the difference shows (`unable to allocate ROCm0 buffer`).

**Tool calling, `tool_choice: "auto"` (what jichi sends — `jc_provider_openai.c`,
`build_request`), `max_tokens: 600`:**

| model | verdict |
|---|---|
| `qwen/qwen3.5-9b` | **native** |
| `prism-ml/bonsai-27b` | **native** |
| `google/gemma-4-12b-qat` | **native** |
| `google/gemma-4-e4b` | **native** |
| `google/gemma-4-12b` | **native** |
| `qwen/qwen2.5-coder-14b` | **prose — unusable by jichi** |

The one that fails is the one that was resident, and therefore the only one
anybody had tested. Its reply, in full:

```json
"finish_reason": "stop",
"tool_calls": [],
"content": "<tools>\n{\n    \"name\": \"read_file\",\n
             \"arguments\": {\n        \"path\": \"notes.txt\"\n    }\n}\n</tools>"
```

The model did everything right. It picked the correct tool and the correct
argument — and emitted it in Qwen2.5's `<tools>` template, which **this GGUF's
prompt template never translates back into a `tool_calls` array**. jichi sees an
assistant message containing angle brackets, no tool call, `finish_reason: stop`,
and stops. This is a *server-side template* problem, not a model capability
problem, and it is worth knowing the difference: the same weights behind a
correctly templated server would work.

**So jichi's own verdict was right the whole time.** `doctor --live` had said
`tool calling observed "text"` about this model on day one. Four layers of my
diagnosis were wrong about the *cause*; jichi's instrument was never wrong about
the *fact*.

---

## 4. The instrument that could not say yes

`scripts/probe-models.sh` classified every one of the five natively-capable
models as `prose`. The classifier:

```sh
if grep -q '"tool_calls"[[:space:]]*:[[:space:]]*\[[[:space:]]*{' "$tmp/out.json"; then
    tools=native
elif grep -q '"name"' "$tmp/flat.json" && grep -q '"arguments"' "$tmp/flat.json"; then
    tools=prose
```

**LM Studio pretty-prints its JSON.** The bytes are:

```json
        "tool_calls": [
          {
            "type": "function",
```

`grep` matches within a line, and `[[:space:]]` does not cross a newline. So the
`native` branch **could not fire at all** against this server — and the fallback
then matched `"name"` and `"arguments"` *inside the real `tool_calls` array* and
reported the exact opposite of the truth.

Three properties made this nasty, and they generalize:

1. **It was silent.** Every run produced a well-formed table with plausible
   verdicts. No error, no empty field, nothing to notice.
2. **It was server-dependent.** The HRZ gateway returns compact single-line JSON,
   so the same code answered *correctly* there. One instrument, two servers, one
   inverted answer — and the correct answers are what built confidence in it.
3. **Its fallback branch was confidently wrong.** A classifier whose "else" is a
   positive claim (`prose`) rather than "I could not tell" converts a matching
   failure into a finding. Had the else been `unknown`, five `unknown`s would have
   sent me straight to the bytes.

The fix is one line — `tr '\n' ' '` before matching — and a header comment saying
why. The verification is the same ritual a test gets: the recorded response goes
from **0 matches** under the old pattern to **1** under the new one.

That makes **three** of my own measuring instruments wrong in one session, all in
the same direction — a budget or a pattern that fabricated a plausible negative:
a `--deadline` that fired, a `max_tokens` that truncated, a `grep` that could not
see past a newline. **A request budget is a measurement instrument.** Too small a
one does not produce a smaller answer; it produces a different one.

### Why `max_tokens: 96` lied

Worth its own paragraph, because reasoning models make this a permanent trap.
`gemma-4-12b` answers like this:

```json
"reasoning_content": "The user wants me to read a file named \"notes.txt\".
                      I should use the `read_file` tool for this purpose...",
"tool_calls": [ { "function": { "name": "read_file", ... } } ]
```

The chain of thought is **billed against the same `max_tokens`** and arrives
*first*. At 96 tokens the reply ended mid-thought: `finish_reason: length`, empty
content, no tool calls — indistinguishable from a model that ignored the tool. At
32 tokens an earlier probe had truncated a prose call before its `arguments` key
and reported `none`. **Both numbers turned a capable model into an incapable
one.** The probe now sends 600 and records in its header why that number is not a
guess.

---

## 5. End to end through jichi, which is the only proof that counts

A probe uses one non-streaming HTTP request. jichi streams, parses SSE, and
accumulates tool-call deltas — a different code path, so a passing probe proves
nothing about the product. Pointing the shipped local config at
`google/gemma-4-12b-qat` (native, and the **smallest footprint of the five** at
3,959 MB, which leaves room for context):

```
$ lms load google/gemma-4-12b-qat -c 32768 --parallel 1
$ jichi --config config.jichi-dev-local.json doctor --live
✓ --live: tool calling observed "native" (configured "native")
    native tool calling confirmed -- probe prefix was 104 real prompt tokens
27 ok, 7 warnings, 0 problems
```

And a real turn, in a scratch workspace holding one file:

```
$ jichi -p "Read the file notes.txt and tell me how many arenas it names."
[tool] list_files  .          [tool list_files -> ok]
[tool] read_file  notes.txt   [tool read_file -> ok]
The file `notes.txt` names 3 arenas: session, scratch, and tool_scratch.

real 0m25.8s        tokens in=3,418/3,449/3,507  out=54/32/26
```

Two tool rounds and a correct answer in 26 seconds, entirely on one desktop card.
**The self-hosting pack has a working local reviewer** — it just was not the model
the pack named.

---

## 6. The defect this uncovered: fifteen configs that unfenced themselves

The most expensive finding of the day was not about models at all. Reading
`doctor`'s full output on the local config — the part I had been scrolling past —
turned up:

```
! path fence off
    file tools may read/write outside the workspace
```

The config says `"pathFence": 1`.

`jc_json_get_bool` required a real JSON boolean. Given the **number** `1` it saw
"wrong type" and returned the caller's default — and the surrounding code is:

```c
if (jc_json_get_obj(root, "pathFence") != NULL) {
    out->path_fence = jc_json_get_bool(root, "pathFence", 0) ? 1 : 0;
}
```

The *presence* check fires, so `path_fence` is explicitly set to **0**,
overriding the `-1` default that means "on in autonomous postures only". Writing
`1` for the fence did not fail to turn it on. **It turned it off** — in exactly
the unattended runs where a fence is the entire safety story.

**Fifteen shipped `examples/` configs said `"pathFence": 1`.** Sixteen occurrences
counting `"selfReview": 1` in the self-hosting write config — added the previous
milestone while *tightening* that config, and it had been disabling self-review
ever since.

The same defect ran the other way too. Seven config keys default to `1`
(`wisdom`, `fuzzyEdit`, `snapshots`, …), so `"snapshots": 0` fell through to the
default and ran **with snapshots on**. A config that said off was on. And
`"lowResource": 1` returned the `-1` "key absent" sentinel, letting
auto-detection override the user's explicit word — which the comment directly
above that line says must never happen.

### The fix, and why it is two functions rather than one

The house had already answered this question one declaration higher in the same
header: `jc_json_get_num` is strict and `jc_json_get_num_lenient` exists
alongside it, accepting a numeric *string* because "that is what a model sends
when it gets the JSON type wrong" (M168). Leniency is **opt-in per call site**.

So: `jc_json_get_bool_lenient` accepts a number (nonzero = true) and the
unambiguous strings (`true`/`false`, `yes`/`no`, `"1"`/`"0"`, case-insensitive),
and **prose still falls through to the default** — the same boundary, for the same
reason: guessing at prose is how a typo becomes a policy change.

Converted, deliberately, only where a **human or a foreign program** writes the
JSON:

| file | sites | why |
|---|---|---|
| `src/config/jc_config.c` | 40 | people write config files |
| `src/convert/jc_convert_opencode.c` | 10 | reads *another* agent's config format |
| `src/mcp/jc_mcp_proto.c` | 2 | a third-party MCP server's `isError`/`readOnlyHint` |

Eight source files still use the strict accessor, and should: they read our own
state files and our own providers' wire data, where a non-bool is a bug to see,
not a dialect to forgive.

The MCP site deserves naming. `isError` is how a foreign tool server reports
failure, and jichi's own invariant is that **tool errors are values**. A server
answering `"isError": 1` was read as *false* — a failed tool call reported to the
model as a success. No test covered it because no mock server sent a number.

### Proving it, twice

- **Unit:** `test_json_bool_lenient` — numbers both ways, real bools unchanged,
  the string forms, prose refused in *both* directions, and three checks
  asserting the **strict** accessor still behaves strictly. Stubbed back to the
  old behaviour it fails **11 checks**; restored, 12,586 checks / 0 failures.
- **Lint:** `tests/smoke/config_bool_lint.sh`, 8 checks. Three of them ask the
  binary for the *effect* — a shipped config with `"pathFence": 1` must report
  `path fence on`, a numeric `0` must switch a default-on key off, and prose must
  still fall through. Reverting a **single** call site to the strict accessor
  turns three checks red, including both effect checks.

Not one of the fifteen configs was edited. Fixing files would have left the
sixteenth config broken; the value was always right, the reading of it was wrong.

---

## 7. What to do on your own machine

1. **Ask the server what it can serve**, not what it lists.
   `sh scripts/probe-models.sh http://127.0.0.1:1234/v1` — `/v1/models`
   advertises what is *installed*, which is a different set from what will load.
2. **Load one model at a time** when probing, and read VRAM after each. A card
   with a resident model and no TTL reports "cannot load" for everything else.
3. **Believe `doctor --live` over any probe**, including this repository's. It is
   the only check that uses jichi's real streaming path.
4. **Mind the per-slot context.** `lms ps` showing `CONTEXT 16384 PARALLEL 4`
   means about **4,096 tokens per client**, not 16,384. Declare the per-slot
   number as `contextLength` or you get mid-turn HTTP 400s.
5. **`prose` means repair the server, not replace the model.** The template is
   the layer that failed. A different quant of the same weights may well work.
6. **If you swap models on a machine that is not yours alone:** write down
   `lms ps` first, restore it after, and say so.

## 8. What is still not known

- **Only one bench.** Six models, one card, one server version. The `prose`
  verdict is about *this GGUF's template*, not about Qwen2.5-Coder as weights.
- **`bonsai-27b` took 18–32 seconds** to answer a one-tool question and was not
  timed under load; native and usable is not the same as pleasant.
- **No quality claim.** Every measurement here is "can it call a tool", not "is
  its review any good". The 12B QAT model reviewing jichi's own diffs well enough
  to be worth reading is an open question, and the next thing to measure.
- **The eight files still on the strict accessor were not audited** for whether
  any of them ever reads a user-written file. That audit is worth doing; the lint
  names them so the next reader knows the boundary was drawn deliberately.
