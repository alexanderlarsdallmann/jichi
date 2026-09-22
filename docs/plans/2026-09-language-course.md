# Learning a language with jichi — the design

*Plan, 2026-09-18. Written for the operator's use-case: a self-learner who
wants to work through an official language tutorial — the Python tutorial, the
Racket guides, any other — with jichi as the tutor, the documents as jichi's
references, and graded assignments built on them.*

*Every number on this page was measured on 2026-09-18 before the page was
written. Nothing here is a projection.*

*No milestone number is claimed: **nothing here is built yet**. The number is
assigned when the first piece ships, which is §6's milestone 1.*

## 0. The use-case, in the operator's words

> A self-learner may want to use jichi for learning a programming language, and
> use a tutorial such as the official Python tutorial, or Racket and its
> tutorials and guides, or any other language. jichi has to be used to set up a
> project directory with a suitable configuration, the tutorial pages, guides,
> language references as usable jichi references. Then the learner has to be
> guided by jichi to walk through the tutorials and use it with supported
> assignments created for that learner, and based on the referenced tutorials
> and guides.

And, clarified: **a generic tutorial**, not a Python-specific one — it supports
the learner in downloading the material for *their* language, setting up the
project, and then working through it.

## 1. Decisions taken before designing

| Decision | Taken | Rejected, and why |
|---|---|---|
| **Corpus is a local snapshot with a manifest** | The learner downloads the project's own published documentation archive once; a manifest records source URL, version, retrieval date, licence and checksum. | *Live `url` sources per page* — jichi supports them (M51) and they are right for a one-page draft, but the Racket docs are hundreds of pages, nothing is version-pinned, and the learner loses the material the moment the network does. |
| **Assignments are deterministic and committed; the model personalises framing only** | One task template per tutorial section with a hand-written grader. The model may reword the prompt and the hints. It may **never** write the grader or the pass condition. | *Generated per learner* — nothing is reviewable, a wrong grader teaches the wrong thing confidently, and no two learners' results are comparable. |
| **It ships inside jichi** | A scaffold, a coaching skill, a docs page, and a lint — so every learner gets it and the gate covers it. | *A separate example repo* — faster, but every learner rebuilds it and nothing is gated. |
| **The learner-facing tutorial is written against what runs TODAY** | `docs` sources, config, assignments and `jichi grade` all exist. The tutorial ships first, by hand; the scaffold then automates its setup steps. | *Write the tutorial for the scaffold first* — it would publish commands nobody can run, against this project's rule that every published command is run in the form published. |

## 2. What already exists, and it is most of it

- **`docs` config sources** (`docs/DOCS.md`, M34a/M45/M51) — an embeddings index
  over an arbitrary directory *or* a URL, disk-cached under
  `~/.jichi.d/docs/<name>/`, PDF-capable, reachable three ways: the
  `search_docs` tool, the `@docs:<name>` reference, and `jichi docs search`.
- **The assignment system** — spec frontmatter (`stage`, `phase`, `difficulty`,
  `points`, a `hints:` ladder, `verify:`), `test.sh` graders, `jichi grade`,
  `jichi assignments`, the hints log and `progress.jsonl`.
- **Skills** — a `SKILL.md` can hold the tutor stance, and `jc_sysmsg.c` already
  carries *NEVER write the solution*.
- **`jichi init`** scaffolding, and **`READING_THE_STANDARD.md`**, which is this
  exact shape for the C89 standard: acquire, configure, ask. What it lacks is the
  guidance loop and the assignments — which is precisely the gap here.

## 3. Measured feasibility

Run on 2026-09-18 against `google/gemma-4-12b` and
`text-embedding-nomic-embed-text-v1.5` on a loopback LM Studio.

| What | Measured |
|---|---|
| Official Python docs archive (text) | `python-3.14-docs-text.tar.bz2`, **3,332,431 bytes**, sha256 `77a6c2e1…` |
| Unpacked | **537 files, 16 MB** — the whole documentation set |
| **The tutorial alone** | **17 files** |
| `jichi docs search py-tutorial "how do I write a list comprehension with a condition"` | **4 s**, first hit `tutorial/datastructures.txt:231-274` — the list-comprehension section |
| A grounded agentic turn (`--auto`, model chooses `search_docs`, must quote and name the file) | **27 s**, quoted `controlflow.txt` on the `for…else` clause and named it |

**Two findings that shape the design:**

**The tutorial and the reference must be separate sources.** 17 files versus 537
is not a tuning detail: embedding 16 MB on a local model to answer a question
about `for…else` is the difference between a course a learner starts and one they
abandon. The scaffold indexes the *tutorial* by default and offers the library
reference as a second, opt-in source.

**Quotes drift, and the anchor is what can be checked.** In the measured turn the
model quoted the tutorial three times. Two were verbatim. The third dropped an
article — corpus *"such as a return or a raised exception"*, model *"such as a
return or raised exception"*. It is a good quote and it is not an exact one. So:
**a citation check verifies that the ANCHOR resolves** (the file exists in the
snapshot, the section is there), and treats the quoted string as something a
learner should verify, never as something the grader trusts. This is the same
lesson `reading_refs_lint` learned about the reading guides, met in a new place.

