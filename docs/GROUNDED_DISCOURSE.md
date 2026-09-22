# Discussing with a model — five moves, and the record that makes them checkable

*The instrument for the conversation itself. It is the third of three:
[DOC_REVIEW.md](DOC_REVIEW.md) catches prose that is coherent and untrue of the
program, [CODE_REVIEW.md](CODE_REVIEW.md) catches a **reading** that is coherent
and untrue of the program, and this page catches a **discussion** that is
coherent and resolves to nothing. Its graded floor is
[`assignments/85-grounded-discourse.md`](assignments/85-grounded-discourse.md).*

## 1. The problem is not that the model is wrong

A model that were simply wrong would be easy. It would contradict the file in
front of you and you would notice.

What it does instead is produce an answer with the **shape** of a grounded one:
a claim, a confident reason, a plausible file name, a function that sounds like
it belongs to that file. Every part is the right kind of thing. The only defect
is that nothing in it **resolves** — the path is off by a directory, the symbol
was renamed two milestones ago, the quoted line is a paraphrase, the paper says
the opposite in its discussion section.

This is the same failure the other two instruments exist for, moved into the
conversation. And it is worse there, because a document gets reviewed and a
conversation does not: you read it once, at the speed of talking, and what you
carry away is the *impression* that the claim was supported.

**So the discipline is not "check what the model says".** You cannot check
everything, and trying is slower than doing the work yourself. The discipline is
to make the conversation **produce a record whose evidence a script can follow**
— and then to look only at what fails to resolve. That is the same move this
project makes everywhere else: take the thing down a tier, from a judgement you
have to remember to make into an artifact something else can check
([`plans/2026-09-argumentation-program.md`](plans/2026-09-argumentation-program.md)).

## 2. The rubric — five moves, in this order

| | The move | What it means | The failure it catches |
|---|---|---|---|
| **1. Claim** | State it so it could be false | One sentence, specific enough that you could say what would refute it | "The config system is well designed." Nothing follows from it and nothing contradicts it |
| **2. Grounds that resolve** | Cite where you can *go and look* | `file.c:symbol`, `doc.md` §section, a URL with the date you read it, a command with its output | A citation that is the right *shape* and points at nothing — the commonest thing a model produces |
| **3. Warrant** | Say why those grounds support *that* claim | The rule connecting the two, written out | Grounds that resolve and do not bear on the claim. A real `grep` proving something adjacent to what you said |
| **4. Counter-argument** | State the strongest objection **before** you answer it | The best case against, in its own words, then the reply | A rebuttal aimed at the weakest version. You cannot straw-man an objection you had to write down first |
| **5. Revision** | Say what changed | The claim after the objection — narrowed, qualified, or withdrawn | A "discussion" whose conclusion is what you already believed. If nothing moved, either the objection was weak or you did not mean it |

**"Resolves" is the load-bearing word, and it is stricter than "exists".** A
citation resolves when you can follow it and it says what you said it says. A
file that exists but no longer contains the symbol does not resolve. A URL that
returns 200 with different content does not resolve. `reading_refs_lint.sh`
enforces exactly this for the reading guides, and it is the reason those guides
can be trusted at all.

**The order is the order to write in.** Grounds second, because a claim you
cannot source should be withdrawn before you argue for it. Counter-argument
fourth, because the objection is against the *warranted* claim, not the bare
one. Revision last, because it is the only move that records that the
conversation changed something.

## 3. How to run one

1. **Write the claim down before you ask.** Not to be stubborn — to have
   something to compare the answer against. A claim formed *while* reading a
   fluent answer is the answer, rewritten.
2. **Ask for the grounds separately from the claim.** "Where in the tree does
   that happen?" is a different question from "does that happen?", and a model
   will answer the second confidently having no answer to the first.
3. **Resolve every citation yourself, once.** Open the file at the symbol. Run
   the command. Fetch the URL and note the date. This is the step that cannot be
   delegated to the thing being checked — and in practice it is where the
   conversation either becomes worth having or collapses.
4. **Write the objection you least want to be true.** If you cannot find one,
   that is evidence about the claim's specificity, not its strength.
5. **Write the revision even when it is "unchanged, and here is why the
   objection does not land."** An unchanged claim with a named surviving
   objection is a result. An unchanged claim with no objection is a mood.

## 4. What the grader checks, and what it cannot

[`85-grounded-discourse`](assignments/85-grounded-discourse.md) checks the
**floor**: that all five moves are present, that the counter-argument section
comes *before* the reply, that the grounds are well-formed citations, that
enough of them resolve against the real tree, and that the revision is not a
copy of the original claim.

It cannot check whether the warrant is sound, whether the objection is the
strongest one, or whether the revision is honest. Those are judgement, and they
stay yours — the same split DOC_REVIEW and CODE_REVIEW make. What a script can
do is refuse the two failures that need no judgement to spot: **a citation that
does not resolve, and a discussion that concluded nothing.**

Nor does the floor make a grounded discussion *good*. It makes an ungrounded one
visible, which is a smaller claim and the only one the structure supports.

## 5. Where this applies, beyond source

The moves do not care what the subject is; only the form a resolvable citation
takes changes.

| Subject | What a citation resolves to |
|---|---|
| **Source** | `file.c:symbol` — open it and the symbol is there. The five readings of [CODE_REVIEW.md](CODE_REVIEW.md) are five kinds of grounds |
| **A paper or a text** | Section and page, plus the sentence quoted verbatim. A paraphrase is not grounds; it is a second claim |
| **The documentation** | `page.md` §heading, and the heading exists. [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) carries a dated `[read]`/`[probed]` marker on every entry for this reason |
| **Language learning** | The rule's statement in the reference grammar, and an attested example — not a sentence the model generated to fit the rule it just stated |
| **Your own writing** | The line in your draft. "This paragraph is unclear" resolves; "the tone is off" does not |
| **Feedback to another writer** | The sentence you are talking about, quoted. An unquoted objection cannot be answered, only felt |

## 6. When to reach for it

Not for every exchange. Asking a model to rename a variable does not need a
warrant. Reach for it when **you would repeat the conclusion to someone else**,
or act on it in a way that is hard to undo — a design you are about to build, a
reading of a subsystem you are about to change, a claim about a text you are
about to cite, a piece of feedback someone will rewrite their draft on.

The names for all of this — grounds, warrant, qualifier, rebuttal, defeater —
are in [ARGUMENT.md](ARGUMENT.md), deliberately last. The moves work without
them; the names are for arguing about the moves with someone who learned them
elsewhere.

---

*Companion pages: [DOC_REVIEW.md](DOC_REVIEW.md) ·
[CODE_REVIEW.md](CODE_REVIEW.md) · [ARGUMENT.md](ARGUMENT.md) ·
[READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md) · [CURRICULUM.md](CURRICULUM.md)*
