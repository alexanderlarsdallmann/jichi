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
marked and come first within each group**: of the **78 entries** below (33 craft,
17 C, 11 C++, 8 Zig, 9 Rust), **45 can be read for nothing** — including a complete C
book, a complete Zig book, SICP, and every language standard that matters here in
its last free working draft. A bibliography a learner cannot afford is a reading
list for somebody else.

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
- **Not complete.** Five areas: the craft, C, C++, Zig, Rust. The six other
  languages with tracks in this tree — Racket, Guile, Elixir, Haskell, Clojure,
  Python — have none of their literature here yet, and saying so is cheaper than
  a thin section per language (DEFERRED).

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

## 1. The craft

Software development as a discipline — the part of the subject that outlives
every language in this file. [CURRICULUM.md](CURRICULUM.md) and
[APPROACH.md](APPROACH.md) teach this practically; these teach it historically,
which is the half that tells you which of today's certainties are fashions.

### Freely readable

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

- **The other six languages.** Racket, Guile, Elixir, Haskell, Clojure and Python
  all have tracks in this tree and no literature here. Deferred on purpose: a
  section per language written thin is worse than an honest gap. (Rust was on
  this list until M636c and is now §5 — it came off because it was the only
  language with a *graded course* and nothing to read, not because the list was
  being worked through.)
- **Anything about LLMs or agents.** It dates in months, this project's own
  [fukabori-11-ai-supported-coding-examined.md](reading/fukabori-11-ai-supported-coding-examined.md)
  covers the ground with measurements rather than citations, and a reading list
  is the wrong instrument for a field that moves faster than its bibliography.
- **Paywalled standards.** ISO's final C and C++ texts cost money and answer
  `403` to a probe; the last free working draft is cited instead, and that is
  what practitioners cite anyway.
- **Video.** Conference talks age well and index badly. If one belongs here it
  will arrive with a transcript.