*(And a third, smaller: the archive URL guessed from the version I expected —
`python-3.13-docs-text.tar.bz2` — answered **404**. The published version was
3.14. The manifest records the version the download page gave, not the one
anybody assumed.)*

## 4. The four pieces to build

### 4.1 Corpus acquisition — `scripts/fetch-course-corpus.sh <recipe>`

A **recipe per language**, naming the project's own published archive — never a
crawl. A crawl is impolite, unversioned, and produces a different corpus every
time it runs.

    corpora/python.recipe    docs.python.org, text archive, PSF licence
    corpora/racket.recipe    the Racket docs bundle
    corpora/<lang>.recipe    contributed the same way

It writes `MANIFEST.json` beside the snapshot:

```json
{ "language": "python", "version": "3.14",
  "source": "https://docs.python.org/3/archives/python-3.14-docs-text.tar.bz2",
  "retrieved": "2026-09-18", "sha256": "77a6c2e1…",
  "licence": "PSF-2.0", "files": 537, "tutorial_files": 17 }
```

The manifest is the thing that makes a course **reproducible and honest**: two
learners on the same version have the same corpus, and a claim can name what it
was checked against. It is the `--ref-secs` lesson in a new domain — a number
without its denominator is not a measurement.

### 4.2 `jichi init course --language <lang>`

Writes into the learner's directory: a config (chat model, an `embed`-role model,
the `docs` sources), `COURSE.md` (the route through the tutorial, section by
section), `assignments/`, and the manifest. Nothing here is novel machinery — it
is the scaffold writing the files the tutorial tells a learner to write by hand.

### 4.3 Assignments: a template per tutorial section

The grader contract, and it is the part that must not be improvised:

- **The grader runs the learner's code.** For Python this is a real floor —
  unlike the reading tasks, whose graders can only check structure. A test file
  per task, and the pass condition is the test passing.
- **The grader is committed and hand-written.** The model may reword the prompt,
  the hints ladder and the worked example. It may not touch `test.sh`.
- **Each spec cites its tutorial section** as a `docs` anchor that resolves in
  the snapshot — checkable by a lint, and the answer to the grounding problem.
- **Two-sided, like every other grader here**: a pristine tree FAILS, the
  reference solution PASSES. `tests/e2e/curriculum_graders.py` already enforces
  that shape for the built-in curriculum and is the model to copy.

### 4.4 The coaching skill

Drives the loop: read a section → ask the learner to **predict** before revealing
→ set the task → grade → on failure, the next hint rung, never the answer. Every
claim about the language cites the snapshot rather than the model's memory, and
the skill is written to make jichi *say which file it read*.

## 5. What a script can check, and what it cannot

**Can:** that every spec's citation anchor resolves in the snapshot; that each
task's grader is two-sided; that the manifest exists and its checksum matches;
that no generated file overwrote a committed grader; that `COURSE.md`'s sections
map onto files that exist in the corpus.

**Cannot:** whether the task teaches the section it claims to; whether a hint
rung actually helps; whether the learner understood. Those need a human, and the
design should not pretend otherwise — the honest instrument for them is the
craft-A/B-style graded read, not a lint.

## 5b. What the retrieval stack needs, from having used it

*Recommendations, grounded in the measured runs above rather than in review of the
code. Ordered by value to a learner.*

**1. ~~Reduce HTML to text for `path` sources~~ — DONE (M667).**
Measured on Racket, 2026-09-19, and it is the clearest improvement available.
`DOCS.md` states that a `url` source is fetched and "reduced to plain text (tags
dropped, `<script>`/`<style>` skipped …)" before indexing. A **`path`** source is
not, and Racket ships its documentation as Scribble HTML — so indexing
`/usr/racket/doc/guide` embeds the markup with the prose:

| | Python (text corpus) | Racket Guide (HTML corpus) |
|---|---|---|
| files | 17 | 151 |
| first `docs search` | **4 s** | **72 s** |
| what a hit contains | the passage | the passage wrapped in `<span class="RktSym">`, `&ldquo;`, and href URLs |

Retrieval still returned the right section (`lambda.html` §4.4.2, *Declaring
Optional Arguments*), so this is a **cost-and-quality** problem rather than a
correctness one — which is precisely why it is worth fixing and not urgent to
panic about. The function already exists and is already called from this file:
`jc_docs_html_to_text` (`src/index/jc_docs.c:145`), used at line 360 on the `url`
path. Applying it to `.html`/`.htm` files on the `path` route is a small,
well-scoped change whose teeth are easy: the same search, with the chunk free of
tags and the first-search time down. **It deserves its own milestone** rather than
a drive-by at the end of a long session — the indexer is the instrument here, and
this project has a rule about changing the instrument casually.

