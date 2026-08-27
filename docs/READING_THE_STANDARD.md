# Reading the C standard — a method, with jichi as the instrument

*For self-learners working in C89. Written 2026-08-21 (M524). Every command here
was run in the form shown, against the real documents, and the outputs quoted are
the outputs it produced. Companion to [`C_STANDARDS.md`](C_STANDARDS.md), which
argues *which* standard and why; this page is about **reading** one.*

## Recommendation first: what to read, and in what order

The C standards committee's hub, [c-language.org](https://www.c-language.org/),
links an unusually good set of **free** C89-specific material. It does not,
however, offer any guidance on *how to read* it — which is the gap this page
exists to fill. In order:

1. **The ANSI C Rationale** —
   <https://www.lysator.liu.se/c/rat/title.html> (HTML). Start here, not with the
   standard. It explains *why* the clauses say what they say, in prose written for
   humans, and it is the single most useful document on this list for a learner.
2. **The C89 draft** — <https://port70.net/~nsz/c/c89/c89-draft.html> (HTML, one
   page, ~4,300 lines of text once reduced). The actual clause text, and the thing
   you will end up searching most.
3. **Technical Corrigenda 1 and 2** —
   [tc1](https://www.open-std.org/jtc1/sc22/wg14/www/docs/tc1.htm),
   [tc2](https://www.open-std.org/jtc1/sc22/wg14/www/docs/tc2.htm). Read these
   *before* you rely on a clause: they are the official corrections, and the draft
   above predates them.
4. **The comp.lang.c FAQ** — <https://c-faq.com/>. The questions learners actually
   hit, answered by people who read the standard so you can start by reading the
   answer.
5. **FIPS PUB 160** —
   <https://nvlpubs.nist.gov/nistpubs/Legacy/FIPS/fipspub160.pdf> (PDF). The US
   federal adoption of C89. Useful if you want a downloadable, page-numbered
   artifact; jichi indexes PDFs too ([`DOCS.md`](DOCS.md)).

**What not to do:** do not read the standard front to back. It is a specification
for implementers, not a course. Read the Rationale for orientation, then use the
standard the way you use a dictionary — one question at a time.

## The vocabulary that changes the meaning of everything

Four terms, and mistaking them is the commonest self-learner error in C. From the
standard itself (§1.6, retrieved with the recipe below):

> *Unspecified behavior* — behavior, for a correct program construct and correct
> data, for which the Standard imposes no requirements.
>
> *Undefined behavior* — behavior, upon use of a nonportable or erroneous program
> construct, of erroneous data, or of indeterminately-valued objects, for which
> the Standard imposes no requirements. […]
>
> *Implementation-defined behavior* — behavior […] that depends on the
> characteristics of the implementation and that each implementation **shall
> document**.
>
> *Locale-specific behavior* — behavior that depends on local conventions […]
> that each implementation shall document.

And the sentence that governs how you read every other clause:

> If a ``shall'' or ``shall not'' requirement that appears **outside of a
> constraint** is violated, the behavior is **undefined**.

So *where* a `shall` appears matters as much as the `shall`. Inside a Constraints
paragraph, violating it is a diagnosable error — your compiler must tell you.
Outside one, violating it is undefined behaviour — your compiler need not say
anything at all, and the program may work for years. `C_STANDARDS.md` argues the
consequences of this for portability; this is the clause it rests on.

## Locating a clause: two tools, and the measured reason for both

jichi can index external documents ([`DOCS.md`](DOCS.md)) so you can ask questions
of the standard. Set it up once:

```jsonc
// in your config -- you need a model with the `embed` role
{
  "docs": [
    { "name": "c89",      "url": "https://port70.net/~nsz/c/c89/c89-draft.html" },
    { "name": "rat-lang", "url": "https://www.lysator.liu.se/c/rat/c1.html" },
    { "name": "rat-lib",  "url": "https://www.lysator.liu.se/c/rat/d1.html" }
  ]
}
```

Then:

```sh
jichi docs search c89 "what makes behaviour undefined"
```

First use fetches the page, reduces it to text, caches it under
`~/.jichi.d/docs/<name>/page.txt` and indexes it. Measured: **12.3 seconds** for
the C89 draft on a local embedding model, and it returned §1.6 — the definitions
quoted above — as its first hit. The cache is re-fetched when older than a day and
a stale copy is reused if the network is down, so this keeps working offline.

**Now the part that matters, because it is where naive use goes wrong.** Semantic
search finds a *neighbourhood*, not a citation. Measured on this very page's
research:

| Query | What came back |
|---|---|
| "what makes behaviour undefined" | §1.6, exactly right |
| "The sprintf function is equivalent to fprintf except that the argument s specifies an array" | the **`scanf`** clause |
| "Translation limits The implementation shall be able to translate and execute" | §2.2.3 **Signals and interrupts** |

The `sprintf` miss is not a bug and not fixable by better phrasing — I tried the
standard's own wording and still got `scanf`. The library clauses are
near-duplicates of each other ("X is equivalent to Y, except…"), so an embedding
cannot separate them. **For a named thing, use a literal search; for a concept,
use the semantic one.** The cache is a plain text file, which makes the literal
half trivial:

```sh
grep -n 'The sprintf function' ~/.jichi.d/docs/c89/page.txt
#  243:4.9.6.5 The sprintf function        <- the table of contents
# 2917:4.9.6.5 The sprintf function        <- the clause
```

Two hits, because the document has a table of contents: the **second** is the
text. That two-step — semantic to find the region, literal to land on the clause,
and the section number to confirm — is the whole method.

**One caveat on the Rationale:** a `url` source indexes **one page**, and the
Rationale is split across many (`a.html`, `c1.html`, `d1.html`, …). Pointing a
single source at `title.html` would index its table of contents and nothing else.
Add the chapters you want as separate sources, as the config above does, or mirror
the site locally and use `path`.

## Exercises: from a rule jichi enforces to the clause that governs it

Each has a checkable answer, and the answer is already in this repository — which
is the point. Find the clause first, then read jichi's rule again.

1. **Why is `sprintf` banned outright?** (`CLAUDE.md`: *never call `sprintf`*,
   enforced by `tests/smoke/sprintf_lint.sh`.) Find §4.9.6.5 and read what it says
   about the size of the destination. *Answer: nothing. It says the output "is to
   be written" into the array `s` and stops — there is no parameter bounding it,
   so bounding it is entirely the caller's problem. `jc_snprintf` is bounded by
   construction; that is the whole argument.*
2. **Why must declarations sit at the top of a block?** Find the grammar
   production for `compound-statement`. *Answer: it is `{`, then an optional
   declaration-list, then an optional statement-list, then `}` — the declarations
   come before the statements in the grammar itself, so a declaration after a
   statement is not "discouraged style", it does not parse.*
3. **Why does this project split long string literals?** (`CONTRIBUTING.md`.)
   Find §2.2.4.1. *Answer: 509 characters in a character string literal, after
   concatenation. The same section is worth reading whole: 31 significant initial
   characters in an internal identifier, **6** in an external one, 15 nesting
   levels of compound statements, 8 levels of `#include`.*
4. **Why `%lu` with a cast rather than `%zu`?** Find the `fprintf` clause's list
   of length modifiers, and `size_t`'s definition. *Answer: the optional modifiers
   are exactly `h`, `l` (ell) and `L`, and the clause closes "If an h, l, or L
   appears with any other conversion specifier, the behavior is undefined." The
   letter `z` appears nowhere in the document — a literal search of the cached
   text returns zero hits — so `%zu` is not a C89 conversion at all. And `size_t`
   is only "an unsigned integral type" defined in `<stddef.h>`, chosen by the
   implementation, so you cannot know which specifier would fit it. Casting to
   `unsigned long` and printing `%lu` is how you make it knowable.*
5. **A trap this page's own author fell into.** Writing a test for this milestone,
   `"\xc2\xa73.6.2"` did not mean *§* followed by *3*. Find the grammar for
   `hexadecimal-escape-sequence` and explain why. *Answer: the production is
   recursive — `hexadecimal-escape-sequence hexadecimal-digit` — so the escape
   consumes **every** hex digit that follows it. `\xa73` is one character of value
   0xA73, not two. The fix is to split the literal: `"\xc2\xa7" "3.6.2"`. Three
   failing checks, and the standard says so in one production.*

## Where this fits

- [`C_STANDARDS.md`](C_STANDARDS.md) — which standard, and what portability costs.
- [`CURRICULUM.md`](CURRICULUM.md) — the graded road; this page is optional
  reading alongside any module that touches C semantics.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) — the C89 rules this project enforces,
  most of which now have a clause you can go and read.

## Honest limits

- **The draft is a draft.** It predates both Technical Corrigenda, and it is not
  the ISO text (which is not free). For anything you would stake a release on,
  check TC1 and TC2.
- **The HTML→text reduction is best-effort**, not a parser: tables and deeply
  nested markup flatten. It is good enough to search and read; it is not a
  faithful rendering.
- **Retrieval quality was measured on one document with one local embedding
  model.** The `sprintf` failure above is reproducible; how much better a larger
  embedder would do was not measured.
- **No exercise here was validated by a second reader.** The clause references
  were checked against the draft text quoted in this page's own research, not
  against the ISO standard.
