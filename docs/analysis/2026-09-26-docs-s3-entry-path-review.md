# The docs S3 entry-path review — method, findings, and what was fixed

*An analysis, 2026-09-26. The record of the start-here review that milestones M782–M788
carried out, written so a later pass can see what was checked, what was fixed, and what was
consciously left. It follows [`DOC_REVIEW.md`](../DOC_REVIEW.md) §3 (how to run a pass) and
§6 (the shape of an honest report).*

## What this was

Step **S3** of the documentation-structure plan
([plans/2026-09-25-docs-structure-and-writing.md](../plans/2026-09-25-docs-structure-and-writing.md)):
revise the **start-here path** for self-learners, page by page, against the DOC_REVIEW
rubric. The operator chose the **full sixteen-page "Start here" group**, a **review pass
first**, and — after the pass — to **fix everything** rather than fix-blockers-and-defer.

## Method

Four **read-only** reviewers, one per coherent journey, each reading its slice as *a
self-learner working alone with nobody to ask* (DOC_REVIEW §1):

1. **build/install** — `PREPARE_AND_BUILD`, `BUILD`, `INSTALL`
2. **setup/first-session** — `SETUP_WIZARD`, `TUTORIAL_BEGINNER`, `DOCTOR`
3. **config/orientation** — `CONFIG_TUTORIAL`, `WORKFLOWS`, `VOCABULARY`
4. **router/meta** — `README` "Start here" + `PLAIN_LANGUAGE`, `STATE`, `LICENSING`, and the
   off-path `CJK`, `READING_THE_STANDARD`, `DISTRIBUTED`, `COMPARED`

Each returned ranked findings (≤6/page) with anchors, a severity (BLOCKER /
TEACHING-DEFECT / POLISH) and a suggested fix, named the passages worth protecting, and
listed what each page mentions but never explains. **Every finding acted on was verified
against the tree first** (DOC_REVIEW §3.5) — the grep, the source read, or the real binary.

## The headline finding — a priced model on the happy path

The single most severe finding, on the three pages a beginner acts from, was that the
copy-paste onboarding examples configured a **priced** model: `anthropic/claude-opus-4-8`
in `INSTALL.md`, `openai/gpt-4o` in `SETUP_WIZARD.md` and `TUTORIAL_BEGINNER.md`. A
self-learner following the happy path spent real money on first use — the exact ANECDOTES
#63 surprise the local-only rule exists to prevent. Verified by grep before fixing. Fixed
in **M782**: all three lead with a free local server (LM Studio `localhost:1234`, no key),
hosted models demoted to a clearly-paid aside.

## What was fixed, by milestone

| Milestone | Slice | The fix |
|---|---|---|
| M782 | onboarding | Free local model is the primary example; INSTALL stale samples (`--version`, `Makefile:311`, legacy names) corrected; FreeMiNT/ARAnyM routing row added |
| M783 | router + orientation | "Start here" → a numbered seven-step path; six pages regrouped; `PLAIN_LANGUAGE`/`STATE`/`LICENSING` given context |
| M784 | VOCABULARY | `role` list corrected against source; `provider`/`apiBase`/`apiKeyEnv`/`selector`/`fallback` and the routing words added |
| M785 | build | `run.sh` introduced (the one real dead-end); BUILD `CC` line-number drift dropped |
| M786 | macOS | "never compiled" corrected for two unverified user reports (operator-brought) |
| M787 | setup/doctor | `DOCTOR` leads with a healthy run; audience headers on `DOCTOR` and `SETUP_WIZARD`; a healthy sample in the tutorial |
| M788 | config | `CONFIG_TUTORIAL` doctor glyphs normalised to `✓`/`!`/`✗` + a JSONC note; `WORKFLOWS` learner-vs-developer tiebreaker |

## What was verified against the tree (not taken on the reviewer's word)

- The priced examples (grep of all three pages).
- `VOCABULARY`'s `role` list, against `src/config/jc_config.c`: real roles are `chat, edit,
  autocomplete, embed, rerank, summarize, apply, image, audio, transcribe`; `vision` is a
  bool, not a role.
- `JC_VERSION` = 0.12.0 (the stale `0.2.0-dev` sample); the `install:` target at Makefile:973
  (the stale `Makefile:311`); `CC ?= cc` at Makefile:15 (the stale "line 13").
- The real `doctor` output and its glyph set (`✓`/`!`/`✗`), by running the binary against an
  empty HOME — which also confirmed `PREPARE_AND_BUILD`'s fresh-machine sample is *accurate*
  and should be left unchanged.

## What was consciously deferred (registered in DEFERRED.md)

Structural preferences that do not make a reader fail:
- `INSTALL.md`'s two-audience ordering (a "just run it" reader passes the old-glibc userland
  ladder before the Install section).
- `SETUP_WIZARD.md`'s axis/M326 migration history in the body (belongs in a design
  appendix), and the `composer` domain named in the axis table but absent from the preset
  table.
- `CONFIG_TUTORIAL.md` POLISH: `--lite` used but undefined on the page; the `0a`-before-`0`
  numbering fighting "read in order"; §1's ~180 lines of env-var mechanics before a first
  working chat.

## What was named worth protecting

The reviewers were asked what to protect, and named (among others): `TUTORIAL_BEGINNER`'s
"read the diff before you press y" and its checkpoint mental model; `DOCTOR`'s "A check
failed" table and its `--live` section; `SETUP_WIZARD`'s "Where the key goes"; `STATE`'s
four-places ownership table and its Windows `noacl` chmod-lies section; `VOCABULARY`'s
English-idioms section; `PLAIN_LANGUAGE`'s JupyterHub subsection. None was sanded off.

## The lesson

The sharpest findings were the ones a lint cannot catch and a fluent page hides: a priced
default that reads perfectly, a glossary that is confidently wrong about the one list that
must be exact, a sample that shows a *failure* as the first thing a new user sees. Each was
caught by reading as the lone learner and then checking the claim against the tree — the
same discipline [`DOC_REVIEW.md`](../DOC_REVIEW.md) prescribes, and the reason it says to
run the pass again rather than automate it.
