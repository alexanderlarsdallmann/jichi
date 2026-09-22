# The bibliography — the reading jichi's documentation points at

jichi teaches from its own source. That is the whole design of
[CURRICULUM.md](CURRICULUM.md), the four reading guides in
[`reading/`](reading/), and the graded tasks: the codebase in front of you is
small enough to read whole, and every lesson is anchored in something you can
compile. It is also, deliberately, a closed world.

**This page is the door out of it.** Every entry here answers a question jichi's
own documentation raises and then declines to answer, because answering it
properly is a book. [fukabori-01-why-c89.md](reading/fukabori-01-why-c89.md)
argues for C89 and does not teach you C; [C_STANDARDS.md](C_STANDARDS.md)
contrasts the dialects and does not hand you the standard;
[CPP_BUILD.md](CPP_BUILD.md) compiles the tree as C++ and teaches you no C++ at
all. Those three gaps, and fifty more like them, are what follows.

**Who it is for.** The same reader as the rest: a self-learner with a laptop,
alone ([CURRICULUM.md](CURRICULUM.md)). So **free and freely-readable works are
marked and come first within each group**: of the **159 entries** below (34 craft,
18 C, 12 C++, 8 Zig, 9 Rust, 8 Python, 8 Racket, 7 Guile, 9 Elixir, 9 Haskell,
9 Clojure, 18 interfaces, 10 Ada/SPARK), **110 carry a link to a text you can read for
nothing** — including a complete C book, a complete Zig book, SICP, *The Scheme
Programming Language*, *Clojure for the Brave and True*, and every language
standard that matters here in its last free working draft. A bibliography a
learner cannot afford is a reading list for somebody else.

> **That second number is computed, not counted by hand** (M677). The rule is
> mechanical and stated so it can be checked: an entry is *free* when its bullet
> carries a bare `<https://…>` link to the text. The figure this replaces was
> maintained by hand at **74**, and the first mechanical recount of the same page
> returned **75** — one entry's worth of drift, in a number nobody could have
> falsified by reading. `bibliography_lint.sh` recomputes both totals now, so
> neither can be incremented again.

Those four counts and the 53 are **counted by the lint, not maintained by hand**
(`tests/smoke/bibliography_lint.sh`), because this project has watched a
hand-maintained count drift for eight straight milestones, each one incrementing
the previous claim instead of recounting (M259).

## What this is not

- **Not a canon.** It is *this project's* reading, chosen because it bears on
  work you can see in this tree. A book missing from it is not a book judged.
- **Not a substitute for the tree.** Reading about arenas is weaker than reading
  `jc_mem.c` with `/context` open. Pair them.
- **Not a syllabus.** Nothing here is graded, sequenced or gated. The graded
  path is [`assignments/INDEX.md`](assignments/INDEX.md), and it needs none of
  these.
- **Not complete.** Eight areas: the craft, C, C++, Zig, Rust, Python, Racket and
  interfaces. The four other languages with tracks in this tree — Guile, Elixir,
  Haskell and Clojure — have none of their literature here yet, and saying so is
  cheaper than a thin section per language (DEFERRED).

## How every entry was checked, and on what date

The project's register is that a claim carries its evidence
([APPROACH.md](APPROACH.md)), and a reading list is unusually easy to write
without any. So, on **2026-09-16**, from this bench:

| Marker | What was actually done |
|---|---|
| `[read]` | The page was fetched and its content read. The description here is written from it, not from memory. |
| `[probed]` | The URL was requested and returned the status shown. Nothing was read; the claim is only that it resolves. |
| `[ISBN verified]` | The ISBN-13 was resolved against Open Library and returned **that exact title, publisher, edition and year**. Nothing was read; no judgement about the book's contents was verified this way. |
| `[DOI]` | A DOI, given because the publisher answers an automated request with `403` and cannot be probed honestly. |

**A link failed while this page was being written, and the first edition of this
paragraph drew the wrong conclusion from it** — which is the more useful lesson,
so both halves are kept.

`c-faq.com`, the canonical home of the comp.lang.c FAQ, refused the connection on
both ports that morning (`curl: (7) Failed to connect`, twice, plus two failed
fetches). This paragraph concluded it was **dead**, cited the FAQ through the
Lysator mirror, and said so in four places including a commit message. Re-probed
the same afternoon at the operator's prompting, it answered **`200` in 0.47 s,
three times running, serving the real FAQ**. It was a transient outage.

So: **one probe distinguishes "down right now" from "gone" not at all.** A single
`curl` failure is evidence about a moment, and the honest report of it is
*unreachable at HH:MM*, not *dead*. The rule this page now follows is the one it
should have started with — **re-probe before you demote a canonical URL**, and
when a mirror is used, say which is canonical so a reader can go back to it. This
is the same failure shape CLAUDE.md names for classifiers: an else-branch stated
as a positive finding.

The FAQ below is therefore cited at `c-faq.com`, with the Lysator mirror as the
alternate. Re-run the whole sweep with `scripts/check-bibliography.sh`;
`tests/smoke/bibliography_lint.sh` enforces the *shape* of every entry offline, on
every `make smoke`. Neither can tell you a site is gone — only that it did not
answer this time.

---

**The ISBN route changed under this page, measured 2026-09-20.** Entries dated
2026-09-19 were verified through Open Library's `/api/books` endpoint. That
endpoint now answers **HTTP 404** while the site itself answers 200 — so a probe
that only checked "is the host up?" would have reported the verification working.
The M677 entries use `https://openlibrary.org/search.json?q=<isbn>` instead, and
each one records the title, author and publisher that came back, which is the
part a reader can actually re-check. The older markers are left as they are:
they were true on their date, and that is what a dated marker means.

## 1. The craft

Software development as a discipline — the part of the subject that outlives
every language in this file. [CURRICULUM.md](CURRICULUM.md) and
[APPROACH.md](APPROACH.md) teach this practically; these teach it historically,
which is the half that tells you which of today's certainties are fashions.

### Freely readable

- **Teach Yourself Programming in Ten Years** — Peter Norvig.
  <https://norvig.com/21-days.html> [probed 2026-09-20: HTTP 200]
  Read it for: the argument against the "learn X in 24 hours" shelf, and a
  realistic clock for the thing you are attempting. Short, free, and addressed
  to exactly the reader this project is for — it is also the essay jichi's
  author set his own course by, which is why the README names it.
- **Structure and Interpretation of Computer Programs** — Abelson & Sussman, 1996. MIT Press, 2nd edition. ISBN 978-0-262-51087-5 [ISBN verified 2026-09-16]. Free PDF from MIT's own course: <https://web.mit.edu/6.001/6.037/sicp.pdf> [read 2026-09-16] · the same edition as HTML: <https://sarabander.github.io/sicp/> [probed 2026-09-16: HTTP 200]
  Read it for: what a program *is*, before any question of which language. The five functional tracks in this tree ([RACKET_PARADIGM.md](RACKET_PARADIGM.md) and its siblings) all descend from it.
  *Both free copies are one edition in two formats* — verified, not assumed: the MIT file is 7,416,886 bytes of `application/pdf`, 883 pages, titled "Structure and Interpretation of Computer Programs, 2nd ed.", CC BY-SA 4.0, and carries the same *Unofficial Texinfo Format* typeset (2.andresraba5.6) the HTML edition serves. **Take the PDF if you are working offline** — one file, and the caution below about boards with no route out is the reason this page bothers to say which copies are downloadable.
- **The Architecture of Open Source Applications** — Brown & Wilson (eds.), 2011–2012. CC BY 3.0. <https://aosabook.org/en/> [read 2026-09-16]
  Read it for: other people's architecture chapters, written by the authors of the systems. The nearest published relative of [ARCHITECTURE.md](ARCHITECTURE.md), and the model for reading a codebase you did not write ([READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md)).
