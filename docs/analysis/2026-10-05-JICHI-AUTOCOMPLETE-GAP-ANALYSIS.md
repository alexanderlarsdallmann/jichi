# JICHI AUTOCOMPLETE.md — gap analysis (analysis 52)

## TL;DR

*Read-only survey of docs/AUTOCOMPLETE.md at HEAD 1b71247d (263 lines, last touched 2026-09-23 by M711's fleet sweep), cross-checked against src/tui/jc_tui.c, src/util/jc_suggest.c, and src/main.c, 2026-10-05.*

*The gap is the recurring drift class, and it is sharp: **the doc quotes the old Ctrl-G prompt in the present tense and attributes it to the wrong file.** M280 (2026-08-04) did two things — it rewrote the prompt from a bare instruction into three worked examples plus an anti-example, and it *moved the prompt body out of `src/tui/jc_tui.c` into a new pure helper `src/util/jc_suggest.c`*. The doc still quotes the pre-M280 sentence and names `jc_tui.c`. M711 (2026-09-23) edited this very file to fix "Four pieces" → "Five pieces" and left the stale quote untouched.*

## 1. The gap, precisely

AUTOCOMPLETE.md:32-33 (present tense):
> The system prompt behind Ctrl-G (`tui_suggest`, `src/tui/jc_tui.c`) says:
> *"Continue the user's partial line naturally and briefly. Output ONLY the
> continuation that should follow it — no preamble, no quotes, no repetition
> of their text."*

Both facts are stale:
1. **The quoted sentence no longer exists in the tree.** It was the original
   M279 prompt (git show 4b3cf69d:src/tui/jc_tui.c:773). M280 (50cafd67,
   2026-08-04) replaced it. The current prompt opener is `src/util/jc_suggest.c:31`:
   *"You complete a half-typed line of input..."* — the demonstrated form with
   three worked examples (verified: `grep -cE "input:" jc_suggest.c` = 3), the
   "do NOT answer the question" anti-example (jc_suggest.c:43), and the
   leading-space rule.
2. **The file attribution is wrong.** The prompt body now lives in
   `src/util/jc_suggest.c` (`jc_suggest_system`, called from
   `tui_suggest` at jc_tui.c:3012); `grep "Continue the user's partial"
   src/tui/jc_tui.c` returns nothing at HEAD.

Irony worth noting: the doc itself, four lines below the stale quote, *correctly
describes* M280 — "The prompt now demonstrates rather than only instructs (M280)...
the system prompt carries three worked continuations" — so the page contradicts
itself across one paragraph: the quote shows the instruction-only prompt, the
prose describes the demonstrated one.

## 2. What M711 did and did not catch here

M711 (fd1be1e1, 2026-09-23 — the "125 rotted claims" fleet sweep) touched this
exact file: its only change was `-Four pieces: / +Five pieces:` (Ctrl-Q advice
had become the 2b piece). It did not touch lines 32-33. The sweep's method was
8 agents × ~219 pages, each finding carrying a proving command — and this
sentence's proof is a one-line grep that would have failed. The most likely
explanation (inference, not verified): the page reads coherently end-to-end —
the "now demonstrates (M280)" paragraph immediately follows — so a sweep
reader plausibly took the quote as historical shorthand for "the prompt"
rather than a claim about current bytes. That is the M492 class in its most
seducing form: the rot is inside a quote, and the quote is *adjacent to the
correct account of the change.*

## 3. Everything else, verified true

- **Five pieces** and their numbering (1, 2, 2b, 3, 4): Tab completion,
  Ctrl-G ghost text, Ctrl-Q printed advice, headless `complete`, headless
  `fim`. `complete`/`fim` subcommands exist (main.c:16963/16978 →
  run_complete :9432, run_fim :9528).
- **Tab completion is offline/deterministic**: pure helpers in
  src/util/jc_complete.c (`jc_complete_token`, `jc_complete_common_prefix`).
- **Ctrl-Q advice is "printed below, never inserted"**: the help text says
  exactly that (jc_tui.c:2551); the advice callback (jc_tui.c:3072, M280) is
  one non-streaming call per press.
- **Cleaner**: `jc_suggest_clean` (jc_suggest.c:171) is pure and unit-tested
  (tests/test_suggest.c:49,55), removing leading blank, the `output:` label,
  surrounding quotes, and verbatim echo — matching the doc's list.
- **Rejection deliberately absent** (doc:103): the cleaner does not
  judge the reply; `tui_suggest` never decides whether to show it — the
  terminal stays model-agnostic (doc:113). Consistent with the code's shape.

## 4. The fix (two lines)

Replace the stale quote block (AUTOCOMPLETE.md:32-33) with the current prompt,
correctly attributed:

> The system prompt behind Ctrl-G (`jc_suggest_system`, `src/util/jc_suggest.c`)
> no longer merely instructs — since M280 it *demonstrates*: three worked
> continuations, then the anti-example ("Note what those outputs do NOT do: they
> do not answer the question..."), and the leading-space rule.

Then delete the now-redundant "now demonstrates (M280)" paragraph that follows,
or fold its remaining detail (the verbatim-echo / leading-space rationale) into
the new paragraph. The page then stops contradicting itself within one
paragraph. No code change, no new lint strictly required — though a narrow
"docs quote a string literal" check is the general guard (and the M366/M158
doc-vs-code pattern from the OBSERVABILITY analysis is the family it would join).

## 5. Verdict

One real stale-quote gap, the recurring drift class, with a sharp twist: the
rot sits *inside a quotation the reader trusts as a verbatim record*, it is
attributed to the wrong file, and it is directly contradicted by the page's own
M280 paragraph two lines below. M711's fleet sweep edited this file and missed
it — the seducing form of the M492 class. Everything else on the 263-line page
verifies true against code.
