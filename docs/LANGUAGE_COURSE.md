# Learning a language with jichi — from an official tutorial

**For a self-learner.** You will download a language's own documentation, set up a
project where jichi can read it, and then work through the tutorial with jichi as
a tutor that **cites the documents instead of its memory**.

Every command on this page was run in the form shown, on 2026-09-19, and the
outputs quoted are the outputs it produced. Where a number appears it was
measured.

> **Unfamiliar word?** [`VOCABULARY.md`](VOCABULARY.md) defines the terms this
> project leans on before it uses them — including the English **idioms**
> (*dogfooding*, *blast radius*, *born red*), which are figures of speech rather
> than technical terms and do not survive a dictionary. *Corpus*, *snapshot*,
> *manifest*, *grounding* and *retrieval* are the ones this page leans on.

---

## 0. What you will have at the end

- The language's official documentation **on your machine**, pinned to a version,
  with a manifest saying exactly what you downloaded and when.
- A project directory jichi opens with that documentation as a **reference it can
  search**.
- A way to ask questions and get answers that **name the file they came from**.
- Graded assignments built on the sections you have read.

You need: jichi built, and a model. A local one is enough — everything below was
measured against LM Studio on this machine.

## 1. Get the documents

```sh
scripts/fetch-course-corpus.sh --list          # which languages have a recipe
scripts/fetch-course-corpus.sh python
```

What it did here:

```
== Python -- reading https://docs.python.org/3/download.html for the current version
ok - published version is 3.14 (archives/python-3.14-docs-text.tar.bz2)
ok - manifest written: …/course-corpora/python-3.14/MANIFEST.json
ok - 537 files, 17 of them in tutorial/
```

**Three things it deliberately does not do**, and each is a decision you inherit:

- **It does not crawl the website.** It downloads the archive the project itself
  publishes. A crawl is impolite to the host, and it gives a different corpus
  every time it runs — so you and another learner would be reading different
  books with the same name.
- **It does not guess the version.** It reads the download page. The first
  hand-run of this fetch guessed `python-3.13-…` from the version its author
  expected and got **HTTP 404**; the published version was 3.14.
- **It does not put anything in a repository.** This is somebody else's
  documentation, and it lives with your other downloads.

The `MANIFEST.json` beside the snapshot is what makes your work checkable later:

```json
{ "language": "python", "version": "3.14",
  "retrieved": "2026-09-19",
  "sha256": "77a6c2e1d57c32c56c3454e1ae7669f8a733a8b1b719f0040b9313d7e5473392",
  "licence": "PSF-2.0", "files": 537, "tutorial_files": 17 }
```

> **If the fetch fails** because the page changed its layout, it says so and names
> the pattern it looked for. Read the page yourself before editing the recipe —
> the recipe is a claim about somebody else's website, and websites move.

## 2. Make the project

A directory of your own, with a config naming two things: a model, and the
documentation.

```sh
mkdir -p ~/development/course-python && cd ~/development/course-python
```

> **Or let the scaffold write it** (M675): `jichi init course` puts a `COURSE.md`
> (this route, condensed, with a log table at the bottom), the **`course-coach`**
> skill and a `/course` command into that directory. It deliberately writes **no
> config** — the `docs` path is the snapshot only you know and the embed model is
> whatever your machine actually has, so a scaffolded one would be a file of
> guesses that `doctor` then reports as broken. Everything below still applies;
> the scaffold saves the typing, not the understanding.

`jichi.json`:

```jsonc
{
  "models": [
    { "name": "tutor", "provider": "openai", "model": "google/gemma-4-12b",
      "apiBase": "http://127.0.0.1:1234/v1", "apiKey": "lm-studio" },
    { "name": "embed", "provider": "openai",
      "model": "text-embedding-nomic-embed-text-v1.5",
      "apiBase": "http://127.0.0.1:1234/v1", "apiKey": "lm-studio",
      "roles": ["embed"] }
  ],
  "docs": [
    { "name": "py-tutorial",
      "path": "/home/…/course-corpora/python-3.14/tutorial" }
  ],
  "snapshots": false
}
```

**You need an `embed`-role model.** Searching documents is not the chat model's
job: the text is turned into vectors once and cached, and that is what makes the
second question cheap.

**Index the tutorial, not the whole documentation set.** Measured here: the
tutorial is **17 files**, the full set is **537 files / 16 MB**. Add the library
reference as a *second, named* source when you actually need it — embedding it to
answer a question about `for` loops is the difference between a course you start
and one you abandon.

## 3. Check where you are

```sh
$ jichi --config jichi.json docs
Configured documentation sources:
  py-tutorial      /home/…/course-corpora/python-3.14/tutorial
```

That is the whole idea this project applies everywhere it can: **you are here,
and this is what you can do here.** `jichi assignments` does the same for a
curriculum, and `jichi describe` for the program's own surfaces. When you are
lost, ask a tool what it has, before asking a model what it thinks.

## 4. Ask the documents something

```sh
$ jichi --config jichi.json docs search py-tutorial \
    "how do I write a list comprehension with a condition"
```

Measured: **4 seconds**, and the first hit was

```
…/python-3.14/tutorial/datastructures.txt:231-274
   >>> [weapon.strip() for weapon in freshfruit]
   …
```

The `file:lines` anchor is the point. It is not decoration — it is what lets you
**go and read the passage yourself**, and what lets an assignment check that a
claim came from the tutorial rather than from the model.