- **The E. W. Dijkstra Archive** — over a thousand manuscripts, 1962–2002. <https://www.cs.utexas.edu/~EWD/> [read 2026-09-16]
  Read it for: the habit of arguing about programs rather than reporting on them. Start at EWD215 ("A Case against the GO TO Statement", <https://www.cs.utexas.edu/users/EWD/ewd02xx/EWD215.PDF> [probed 2026-09-16: HTTP 200]). The house register in [ARGUMENT.md](ARGUMENT.md) is this, with the names attached.
- **On the Criteria To Be Used in Decomposing Systems into Modules** — D. L. Parnas, 1972. *CACM* 15(12). [DOI 10.1145/361598.361623] — publisher returns `403` to an automated request; open copy: <https://www.win.tue.nl/~wstomv/edu/2ip30/references/criteria_for_modularization.pdf> [probed 2026-09-16: HTTP 200]
  Read it for: the one paper under every module boundary in this tree. The rule that *the agent never branches on provider* (CLAUDE.md) is Parnas's criterion applied to a vtable: what varies is hidden behind the interface, not spread through the caller.
- **Hints for Computer System Design** — Butler Lampson, 1983. <https://www.microsoft.com/en-us/research/publication/hints-for-computer-system-design/> [probed 2026-09-16: HTTP 200]
  Read it for: "Handle normal and worst case separately", "Use hints", "End-to-end" — design advice at the altitude jichi's arena and cap/fence decisions live at ([HARDENING.md](HARDENING.md)).
- **Software Engineering at Google** — Winters, Manshreck & Wright, 2020. O'Reilly. ISBN 978-1-4920-8279-8. Free HTML: <https://abseil.io/resources/swe-book> [probed 2026-09-16: HTTP 200] [ISBN verified 2026-09-16]
  Read it for: the distinction between programming and *engineering* — programming integrated over time and people. Read it against [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md)'s honest single-author numbers; the disagreement is the interesting part.

### In print

- **The Mythical Man-Month** — Frederick P. Brooks Jr., 1995. Addison-Wesley Professional, Anniversary edition. ISBN 978-0-201-83595-3 [ISBN verified 2026-09-16]
  Read it for: the essay "No Silver Bullet" and the coordination tax. [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md) measures that tax against an agent-assisted solo author and reaches a result Brooks would recognise.
- **A Philosophy of Software Design** — John Ousterhout, 2021. Yaknyam Press, 2nd edition. ISBN 978-1-7321022-1-7 [ISBN verified 2026-09-16]
  Read it for: "deep modules" and complexity as the thing you are actually fighting. The shortest useful book here, and the one closest to [ARCHITECTURE.md](ARCHITECTURE.md)'s layer argument.
- **The Practice of Programming** — Kernighan & Pike, 1999. Addison-Wesley Professional. ISBN 978-0-201-61586-9 [ISBN verified 2026-09-16]
  Read it for: style, debugging, testing and portability in one small volume, all in C. If you read one book beside this tree, this is the one whose examples you can paste into it.
- **The Elements of Programming Style** — Kernighan & Plauger, 1978. McGraw-Hill, 2nd edition. ISBN 978-0-07-034207-1 [ISBN verified 2026-09-16]
  Read it for: the ancestor of every style guide, arguing each rule from a real bad program. Forty-eight years old and still the model for CONTRIBUTING.md's tone.
- **Programming Pearls** — Jon Bentley, 1999. Addison-Wesley Professional, 2nd edition. ISBN 978-0-201-65788-3 [ISBN verified 2026-09-16]
  Read it for: how to think about a problem before reaching for the keyboard — the skill [PSEUDOCODE_TUTORIAL.md](PSEUDOCODE_TUTORIAL.md) and module M6 are after.
- **Working Effectively with Legacy Code** — Michael Feathers, 2004. Prentice Hall. ISBN 978-0-13-117705-5 [ISBN verified 2026-09-16]
  Read it for: seams, and how to get a test around code that has none. This is what module M2 ("the smallest change") is doing at a smaller scale, and what an agent pointed at a strange codebase must be taught to do.
- **Test-Driven Development by Example** — Kent Beck, 2002. Addison-Wesley. ISBN 978-0-321-14653-3 [ISBN verified 2026-09-16]
  Read it for: red-green-refactor as a rhythm. jichi's [TESTING_RUNBOOK.md](TESTING_RUNBOOK.md) demands red-before-green for a harder reason than Beck's — a test never seen failing has never been seen working — so read both and note where they differ.
- **Refactoring** — Martin Fowler, 2018. Addison-Wesley, 2nd edition. ISBN 978-0-13-475759-9 [ISBN verified 2026-09-16]
  Read it for: the vocabulary of behaviour-preserving change, which module M7 grades you on.
- **The Pragmatic Programmer** — Hunt & Thomas, 2019. Pragmatic Bookshelf, 20th Anniversary edition. ISBN 978-0-13-595705-9 [ISBN verified 2026-09-16]
  Read it for: the broad professional habits — tracer bullets, orthogonality, "don't live with broken windows". Closest in spirit to [JOURNEY.md](JOURNEY.md).
- **Release It!** — Michael Nygard, 2018. Pragmatic Bookshelf, 2nd edition. ISBN 978-1-68050-239-8 [ISBN verified 2026-09-16]
  Read it for: what breaks in production and why — timeouts, circuit breakers, bulkheads. Read jichi's retry, stall-timeout and budget logic against it ([HARDENING.md](HARDENING.md) §6).
- **Code: The Hidden Language of Computer Hardware and Software** — Charles Petzold, 2022. Microsoft Press, 2nd edition. ISBN 978-0-13-790910-0 [ISBN verified 2026-09-16]
  Read it for: the floor under everything else — relays to CPUs, with no prerequisites. The right first book for someone arriving at module M0 with no background at all.
- **The Psychology of Software Teams** — Cat Hicks, 2026. CRC Press. ISBN 978-1-032-96338-9 [ISBN verified 2026-09-16]
  Read it for: empirical evidence on what actually sustains developers — thriving, agency, learning — from a psychological scientist rather than a methodologist. It sits in deliberate tension with this project's design case of one learner alone, and that is why it is here: Brooks measures what coordination costs a *project*, this measures what the team does to the *people* in it, and [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md)'s solo numbers can see neither.
  *Two honesty notes.* It is **the only entry in this section not yet settled by time** — everything else here has had a decade or more to be argued with. And its date is genuinely unclear: the catalogue resolves the ISBN to **2026** while the publisher's own page lists **2027** and 210 pages; it is recorded as 2026 because the operator of this project had read it by 2026-09-16. Its "read it for" line above was written from the publisher's description and chapter list, **not** from reading it — which every `[ISBN verified]` entry here is (the marker table says so), but is worth naming where the book is too new for that description to rest on anything else.
- **Computer Systems: A Programmer's Perspective** — Bryant & O'Hallaron, 2015. Pearson, 3rd edition. ISBN 978-0-13-409266-9 [ISBN verified 2026-09-16]
  Read it for: the machine your C actually runs on — linking, memory hierarchy, system-level I/O. The bridge between this section and the next.

---

### The design tutorials' own sources (added M650)

Six tutorials in this tree — [USE_CASE_TUTORIAL.md](USE_CASE_TUTORIAL.md),
[UML_TUTORIAL.md](UML_TUTORIAL.md),
[DOMAIN_MODELLING_TUTORIAL.md](DOMAIN_MODELLING_TUTORIAL.md),
[ARCHITECTURE_TUTORIAL.md](ARCHITECTURE_TUTORIAL.md),
[PSEUDOCODE_TUTORIAL.md](PSEUDOCODE_TUTORIAL.md) and
[TESTING_TUTORIAL.md](TESTING_TUTORIAL.md) — each ended with a list of works and
concepts under the instruction *"search these; prefer primary sources"*. Naming a
book and then telling the reader to go and find it is the one thing this page
exists to stop, so the works those lists name are cited here properly and the
tutorials now point at this section. **Concepts** in those lists stayed concepts;
only named works are entries.

#### Freely readable

- **Notes on Structured Programming** — E. W. Dijkstra, 1970. EWD249. <https://www.cs.utexas.edu/users/EWD/ewd02xx/EWD249.PDF> [probed 2026-09-17: HTTP 200]
  Read it for: sequence, selection and iteration argued from first principles — the claim [PSEUDOCODE_TUTORIAL.md](PSEUDOCODE_TUTORIAL.md) rests on when it says three constructs are enough.
- **The C4 model for visualising software architecture** — Simon Brown. <https://c4model.com/> [probed 2026-09-17: HTTP 200]
  Read it for: the Context / Container / Component / Code zoom levels that [ARCHITECTURE_TUTORIAL.md](ARCHITECTURE_TUTORIAL.md) §2 borrows, from the person who defined them.
- **Documenting Architecture Decisions** — Michael Nygard, 2011. <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions> [probed 2026-09-17: HTTP 200]
  Read it for: the original ADR template. [DECISIONS.md](DECISIONS.md) is this idea with one addition made mandatory — the alternatives that were **rejected**.
- **OMG Unified Modeling Language specification** — Object Management Group. <https://www.omg.org/spec/UML/> [probed 2026-09-17: HTTP 200]
  Read it for: the diagram types [UML_TUTORIAL.md](UML_TUTORIAL.md) deliberately does *not* teach. Knowing what you are declining is the point of that tutorial.
- **Mermaid documentation** — <https://mermaid.js.org/intro/> [probed 2026-09-17: HTTP 200]
  Read it for: the full syntax behind every diagram in this repository, since "diagrams as code" is why they survive review at all.
- **How Do Committees Invent?** — Melvin E. Conway, 1968. *Datamation* 14(5). <https://www.melconway.com/Home/Committees_Paper.html> [probed 2026-09-17: HTTP 200]
  Read it for: Conway's Law in the author's own words, which is narrower and more interesting than the slogan. [ARCHITECTURE_TUTORIAL.md](ARCHITECTURE_TUTORIAL.md) asks what it means for a solo author, which is a question the paper does not answer.
- **AnemicDomainModel** — Martin Fowler, 2003. <https://martinfowler.com/bliki/AnemicDomainModel.html> [probed 2026-09-17: HTTP 200]
  Read it for: the anemic-versus-rich argument [DOMAIN_MODELLING_TUTORIAL.md](DOMAIN_MODELLING_TUTORIAL.md) tells you to make on purpose, stated by the person who named the anti-pattern.
- **EventStorming** — Alberto Brandolini. <https://www.eventstorming.com/> [probed 2026-09-17: HTTP 200]
  Read it for: the workshop technique for discovering a domain model with experts — the half of domain modelling a solo learner cannot practise, which is worth knowing precisely because of that.

#### Behind a publisher's paywall — cited by DOI

Both return `403` to an automated request, which is the case this page's marker
table covers; neither is quoted here beyond its bibliographic record.

- **Program Development by Stepwise Refinement** — Niklaus Wirth, 1971. *Communications of the ACM* 14(4), 221–227. [DOI 10.1145/362575.362577]
  Read it for: pseudocode as a program you sharpen in passes, which is exactly [PSEUDOCODE_TUTORIAL.md](PSEUDOCODE_TUTORIAL.md)'s method and older than most of the languages it could be written in.
- **Literate Programming** — Donald E. Knuth, 1984. *The Computer Journal* 27(2), 97–111. [DOI 10.1093/comjnl/27.2.97]
  Read it for: the opposite bet to this tree's — that prose and code should live in one artifact. Worth reading so that deciding against it is a decision.

#### In print

- **Domain-Driven Design: Tackling Complexity in the Heart of Software** — Eric Evans, 2003. Addison-Wesley, 529 pp. ISBN 978-0-321-12521-7 [ISBN verified 2026-09-17]
  Read it for: entity, value object, aggregate and ubiquitous language from the source — the four words [DOMAIN_MODELLING_TUTORIAL.md](DOMAIN_MODELLING_TUTORIAL.md) is built on. The "blue book"; long, and the first three chapters carry most of what that tutorial uses.
- **Implementing Domain-Driven Design** — Vaughn Vernon, 2012. Addison-Wesley Professional. ISBN 978-0-321-83457-7 [ISBN verified 2026-09-17]
  Read it for: the practical companion to Evans — what the patterns look like in code rather than in definition.
- **Writing Effective Use Cases** — Alistair Cockburn, 2000. Addison-Wesley Professional, 304 pp. ISBN 978-0-201-70225-5 [ISBN verified 2026-09-17]
  Read it for: goal levels and extension numbering, which [USE_CASE_TUTORIAL.md](USE_CASE_TUTORIAL.md) uses and credits without previously telling you where to find them.
- **Fundamentals of Software Architecture: An Engineering Approach** — Mark Richards & Neal Ford, 2020. O'Reilly Media, 432 pp. ISBN 978-1-4920-4345-4 [ISBN verified 2026-09-17]
  Read it for: architecture as the decisions that have no right answer, only trade-offs — the framing [ARCHITECTURE_TUTORIAL.md](ARCHITECTURE_TUTORIAL.md) adopts.

## 2. C — the C89 this project is written in, and the C the rest of the world writes

jichi is strict C89 ([fukabori-01-why-c89.md](reading/fukabori-01-why-c89.md)),
which makes it an unusual place to *learn* C: you will read a dialect most
tutorials no longer teach. [C_STANDARDS.md](C_STANDARDS.md) is the in-tree guide
to that gap. These are the works on both sides of it.

### The standards themselves — all free

- **c-language.org — the official C language site** — WG14-affiliated; its FAQ is approved by ISO/IEC JTC1/SC22/WG14. <https://www.c-language.org/> [read 2026-09-16]
  Read it for: **the hub the four entries below hang off.** It indexes every revision from K&R (1978) through C23 with both the ISO/IEC numbers and the **draft document numbers** practitioners actually cite (N3220, N1570) — so it answers "which draft do I link?" in one page. Its resources page, <https://www.c-language.org/resources> [read 2026-09-16], is itself a bibliography (K&R, Gustedt, Seacord, papers on undefined behaviour and floating point, tools, tutorials, talks), and its short official FAQ is **deliberately not** the comp.lang.c FAQ cited below — it says so and points at the community lists. Start here if you do not yet know which document you want.
  *This page's first edition omitted it, and [READING_THE_STANDARD.md](READING_THE_STANDARD.md) had linked it since M191.* The C standards material here was enumerated from what its author knew rather than from what this tree already cites — the "audit the universe, not the result" failure CLAUDE.md warns about, committed inside a page whose whole subject is checking claims. Added 2026-09-16 at the operator's request.

- **ANSI X3.159-1989 / ISO C90, public draft** — <https://port70.net/~nsz/c/c89/c89-draft.html> [read 2026-09-16]
  Read it for: the actual text of the standard this codebase conforms to. When CONTRIBUTING.md says "declarations at block top" or "no `//`", this is where that comes from. Also the subject of [READING_THE_STANDARD.md](READING_THE_STANDARD.md).
- **Rationale for ANSI C** — X3J11, 1989. <https://www.lysator.liu.se/c/rat/title.html> [read 2026-09-16]
  Read it for: *why* the committee decided what it decided — the closest thing in computing to a design record for a language. The genre [DECISIONS.md](DECISIONS.md) imitates.
- **ISO/IEC JTC1/SC22/WG14 — the C working group** — <https://www.open-std.org/jtc1/sc22/wg14/> [probed 2026-09-16: HTTP 200], with the C23 working draft N3220 at <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf> [probed 2026-09-16: HTTP 200]
  Read it for: what C became after 1989 — and note that the *final* standards are paywalled by ISO while the last working drafts are free, which is why every practitioner cites a draft number.
- **cppreference — C** — <https://en.cppreference.com/w/c> [read 2026-09-16]
  Read it for: the day-to-day reference, with per-feature notes on which standard introduced what. The fastest way to answer "is this C89?" without opening the draft.

### Freely readable

- **Modern C** — Jens Gustedt, 2024. Manning, 3rd edition (C23). ISBN 978-1-63343-777-7. Free CC-licensed PDF linked from <https://gustedt.gitlabpages.inria.fr/modern-c/> [read 2026-09-16] [ISBN verified 2026-09-16]
  Read it for: **the single best answer to "what does C look like now?"** — and therefore the exact counterweight to this tree. Read it beside [C_STANDARDS.md](C_STANDARDS.md) and assignment 20 (the port down to C89).
- **comp.lang.c FAQ** — Steve Summit, 1995–2014. <https://c-faq.com/> [read 2026-09-16] · mirror: <https://www.lysator.liu.se/c/c-faq/index.html> [probed 2026-09-16: HTTP 200]
  Read it for: the canonical answers to the questions C actually confuses people with — arrays vs. pointers, `NULL`, undefined behaviour, integer promotion. Both URLs are given deliberately: the canonical host was unreachable for part of 2026-09-16 and reachable later the same day, which is why this page now names a canonical **and** a mirror rather than silently demoting one (see "How every entry was checked").
- **A Guide to Undefined Behavior in C and C++** — John Regehr, 2010. Three parts, from <https://blog.regehr.org/archives/213> [read 2026-09-16]
  Read it for: why a program that "works" can be wrong, with the compiler as the adversary. This is the ground under `make SAN=1` and under assignment 21.
- **What Every C Programmer Should Know About Undefined Behavior** — Chris Lattner, 2011. Three parts, from <https://blog.llvm.org/2011/05/what-every-c-programmer-should-know.html> [read 2026-09-16]
  Read it for: the same subject from the compiler-writer's side. Read it *with* Regehr — one explains the hazard, the other explains the incentive.
- **SEI CERT C Coding Standard** — Carnegie Mellon SEI. <https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/> [read 2026-09-16]
  Read it for: rule-by-rule secure C, each with non-compliant and compliant examples. `tests/smoke/sprintf_lint.sh` enforces one of its rules; this is where the other few hundred are.
- **libcurl API documentation** — <https://curl.se/libcurl/c/> [probed 2026-09-16: HTTP 200]
  Read it for: jichi's one dependency, and a fine example of a C API designed for stability over decades. Relevant to `src/net/jc_http.c` and to [BUILD.md](BUILD.md).

### In print

- **The C Programming Language** — Kernighan & Ritchie, 1988. Prentice Hall, 2nd edition. ISBN 978-0-13-110362-7 [ISBN verified 2026-09-16]
  Read it for: **the book this codebase is written in.** K&R2 documents exactly ANSI C — C89 — so unlike every other C book on this list it needs no mental translation to match `src/`. 272 pages.
- **Effective C, 2nd Edition** — Robert C. Seacord, 2024. No Starch Press.
  ISBN 978-1-7185-0412-7 [ISBN verified 2026-09-19] — Open Library returned
  *Effective C, 2nd Edition* / *An Introduction to Professional C Programming*,
  No Starch Press, 2024, by Robert C. Seacord.
  Read it for: **how to write C that does not have the defects**, from the author
  of the SEI CERT C standard listed above — objects and lifetimes, the integer
  conversions that quietly break arithmetic, error handling, and the undefined
  behaviour that the two Regehr/Lattner series describe from the *compiler's*
  side. This one gives you the practitioner's side of the same subject, which is
  why they belong together.
  **Note the standard it targets**, because this project is a C89 codebase: the
  second edition is written to **C23**, with C17 as the fallback. That is a
  feature for a learner and a caveat here — the *reasoning* about lifetimes,
  conversions and UB transfers unchanged, while `_Generic`, `constexpr` and the
  newer library are not available in this tree. Read the reasoning; check the
  feature against [`C_STANDARDS.md`](C_STANDARDS.md) before using it.
- **C: A Reference Manual** — Harbison & Steele, 2002. Prentice-Hall, 5th edition. ISBN 978-0-13-089592-9 [ISBN verified 2026-09-16]
  Read it for: the precise per-feature reference, C89 through C99, with the differences called out. The book to own if you write portable C across old compilers — which is what [PLATFORMS.md](PLATFORMS.md) is about.
- **C Interfaces and Implementations** — David R. Hanson, 1997. Addison-Wesley. ISBN 978-0-201-49841-7 [ISBN verified 2026-09-16]
  Read it for: **the closest published relative of jichi's own house style** — opaque types, an explicit arena allocator, and a status/exception discipline, all in C89. Read Chapter 5–6 (arenas) directly against [fukabori-03-the-three-arena-lifetime-model.md](reading/fukabori-03-the-three-arena-lifetime-model.md) and `src/util/jc_mem.c`.
- **Expert C Programming: Deep C Secrets** — Peter van der Linden, 1994. SunSoft Press. ISBN 978-0-13-177429-2 [ISBN verified 2026-09-16]
  Read it for: declaration syntax, the array/pointer confusion, and linker behaviour, told as war stories. The closest thing in C's literature to [ANECDOTES.md](ANECDOTES.md).
- **Advanced Programming in the UNIX Environment** — Stevens & Rago, 2013. Addison-Wesley Professional, 3rd edition. ISBN 978-0-321-63773-4 [ISBN verified 2026-09-16]
  Read it for: the POSIX half of this codebase — `fork`, pipes, signals, terminals, file descriptors. `src/platform/`, the fork-based parallel pool and the pty work in the smoke tier are all this book's subject matter.
- **The Linux Programming Interface** — Michael Kerrisk, 2010. No Starch Press. ISBN 978-1-59327-220-3 [ISBN verified 2026-09-16]
  Read it for: the same territory as Stevens, Linux-specific and more current. The reference for anything `docs/PLATFORMS.md` calls Linux-only.

---

## 3. C++ — modern, and why this tree is not written in it

[CPP_BUILD.md](CPP_BUILD.md) compiles this tree with a C++ front-end and
concludes, honestly, that it buys **no runtime advantage whatsoever** —
C++-buildability is a property of the codebase, not a target of it, and
`make cpp-check` keeps it cheap. [CPP_INTEROP.md](CPP_INTEROP.md) is the
migration track. Neither teaches C++. These do.

### Freely readable

- **C++ Core Guidelines** — Stroustrup & Sutter (eds.), revised 2026-06-14. <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines> [read 2026-09-16]
  Read it for: what the language's own designers think good modern C++ looks like, rule by rule with rationale. The single most useful free C++ document, and the right first stop after reading [CPP_INTEROP.md](CPP_INTEROP.md).
- **cppreference — C++** — <https://en.cppreference.com/w/cpp> [read 2026-09-16]
  Read it for: the working reference. Better than any book for "what does this do, and since which standard".
- **Standard C++ Foundation FAQ** — <https://isocpp.org/faq> [probed 2026-09-16: HTTP 200]
  Read it for: the questions a C programmer arrives with — why RAII, what a reference really is, when to use exceptions.
- **WG21 papers** — the C++ committee's public archive. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/> [probed 2026-09-16: HTTP 200]
  Read it for: proposals with their rationale, the C++ equivalent of C's Rationale document — and a view of how a living standard actually moves.
- **Compiler Explorer** — Matt Godbolt. <https://godbolt.org/> [probed 2026-09-16: HTTP 200]
  Read it for: the fastest way to see what a C or C++ construct compiles to, across compilers and standards. Directly useful for [CPP_BUILD.md](CPP_BUILD.md)'s claim that g++ compiling C-style code emits what gcc emits — check it yourself rather than believing the page.

### In print

- **Programming: Principles and Practice Using C++** — Bjarne Stroustrup, April
  2024. Addison-Wesley, **3rd edition** (C++20, with C++23 where compilers allow).
  ISBN 978-0-13-830868-1 [ISBN verified 2026-09-19] — Open Library returned
  *Programming* / *Principles and Practice Using C++*, Pearson Education, 2024,
  by Bjarne Stroustrup; the author's own support page
  (<https://www.stroustrup.com/programming.html> [probed 2026-09-19: HTTP 200])
  states the edition and the April 2024 date, which Open Library's record does
  not carry.
  Read it for: **the one book here written for someone learning to program at
  all**, rather than for a programmer learning C++. That makes it the odd entry
  in this section and the most useful one for this project's actual reader — it
  teaches the discipline (types, invariants, error handling, testing) with C++ as
  the vehicle, and the third edition rewrites the whole thing around modern C++
  rather than bolting it on. **It is the C++ counterpart to the design recipe**
  that *How to Design Programs* (§7) teaches in Racket: both answer *how do I get
  from a problem to a program*, before any question of syntax. A self-learner
  should expect a long book and a real course, not a tour.
- **A Tour of C++** — Bjarne Stroustrup, 2021. Pearson, 3rd edition (C++20). ISBN 978-0-13-681648-5 [ISBN verified 2026-09-16]
  Read it for: **the right entry point for someone who already knows C.** Short, written by the language's designer, and current. Read this before the C++ systems course (tasks 59–62).
- **The C++ Programming Language** — Bjarne Stroustrup, 2013. Addison-Wesley, 4th edition (C++11). ISBN 978-0-321-56384-2 [ISBN verified 2026-09-16]
  Read it for: the complete treatment. Note the date: it predates C++17/20/23, so pair it with the Core Guidelines rather than treating it as current.
- **Effective Modern C++** — Scott Meyers, 2014. O'Reilly. ISBN 978-1-4919-0399-5 [ISBN verified 2026-09-16]
  Read it for: the 42 items on C++11/14 that explain *why* modern C++ looks the way it does — `auto`, move semantics, smart pointers. The best account of the change that made C++ a different language from the one a C programmer remembers.
- **The C++ Standard Library** — Nicolai Josuttis, 2012. Addison-Wesley, 2nd edition. ISBN 978-0-321-62321-8 [ISBN verified 2026-09-16]
  Read it for: the containers and algorithms that are the actual argument for C++ over C. Task 60 grades you on them.
- **C++ Templates: The Complete Guide** — Vandevoorde, Josuttis & Gregor, 2017. Addison-Wesley Professional, 2nd edition. ISBN 978-0-321-71412-1 [ISBN verified 2026-09-16]
  Read it for: the feature with no C analogue at all, treated properly. Deep water; skip it until the rest is comfortable.
- **C++ Concurrency in Action** — Anthony Williams, 2019. Manning, 2nd edition. ISBN 978-1-61729-469-3 [ISBN verified 2026-09-16]
  Read it for: the memory model and threads. jichi uses **processes**, not threads ([fukabori-07-fork-based-parallelism.md](reading/fukabori-07-fork-based-parallelism.md)); this is the road not taken, and worth understanding before you conclude the choice was arbitrary.

---

## 4. Zig

The smallest section, and honestly so. Zig is pre-1.0, its language reference
moves with the compiler, and there is **no established book from a major
publisher** — so this section is weighted toward primary documentation and one
open-access book, and every version-bearing link is given **pinned as well as
floating**. [ZIG_BUILD.md](ZIG_BUILD.md) records this project's own `zig cc`
findings (verified again 2026-09-16: builds, full suite green, and a static
musl cross in 17 s); [ZIG_INTEROP.md](ZIG_INTEROP.md) is the migration track and
[ZIG_REWRITE_ANALYSIS.md](ZIG_REWRITE_ANALYSIS.md) is the honest cost estimate
for not doing it.

### Primary documentation — free

- **Zig Language Reference** — <https://ziglang.org/documentation/master/> [read 2026-09-16] · pinned: <https://ziglang.org/documentation/0.16.0/> [probed 2026-09-16: HTTP 200]
  Read it for: the whole language, in one page, with runnable examples. **Cite the pinned version, not `master`** — this is the same discipline as `jichi --version` printing its build hash, and for the same reason.
- **Zig Build System** — <https://ziglang.org/learn/build-system/> [read 2026-09-16]
  Read it for: `build.zig`, which is the part of Zig with no C analogue — the build is a Zig program describing a DAG of steps. The contrast with this project's Makefile is instructive in both directions.
- **Zig in Overview** / **Why Zig When There is Already C++, D, and Rust?** — <https://ziglang.org/learn/overview/> [probed 2026-09-16: HTTP 200] · <https://ziglang.org/learn/why_zig_rust_d_cpp/> [read 2026-09-16]
  Read it for: the design argument — no hidden control flow, no hidden allocations, explicit allocators. Read the allocator claim directly against jichi's three arenas: two languages reaching the same conclusion, one by design and one by discipline.

### Learning — free

- **Introduction to Zig: a project-based book** — Pedro Duarte Faria. Open access, CC-licensed. <https://pedropark99.github.io/zig-book/> [read 2026-09-16]
  Read it for: the only book-length open treatment — syntax, memory management, data structures, testing, C interop. The best single starting point, and it costs nothing.
- **zig.guide** — maintained by Sobeston. <https://zig.guide/> [read 2026-09-16]
  Read it for: a tutorial path through the language, standard library, build system and C interop. Successor in role to the older `ziglearn.org`.
- **Ziglings** — <https://codeberg.org/ziglings/exercises> [read 2026-09-16]
  Read it for: ~100 deliberately broken programs you fix in order, compiler error by compiler error. The closest thing in this whole bibliography to jichi's own graded assignments, and the right companion to the Zig systems course (tasks 55–58).

### People worth following

- **Andrew Kelley** (Zig's creator) — <https://andrewkelley.me/> [probed 2026-09-16: HTTP 200]
  Read it for: design posts on why the compiler, allocator and build system are shaped as they are.
- **Loris Cro** — <https://kristoff.it/> [probed 2026-09-16: HTTP 200]
  Read it for: writing on Zig's ecosystem, tooling and the pre-1.0 trade-offs, from inside the project.

---

## 5. Rust — the clean boundary

The newest section, and the one whose in-tree companion argues *against* the
gradual path the other two take. [CPP_INTEROP.md](CPP_INTEROP.md) and
[ZIG_INTEROP.md](ZIG_INTEROP.md) are compile → extend → refactor arcs behind an
unchanged C header; [RUST_INTEROP.md](RUST_INTEROP.md) is "the clean-boundary
track (why this one is different)", because Rust will not be spliced into a C
translation unit — it meets C at an FFI seam or not at all. So this section is
weighted toward **ownership** (the idea the graded course, tasks 63–66, is really
about) and toward **the seam itself**, which is where a C programmer actually
arrives.

Added 2026-09-16 (M636c). It closes the gap the coverage review named: Rust was
the only language in this tree with a graded course and no literature at all.

### Freely readable — the official set

- **The Rust Programming Language** ("the book") — Klabnik, Nichols & Krycho with the Rust Community. <https://doc.rust-lang.org/book/> [read 2026-09-16] · in print: No Starch Press, 2nd edition, ISBN 978-1-7185-0310-6 [ISBN verified 2026-09-16]
  Read it for: **the starting point, and the one to start at.** Free, official, and tracking the compiler — the copy read on 2026-09-16 targets Rust 1.90 and the 2024 edition, and `rustup doc --book` puts it on your disk for a bench with no route out. *Prefer the online copy to the print one:* a No Starch **3rd edition** is announced for March 2026 on the publisher's page, but it is not yet in Open Library, and the publisher's page refuses automated requests — so **its ISBN is deliberately not printed here.** Every other ISBN on this page resolved to its exact edition in a catalogue; one that carries only a publisher's word would look identical and mean less, which is the whole reason the markers exist. Look it up at No Starch if you want the paper. The free copy has no such lag: the one read on 2026-09-16 tracks Rust 1.90.
- **Rust by Example** — the Rust project. <https://doc.rust-lang.org/rust-by-example/> [probed 2026-09-16: HTTP 200]
  Read it for: the same ground as the book, as runnable examples rather than prose. The right companion if you learn by changing something and seeing what the compiler says.
- **Rustlings** — the Rust project. <https://github.com/rust-lang/rustlings> [probed 2026-09-16: HTTP 200]
  Read it for: small broken programs you fix in order, compiler error by compiler error. **The direct analogue of Ziglings** in this bibliography's Zig section, and of this project's own graded tasks — the closest thing to jichi's assignments that another language ships.
- **Learn Rust With Entirely Too Many Linked Lists** — rust-unofficial. <https://rust-unofficial.github.io/too-many-lists/> [read 2026-09-16]
  Read it for: **the best single answer to "why won't Rust let me build the data structure I know how to build in C?"** It implements six linked lists — a bad stack, a good stack, a persistent stack, a safe deque, an unsafe queue, an unsafe deque — and uses each to teach ownership, `Box`/`Rc`/`Arc`, raw pointers, variance and Miri. Read it directly against Set D (memory & lifetimes) and the C systems course's growable array (task 52): the same structures, under a compiler that refuses the shapes C accepts silently.

### Freely readable — the seam with C

- **The Rustonomicon** — the **Rust project's** official guide to unsafe Rust. <https://doc.rust-lang.org/nomicon/> [read 2026-09-16]
  Read it for: what `unsafe` actually promises, and the FFI chapter — interoperating with other languages is explicitly in its scope. This is the document that matters for [RUST_INTEROP.md](RUST_INTEROP.md), because the boundary between a C library and a Rust caller *is* unsafe code with a safe abstraction wrapped round it. It opens by telling you to turn back; take that as a statement about prerequisites, not a joke.
- **The Rust FFI Omnibus** — Jake Goulding. <https://jakegoulding.com/rust-ffi-omnibus/> [read 2026-09-16]
  Read it for: worked, tested examples of calling Rust *from* other languages — integers, string arguments and returns, slices, tuples, objects. The concrete half of the Rustonomicon's FFI chapter, and the fastest way to see what actually crosses the boundary and what has to be marshalled.
- **rust-bindgen user guide** — the Rust project. <https://rust-lang.github.io/rust-bindgen/> [probed 2026-09-16: HTTP 200]
  Read it for: generating Rust bindings from a C header automatically — the tool that decides whether "call this C library from Rust" is an afternoon or a fortnight. Relevant to any reader who wants to point Rust at jichi's own `include/` (which carries `extern "C"` guards since M188, see [CPP_BUILD.md](CPP_BUILD.md)).

### In print

- **Programming Rust** — Blandy, Orendorff & Tindall, 2021. O'Reilly. ISBN 978-1-4920-5259-3 [ISBN verified 2026-09-16]
  Read it for: the systems-programmer's Rust book — written for people who already know what a pointer and a destructor are, which is the reader this tree produces. The closest Rust analogue to *The Practice of Programming* in tone.
- **Rust for Rustaceans** — Jon Gjengset, 2021. No Starch Press. ISBN 978-1-7185-0185-0 [ISBN verified 2026-09-16]
  Read it for: the rung *after* the book — variance, trait objects, unsafe abstractions, API design. Deep water, and the right place to go once tasks 63–66 stop being hard for the reasons they were meant to be hard.

---

## 6. Python

The language with the widest audience on this list and, for a self-learner, the
one with the most material of uneven quality. So this section is **almost
entirely primary**: the documentation is written by the people who wrote the
language, it is free, and it is better than most of what is sold beside it.

**The thing to know before you start**, and the tutorial says it about itself:

> This tutorial is designed for **programmers** that are new to the Python
> language, **not beginners** who are new to programming.

If you are new to programming, that sentence is doing you a favour. Read it and
choose accordingly.

### Primary documentation — free

- **The Python Tutorial** — <https://docs.python.org/3/tutorial/index.html> [read 2026-09-19]
  Read it for: the guided path, in the order its authors intended, and the only
  part of the documentation designed to be read front to back. It is also the
  corpus [`LANGUAGE_COURSE.md`](LANGUAGE_COURSE.md) indexes, because Python
  publishes a downloadable text build of it — 17 files, against 537 for the whole
  documentation set. **Read the version you are running**, not "3".
- **The Python Language Reference** — <https://docs.python.org/3/reference/index.html> [probed 2026-09-19: HTTP 200]
  Read it for: what the language *is*, when the tutorial's answer stops being
  precise enough — the data model, execution model, and the grammar. This is
  where you go when two explanations disagree.
- **The Python Standard Library** — <https://docs.python.org/3/library/index.html> [probed 2026-09-19: HTTP 200]
  Read it for: the batteries. Worth *browsing* once rather than searching
  forever: most "how do I do X in Python" questions are answered by a module the
  asker did not know existed.
- **Python HOWTOs** — <https://docs.python.org/3/howto/index.html> [probed 2026-09-19: HTTP 200]
  Read it for: the topics that need more than a reference entry and less than a
  book — logging, sorting, regular expressions, Unicode. Official, and
  consistently better than the blog post you would otherwise find.

### The conventions

- **PEP 8 — Style Guide for Python Code** — <https://peps.python.org/pep-0008/> [probed 2026-09-19: HTTP 200]
  Read it for: the conventions a Python reader expects, and the sentence people
  quote it for and then ignore — *"a foolish consistency is the hobgoblin of
  little minds"*. Style guides are **house rules**, and this page says so itself.
- **PEP 20 — The Zen of Python** — <https://peps.python.org/pep-0020/> [probed 2026-09-19: HTTP 200]
  Read it for: nineteen aphorisms that are quoted constantly and argued with
  rarely. Read it as a *statement of taste with a history*, not as a
  specification — "there should be one obvious way to do it" is a design goal
  the language itself does not always meet.
- **Python Developer's Guide** — <https://devguide.python.org/> [probed 2026-09-19: HTTP 200]
  Read it for: how the language is actually changed — the PEP process, the
  branches, the release cycle. The best available answer to *"why is it like
  that?"* is usually a PEP, and this is the door to them.

### One book

- **Fluent Python**, 2nd edition — Ramalho, 2021. O'Reilly Media.
  ISBN 978-1-492-05635-5 [ISBN verified 2026-09-19] — Open Library returned
  *Fluent Python*, O'Reilly Media, 2021.
  Read it for: the gap between *writing Python* and *writing Python the way the
  language wants* — the data model, protocols, and why `__len__` is not an
  implementation detail. Assumes you already know the syntax, which is exactly
  the reader the tutorial produces.

## 7. Racket

A small section, and a well-served one: Racket's documentation is unusually good
and unusually *complete*, and it routes readers deliberately. The Guide says so
about itself —

> It assumes programming experience, so if you are new to programming, consider
> instead reading **How to Design Programs**. If you want an especially quick
> introduction to Racket, start with **Quick: An Introduction to Racket with
> Pictures**.

— which is three audiences named on one page, and a model of the orientation
[`INTERFACE_TUTORIAL.md`](INTERFACE_TUTORIAL.md) argues for.

**One practical note this project measured.** Racket publishes no downloadable
text build of its documentation; the docs ship *with the distribution*
(`/usr/racket/doc` here, 4,089 files). That is why
[`LANGUAGE_COURSE.md`](LANGUAGE_COURSE.md) has a second recipe kind, and why
jichi now reduces HTML to prose before indexing it.

### Primary documentation — free

- **The Racket Guide** — <https://docs.racket-lang.org/guide/> [read 2026-09-19]
  Read it for: the guided path for someone who can already program. Chapter 2 is
  the essentials; from chapter 3 it becomes a tour of the toolbox and hands
  precise details to the Reference.
- **The Racket Reference** — <https://docs.racket-lang.org/reference/> [probed 2026-09-19: HTTP 200]
  Read it for: the precise details the Guide deliberately does not give. Large,
  and meant to be searched rather than read.
- **Quick: An Introduction to Racket with Pictures** — <https://docs.racket-lang.org/quick/> [probed 2026-09-19: HTTP 200]
  Read it for: the fastest honest look at what the language feels like, using
  pictures as the values so the results are visible rather than printed.
- **More: Systems Programming with Racket** — <https://docs.racket-lang.org/more/> [probed 2026-09-19: HTTP 200]
  Read it for: the counter-argument to "Racket is a teaching language" — it
  builds a web server, continuations and all, in one sitting.
- **The Racket Style Guide** — <https://docs.racket-lang.org/style/> [probed 2026-09-19: HTTP 200]
  Read it for: house conventions written by people who had to maintain a large
  Racket codebase, which is a different and more useful thing than taste.

### The design argument

- **The Racket Manifesto** — Felleisen, Findler, Flatt, Krishnamurthi, Barzilay,
  McCarthy, Tobin-Hochstadt. SNAPL 2015, Dagstuhl LIPIcs.
  <https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.SNAPL.2015.113> [probed 2026-09-19: HTTP 200]
  Read it for: **why** the language is shaped this way — language-oriented
  programming, and the claim that building a *language* for a problem should be
  as ordinary as building a library. Open access, peer-reviewed, and short.

### Books

- **How to Design Programs**, 2nd edition — Felleisen, Findler, Flatt,
  Krishnamurthi. <https://htdp.org/> [probed 2026-09-19: HTTP 200] · print: MIT
  Press, 2018, ISBN 978-0-262-53480-2 [ISBN verified 2026-09-19] — Open Library
  returned *How to Design Programs*, The MIT Press, 2018.
  Read it for: **the design recipe** — a repeatable procedure for getting from a
  problem statement to a program, taught before any language feature. It is the
  book the Racket Guide sends beginners to, it is free to read in full online,
  and its method transfers to every language in this bibliography.
- **Realm of Racket** — Bice, Foo, Felleisen et al., 2013. No Starch Press.
  ISBN 978-1-59327-491-7 [ISBN verified 2026-09-19] — Open Library returned
  *Realm of Racket*, No Starch Press, 2013.
  Read it for: learning the language by writing games, chapter by chapter. The
  lightest entry here and the one most likely to keep a discouraged learner
  going, which is a real property of a book.

## 8. Guile — Scheme, and the GNU extension language

The smallest literature in this bibliography, and the reason is worth stating
rather than apologising for: **Guile's own manual is the book**, and the rest of
what a Guile programmer reads is *Scheme* literature that predates Guile by
decades and outlives any one implementation. A section padded with Guile-branded
tutorials would be longer and worse.

The one thing to understand before starting is what Guile is *for*. It is an
extension language — the GNU project's answer to "this application needs a
scripting layer" — so its manual spends as much time on embedding it in a C
program as on the language itself. That is the half most relevant to a reader of
this tree.

### Primary documentation — free

- **The GNU Guile Reference Manual** — <https://www.gnu.org/software/guile/manual/> [probed 2026-09-20: HTTP 200]
  Read it for: the whole language and, unusually, the whole C API beside it.
  Part of it is a tutorial and part is a reference, and it says which is which.
- **Guile: Learn** — <https://www.gnu.org/software/guile/learn/> [probed 2026-09-20: HTTP 200]
  Read it for: the routing page — which manual, which tutorial, which SRFI, for
  which question. Short, and the right first click.
- **Guile documentation index** — <https://www.gnu.org/software/guile/docs/> [probed 2026-09-20: HTTP 200]
  Read it for: the versioned manuals, including the older ones a distribution
  may still be shipping.

### The standard the language tracks

- **R7RS-small** — <https://small.r7rs.org/> [probed 2026-09-20: HTTP 200]
  Read it for: what "Scheme" means when someone says it without qualification.
  It is **88 pages** for a whole language, which is itself the argument: the
  report is short because the language is, and reading it end to end is a
  realistic afternoon rather than a project.

### The Scheme books, which are the Guile books

- **Structure and Interpretation of Computer Programs**, 2nd edition — Abelson,
  Sussman, Sussman. MIT Press, ISBN 978-0-262-51087-5 [ISBN verified 2026-09-20]
  — Open Library returned *Structure and Interpretation of Computer Programs
  (SICP)*, Abelson and Sussman, MIT Press. Full text free:
  <https://mitp-content-server.mit.edu/books/content/sectbyfn/books_pres_0/6515/sicp.zip/index.html> [probed 2026-09-20: HTTP 200]
  Read it for: the book that argues programs are written to be **read**, and
  then demonstrates it by building an interpreter for its own language in its
  own language. Slow going and worth it; the metacircular evaluator in chapter 4
  is the single best answer to "what is a language, actually?".
- **The Scheme Programming Language**, 4th edition — R. Kent Dybvig. MIT Press,
  ISBN 978-0-262-51298-5 [ISBN verified 2026-09-20] — Open Library returned *The
  Scheme programming language*, R. Kent Dybvig, MIT Press. Full text free:
  <https://www.scheme.com/tspl4/> [probed 2026-09-20: HTTP 200]
  Read it for: the fastest route from "I can program" to "I can write Scheme" —
  it is a language book rather than a teaching book, so it assumes you and gets
  on with it.
- **The Little Schemer**, 4th edition — Friedman, Felleisen. MIT Press,
  ISBN 978-0-262-56099-3 [ISBN verified 2026-09-20] — Open Library returned *The
  Little Schemer*, Friedman and Felleisen, MIT Press.
  Read it for: recursion, taught entirely as a dialogue of questions. It is the
  one book here that will change how you *think* about a base case, and it can
  be finished on a train.

## 9. Elixir — processes as the unit of failure

Elixir's literature has a shape the others do not: the language is the smaller
half. What you are really learning is **OTP** — supervision trees, and a model
in which a process crashing is an ordinary event the system is designed around
rather than an emergency. A reading list that teaches the syntax and stops has
taught the easy part.

That matters to a reader of this tree for a concrete reason. jichi's own
subagent and parallel-tool machinery solves a related problem — a child that
fails must not take the parent with it — in C, with `fork`, exit codes and
explicit reaping. Elixir's answer is worth reading precisely because it is a
*different* answer to the same question, made a language feature rather than a
discipline.

### Primary documentation — free

- **Elixir: Getting Started** — <https://hexdocs.pm/elixir/introduction.html> [probed 2026-09-20: HTTP 200]
  Read it for: the official guided path, maintained with the language and
  versioned with it.
- **elixir-lang.org documentation index** — <https://elixir-lang.org/docs.html> [probed 2026-09-20: HTTP 200]
  Read it for: which docs exist for which version — including Erlang/OTP's,
  which you will need.
- **`GenServer` module docs** — <https://hexdocs.pm/elixir/GenServer.html> [probed 2026-09-20: HTTP 200]
  Read it for: the single abstraction most Elixir code is built out of, with its
  callbacks and its failure semantics stated exactly.
- **OTP Design Principles** — <https://www.erlang.org/doc/system/design_principles.html> [probed 2026-09-20: HTTP 200]
  Read it for: supervision trees from the source. This is Erlang documentation
  and it is the important reading; Elixir's own guides route you here.

### Free, and not official

- **Elixir School** — <https://elixirschool.com/en> [probed 2026-09-20: HTTP 200]
  Read it for: short lessons with exercises, translated into many languages. A
  second explanation when the official one has not landed.
- **The Elixir Style Guide** — <https://github.com/christopheradams/elixir_style_guide> [probed 2026-09-20: HTTP 200]
  Read it for: community conventions, and the arguments behind them.

### In print

- **Programming Elixir ≥ 1.6** — Dave Thomas. Pragmatic Bookshelf, 2018,
  ISBN 978-1-68050-299-2 [ISBN verified 2026-09-20] — Open Library returned
  *Programming Elixir ≥ 1.6*, Dave Thomas, Pragmatic Bookshelf, 2018.
  Read it for: the language taught by someone who is candid about which parts
  are elegant and which are merely conventional.
- **Elixir in Action**, 3rd edition — Saša Jurić. Manning, 2023,
  ISBN 978-1-63343-851-4 [ISBN verified 2026-09-20] — Open Library returned
  *Elixir in Action, Third Edition*, Sasa Juric, Manning, 2023.
  Read it for: **the OTP book**, and the one to reach for if you read only one.
  It treats concurrency and fault tolerance as the subject rather than as later
  chapters.
- **Designing Elixir Systems with OTP** — Gray, Tate. Pragmatic Bookshelf, 2019,
  ISBN 978-1-68050-661-7 [ISBN verified 2026-09-20] — Open Library returned
  *Designing Elixir Systems With OTP*, Gray and Tate, Pragmatic Bookshelf, 2019.
  Read it for: how to lay out an application so the OTP parts stay small and the
  functional core stays testable — the architectural question the other two
  answer only in passing.

## 10. Haskell — types as the design tool

The reason to read Haskell literature, whether or not you write Haskell: it is
where the argument that **types are a design medium rather than a safety net**
is made most completely. Every other language in this bibliography borrows from
that argument, and reading the original is cheaper than reconstructing it from
the borrowings.

**A warning about this section's links, measured 2026-09-20.** Two of the
best-known free Haskell books were published at domains that **no longer
resolve** — `learnyouahaskell.com` and `book.realworldhaskell.org` both fail DNS
resolution outright, not with a 404. Both texts survive at community-maintained
locations, cited below. This is the ordinary fate of a URL and the reason every
entry on this page carries a date.

### Primary documentation — free

- **haskell.org documentation** — <https://www.haskell.org/documentation/> [probed 2026-09-20: HTTP 200]
  Read it for: the routing page — tutorials, the report, the libraries, the
  tooling, sorted by what you are trying to do.
- **The Haskell 2010 Language Report** — <https://www.haskell.org/onlinereport/haskell2010/> [probed 2026-09-20: HTTP 200]
  Read it for: the language as specified, which is a different document from the
  language as implemented. Short by the standards of language reports.
- **GHC User's Guide** — <https://downloads.haskell.org/ghc/latest/docs/users_guide/> [probed 2026-09-20: HTTP 200]
  Read it for: the language as *implemented*, which in practice is the one you
  are writing — the extensions, the pragmas and the flags that real code uses.
- **GHCup** — <https://www.haskell.org/ghcup/> [probed 2026-09-20: HTTP 200]
  Read it for: how to get a toolchain without a fight. Listed because the
  toolchain is the first obstacle and pretending otherwise wastes an evening.

### Freely readable

- **Learn You a Haskell for Great Good!** — Miran Lipovača.
  <https://learnyouahaskell.github.io/> [probed 2026-09-20: HTTP 200] · print:
  No Starch Press, 2011, ISBN 978-1-59327-283-8 [ISBN verified 2026-09-20] —
  Open Library returned *Learn You a Haskell for Great Good!*, Miran Lipovača,
  No Starch Press, 2011.
  Read it for: the gentlest first pass, and the one that makes typeclasses feel
  ordinary. **The original domain `learnyouahaskell.com` no longer resolves**
  (checked 2026-09-20); the link above is the community-maintained edition.
- **Real World Haskell** — O'Sullivan, Goerzen, Stewart. Print: O'Reilly, 2008,
  ISBN 978-0-596-51498-3 [ISBN verified 2026-09-20] — Open Library returned
  *Real World Haskell*, O'Sullivan and Goerzen, O'Reilly, 2008. Updated free
  text: <https://github.com/tssm/up-to-date-real-world-haskell> [probed 2026-09-20: HTTP 200]
  Read it for: the book that answers "but how do you actually *build* something"
  — files, concurrency, parsing, profiling. It is from 2008 and says so; the
  linked project is a community effort to keep the code compiling.
  **`book.realworldhaskell.org` no longer resolves** (checked 2026-09-20).
- **Typeclassopedia** — Brent Yorgey. <https://wiki.haskell.org/Typeclassopedia> [probed 2026-09-20: HTTP 200]
  Read it for: the map of Functor → Applicative → Monad → Traversable and what
  each one is actually *for*. The single most useful free page in Haskell, and
  the answer to the tutorial problem that produced a decade of bad monad
  analogies.
- **CIS 194: Introduction to Haskell (Penn)** — <https://www.cis.upenn.edu/~cis1940/spring13/> [probed 2026-09-20: HTTP 200]
  Read it for: a real university course with homework, which is a different
  instrument from a book and better for some readers.

### In print

- **Programming in Haskell**, 2nd edition — Graham Hutton. Cambridge University
  Press, 2016, ISBN 978-1-316-62622-1 [ISBN verified 2026-09-20] — Open Library
  returned *Programming in Haskell*, Graham Hutton, Cambridge University Press,
  2016.
  Read it for: the tightest treatment of the *ideas*, by someone who teaches
  them for a living. Short chapters, exercises that matter, and the equational
  reasoning chapter that justifies the whole enterprise.

## 11. Clojure — a Lisp with an argument about state

Clojure earns a section here for one reason above the rest: it is a language
designed around an explicit, written-down **thesis about mutable state**, and
its author argued that thesis in public before the language had users. Reading
the rationale first and the syntax second is the right order, and it is unusual
to be able to.

The relevance to this tree is direct. jichi is C89 with arenas and manual
lifetimes — as far from persistent immutable data structures as a program gets —
and the value of reading Clojure's argument is not that it should have been
written that way. It is that the argument names *which* problems come from
shared mutable state, and those problems appear in this tree too, solved by
different means: the descriptor fence, the turn scratch arena, the rule that a
tool result is copied rather than aliased.

### The argument, first

- **Clojure rationale** — <https://clojure.org/about/rationale> [probed 2026-09-20: HTTP 200]
  Read it for: the design thesis in the author's own words — why identity and
  state are separated, and why that separation is the language rather than a
  library.
- **`clojure.spec` guide** — <https://clojure.org/guides/spec> [probed 2026-09-20: HTTP 200]
  Read it for: the other half of the argument — what you do about correctness in
  a language that declined static types, answered with runtime specifications
  that also generate tests.

### Primary documentation — free

- **Clojure reference documentation** — <https://clojure.org/reference/documentation> [probed 2026-09-20: HTTP 200]
  Read it for: the reference proper, organised by concept rather than by
  alphabet.
- **Learn Clojure: syntax** — <https://clojure.org/guides/learn/syntax> [probed 2026-09-20: HTTP 200]
  Read it for: the official guided introduction, starting from the reader.
- **ClojureDocs** — <https://clojuredocs.org/> [probed 2026-09-20: HTTP 200]
  Read it for: every core function with **community examples**, which is the
  thing the official docs deliberately do not carry and the thing you actually
  want at three in the afternoon.
- **clojure-doc.org** — <https://clojure-doc.org/> [probed 2026-09-20: HTTP 200]
  Read it for: community tutorials and per-topic guides, including the ecosystem
  questions (build tools, editors) the language docs leave alone.

### Freely readable

- **Clojure for the Brave and True** — Daniel Higginbotham.
  <https://www.braveclojure.com/> [probed 2026-09-20: HTTP 200] · print: No
  Starch Press, 2015, ISBN 978-1-59327-591-4 [ISBN verified 2026-09-20] — Open
  Library returned *Clojure for the Brave and true*, Daniel Higginbotham, No
  Starch Press, 2015.
  Read it for: the whole book free online, and the one that gets a beginner from
  nothing to a working program without pretending the JVM is not there.

### In print

- **Programming Clojure**, 3rd edition — Miller, Halloway, Bedra. Pragmatic
  Bookshelf, 2018, ISBN 978-1-68050-246-6 [ISBN verified 2026-09-20] — Open
  Library returned *Programming Clojure (The Pragmatic Programmers)*, Alex
  Miller and Stuart Halloway, Pragmatic Bookshelf, 2018.
  Read it for: the standard treatment, co-written by a core maintainer, and
  clear about where the language's idioms come from.
- **The Joy of Clojure**, 2nd edition — Fogus, Houser. Manning, 2014,
  ISBN 978-1-61729-141-8 [ISBN verified 2026-09-20] — Open Library returned *The
  Joy of Clojure*, Michael Fogus and Chris Houser, Manning, 2014.
  Read it for: the *why* behind the idioms, once the syntax is no longer in the
  way. It is the second Clojure book, deliberately, and it is the one that
  explains laziness and persistence properly.

## 12. Interfaces — CLI, terminal, desktop, web, and documents

**Why this section is here at all.** jichi *is* an interface — a CLI, a TUI, an
ACP server and a great deal of prose — and this project has spent milestones on
interface defects without ever reading the literature on them: a cap that fired
silently, a refusal that named no way forward, a `note:` line that vanished, and
a matrix whose most substantive rows rendered as plain text. Each of those is the
same failure in a different surface: **the system knew where the user was and did
not tell them.**

**The ordering principle, and it is not neutral.** Standards first, then living
guides, then books, then method — because the standards outlive the fashion.
Most writing about interfaces dates in months; `ECMA-48` does not, and neither
does POSIX chapter 12. Where a recommendation is current practice rather than a
settled question, this page says so instead of implying otherwise.

**The honest gap:** this section is weighted toward **CLI, terminal and
documents**, the three surfaces this project actually builds and can demonstrate
from its own tree. Desktop and web are represented by their primary guidelines
and nothing more. A thin section written to look complete is worse than a stated
absence — the same rule the six deferred languages get.

### Command line — the standards

- **POSIX.1-2024, XBD chapter 12: Utility Conventions** —
  <https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap12.html> [read 2026-09-19]
  Read it for: **the rules an argument parser is judged against**, in two parts —
  §12.1 the argument syntax (what `-c option_argument`, `[-f[option_argument]]`
  and `--` actually mean) and §12.2 the numbered Utility Syntax Guidelines. The
  part worth internalising is the one about *optional* option-arguments: a
  mandatory one may be a separate argument, an optional one **must** be adjacent,
  and a utility must not consume the next argument to fill it. That single rule
  is where most hand-written parsers quietly diverge from every standard utility
  on the system.
- **GNU Coding Standards — Standards for Command Line Interfaces** —
  <https://www.gnu.org/prep/standards/html_node/Command_002dLine-Interfaces.html> [probed 2026-09-19: HTTP 200]
  Read it for: the long-option convention POSIX does not define, and the
  `--help`/`--version` obligations. It is a *house* standard, widely adopted —
  read it as such rather than as a specification.

### Command line — the living guide

- **Command Line Interface Guidelines** — <https://clig.dev/> [read 2026-09-19]
  Read it for: **"human-first design"**, which is its own framing — an
  open-source guide that takes the traditional UNIX principles and updates them
  for programs whose users are people at a terminal rather than pipelines. Its
  structure (Philosophy → Human-first design → Guidelines) is the useful part: it
  argues the *why* before the rules, which is exactly the half a checklist omits.
  **Current practice, not a standard**, and it says so itself.

### Terminal and TUI

- **XTerm Control Sequences** —
  <https://invisible-island.net/xterm/ctlseqs/ctlseqs.html> [probed 2026-09-19: HTTP 200]
  Read it for: what a terminal actually does with the bytes you send it. The
  reference the rest of the ecosystem is measured against.
- **terminfo(5)** — <https://invisible-island.net/ncurses/man/terminfo.5.html> [probed 2026-09-19: HTTP 200]
  Read it for: why you ask the *database* what this terminal can do instead of
  hardcoding escapes — the difference between a TUI that works over ssh into a
  BSD and one that works on your laptop.
- **ECMA-48, Control Functions for Coded Character Sets** —
  <https://ecma-international.org/publications-and-standards/standards/ecma-48/> [probed 2026-09-19: HTTP 200]
  Read it for: the standard under the escape sequences, free and stable. Cite it
  when you need to say what is *specified* rather than what xterm happens to do.
- **NCURSES Programming HOWTO** — <https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/> [probed 2026-09-19: HTTP 200]
  Read it for: the curses model — windows, refresh, and why you do not print. It
  is old and that is mostly fine; the model has not moved.

### Desktop

**Read these as a set, and read two of them on the same question.** A destructive
confirmation, say: they will disagree. That disagreement is the lesson — the
conventions belong to the platform, and a cross-platform application that invents
a third answer is worse than one that concedes to each host.

- **GNOME Human Interface Guidelines** — <https://developer.gnome.org/hig/> [probed 2026-09-19: HTTP 200]
  Read it for: the freedesktop-side conventions, and a patterns section organised
  by the question you have rather than by widget name.
- **KDE Human Interface Guidelines** — <https://develop.kde.org/hig/> [probed 2026-09-19: HTTP 200]
  Read it for: the second opinion on the same desktop. Where it differs from
  GNOME is where "the platform convention" turns out to be two platforms.
- **Apple Human Interface Guidelines** — <https://developer.apple.com/design/human-interface-guidelines> [probed 2026-09-19: HTTP 200]
  Read it for: the most opinionated of the four, and the one whose vocabulary
  most of the field borrowed. Useful precisely because it refuses to be neutral.
- **Windows app design** — <https://learn.microsoft.com/en-us/windows/apps/design/> [probed 2026-09-19: HTTP 200]
  Read it for: the host most developers ship to and fewest read the guidance for.

### Web, and accessibility as its own subject

- **WCAG 2.2** — <https://www.w3.org/TR/WCAG22/> [probed 2026-09-19: HTTP 200]
  Read it for: **the part of interface quality that is measurable.** Contrast
  ratios, target sizes, focus visibility and motion are testable; most of the
  rest of this field is taste. Accessibility is listed here and not as a footnote
  because it is the strongest evidence a design decision can carry.
- **WAI-ARIA Authoring Practices Guide** — <https://www.w3.org/WAI/ARIA/apg/> [probed 2026-09-19: HTTP 200]
  Read it for: the expected keyboard behaviour of composite widgets. Also the
  best available answer to *"what should Tab do here?"* on any surface, including
  a TUI.
*This project's own measurement against that literature is
[`analysis/2026-08-22-screen-reader-audit.md`](analysis/2026-08-22-screen-reader-audit.md):
what happened when jichi's output was put through a screen reader — a redrawn
panel is not a line-oriented one, and the fix was a path, not a setting. It is a
finding rather than a work to read, so it is named here and not listed as an
entry.*

### Documents, which are interfaces

- **Butterick's Practical Typography** — <https://practicaltypography.com/> [probed 2026-09-19: HTTP 200]
  Read it for: the small number of decisions that carry most of a document's
  readability, argued rather than asserted. Free to read; the author asks for
  payment on the honour system.
- **The Visual Display of Quantitative Information**, 2nd edition — Tufte, 2001.
  Graphics Press. ISBN 978-1-930824-13-3 [ISBN verified 2026-09-19] — Open Library returned
  *The Visual Display of Quantitative Information, 2nd Ed.*, Graphics Press, 2001.
  Read it for: the standard against which a diagram earns its place. The rule
  this project takes from it is negative — a diagram that answers no question the
  prose cannot is a cost, and `docs/UML_TUTORIAL.md` applies that to mermaid.

### The craft, for interfaces

- **The Design of Everyday Things** — Norman, 2013. Basic Books.
  ISBN 978-0-465-05065-9 [ISBN verified 2026-09-19] — Open Library returned
  *The Design of Everyday Things*, Basic Books, 2013.
  Read it for: **affordances, signifiers and feedback** — the vocabulary for
  saying *why* a silent cap is a defect rather than merely annoying. It is about
  doors and stoves, and it transfers exactly.
- **Don't Make Me Think, Revisited** — Krug, 2014. Pearson Education.
  ISBN 978-0-321-96551-6 [ISBN verified 2026-09-19] — Open Library returned
  *Don't Make Me Think, Revisited: A Common Sense Approach to Web Usability*,
  Pearson Education, 2014.
  Read it for: the cheapest usability test that works, and the discipline of
  watching one person without coaching them. Its examples are web and its method
  is not.

### Method

- **Why You Only Need to Test with 5 Users** — Nielsen Norman Group —
  <https://www.nngroup.com/articles/why-you-only-need-to-test-with-5-users/> [probed 2026-09-19: HTTP 200]
  Read it for: the argument that small-n qualitative testing finds most problems.
  **Read it sceptically**, and read it beside this project's own rule that *n is
  small by construction, so a direction is the most it can show* — the two are
  compatible, and the failure mode they share is reporting a magnitude.

## 13. Ada and SPARK — proving what the rest of this list tests

The only section here for a language **no track in this tree uses**, and it earns
its place by being the counterargument to the rest of the shelf. Every rule this
project has about the limits of testing — *a floor of zero cannot validate*,
*perturb per check*, *audit the universe* — circles one question, and Ada answers
part of it with a type system while SPARK answers more of it with a prover.
[ADA_AND_C.md](ADA_AND_C.md) is the lens; this is the reading, including the
honest bound: a proof is only as wide as its model, and this project's two most
recent defects were a libc return convention and a `tar` behaviour, neither of
which any prover knows.

### The standard, and the prover's own documentation — free

- **Ada 2022 Reference Manual** — <http://www.ada-auth.org/standards/22rm/html/RM-TOC.html> [probed 2026-09-22: HTTP 200]
  Read it for: the language definition, free and complete, the way §2's C standards are. Ada's is not paywalled, which is worth noticing beside the ISO note below.
- **SPARK User's Guide** (27.0w) — <https://docs.adacore.com/spark2014-docs/html/ug/index.html> [read 2026-09-22]
  Read it for: "Formal Verification with GNATprove" and "Applying SPARK in Practice", which is the adoption story rather than the theory. **Then read appendix G, "GNATprove Limitations", and appendix H, "Portability Issues"** — a tool that ships a chapter on what it cannot do is exactly the instrument this project keeps asking for, and those two appendices are the honest boundary of everything the section above claims.
- **What is SPARK?** — <https://www.adacore.com/about-spark> [probed 2026-09-22: HTTP 200]
  Read it for: the adoption levels — Stone, Bronze, Silver, Gold, Platinum. **Silver** is absence of runtime errors, is where most real projects stop, and is the level that would matter to a program like this one.

### Learning — free

- **Introduction to Ada** — <https://learn.adacore.com/courses/intro-to-ada/index.html> [read 2026-09-22]
  Read it for: a full course in the browser. Two chapters matter most from here — **"Design by contracts"** (`Pre`, `Post`, type invariants, the thing `-Wswitch` and `arena_lint.sh` approximate) and **"Interfacing With C"**, which is where a C programmer can actually check the claims rather than take them.
- **Introduction to SPARK** — <https://learn.adacore.com/courses/intro-to-spark/index.html> [read 2026-09-22]
  Read it for: the subset and the prover, worked, with exercises that run in the page. The shortest route from "proof sounds impractical" to having discharged one.
- **Alire** (the Ada package manager and toolchain installer) — <https://alire.ada.dev/> [probed 2026-09-22: HTTP 200]
  Read it for: how to get GNAT and `gnatprove` onto a machine without a vendor installer — and, if you are weighing the rewrite [ADA_AND_C.md](ADA_AND_C.md) argues against, for how few of this project's 24 platform rows it covers.
- **Ada Programming** (Wikibook) — <https://en.wikibooks.org/wiki/Ada_Programming> [probed 2026-09-22: HTTP 200]
  Read it for: a reference-shaped alternative when the course format is not what you want.

### In print

- **Building High Integrity Applications with SPARK** — John W. McCormick and Peter C. Chapin. Cambridge University Press, 2015. ISBN 978-1-107-04073-1 [ISBN verified 2026-09-22] — Open Library returns that title, both authors, Cambridge University Press, 2015.
  Read it for: the one book-length treatment of *doing* SPARK rather than admiring it. The closest thing on this shelf to `TESTING_RUNBOOK.md` — a procedure, with the failures that shaped it.
- **Programming in Ada 2012** — John Barnes. Cambridge University Press. ISBN 978-1-009-18134-1 [ISBN verified 2026-09-22] — Open Library returns that title, John Barnes, Cambridge University Press.
  Read it for: the standard reference by the person who wrote much of the rationale. Long, and meant to be consulted rather than read through.
- **Concurrent and Real-Time Programming in Ada** — Alan Burns and Andy Wellings. Cambridge University Press, 2007. ISBN 978-0-521-86697-2 [ISBN verified 2026-09-22] — Open Library returns that title, Burns, Cambridge University Press, 2007.
  Read it for: tasking as a *language* feature rather than a library. Worth reading against [`proposals/2026-09-sustained-task.md`](proposals/2026-09-sustained-task.md), because the concurrency rung of that design is the one place this tree is arguing about exactly what Ada decided in 1983.

---

## How to read these with jichi at your side

A bibliography is not a pile of links if you can pull one into a turn. jichi's
[`@`-references](REFERENCES.md) do exactly that:

```
› explain @url:https://blog.regehr.org/archives/213 against @src/util/jc_mem.c
› compare @url:https://ziglang.org/learn/why_zig_rust_d_cpp/ with @docs/ZIG_REWRITE_ANALYSIS.md
› @ref:corec   # a named alias, defined once in config
```

Three cautions, all of them earned:

1. **Fetched pages are data, never instructions.** Anything `@url:` pulls in is
   external content and is fenced as such (M300, [HARDENING.md](HARDENING.md)
   §6b). A page that contains "ignore your instructions" is a page, not an
   instruction.
2. **The model has read these books too, badly.** A model will confidently
   paraphrase K&R and get the edition wrong — this page's own first draft
   carried a URL that had been dead for years. Ask it to quote, then check the
   quote. That is module M9's whole subject.
3. **Nothing here needs the network to be useful.** The books are books. If you
   are on a board over SSH with no route out, the four language standards and
   two of the free books are the ones to have downloaded first.

## What is deliberately absent

- ~~**The other four languages.**~~ **Closed at M677.** Guile (§8), Elixir (§9),
  Haskell (§10) and Clojure (§11) now have sections, and the list this entry
  existed for is empty: **every language with a track in this tree has
  literature behind it.** §13 (Ada and SPARK, added 2026-09-22) is the one
  section for a language with **no** track, and it is here as the argument
  against the rest of the shelf rather than as a twelfth family member — see
  [ADA_AND_C.md](ADA_AND_C.md) for why that is a lens and not a course. The entry is kept rather than deleted because the
  reasoning is the useful part — the list was worked through *when there was a
  reason*, never alphabetically. Rust came off at M636c because it was the only
  language with a graded course and nothing to read; Python and Racket at M671
  because the language course points at them for *"the important literature"*
  and leaving them empty would have made that page cite nothing; these four last,
  together, because by then the gap itself had become the anomaly.
  **What the four cost, said plainly:** Guile's section is the smallest here and
  deliberately so — its manual is the book, and the rest of what a Guile
  programmer reads is Scheme literature older than Guile. A section padded to
  match the others would have been longer and worse.
- **Anything about LLMs or agents.** It dates in months, this project's own
  [fukabori-11-ai-supported-coding-examined.md](reading/fukabori-11-ai-supported-coding-examined.md)
  covers the ground with measurements rather than citations, and a reading list
  is the wrong instrument for a field that moves faster than its bibliography.
- **Paywalled standards.** ISO's final C and C++ texts cost money and answer
  `403` to a probe; the last free working draft is cited instead, and that is
  what practitioners cite anyway.
- **Video.** Conference talks age well and index badly. If one belongs here it
  will arrive with a transcript.