**Built, measured, and the numbers held.** `jc_index_build` gained a `with_html`
flag — opt-in, because a **codebase** index must keep markup (in a web project the
markup *is* the code) while a **docs** index wants prose. The same reducer the
`url` path already used now runs on `path` sources, and **the file path is kept**,
so a citation still points at the real page. The Racket Guide's first cold search
went **72 s → 7 s**, the passage came back as prose, and the same first hit
(`lambda.html` §4.4.2) was returned both times — retrieval was never wrong, it was
paying to embed markup.

Two drivers hold it: `docs_html.sh` indexes an HTML fixture through a mock
embedder and asserts the chunk carries the prose, **no tags**, no class names or
`<script>`/`<style>` bodies, and decoded entities; `docs_html_scope_lint.sh` parses
every `jc_index_build` call site and holds codebase calls at 0 and docs calls at
1. Both were proven red first.

**And a second, smaller thing the corpus surfaced:** the reducer decoded `&amp;`,
`&lt;`, `&gt;`, `&quot;`, `&apos;`, `&nbsp;` and numeric references, but not the
**named typographic** ones — so a learner read `&ldquo;rest&rdquo;` inside a
passage. That is M524's finding (`&#167;3.6.2` out of the C standard) in its named
spelling. `ldquo rdquo lsquo rsquo mdash ndash hellip sect` are decoded now.

**2. A way to CHECK a quote — the one real gap.** The tutorial teaches the habit
(*verify the anchor, read the quotation against the file*) because the measurement
forced it: of three passages the model quoted, two were verbatim and one dropped
an article. A habit is the right answer for a person and the wrong answer for a
grader. What is missing is a helper — `jichi docs verify <source> <text>`, or the
same thing exposed to a grader — that **whitespace-normalises both sides** and
answers *found at `file:line-line`* or *not found*. Normalisation is not a detail:
these corpora are hard-wrapped at ~70 columns, so a quoted sentence spans lines and
a naive exact match fails on text that is genuinely present. I made exactly that
mistake while checking the model's quotes, with `grep`, and briefly believed the
model had invented them. This is the single highest-value addition for a course,
because it converts "cite your source" from advice into a gate.

**3. The anchor is already there; the citation does not use it.** `jc_docs.c` hands
the model `path:start-end` for every hit. In the measured turn the model named the
file and not the lines — so the precision exists and is being dropped. That is a
prompt-and-skill problem rather than a retrieval one, and it is cheap: the coaching
skill should require `file:line-line`, and the citation lint should accept nothing
less. A line range is what makes "go and read it yourself" a two-second action.

**4. `jichi docs` should say whether a source is INDEXED.** Today it lists name and
path, which is the right shape (*you are here*) and half the information. After a
first search that takes seconds a learner's next question is always *did that work,
or is it still thinking?* — so the listing should carry chunk count, the embedding
model, and the cache's age. This is the same argument as the platform matrix's
missing denominators: the number is free at the time and puzzling by its absence.

**5. Warn when a source is large, at the moment it is added.** 537 files versus 17
is the difference between a course someone starts and one they abandon, and nothing
in the tool says so — the learner discovers it by waiting. A line at index time
naming the file count, with the narrower subdirectory suggested, costs nothing.

**Not recommended.** Chunk-size or reranker tuning: the retrieval measured here
returned the right passage as its *first* hit on the first question asked, at 4 s.
Tuning a component that is not failing is how a session spends an afternoon and
reports an improvement it cannot demonstrate.

## 6. Milestones

1. **The generic tutorial**, written against today's jichi, every command run in
   the form published — the deliverable the operator asked for, and it makes the
   whole path usable before any code is written.
2. `fetch-course-corpus.sh` + the manifest + the Python and Racket recipes.
3. The citation lint (anchors resolve in the snapshot).
4. `jichi init course` — the scaffold, once the hand path is proven.
5. The coaching skill.
6. One complete Python track: templates + graders for the tutorial's sections,
   two-sided in `curriculum_graders.py`.

Bibliography sections for **Python and Racket** supply the "important literature
and papers" this course points at, and are the natural companion piece.

> **Superseded 2026-09-21.** This paragraph said `docs/BIBLIOGRAPHY.md` *"defers
> six languages: Racket, Guile, Elixir, Haskell, Clojure, Python"*. It no longer
> does: Python §6 and Racket §7 landed at **M671** — this plan is why — and Guile,
> Elixir, Haskell and Clojure followed at **M677**. The companion piece was
> written. Kept rather than edited away, because a plan is a record of what was
> true when it was written, and the sentence is only misleading if nobody says so.

## 7. Open questions

- ~~**Which languages get recipes first?**~~ **Answered by measuring, 2026-09-19.**
  Python ships a text archive; **Racket publishes none** — docs.racket-lang.org is
  a Scribble HTML site with no bundle, and the release listing has no doc tarball.
  What Racket ships is documentation *with the distribution*, so the fetch script
  now has two kinds: `archive` (download, verify, unpack) and `distribution` (ask
  the installation where its documents are, and say plainly when it is not
  installed). Both recipes exist and both were run.
- **Does a course track belong in `jichi assignments`' stage system**, or does a
  learner's course stay entirely in their own directory? The second is simpler
  and keeps jichi's curriculum about jichi; the first gives the learner one
  progress view across everything they are learning.