## 5. Work through a section with jichi

Now ask jichi, and require it to use the documents:

```sh
$ jichi --config jichi.json --auto -q -p \
  "Use the search_docs tool on the py-tutorial source to find what the tutorial
   says about the 'else' clause on a for loop. Quote the tutorial and give the
   file it came from. Do not answer from memory."
```

Measured: **27 seconds**, and it quoted `controlflow.txt` on `for … else`.

**Then check the quote.** This is a habit, not an insult to the model: of the
three passages it quoted here, two were word-for-word and the third dropped an
article. A good quote is not automatically an exact one, so:

- the **anchor** (the file) is what you trust and verify;
- the **quotation** is what you read against the file before you rely on it.

> **If jichi answers without searching**, it is answering from memory — and for a
> famous tutorial it will often be right, which is exactly what makes it a bad
> habit. Say *"use the search_docs tool"* explicitly, and ask which file the
> answer came from. If it cannot name one, it did not look.

## 6. The assignments

Working through a tutorial is reading. An assignment is where you find out
whether you can *use* what you read.

```sh
jichi assignments --stage extras        # you are here: what exists, what it is worth
jichi grade docs/assignments/<id>.md    # what a script can check
```

The graders in this project check the things a script can honestly check — that
the code runs, that the tests pass, that a citation resolves. **They do not grade
whether you understood**, and no grader here pretends to. That judgement stays
with you and your reviewer.

**Python has a graded track, and it is four tasks long** (M674). Each one names
the section of the tutorial you just downloaded that it comes from, so the answer
to *"where does this come from?"* is a file on your disk:

```sh
jichi assignments --stage python         # the four, and what each is worth
jichi grade docs/assignments/81-python-make-it-pass.md
```

| | What you practise | Pts |
|---|---|---|
| `81-python-make-it-pass` | the fix-forward loop; a function that remembers cannot be tested alone | 2 |
| `82-python-test-first` | write the failing test **first**, then fix — the bug a bare `except:` was hiding | 3 |
| `83-python-loops-to-comprehensions` | refactor under green tests; the smell is checked mechanically | 3 |
| `84-python-capstone` | a test file read as a specification, and a compound sort key | 4 |

They need **`python3` and nothing else** — no package manager, no virtual
environment, no third-party library — and the graders name the tool if it is
missing, so an absent toolchain never reads as a wrong answer.

> **What does not exist yet, said plainly.** **Racket has no course track**, and
> nor does any other language you might snapshot with §1 — only Python does. The
> design decision is fixed and will not change when they arrive: the task
> templates and their graders are **hand-written and committed**, and a model may
> personalise the wording and the hints but **never** the grader. For every
> language except Python, §5 is the part of this page that gives you a course,
> and the built-in curriculum ([`ASSIGNMENTS.md`](ASSIGNMENTS.md)) is where the
> graded work is.

## 7. When it goes wrong

| Symptom | What it usually is |
|---|---|
| `docs search` returns nothing | No `embed`-role model in the config. The chat model cannot do this. |
| The first search is slow, later ones fast | Correct. The corpus is embedded once and cached under `~/.jichi.d/`. |
| Answers do not name a file | The model is not calling the tool. Ask for the tool by name; check the model does native tool calls (`jichi doctor --live`). |
| The fetch 404s | The version moved. Re-run it — it reads the page rather than trusting a remembered URL. |
| Answers cite a file that does not exist | Stop trusting that session's citations and re-ask with the tool named. An invented anchor is the failure this whole arrangement exists to make visible. |

## 8. Another language

The recipe is the only language-specific part:

```sh
ls scripts/corpora/            # python.recipe, …
scripts/fetch-course-corpus.sh <language>
```

A recipe is one of two **kinds**, and which one it is depends on what the
project publishes — not on preference:

- **`archive`** — the project publishes a documentation download. Python does.
  The recipe names the page that states the version, the pattern of the archive
  link, and which subdirectory is the tutorial.
- **`distribution`** — the project ships its documentation *with the software*
  and publishes no bundle. **Racket is this**: docs.racket-lang.org is a Scribble
  HTML site with no download, and the release listing has no doc tarball. The
  recipe instead names a command that asks the installation where its documents
  live, and the fetch says plainly when the language is not installed rather than
  inventing a URL.

```sh
$ scripts/fetch-course-corpus.sh racket
== Racket -- asking the installation where its documentation is
ok - documentation directory: /usr/racket/doc
ok - 4089 files in the doc tree, 151 of them in guide/
```

> **A note on HTML corpora, because you may read about the old behaviour.**
> Racket's pages are HTML, and until M667 jichi reduced HTML to prose only for
> `url` documentation sources — so a `path` source embedded the markup along with
> the text. The first search over the Guide took **72 s**, and passages came back
> wrapped in `<span class="RktSym">`. That is fixed: `path` sources are reduced
> too, the same search now takes **7 s**, and the passage reads
> *“…accepts the “rest” of the function arguments”* with real quotation marks.
> The citation still points at the real `.html` file, so you can open it. If you
> are on an older build, this is what you are seeing and why.

---

**Design and decisions:**
[`plans/2026-09-language-course.md`](plans/2026-09-language-course.md).
**How the document index works:** [`DOCS.md`](DOCS.md).
**The same method applied to a standard rather than a tutorial:**
[`READING_THE_STANDARD.md`](READING_THE_STANDARD.md).
