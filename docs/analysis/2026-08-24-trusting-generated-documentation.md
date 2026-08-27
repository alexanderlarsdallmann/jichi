# Can we trust documentation a model wrote?

*2026-08-24. jichi's `docs/` holds **1,075,353 words across 488 files**, essentially all written
by a model. This page asks the question the operator asked — and answers it with evidence from
this repository rather than with reassurance.*

---

## The question, and why it arrived when it did

I had just measured three Japanese models against deliberately planted errors and concluded they
could not be trusted to **review** Japanese. The operator's reply:

> *"The point in using LLMs for the Japanese documentation is to write it, then have it reviewed.
> And we have to use LLMs for documentation — we are generating the English texts as well. Can we
> trust the English language texts that models create? … did you ask me if I was an English
> native speaker? Did you flag the English documentation?"*

**Three separate points, all correct.**

1. **The Japanese use case is write-then-review**, and my conclusion answered only half of it.
   Model writes → native speaker reviews is a sound pipeline; what the planted errors falsified
   is *model writes → model reviews*. The finding narrows the second step, it does not condemn
   the first.
2. **The same reasoning applies to English**, which is also model-written.
3. **Nobody asked who reviews the English.** The operator is a German native speaker. There is no
   native English speaker in this project.

## The evidence, from this session alone

Not speculation. English prose written into this repository in a single working session that was
fluent, confident and **false**:

| claim, as written | reality |
|---|---|
| a driver header: checks 2–5 "are all clean on a pair of empty files" | measured — all four redden |
| a source comment: `name` "is now read rather than discarded" | the code below it said `(void)name;` |
| `test_width`: "22 ids … so 22 + 4\*11 = 66 measurements" | stale for two milestones |
| `group_sep_lint`: `main.c`'s sites "honour `--accessible` just as the TUI does" | half true; the separator did, the prose did not |
| `README.md`: "**561** milestones" | 11 behind; a lint caught it, nobody else had |
| an analysis heading: "only ELYZA works" | true of the *configuration*, phrased as a fact about the *models* |
| a smoke check: "different calls do NOT stop the run" | measured exactly what it claimed, and claimed the wrong thing |

**Seven, and several of them sat inside documents arguing for measurement over assumption.**

**How each was caught matters more than the count.** Not one was found by re-reading. Two were
found by a lint, three by a later measurement, two by the operator using the software. *Prose is
not self-checking, and the author is the worst-placed reader of it.*

## The failure mode is identical to the one I diagnosed in a model

ELYZA, asked about a correct string, produced:

> 「拒否する」という動詞は自動詞なので、「した」が付くことはない

*"拒否する is intransitive, so it cannot take した."* Fluent, unhedged, and false on both counts.
I wrote that up as the reason not to trust the model's reasoning — while the table above was
accumulating in the same session, in the same repository, under my own name.

**The distinction that actually holds between the English and the Japanese is not reliability —
it is DETECTABILITY.** The operator can read English and catch things; neither of us can read
Japanese. That is a difference in the *review* step, not in the *generation* step, and it is the
opposite of what my flagging implied.

## What actually verifies the English today

Seven smoke lints check documentation claims against reality rather than against style:

| lint | what it can falsify |
|---|---|
| `docs_flags` | every `--flag` the docs mention exists |
| `docs_counts_lint` | advertised numbers equal counted numbers |
| `reading_refs_lint` | reading-guide anchors still resolve to the source |
| `docs_locators_lint` | teaching docs name what a reader needs before they need it |
| `docs_index_lint` | every top-level page is indexed, and nothing else is |
| `changelog_coverage_lint` | the CHANGELOG does not fall behind the ROADMAP |
| `self_learner_lint` | the pages and sections a self-learner needs exist |

**What they have in common is that they check *mechanical* properties** — existence, equality,
resolution. None of them can check whether a sentence describing a design decision is *true*.

Five of the seven failures in the table above are of exactly that unverifiable kind.

## The measurement this page commits to

**Plant known-false claims in the documentation and count how many of the seven lints fire.**
The same method that made the Japanese measurement honest, pointed at the English — because
"the docs are checked" is precisely the sort of fluent, settled-sounding claim this page exists
to distrust.

**A prediction, recorded before measuring so that it can be wrong:** most prose claims are
unverifiable by any current lint, and the ones that *are* checkable — numbers, flags, anchors —
are the ones least likely to be subtly wrong in the first place. If that holds, the honest
conclusion is that jichi's documentation is **spell-checked, not fact-checked**, and the
caveat currently applied only to Japanese belongs on all of it.

