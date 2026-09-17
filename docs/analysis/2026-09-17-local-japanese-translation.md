# Five local models asked to translate one paragraph of this documentation

*2026-09-17 (M651). The operator asked to use the local LM Studio server and its
Japanese models to translate jichi's documentation. Before translating anything,
this measures whether they can — against a passage whose correct Japanese
already exists in the tree, so the answer is checkable rather than a matter of
taste.*

**Verdict: do not bulk-translate this documentation with a local model.** One of
the five is good enough to produce a **draft for a human reviewer**, which is a
real and useful workflow. One of them fabricated a different document, with
invented numbers, in the wrong language, and repeated it until it hit the token
cap.

---

## 1. Why this was measured before it was used

`docs/presentations/README.md` already states the policy this measurement was
built to test: *"a confident wrong translation of a claims deck would be worse
than an absent one."* That is an assertion. This is the check.

The method is the only part that makes the result trustworthy: **translate a
passage that already has a reviewed Japanese version in this repository**, and
compare. The chosen passage is the opening of `PROJECT_TIMELINE.md` — 1,084
characters including the "Method & honesty note" blockquote — because its
Japanese counterpart in `docs/i18n/ja/PROJECT_TIMELINE.md` was written and
reviewed, and because it is **dense with exactly what a technical translation
must not lose**: two load-bearing qualifiers (*under supervision*, *active*
days), an emphasis structure, and four parallel noun phrases that a summariser
would collapse.

Identical request to every model: same system prompt (keep Markdown and emphasis
exactly; keep product and technical terms in Latin script; keep every number
unchanged; output only the translation), `temperature` 0.2, one model resident at
a time.

**Two reference points from the reviewed Japanese**, used as the grading criteria:

| English | Reviewed Japanese | Why it is the test |
|---|---|---|
| "under supervision" | 監督のもとで | *監督* is supervision in the directing sense. *監視* is surveillance. The sentence describes how the project was run. |
| "13 weeks, **54 active** days" | 稼働…日 | Dropping *active* turns a measured working figure into a calendar figure. |

---

## 2. Results

| Model | Verdict | What happened |
|---|---|---|
| **`llm-jp-4-8b-thinking`** | **Best — usable as a draft** | Got **both** criteria right: 監督下で, and **54 活動日**. Translated the headings into Japanese rather than leaving them English, kept the emphasis markers, and numbered the four delivery models ①–④, which is *clearer than the English*. Two flaws: it emitted a raw `<\|im_end\|>` control token at the end, and it dropped "delivery models" from the final clause. |
| **`prism-ml/bonsai-27b`** | Mixed | Kept "under supervision" as **監視下で** — surveillance, the wrong sense. Left "four", "Method & honesty note.", "AI-assisted", "human-equivalent" and "13 weeks, 54 active days" **untranslated in English**. Rendered "delivery models" as 配信モデル — *distribution/streaming*, the wrong sense. Correct where the smaller Japanese model was not: it kept "a single developer **assisted by an AI agent**". Cost **6,216 reasoning tokens for 287 tokens of output** — 21 tokens of thinking per token of answer. |
| **`llama-3-elyza-jp-8b`** | Fluent and wrong | Fast (4.5 s) and reads naturally, which is what makes it the more dangerous of the two failures. "under supervision" became **下位で** ("in a lower position") — a meaning error, not an awkwardness. Dropped **active** from the day count. Dropped "assisted by an AI agent" from the first delivery model, which is the one feature distinguishing it from the other three. Left four English phrases untranslated and transliterated "data-grounded review" as データグランドレビュー, which is not Japanese. |
| **`qwen/qwen3.5-9b`** | **No output** | Produced **zero content tokens** at `max_tokens` 1,200 and again at **8,000**, spending the entire budget on reasoning both times, `finish_reason: length`. Not a Japanese model; included because it is the strongest general model on this box. |
| **`rakutenai-7b`** | **Catastrophic** | Did not translate. It **generated a different English document**, with **fabricated figures** ("Expert: 6-8 weeks", "Balanced team: 7-9 weeks", "Junior solo dev: 20+ weeks" — none of which appear in the source or anywhere in this project), then **repeated that block six times** until it hit the cap. Confident, fluent, fabricated, and in the wrong language. |

---

## 3. An error of the measurer's own, recorded because it changes how to read the table

The first run gave `qwen3.5-9b` and `bonsai-27b` a `max_tokens` of **1,200**.
Both returned empty content having spent all 1,200 on reasoning, and the obvious
reading — *"these two cannot do it"* — would have been **wrong for one of them**.
Re-run at 8,000, `bonsai-27b` produced a real translation.

This project's own rule says caps stay **off** for measurement runs, because *a
cap that fires does not merely hide the answer, it manufactures a plausible
different one*. That is exactly what happened, and it is the **third** time in
this session that an infrastructure or harness artefact has been indistinguishable
from a capability verdict (the other two are in
`2026-09-17-local-tool-calling-sweep.md`). `qwen3.5-9b`'s result stands only as
*"produced no translation within 8,000 tokens"* — a bound, not a verdict.

---

## 4. What follows for translating the documentation

1. **No unsupervised bulk translation.** Two of the five produced
   meaning-level errors on a 1,084-character passage, and one fabricated content
   outright. Applied to 466 English pages, that is not a translation project, it
   is a corpus of plausible-looking claims nobody can check — against a page set
   whose entire value is that its claims are checkable.
2. **`llm-jp-4-8b-thinking` is fit to draft, for a reviewer.** That is a real
   workflow and this repository already has the convention for it: the ja and zh
   pages carry `tracks:`, `figures-behind:` and `slides-behind:` markers, and
   the ROADMAP lists *native review of the ja/zh doc drafts* as an open human
   track. A drafted page must enter as a **declared draft**, not as a
   translation.
3. **Strip control tokens, and check for them.** The best model leaked a raw
   `<\|im_end\|>` into its output. Any pipeline built on this must treat the raw
   completion as untrusted text.
4. **Never machine-translate a page that makes claims.** The decks `07` and `08`
   and the analysis notes argue and cite; a wrong qualifier there is worse than
   an absent page, which is the policy this measurement now supports with
   evidence instead of assertion.
5. **The numerals are the cheap win, and they are not translation.** The
   outstanding Japanese debt in this tree is `figures-behind: 25` on
   `PROJECT_TIMELINE.md` — figures that went stale when the English page was
   recounted at M646. Those are **numerals, not prose**: they can be carried
   across mechanically and checked by the existing lint, with no model involved
   and no Japanese judgement required. Do that first; it is the part that needs
   no reviewer.

---

## 5. What this did not measure

- **One passage, one prompt, one temperature.** A different system prompt — in
  particular one that does *not* ask to preserve Latin-script technical terms —
  might fix the "left English phrases untranslated" failure shared by two models.
  That is the first thing to vary if anyone reopens this.
- **No native reader saw any of this.** The grading criteria are two specific
  lexical choices where the reviewed Japanese in this repository disagrees with
  the model, plus omissions that are checkable without Japanese at all. That is
  enough to show *unfitness*; it is **not** enough to certify the best output as
  good, and this page does not.
- **Nothing was translated into the tree.** No page in `docs/i18n/` was changed
  by this work.
- **Five models on one backend on one box**, chosen because they were installed.
