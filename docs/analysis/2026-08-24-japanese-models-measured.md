# Three Japanese models, measured against planted errors

*2026-08-24. ELYZA, RakutenAI and LLM-jp, downloaded for stage A6 (the Japanese review). The
short answer: **one of the three works, and none of them can be trusted to judge Japanese.**
Measured with deliberately broken strings, because fluent output is not evidence of review
ability — and without the planted errors I would have concluded the opposite.*


> ## ⚠ CORRECTED, same day — the verdicts below were the instrument's, not the models'
>
> **Everything in this page from "Results" onward measured my own tooling.** Two faults
> compounded, and both were mine:
>
> 1. **The prompt asked the wrong question.** It asked whether each string was natural *as
>    software UI*. Almost any short UI string is deficient by that standard — it is short by
>    design and takes its referent from context — so the models answered with UX critique
>    (*"which file?"*, *"unclear what work"*), which I scored as language errors.
> 2. **The extractor read the wrong channel.** `llm-jp` emits
>    `analysis<|message|> …thinking… <|end|> assistant<|channel|> final<|message|> **answer**`.
>    I was reading the *analysis* channel's opening words and recording them as the verdict.
>
> **Asked about grammar and parsed correctly, `llm-jp` scores 0 false positives on 5 correct
> strings and finds both grammar-class planted errors with accurate reasoning.** `ELYZA`, on
> the identical prompt, marks **3 of 5 correct strings wrong** and justifies two with the same
> fabricated claim. The conclusion this page originally reached — *"none of the three can
> review Japanese"* — is **withdrawn**. See the corrected measurement at the end, and
> ANECDOTES #75.
>
> The original text is kept unedited below. A retraction that rewrites the mistake out of
> existence teaches nothing about how it happened.

---

## The method, and why it is the whole point

A Japanese model asked *"is this natural?"* will produce fluent, confident Japanese either way.
That is unfalsifiable. So the test set is **eight of jichi's own UI strings: five correct, three
with unambiguous planted errors**:

| # | string | status |
|---|---|---|
| 1 | 作業中 | correct |
| 2 | 許可しました | correct |
| 3 | 拒否しました | correct |
| 4 | キューの入力を破棄しました | correct |
| 5 | 未送信です。Enter でキューに追加 | correct |
| **6** | 許可しました（このセッションでは**時々**） | **semantic** — 時々 is "sometimes"; must be 常に "always" |
| **7** | **拒否したぞ** | **register** — rough masculine casual, wrong for UI text |
| **8** | キューが一杯です。行**に**破棄しました | **particle** — must be 行**を**破棄しました |

A model that finds 6, 7 and 8 and leaves 1–5 alone can review. Anything else is measurable.

## Results

| | reachable | found 6 | found 7 | found 8 | false positives |
|---|---|---|---|---|---|
| **llama-3-elyza-jp-8b** | ✅ chat API, ~7 s | ✅ (vague reason) | ✅ correct reason | ❌ **missed** | **1, reproducible** |
| **rakutenai-7b** | ⚠ completions only | — | — | ⚠ right verdict, invented reason | n/a |
| **llm-jp-4-8b-thinking** | ❌ engine error | — | — | — | — |

**ELYZA is reproducible**, which matters more than the score: two runs at `temperature 0.2`
produced the *same* four verdicts, including the same false positive. So this is its behaviour,
not sampling noise.

**Its false positive is the interesting failure.** On string 3 — `拒否しました`, correct — it
answered *"「拒否した」should be 「拒否しました」; the verb ending is unnatural."* **The input was
already `拒否しました`.** It misread the string it was reviewing and then corrected it to itself.
A reviewer that invents defects in correct lines cannot be trusted on the lines it passes.

**And it misses the particle error every time**, which is the most mechanical of the three and
the kind a reviewer exists to catch.

**A second, independent check of the same weakness.** Driven through jichi, asked to explain the
difference between 拒否しました and 拒否した, ELYZA answered that the first *"indicates a completed
past action"* and the second *"has the nuance that the action is already over."* The actual
difference is **politeness register** — です／ます polite form versus plain form — which is the
distinction that governs every UI string in the catalog. Fluent Japanese, wrong analysis.

## The two that need configuration, and exactly what

**`rakutenai-7b` behaves like a completion model, not a chat model.** Through
`/v1/chat/completions` it echoes the prompt and then invents further list items until
`finish_reason: length`. Its output imitates an `Instruct:` / `Human:` / `AI:` template, which
means LM Studio is applying a generic template the model then continues rather than terminates.

- **Client-side workaround, verified:** `stop: ["\nInstruct:", "\nHuman:", "\nAI:"]` makes it
  terminate cleanly. It still does not follow the instruction — on the eight-case list it echoed
  the prompt and stopped.