## The measurement, run

Nine false claims planted one at a time into the real documentation, each followed by all seven
lints, each reverted before the next. **The prediction held.**

| planted claim | kind | caught | by |
|---|---|---|---|
| `**999 of them**` milestones in the README | mechanical | ✅ | `docs_counts_lint` |
| a `--speak-slowly` option that does not exist | mechanical | ✅ | `docs_flags` |
| a function name in an anchor that does not resolve | mechanical | ✅ | `reading_refs_lint` |
| a top-level page listed in no index | mechanical | ✅ | `docs_index_lint` |
| *"any other key refuses the call immediately"* | **prose** | ❌ | — |
| *"the count persists for the whole session"* | **prose** | ❌ | — |
| *"the digits were chosen to satisfy DIN 5009"* | **prose** | ❌ | — |
| *"**Five** refusals in a row end the turn"* | **prose** | ❌ | — |
| a false description on a real, existing anchor | **prose** | ❌ | — |

**Four of nine. All four mechanical. None of the five prose claims.**

The sharpest is the last planted in plain prose: *"A denied tool call aborts the whole turn
immediately, and the model is not told why. This is deliberate: telling it would let it retry."*
Two sentences, both false since M570 and M573, contradicting behaviour documented three pages
away — and every lint passes.

**Note which claims those are.** The five survivors are about **behaviour, lifetime, rationale
and thresholds** — precisely what a reader relies on, and precisely the kinds that were wrong
seven times in the table above.

## Two false attributions inside the measurement itself

Recorded because they are the same failure the measurement is about.

**A test that was not an instance of what it claimed to test.** The first anchor case was planted
inside an HTML comment without backticks; `reading_refs_lint` reads inline-backticked
`` `path.c:function` `` tokens, so nothing was ever offered to it. The result would have been
recorded as *"the lint missed a broken anchor"* — a false claim about the lint. Re-planted in the
right syntax, it catches it immediately.

**A pass attributed to the wrong cause.** Adding a false *description* beside a real anchor did
turn `reading_refs_lint` red — but its message reads `anchor count(s) off … claims 9 anchors, 10
present`. It fired on the **count**, not the claim. Recorded uncritically that becomes *"the lint
verifies descriptions"*, which it cannot do.

*A lint's red tells you a lint fired. It does not tell you why, and the two are different
findings.* Both directions of that error, in one nine-case measurement, in the same lint.

## What follows: cite the test, or do not claim it

The mechanical lints work and should be extended wherever a claim can be made mechanical. But
five of the seven real defects in this session were **behavioural prose**, which no lint here can
reach — so the honest mitigation is a convention rather than a tool:

> **A documented claim about behaviour names the driver and check that pins it.**

`docs/ACCESSIBILITY.md` now reads *"three refusals in a row end the turn
(`tests/smoke/deny_stops.sh` check 3)"*. That does not make the sentence machine-verifiable, but
it does three useful things: a reader can check it in one step, a change that breaks the
behaviour breaks a **named** test, and **a claim with no test to cite becomes visibly a claim
with no test** — which the current prose hides.

**And the citation itself is mechanical**, which is the part a lint can hold.
`tests/smoke/doc_claims_lint.sh` (M576) verifies that every cited driver exists and every cited
check number is inside that driver's plan. It found a stale citation **before it was finished**:
`docs/analysis/2026-08-23-chrome-width.md` pointed at check 4 of a driver named
chrome_width_lint, which M557 deleted twenty milestones earlier. (Written here without backticks
on purpose — the lint reads that syntax as a live citation, and caught this paragraph naming a
dead driver on its first run.) *A citation rots exactly like the code it describes.*

**The universe is the citations that exist, not a mandate to write them.** `docs/` holds 242
mentions of "check N" and only 46 name a driver alongside; the rest are prose references inside a
milestone entry that named its driver paragraphs earlier. Demanding a citation everywhere would
fail ~198 legitimate sentences.

## What follows either way

- **The Japanese pipeline is unchanged and sound.** Model writes, native speaker reviews. The
  measurement narrowed the reviewer, not the writer.
- **The English pipeline has never been named**, and naming it is most of the work: one
  non-native reader plus seven mechanical lints, reviewing a million words.
- **A caveat belongs where the risk is, not where the author's blind spot is.** "Needs native
  speaker review" was applied to the language nobody here can audit and withheld from the one
  the author writes in — which is backwards, because the author's own language is where the
  author's errors survive review.
