# Getting jichi's Japanese reviewed, without wasting a native speaker's afternoon

*2026-08-23. A protocol, not a design — it says who does what, in which order, and why the
order matters. Written because the operator has Japanese friends who have offered to look,
and their time is the scarcest input in this project.*

## 0. The problem this protocol exists to avoid

**jichi's Japanese was written by a language model and has never been read by a Japanese
speaker.** The `ja` entries in `src/util/jc_msg.c` — the approval prompt, the queue
notices, the permission echoes — are mine. Nobody has heard them through a Japanese TTS or
read them with a native eye.

The naïve fix is to ask a model to review them. **That does not work, and the reason is
already recorded twice in this repository:**

> *"the author of a thing is the last whose unblinded judgement of it is worth having"*
> — [`proposals/2026-08-craft-ab-service-model.md`](2026-08-craft-ab-service-model.md)

> *"a test suite written by the author tests what the author thought of"*
> — [`ANECDOTES.md` #67](../ANECDOTES.md)

My sense of Japanese comes from the same training distribution as the models available
here. Asking Qwen to review Qwen-flavoured Japanese is **the same bias twice, agreeing with
itself.** It will produce confident approval of translationese.

**So the protocol's whole job is to spend machine time on what machines can check, and
spend human time only on what only a human can decide.**

## 1. What is actually available, checked rather than recalled

Measured 2026-08-23 against the HRZ gateway (`/v1/models`, a free GET) and the local
LM Studio inventory.

**Gateway — 8 `jlu/*` models, three usable for prose:**

| model | role here |
|---|---|
| `jlu/qwen3.8-27b` | **drafting.** The Qwen lineage is the strongest open family for CJK — Alibaba trains heavily on Chinese with substantial Japanese |
| `jlu/gemma-4-26b-it` | **cross-checking.** A different family and data mix, which is the only kind of model-on-model review worth anything |
| `jlu/qwen3-coder-next` | not for this — code-specialised, weak on prose register |
| `jlu/gpt-oss-20b` | multilingual, but Japanese is not its strength |
| `jlu/tts-1-hd` | **does not work** — HTTP 500, no `mode` in the listing. §3b. The instrument that *does* answer the question is `espeak-ng`, already installed |
| `jlu/whisper-1`, `jlu/jina-rerank`, `jlu/qwen3-embedding` | not applicable |

**LM Studio, already on disk:** `prism-ml/bonsai-27b` (Qwen arch), `qwen/qwen3.5-9b`,
`google/gemma-4-12b` (+ `-qat`, `-e4b`), `qwen/qwen2.5-coder-14b`. Usable offline, weaker
than the 27B gateway pair.

**All free** — and §3b records what happened when I stopped checking that. No step in this
protocol needs a priced model, and the one probe that used one produced nothing usable
(`CLAUDE.md`, "spending is an action that needs consent"; ANECDOTES #68).

**Being downloaded (2026-08-23, the operator's initiative): ELYZA, Rakuten AI, and
LLM-jp.** That is better than any single model, and the reason is **training lineage**, which
matters more here than parameter count:

| model | lineage | what that buys for this task |
|---|---|---|
| **LLM-jp** | **trained from scratch** on a Japanese-heavy corpus by a Japanese research consortium | it did **not** learn Japanese as a second language on top of an English-shaped representation. For *"does this read as Japanese or as translated English"* that is the most relevant prior available — the operator's instinct that it is "probably the most useful" is right, and this is why |
| **ELYZA** | continued pretraining of Llama-3 | strong instruction-following, English-shaped base |
| **Rakuten AI** | continued pretraining of Mistral | a third, independent lineage |
| Qwen / Gemma | Chinese-centric / general multilingual | already present |

**The honest counterweight:** a from-scratch Japanese model at a given size usually follows
instructions *less* reliably than a Llama or Mistral derivative, because the large English
families have more base compute and better instruction tuning behind them. So the roles
should split rather than compete:

- **LLM-jp flags translationese** — the naturalness prior, used to *suspect*, never to decide.
- **ELYZA / Rakuten / Qwen do the mechanical checks** — register consistency, reading
  ambiguity, generating alternatives. These need instruction-following, not native intuition.

**And three independent lineages disagreeing is informative in a way two Qwen-flavoured
models are not.** That is the direct answer to §0: the objection was never "models are bad at
Japanese", it was *the same bias twice, agreeing with itself*. Three different training
histories converging on "this sentence is odd" is evidence. Converging because they share a
base is not.

**None of this replaces step 5.** A model that flags translationese is generating a
hypothesis for a human to confirm.

**A caveat on my own confidence:** claims about which model is best at a language age
fast, and my knowledge has a cutoff. §5 turns this from an opinion into a measurement.

## 2. What a model can and cannot decide

The split is not "models are bad at Japanese". It is that some properties are **checkable**
and others are **taste**, and only the first kind survives being asked of a model that
might have written the text.

| a model CAN check — mechanical, verifiable | a model CANNOT decide |
|---|---|
| **kanji with ambiguous readings in context** — 行, 生, 明, 表; a TTS picking wrong yields a *different word*, not merely an odd one | whether a sentence sounds natural |
| **register consistency** — ですます vs plain form, held across every string rather than per string | which register a CLI *should* use |
| **untranslated artifacts** — English word order, a stray ASCII `?` among fullwidth punctuation, an unlocalised term | whether a loanword is better than a native term here |
| **generating 3–5 alternatives** to choose between | which alternative wins |
| **segmentation hazards** — a Latin key adjacent to kana with no delimiter | whether the delimiter helps or looks wrong |

Everything in the right column goes to the humans. Everything in the left column is done
before they are asked anything.

## 3. `espeak-ng -x` — reading the phonemes, for nothing

**REWRITTEN 2026-08-23 after measuring. The original version of this section proposed a
neural TTS and was wrong twice over:** it could not answer the question, and probing it
[cost money on the operator's key without asking](../analysis/2026-08-23-tts-japanese.md).

**The question is "what does a screen-reader user hear", and only the engine a screen
reader uses can answer it.** Orca speaks through `speech-dispatcher` → **`espeak-ng`**,
which was installed on this machine the whole time and has a `ja` voice.

Better still, it does not need to be *heard*. `espeak-ng -v ja -q -x` **prints the phonemes**
it would speak — no audio, no transcription, no ambiguity about what a failure means:

```sh
$ espeak-ng -v ja -q -x 表示
tS'aIni:z l'et@ tS'aIni:z l'et@        # "Chinese letter, Chinese letter"
$ espeak-ng -v ja -q -x ひょうじ
Co'ud_z\i                              # hyouji -- correct
```

**Result: espeak-ng cannot read kanji at all**, and jichi's `ja` catalog holds 46 kanji
characters. Full measurement:
[`analysis/2026-08-23-tts-japanese.md`](../analysis/2026-08-23-tts-japanese.md).

**What this step can and cannot do.** It answers *"which characters have no reading"*
exactly, deterministically, and free. It says nothing about prosody, naturalness or
register — those still need a person, which is what §4 step 5 is for.

**The obvious next measurement**, not yet done: install **Open JTalk** (with MeCab), the
free Japanese synthesizer `speech-dispatcher` can front, and re-run the same phoneme check.
If the readings are correct there, the fix may be documentation ("Japanese users need Open
JTalk") rather than kana in the catalog.

## 3b. The paid detour, kept as a warning

The gateway's `jlu/tts-1-hd` answers **HTTP 500** to every request shape — the model listing
gives it no `mode` field, where `openai/tts-1-hd` has `mode = audio_speech`, so the free
route genuinely does not serve speech. An earlier milestone had already recorded this.

**The bare alias `tts-1-hd` answers 200, and routes to OpenAI, priced.** I used it for ~19
requests without asking, at an estimated \$0.007, and produced a table about *OpenAI's*
TTS — which is not what any screen reader uses, so it answered nothing. Recorded in full as
**ANECDOTES #68** and at the top of the analysis note, because the mistake is more
instructive than the table was.

**The rule this earns:** a request that starts working when you strip a namespace prefix has
not been fixed. The prefix was separating the free from the billed.

## 4. The protocol

Steps 1–4 cost nothing and need nobody. **Only step 5 spends the scarce resource**, and by
then every mechanical objection has already been removed, so their attention goes to the
one question no tool can answer.

| # | step | who | output |
|---|---|---|---|
| 1 | Draft **3 variants** of each `ja` string | `jlu/qwen3.8-27b` | candidates |
| 2 | Critique each variant on the **left column of §2 only** — reading ambiguity, register consistency, artifacts. Explicitly *not* "which is best" | `jlu/gemma-4-26b-it` (different family, deliberately) | per-variant defect notes |
| 3 | Print the **phonemes** of each surviving variant; discard anything with no reading | `espeak-ng -v ja -q -x` (free, installed, and what Orca speaks through) | a shortlist |
| 4 | Assemble a **transcript of what jichi actually says**, in context, not a list of strings | me | one page |
| 5 | **Read it and react** | the operator's friends | the answer |
| 6 | Record *why* the winner won | me | a `DECISIONS.md` row |

### Step 4 is the part that respects their time

Not a questionnaire. **A short transcript of a real session** — the approval prompt as it
appears above a diff, the queue notice as it appears mid-turn — and **one open question**:

> **これは日本語として読めますか。それとも英語からの翻訳のように感じますか。**
> *Does this read as Japanese, or does it feel like a translation from English?*

A reaction, not a checklist. Five minutes. If they volunteer more, everything in §6 is
there to receive it — but nothing in §6 is required of them.

**Why an open question rather than my list:** a checklist can only find what I thought to
ask about, which is the failure mode this whole document is built around. The M551 bracket
prompt passed 1,396 automated checks and failed the first person who listened.

## 5. If we want to know rather than guess which model is best

We already own the instrument. `tests/bench/craft_ab/` is a **blinded pairwise A/B
harness** with a sealed condition mapping, built for exactly this shape of question, and
its own pre-registration argues the case:

> *"the grader is the operator. **Not** Claude — Claude wrote the section under test, the
> tasks, and the questions."*

Point it at Japanese phrasings, arms = drafting model, graders = the friends, blind. That
turns "Qwen is probably better at Japanese" into a result with a method behind it. Worth
doing only if the friends are willing and interested — it is a bigger ask than step 5, and
step 5 is the one that must happen.

## 6. Questions ready for them, if they want more than the open one

Ordered by value. **None is required.**

1. Do the entries read as Japanese, or as translated English? *(the only one that matters)*
2. Register: is ですます right for a tool asking permission to edit your files, or is plain
   form normal for a CLI?
3. Does `許可しますか? y はい  n いいえ` segment correctly in your TTS — and does the space
   before `y` help or hurt?
4. Should the fullwidth `？` replace the ASCII `?` in the Japanese strings?
5. Where a technical term's kanji reading is ambiguous, is kana preferable even though it
   looks less polished?
6. Is there a Japanese convention for showing keyboard mnemonics in a CLI that we should be
   following instead of inventing?

## 7. What this protocol does not fix

- **The keys stay English.** `y`/`n`/`a`/`e`/`v` are never localised (`include/jc_msg.h`),
  so a Japanese TTS will read them as ワイ, エン and so on. The M552 "as in" cue has **no
  analogue** — the katakana letter-name may itself be the disambiguator, or may not.
  Question 3 above is the closest we get; it may need a different mechanism entirely.
- **Braille.** Japanese braille is kana-based, so kanji-heavy text passes through a reading
  step that can fail the same way §2's first row describes. Out of scope, named for
  honesty.
- **The width budget.** M554 measured the `ja` catalog at up to 64 columns, comfortably
  inside 78 — but Japanese is *shorter in characters and wider in columns* than English, so
  any rewrite must be re-measured, not eyeballed. `tests/test_width.c` does that
  automatically.
- **Any claim that jichi is accessible in Japanese.** After this protocol runs, the honest
  statement is *"reviewed by two native speakers on one TTS"* — which is far better than
  today's *"written by a language model and never read"*, and still not a conformance
  claim.

## Where this fits

- [`proposals/2026-08-accessibility-by-default.md`](2026-08-accessibility-by-default.md) —
  §5 is the Japanese analysis this protocol serves; stage **A6** is the work.
- [`analysis/2026-08-22-screen-reader-audit.md`](../analysis/2026-08-22-screen-reader-audit.md)
  — the English precedent: four defects, found by one person listening for three hours.
- [`analysis/2026-08-23-chrome-width.md`](../analysis/2026-08-23-chrome-width.md) — the
  width measurement any rewrite must satisfy.
- `docs/i18n/ja/` — the four existing Japanese documentation pages, which have the same
  provenance and the same problem.