- **Native form, verified:** through `/v1/completions` with an explicit `Instruct:/Human:/AI:`
  prompt it does answer. But the reasoning is fabricated: it judged string 8 unnatural *because
  「キュー」is not a Japanese word* (it is — a standard katakana loanword) *and because the reading
  of 行 is uncertain*. Right verdict, invented justification.
- **The fix that would matter** is its chat template in LM Studio's per-model settings. Until
  then it is not usable from jichi, which speaks the chat API.

**`llm-jp-4-8b-thinking` cannot be reached THROUGH THE CHAT API.** Every chat request,
including a 60-token trivial one, returns:

```
Engine protocol predict stream returned an error: {"code":500,
 "message":"The model produced output that does not match the expected peg-native format"}
```

Not a length problem — the runtime's reasoning-section parser rejects the model's output format
outright. This needs LM Studio's per-model reasoning-format setting changed, or a non-thinking
build of the model. **Nothing on jichi's side can work around it.**

**But the model itself is fine, and this is the correction that matters.** The operator asked
whether the downloads were damaged — both Rakuten and LLM-jp were interrupted and resumed. They
are not, and the same probe that proves it changes the recommendation. Through
`/v1/completions`, which uses no chat template and no reasoning parser, LLM-jp answered:

> はじめまして、〇〇と申します。どうぞよろしくお願いいたします。

with a structured explanation of why the 敬語 form is preferable. **That is better Japanese than
ELYZA produced anywhere in this measurement** — more idiomatic, correct register, and the
register distinction is exactly what ELYZA got backwards.

**So "only ELYZA works" was a statement about CONFIGURATION that read as a statement about the
MODELS.** The honest form: ELYZA is the only one reachable *from jichi* today. LLM-jp is the one
most worth re-testing against the planted errors once its reasoning-format setting is changed,
and on this evidence it may well beat ELYZA at precisely the thing ELYZA failed.

## The downloads are intact, verified four ways

The operator's hypothesis was reasonable — both downloads were interrupted and resumed — and it
is worth recording how it was settled rather than argued:

| check | ELYZA | Rakuten | LLM-jp |
|---|---|---|---|
| GGUF magic / version | ✅ v3 | ✅ v3 | ✅ v3 |
| tensor count | 291 | 291 | 291 |
| size vs `lms ls` | 4.92 GB ✅ | 4.23 GB ✅ | 5.30 GB ✅ |
| leftover `.part` / `.incomplete` | none | none | none |
| **generates coherent Japanese** | ✅ | ✅ | ✅ |

**The last row is the one that settles it.** A header reads fine on a file truncated after it, so
structural checks alone would not have been enough. All three actually generate correct
Japanese; corrupt weights produce garbage or fail to load. Both failures are per-model LM Studio
settings.

## One configuration finding that applies to everything

**The LM Studio server is bound to `192.168.0.24:1234`, not to localhost.** `127.0.0.1:1234`
refuses the connection outright:

```
LISTEN 0 511 192.168.0.24:1234 0.0.0.0:* users:(("lm-studio",pid=4859))
```

So any jichi config must use the LAN address, or LM Studio's "serve on local network" setting
should be changed if a localhost `apiBase` is wanted. Worth knowing before debugging a config
that looks correct.

## The working configuration

Verified end to end — jichi drove ELYZA through this and got a Japanese answer back:

```json
{
  "models": [
    { "name": "ja-review", "provider": "openai", "model": "llama-3-elyza-jp-8b",
      "apiBase": "http://192.168.0.24:1234/v1",
      "apiKeyEnv": "LMSTUDIO_KEY",
      "contextLength": 8192,
      "inputCostPer1M": 0.0, "outputCostPer1M": 0.0,
      "roles": ["chat"] }
  ],
  "repoMap": false, "references": false, "snapshots": false,
  "toolProfile": "readonly", "maxRetries": 1
}
```

`toolProfile: readonly` because a reviewer has no business writing; the zero costs so `doctor`
does not warn about unpriced spend on a model that is genuinely free (`CLAUDE.md`).

**One rough edge:** ELYZA opened its reply with *"A non-technical question!"* — it is reacting to
jichi's coding-agent system prompt, which is the wrong frame for language review. An
`--output-style` or a leaner system prompt would help, and is worth doing if this model gets a
standing role.

## What this changes for A6

**It does not replace the native speakers, and the protocol stands.** What I can now say with
evidence rather than assumption:

- **Do not use ELYZA as a reviewer.** It misses a basic particle error, invents a defect in a
  correct string reproducibly, and gets the politeness/aspect distinction backwards. Its verdicts
  would add noise to a native speaker's review, not signal.
- **LLM-jp is untested as a reviewer** and is the obvious next measurement, because the one
  sample available shows better Japanese than ELYZA's. Running the eight planted-error cases
  against it — once its reasoning format is configured — is a ten-minute job and would either
  give A6 a usable first-pass tool or rule the class out on stronger evidence than one model.
- **ELYZA is defensible as a first-pass *generator*** — proposing candidate Japanese for a person
  to correct — because generating fluent text is what it demonstrably does well. That is the
  opposite direction of use from what "review model" implies.
- **The `doctor` check from M567 remains the useful automated part** for Japanese: it tells a
  Japanese user that espeak-ng has no kanji readings and names the Open JTalk packages. That was
  measured (M556) and needs no model at all.

## The template is Harmony, and ChatML fixes it — verified before recommending it

`llm-jp-4-8b-thinking`'s GGUF ships a chat template containing `<|channel|>`, `<|start|>`,
`<|message|>`, `analysis` and `reasoning_effort` — **the OpenAI Harmony format (gpt-oss)**,
inside a Llama-architecture 8B. LM Studio applies that template and then parses the output with
its Harmony parser; the model does not emit well-formed Harmony markers, so every chat request
fails. **A packaging problem, not a download problem and not a jichi problem.**

Framed as **ChatML** through `/v1/completions` instead, the same weights answered the particle
case correctly, with the right reason and a natural rewrite, and terminated cleanly:

> NG 「行に破棄した」という表現は不自然で、「キューがいっぱいなので、メッセージを破棄します」などと
> 言い換える必要があります。

**That is the case ELYZA missed twice.** So the GUI change — override the embedded template with
ChatML — is confirmed to work *before* anyone makes it.

## Batching degrades detection, and that is a usable finding

Scoring `llm-jp` on the full eight-case list gave a *worse* result than the single question: it
marked case 8 **OK** in the list having marked it **NG** alone, and it hallucinated a 「ぞ」 into
case 3 — text that belongs to case 7. **Items bleed into each other.**

ELYZA shows the same effect: asked about case 8 **alone**, it answered NG, having missed it
twice inside the list.

**So: one string per request, never a list.** That is worth more than any model choice here.

## But isolation does not fix the deeper problem

ELYZA's false positive on `拒否しました` **survives isolation**, and its justification gets worse:

> 「拒否する」という動詞は自動詞なので、「した」が付くことはないため、「拒否しました」は不自然です。

*"拒否する is an intransitive verb, so it cannot take した."* This is **flatly false** on both
counts — 拒否する is transitive, and する-verbs take した by construction. Fluent, confident,
fabricated grammar.

And note case 8 in isolation: ELYZA reached the **right verdict for the wrong reason**, claiming
「行」means "column or order" and the object is unclear, rather than identifying the particle.

**The conclusion this forces:** these models cannot be trusted for REASONS, only sometimes for
VERDICTS. A native speaker reading their output would have to verify every claim — which is more
work than reviewing eleven strings directly. That is the argument against using them as
reviewers, and it is now measured rather than assumed.

## Honest limits of this measurement

- **Three planted errors, one prompt, two samples.** Enough to show ELYZA's failures are
  reproducible; not enough to rank models on translation quality. **The LLM-jp comparison rests
  on a SINGLE sample** and is stated as a reason to test, not as a result.
- **The models were tested as LM Studio ships them.** Rakuten and LLM-jp may both improve
  substantially with the right template and reasoning-format settings — which is a per-model GUI
  change, and stated here as the next step rather than a verdict on the models.
- **No native speaker has seen any of this**, including my judgement that the three planted
  errors are unambiguous. That judgement is itself a candidate for review.
- LM Studio state was recorded before (`text-embedding-nomic-embed-text-v1.5`, idle) and
  restored after; the only model that loaded was ELYZA, and it was unloaded.

---

# The corrected measurement (2026-08-24, M580)

**Same eight strings. A prompt about grammar rather than about UI quality. The final channel
extracted rather than the first words of the model's thinking.**

| # | string | planted? | `llm-jp` | `ELYZA` |
|---|---|---|---|---|
| 1 | 作業中 | — | 正しい ✅ | 正しい ✅ |
| 2 | 許可しました | — | 正しい ✅ | **誤り ❌** |
| 3 | 拒否しました | — | 正しい ✅ | **誤り ❌** |
| 4 | キューの入力を破棄しました | — | 正しい ✅ | 正しい ✅ |
| 5 | 未送信です。Enter でキューに追加 | — | 正しい ✅ | **誤り ❌** |
| 6 | …（このセッションでは**時々**） | semantic | 誤り ✅ | 誤り ✅ |
| 7 | 拒否した**ぞ** | register | 正しい — *see below* | 誤り ✅ |
| 8 | 行**に**破棄しました | particle | 誤り ✅ | 誤り ✅ |

| | false positives (of 5) | planted found (of 3) | reasoning |
|---|---|---|---|
| **`llm-jp-4-8b-thinking`** | **0** | 2, +1 defensible | **accurate** |
| **`llama-3-elyza-jp-8b`** | **3** | 3 | **fabricated** |

## Case 7 is not a miss

Asked whether `拒否したぞ` is *grammatically* correct, `llm-jp` answered **正しい** and explained:
*「動詞「拒否する」の過去形「拒否した」に、男性的・強調を表す終助詞「ぞ」を付けた自然な文です」* — past
form plus the **masculine emphatic particle ぞ**. That is linguistically exact. It detected the
register and correctly judged the grammar. The case was *designed* as a register violation; the
question I asked was about grammar. **The model answered the question put to it.**

## ELYZA's failures are the kind that cannot be worked around

Two of its three false positives rest on the same invented claim — that 許す and 拒否する are
**自動詞 (intransitive)**. Both are transitive. It had produced the identical fabrication in the
earlier run (*「拒否する」という動詞は自動詞なので*), so this is reproducible rather than a slip. And
its 3-of-3 on the planted errors is partly a broken clock: it answered 誤り to **six of eight**.

## What this changes

- **`llm-jp-4-8b-thinking` is a usable first-pass reviewer** — after the template fix (below),
  asked a well-formed question, with the final channel extracted.
- **`ELYZA` is not.** It is the one that invents grammar to justify a verdict.
- **The pipeline stands unchanged**: a model drafts and flags, **a native speaker adjudicates**.
  Nothing here promotes a model to reviewer-of-record; it makes one of them worth listening to
  first.

## The template fix that made this possible

`llm-jp-4-8b-thinking` ships **gpt-oss's Harmony chat template** with the identity string
swapped — `model_identity = "You are LLM-jp-4…"` inside a template full of `<|channel|>`,
`<|start|>`, `raise_exception` and browser/python tool macros. LM Studio saw Harmony, engaged its
Harmony parser, and every chat request failed with `does not match the expected peg-native
format`.

Replacing the template with plain ChatML fixed it. The override is written to
`~/.lmstudio/.internal/user-concrete-model-default-config/<publisher>/<repo>/<file>.gguf.json`
as `llm.load.promptTemplate`, and **LM Studio saves it as you type — there is no save button**,
which is what made it look like the edit had not taken.

## Do NOT add a stop string here — it would cut the answer off

**This section exists because I recommended the opposite, and was wrong.** It is the most
transferable thing on this page, so it is written for somebody meeting the problem for the first
time.

**What a stop string is.** A model normally stops because it emits a special *end-of-turn token*
— one entry in its vocabulary meaning "finished". A **stop string** is a text-level substitute:
the server watches the generated characters and halts the moment a given string appears. You
reach for one when the template's terminator is not a real token for that model.

**Why it looked necessary.** The ChatML template wraps turns in `<|im_start|>` / `<|im_end|>`,
but this model was trained on **Harmony** (`<|end|>`, `<|return|>`). `<|im_end|>` is therefore
just five characters of text to it: it types them out and keeps going. Replies visibly run past
the answer. Adding `<|im_end|>` as a stop string is the obvious fix.

**Why it is the wrong fix.** Measured — the character offset of the marker against the offset of
the actual answer:

| string reviewed | `<|im_end|>` at | the answer begins at |
|---|---|---|
| 作業中 | 567 | **599** |
| 拒否しました | 296 | **328** |
| 行に破棄しました | *(`<|start|>`)* 298 | **329** |

**The marker comes BEFORE the answer, every time.** The output is shaped:

```
analysis<|message|> …the model thinking, in English… <|end|><|im_end|>
assistant<|channel|> final<|message|> 正しい – 「作業中」は名詞として単独でも…
```

`<|im_end|>` is the model closing its *thinking* turn and opening a fresh one for the verdict.
**Stopping there truncates the reply to the deliberation and discards the answer** — and it would
look exactly like the model had broken.

**What to do instead: nothing.** The output is not malformed, it is *structured*. Take everything
after `final<|message|>`. That is more information than a plain reply, not less: the model is
telling you which part is reasoning and which part is its answer.

**How the wrong advice was reached**, because that is the reusable part. An early probe of this
model through `/v1/completions` had passed `<|im_end|>` as a stop string **and it worked** — in
that test the model answered directly, with no thinking channel. A different prompt produced a
different output *shape*, and the advice was carried across from memory without re-checking where
the marker fell. **A stop string is only safe if you have looked at where the string actually
appears in that model's output, for that prompt.**

**The one real consequence.** A client with no channel extraction — jichi, today — will show the
English deliberation followed by the Japanese answer. Usable but noisy, and now a measured
requirement rather than a guess if channel extraction is ever wanted.
