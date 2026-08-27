# Accessibility as the default, and in more than one language (design)

*Written 2026-08-23, after three sittings of the manual screen-reader protocol and four
defects found by ear. **Design only — nothing in `src/` changes on this page.** The
operator's thesis is stated first, then tested rather than assumed; the questions at the
end are real questions, not rhetorical ones.*

## 0. The proposal, in the operator's words

> *"jichi has to be accessible by design, so that accessibility becomes the default.
> Every phrase that is easily readable, understandable, parsable by a screenreader is so
> for any other seeing reader, user, or agent. Natural language text is how we
> communicate, and a strength of language models, too. Let's make use of that. And I want
> to optimize the interface for languages such as Japanese, and German, too."*

This is the **curb-cut argument**: the kerb ramp cut for wheelchairs turned out to serve
prams, trolleys, cyclists and anyone with a suitcase. It is a strong argument and it is
mostly right here. **It is not uniformly right, and a design that pretends otherwise will
be wrong in a way that is hard to walk back.** §1 says where it holds, §2 says where it
reverses, and the rest is built on that split.

## 1. Where the thesis holds — and it holds for most of the surface

**1.1 Wording.** `[tokens in=4,946 out=37]` requires learning a convention.
`4,946 input tokens used, and 37 output tokens used.` requires knowing English. The
second is better for a listener, for a newcomer, for anyone reading a pasted transcript,
and for a reader whose terminal ate the colour. No sighted user has ever benefited from
having to learn what `in=` meant.

**1.2 Error and permission text.** A sentence that names what happened and what to do
next is better for everyone. The whole M551/M552 sequence is this: `[y]es  [n]o` was a
*visual* affordance that failed aloud, and its replacement — *"Press y as in yes"* — is no
worse on screen.

**1.3 Role marking.** `Model qwen3-coder-next responds with the following:` beats a
bold-cyan header for: a listener; a colour-blind reader; anyone with `NO_COLOR` set; a
transcript pasted into an issue; and — the operator's point, and a good one — **a language
model reading the transcript.** jichi has `--output json` for machines, but the *text*
transcript is what a supervising agent, a `spawn_subagent` parent, or a human bug report
actually carries. Prose survives copy-paste; ANSI styling does not.

**1.4 Agents specifically.** This deserves its own line because it is the argument that
makes the change cheap to justify. jichi is an agent that also *drives* agents. Chrome
that a model must reverse-engineer is chrome the model can misread — and a model
misreading `x error denied` as content rather than as status is a real failure mode we
have already seen the shape of (`docs/analysis/` has the tool-result-as-content
confusions). Prose chrome is self-describing.

**1.5 Braille, which nobody has mentioned yet.** A refreshable braille display renders the
terminal line by line, and `[chat·qwen3·2%] ›` costs six cells of punctuation on a 40-cell
display. Braille users are a real constituency for a *terminal* program — `brltty` is a
console driver, not a web technology — and they are served by the same fix. **We have
tested no braille display and must not claim we have.**

## 2. Where the thesis reverses — the honest half

**2.1 Density under scanning.** A fixed-format, fixed-position `[tokens in=… out=…]` is
*scannable*: a sighted user hunting for the call where the input jumped runs their eye
down a column. Twenty sentences of varying length defeat that. **Comprehension and
scanning are different tasks and they want opposite things.**

**2.2 Alignment IS information.** A diff's `+`/`-` column, a table's columns, a tree's
indentation, `cat -n` line numbers: the *position* carries meaning for a sighted reader.
Linearising them helps a listener and destroys something real. This is a genuine
conflict, not a curb cut, and it is exactly where the operator's own constraint bites:

> *"Within program code all symbols are important, and must be read."*

**Design consequence: the content/chrome split from M553 is not a temporary convenience,
it is the permanent boundary.** Chrome may become prose. Content may not.

**2.3 Terminal width, and this is the one that will bite German.** Prose is longer.
`[tokens in=4,946 out=37]` is 25 columns; the sentence is 50. On an 80-column terminal
that is fine; in German it is ~65 (see §4.1) and a wrapped chrome line is *worse for both
audiences* — some readers re-announce a wrapped line, and visual alignment breaks. **Width
is a shared cost, and it is the strongest argument against prose everywhere.**

**2.4 Volume.** Two hundred chrome lines a session cost a sighted reader scrollback and a
listener minutes. The curb cut is real for *comprehensibility* and **reverses for
duration**. A sighted reader can skip; a listener cannot. So prose must not become an
excuse for saying more.

**2.5 Grep stability.** `grep 'tokens in=' session.log` is a stable anchor that people and
scripts rely on. M553 changed four test assertions for exactly this reason. Prose varies
under revision — which is why the tests now pin *relations* (`" a as in a"`) rather than
sentences, and why anything machine-readable belongs in `--output json`, not in prose.

### The synthesis

> **Prose where a human must understand. Compression where a human must scan. Never
> linearise data whose layout is its meaning. And prose is not a licence to say more.**

## 3. Making it the default — decompose the flag, do not flip it

`--accessible` is not one behaviour. It is at least seven, bundled by history, and they
have **different costs to a sighted user**. Flipping the bundle would ship regressions
inside an accessibility improvement, which is the worst possible way to lose the argument.

| # | behaviour | milestone | cost to a sighted user if default | verdict |
|---|---|---|---|---|
| a | static `working…` instead of an animated spinner | M118 | **real** — motion is honest progress feedback during a long call | **keep opt-in.** The static form must stay *available*, not become default |
| ~~b~~ | ~~incremental input echo~~ | M362 | none | **DONE at M558.** The flag gated one line; the correctness predicates already refused every unsafe case. Default arm **37 → 4** erase-belows, traffic halved, model request byte-identical, flag *deleted* rather than defaulted. The flip also exposed a bug in `e2e/redraw.py`'s VT emulator that the redundant repaint had been masking for ~200 milestones — see ANECDOTES #69 before touching this |
| ~~c~~ | ~~linear role labels~~ | M184 | none | **DONE at M560, narrower than written.** Measuring which lines lose a marker without colour: only the **header** — the tool lines carry glyphs that survive. And the fix is the *word* (`assistant:`), not the accessible prose form, because prose is for understanding and compression for scanning, and there is no `NO_COLOR` sighted user to ask |
| d | line-buffered model text | M549 | perceptible smoothness; a line is tens of ms | **measure, then decide.** §6 |
| e | bracket-free key lists | M551/M552 | none on screen | **flip to default.** `Press y as in yes` is no worse sighted |
| ~~f~~ | ~~prose chrome~~ | M553 | density, width, volume | **DECIDED NO at M561.** §1 overstated the case: information carried by *colour* vanishes (so M560 was a defect fix), information carried by *punctuation* does not. Costs measured — 50 vs 25 columns, German +22, five of six lines fire per event. `DECISIONS.md` carries the row |
| g | no ghost-suggestion overlay | pending | dim overlay is a genuinely nice sighted feature | **keep opt-in**; the fix is to *also* emit it as text |

**So the plan is not "flip `--accessible`". It is: retire the bundle.** Each property gets
its own default, chosen on its own evidence, and `--accessible` becomes a *preset* that
sets the remaining opt-ins — which is what a flag should have been all along.

**What this buys, concretely:** a user who has never heard of the flag gets (b), (c) when
colour is off, (e), and the low-width parts of (f). Nobody has to know an accessibility
option exists in order to receive its benefit — which is the operator's actual
requirement, and it is stronger than making one flag default.

**What it costs, stated plainly:** every default change is a compatibility event. Anything
grepping `[tokens in=` breaks, four of our own docs quote that string, and screenshots go
stale. `EMBEDDING.md`'s stability tiers govern the machine surfaces (`--output json`, ACP,
the JSONL) and **those do not change at all** — this is human-facing text only. That
distinction is the entire safety argument and must be stated in the changelog entry, not
inferred.

## 4. German

*The operator is a native speaker, so this section is written to be argued with.*

**4.1 Expansion. MEASURED at M554 — the figure below was folklore and it was low.**
The real worst-case expansion on this catalog is **+50%** (`ALLOW_PROMPT`, 44 → 66
columns), not 20–35%; see
[`analysis/2026-08-23-chrome-width.md`](../analysis/2026-08-23-chrome-width.md). German
runs ~20–35% longer than English for UI strings *on average*, which is the number
localisation guidance quotes and the wrong one to budget against. Our own catalog shows
the average too: `allowed (always this session)` is 30 characters,
`erlaubt (immer in dieser Sitzung)` is 33; `queued for the next step` is 24,
`für den nächsten Schritt eingereiht` is 35 (+46%). **Every prose chrome line must be
budgeted at English + 40% and checked against 80 columns**, because §2.3 says a wrapped
chrome line is worse for both audiences. This is a lint, not a hope: measure the longest
rendered chrome line per language and fail the build over a threshold.

**4.2 Compounds and line breaking.** German compounds are single long tokens
(`Berechtigungsanfrage`, `Kontextfenstergröße`). They cannot be broken at a space because
there is none, and Unicode UAX #14 line-breaking is the right reference for where they
*may* break. jichi's line breaking today is space-based. **This is a real gap** and it
affects the sighted German reader as much as the listener.

**4.3 The key-mnemonic problem, and I think German has a clean answer.** M552 established
that `a as in always` works because the keys are the English words' initials — and that
`a wie immer` would be **false**, since the German option words are
ja/nein/immer/bearbeiten/ansehen. The keys are deliberately never localised
(`include/jc_msg.h`), so this is structural, not an oversight.

**ANSWERED 2026-08-23: DIN 5009**, and the answer produces a finding neither half gives
alone — see the box below. German has a **published spelling alphabet** for exactly this
problem. It identifies the *letter* rather than the action, which is precisely the missing
piece:

```
Erlauben? Drücken Sie y für ja, n für nein, a (wie Aachen) für immer,
          e (wie Essen) für bearbeiten, v für ansehen.
```

Only `a` and `e` need the cue — the same two letters that failed in English, and for the
same reason: German letter names `a` /aː/ and `e` /eː/ are both long vowels, while
`y` (Ypsilon), `n` (En) and `v` (Vau) are distinct.

**Which revision, and an assumption stated so it can be corrected.** DIN 5009 was revised
in **2022** to use **city names** (Aachen, Berlin, Chemnitz…), replacing the older
personal-name list — a revision made specifically to remove substitutions introduced in
1934. The operator answered *"Go with DIN 5009"* without naming a revision, so **this
design assumes the 2022 city list**: it is the current standard and the change was
deliberate. It is an editorial decision about the words this program says, so it is flagged
rather than buried — correct it before A4 writes it into the catalog.

> **FINDING — the German accessible prompt cannot fit one line, and the English one can.**
> Formal register plus the DIN cue plus five options measures **~108 columns** against the
> **78-column budget** measured at [M554](../analysis/2026-08-23-chrome-width.md). German
> therefore needs a **two-line** prompt:
>
> ```
> Erlauben? Drücken Sie y für ja, n für nein, a wie Aachen für immer.
> e wie Essen für bearbeiten, v für ansehen.
> ```
>
> Neither the operator's answer nor the width measurement says this alone; it falls out of
> combining them. That is the argument for writing measurements down — this one was taken
> for a different purpose, two milestones earlier.

**4.4 Numbers. ANSWERED, and it was a defect — fixed as M555.** The operator's reader
speaks `4.946` **as digits, reading the `.` aloud**. A punctuation mark inside a numeral
is verbalised by the reader, so a listener gets four digits and the name of a punctuation
mark. **Mechanism corrected at M559:** the layer is Orca's punctuation verbalisation
(`verbalizePunctuationStyle` at SOME), not the synthesizer — `espeak-ng -v de -q -x 4.946`
reads it correctly as a number.

**It was never only a German problem**, which is what made it urgent rather than
deferrable: `jc_config.c` sets `.` as the fallback separator **when the locale supplies
none** — exactly what `LC_ALL=C` gives — so an English user with no locale configured heard
the same thing. The comma form is the same shape.

Fixed by `jc_group_sep_audience` (`jc_str.h`): the configured separator normally, **none**
in accessible mode, since `jc_group_num` already reads `sep == 0` as "do not group". The
sighted rendering keeps its grouping, because a bare six-digit integer is harder to scan —
opposite needs, one decision point.

**Eight call sites, and I enumerated five.** Six in the TUI (the sixth is `/promptcache`'s
hit-rate line) and two in `src/main.c`'s headless path, which honours `--accessible` too.
The three I missed were found by grepping for what was **left over** after the first five
were changed — a list written from reading was short by 37%.
`tests/smoke/group_sep_lint.sh` now fails the build on a raw use.

**4.5 Register. ANSWERED: formal.** *"Let's be formal rather than informal"* — so **`Sie`**,
matching the Spanish catalog's *"Pulse"*. The register was undecided by accident (German
has no verb today); A4 makes it deliberate. The cost is not free: the formal imperative is
~12 columns, and §4.3's finding already needs two lines.

**4.6 Verb-final clauses, and why truncation policy is language-dependent.** German
subordinate clauses put the verb last. So a truncated German line loses the *verb* where a
truncated English line loses the object — i.e. German degrades worse under truncation.
Anywhere jichi elides (the 360-byte tool-result bound, the arg summary's 200 bytes,
`jc_utf8_trunc_len`) this matters, and "truncate at N bytes" is not a language-neutral
policy.

## 5. Japanese

*I have no native Japanese speaker in the loop and **cannot validate any of this from
here.** Everything below is a hypothesis with a question attached. The current `ja`
catalog was written by me and has never been reviewed by a native speaker or heard through
a Japanese TTS — that is the most important sentence in this section.*

**5.1 No spaces, so segmentation is the reader's problem.** Japanese has no word
delimiters; a screen reader or TTS segments with a dictionary. Latin letters embedded in
Japanese text are a known hazard: `許可しますか? y はい` versus `許可しますか?yはい` may
segment differently. M551 space-delimited the keys deliberately for this reason, on
reasoning rather than on evidence. **Q4.**

**5.2 How is a Latin key spoken in Japanese?** Most Japanese TTS reads `y` as ワイ
(*wai*), `n` as エン (*en*). That is fine *if* the listener maps ワイ back to the key they
must press — but it means the English "as in" trick has no analogue, and the
katakana letter-name may itself be the disambiguator. **Q5.**

**5.3 Kanji readings. MEASURED 2026-08-23, and it is not the trade-off this section
assumed.** The worry was *ambiguity* — 行 can be いく / おこなう / ぎょう / こう, and a wrong
reading yields a different word. The measurement found something blunter:
**`espeak-ng`, which is what Orca speaks through, cannot read kanji at all.** It emits
`tS'aIni:z l'et@` — *"Chinese letter"* — per ideograph, and for 常に it **drops** the kanji
and says only *"ni"*. Every kana form tested was correct.

jichi's `ja` catalog holds **46 kanji characters**, including 表示 in the approval prompt —
the option that shows a diff before an edit is authorised.

So kana-versus-kanji is **not** correctness against polish: under this engine kanji is not
mispronounced, it is *not pronounced*. Three responses, not exclusive: kana where a word
must be **heard** to be acted on; document that Japanese users need **Open JTalk** (the free
synthesizer `speech-dispatcher` can front, not installed here); or furigana-style
duplication, which doubles the width against §4.1's measured budget. Recommendation and the
native-speaker question:
[`analysis/2026-08-23-tts-japanese.md`](../analysis/2026-08-23-tts-japanese.md).

**5.4 Width, which jichi already gets right.** East-Asian Wide characters occupy two
terminal columns; `jc_utf8_width` carries the UAX #11 table and `jc_term_str_cols` uses
it, so column arithmetic is correct (`docs/ENCODING.md`). Japanese prose is *shorter in
characters* and *wider in columns* than English — the opposite of German — so §4.1's width
budget needs a per-language figure, not one constant.

**5.5 Punctuation and pauses.** 、 and 。 versus `,` and `.` produce different pause
lengths in TTS, and the fullwidth ？ differs from `?`. The current `ja` catalog mixes an
ASCII `?` into Japanese text (`許可しますか?`), which is at best inconsistent. **Q7.**

**5.6 Braille.** Japanese braille is kana-based, so kanji-heavy text transliterates
through a reading step that can go wrong in the same way as 5.3. Noted for honesty; out of
scope to solve.

## 6. What must be measured before it is designed

This project's rule is *measure the population before building a gate*, and four items
above are guesses until measured:

| # | measurement | method |
|---|---|---|
| ~~M1~~ | ~~the longest rendered chrome line, per language, in columns~~ | **DONE at M554** — `tests/test_width.c`. Found three over-budget chrome lines and an 81-column approval prompt; corrected the German figure from folklore to +50%. Record: [`analysis/2026-08-23-chrome-width.md`](../analysis/2026-08-23-chrome-width.md) |
| M2 | how many chrome lines an ordinary session emits, and how many the operator needed | instrument a real session; this is also what §2.4 and the 360-byte bound need |
| M3 | whether the line-buffering delay (item d) is perceptible to a sighted user | A/B two captures with timestamps; the question is milliseconds, and it is answerable |
| M4 | whether a German-locale reader speaks `4.946` as a number or as digits | one listening test by the operator, five minutes |
| M5 | whether Japanese TTS segments `y はい` correctly, with and without the space | needs a native speaker with a Japanese TTS; nobody here can do it |

**M1 is the one to do first**, because it gates every prose decision in every language and
it needs no listener — it is arithmetic over the catalog.

## 7. Implementation plan

Ordered so that each stage is shippable, gated, and reversible, and so that **no stage
depends on an answer we do not have yet.**

| stage | work | blocked on |
|---|---|---|
| ~~**A1**~~ | ~~width measurement + lint~~ — **DONE at M554.** It paid for itself immediately: three of M553's ten prose lines did not fit, one of them unconditionally. | — |
| ~~**A2**~~ | ~~move the chrome into `jc_msg`~~ — **DONE at M557.** Catalog 11 → 22. Untranslated entries are NULL (falling back to English) with the per-language count pinned and printed, because text that looks translated and is not is worse than a gap. Format specifiers in a catalog needed a new guard: every translated entry's specifier sequence must match English's, or a `%d` for a `%s` is undefined behaviour. The M554 stopgap lint is deleted, its end condition reached. | — |
| **A3** | **Decompose `--accessible`** per §3: (b) and (e) become default; (c) becomes default when colour is off; `--accessible` becomes a preset for (a), (d), (g). One milestone per property, each with a sighted control check. | nothing; independent of A1/A2 |
| **A4** | **German: the DIN 5009 cue for `a` and `e`**, plus register and number-format decisions. | **Q1, Q2, Q3** |
| **A5** | **UAX #14 line breaking** for compounds, benefiting German and Japanese and every long identifier in English. Compiled-in table, consistent with the `libcurl-and-nothing-else` rule. | A1 (it is the same width machinery) |
| **A6** | **Japanese review pass.** Every `ja` entry read by a native speaker through a Japanese TTS; the space-delimiting question settled; punctuation normalised. | **Q4–Q7, and a person** |
| **A7** | The remaining chrome from the previous design: `/status`, the prompt line, the wisdom line, the ghost suggestion. | nothing |
| **A8** | **The volume question** (§2.4, the 360-byte bound). | M2 |

**A1 and A2 first, deliberately.** They are unblocked, they are mechanical, and A2 is the
thing that makes "and in more than one language" possible at all — today a German user
gets German for the *approval prompt* and English for every line M553 touched.

## 8. What jichi must not claim

- **Not "WCAG compliant".** WCAG 2.2 is written for web content; there is no equivalent
  conformance standard for a terminal program, and claiming one would be false. The
  relevant *ideas* transfer (1.3.1 info and relationships, 3.1.2 language of parts, 2.4.6
  labels) and can be cited as influences, not as certifications.
- **Not "tested with screen readers".** It has been tested with **Orca + espeak-ng, on one
  machine, in English, by one person who does not normally use a screen reader.** That is
  the honest ceiling on every claim on this page, it found four real defects, and it is
  not a substitute for a user who depends on the tool.
- **Nothing about braille, about NVDA or JAWS** (Windows; jichi is POSIX-only), **or about
  Japanese or German speech**, until someone has heard them.

## 9. Resources worth grounding this in

Named because the operator offered to find them, and marked by whether they are *usable
from here* or need a person.

| resource | what it settles | usable now? |
|---|---|---|
| **DIN 5009** (2022-06 revision) | the German spelling alphabet for §4.3 — and which list, city or personal names | needs the text; the operator may know it directly |
| **Unicode UAX #14** (line breaking) + **UAX #11** (East Asian width) | A5; UAX #11 is already in the tree | yes, public specs |
| **CLDR** number/date patterns | §4.4, per-language grouping — as *reference data to hand-transcribe*, since jichi cannot link ICU | yes, as a reference |
| **Orca documentation** (punctuation levels, verbosity, `speakBlankLines`) | why the operator heard what they heard; several protocol findings are reader-side | yes |
| **`brltty` documentation** | whether any of §1.5 is real or wishful | yes |
| **JIS X 8341-3** | the Japanese accessibility standard, for §5 vocabulary | needs access; may be paywalled |
| **A native Japanese speaker with a Japanese TTS** | Q4–Q7. Nothing substitutes for this | **no — this is the blocking resource** |
| **A person who uses a screen reader daily** | everything. §8's ceiling | **no** |

## 10. Questions

**For the operator (German native):**

**All five were answered on 2026-08-23. Kept with their answers rather than deleted, so a
future reader can see what was decided on evidence and what on preference.**

- ~~**Q1**~~ — the `a`/`e` key cue. **DIN 5009.** Revision not named; §4.3 assumes the
  2022 city list and flags the assumption.
- ~~**Q2**~~ — how a reader speaks `4.946`. **As digits, reading the `.` aloud.** This was
  the most consequential answer on the page: it turned out to be a defect on the *default*
  configuration rather than a German preference, and it shipped as **M555** the same day.
- ~~**Q3**~~ — register. **Formal (`Sie`).**
- ~~**Q4**~~ — are the German strings idiomatic? **No, and the answer is not a patch.**
  The operator, a native speaker, on strings I wrote: ***"eingereiht/verworfen is not
  idiomatic, and not the correct tense depending on the context."*** Two separate faults —
  the word choice, and the **tense**, which varies with where the string appears. A catalog
  whose entries are reused across contexts cannot fix a context-dependent tense by editing
  a word: it needs per-context entries or a tense-neutral rephrasing. That is a rewrite
  with a native speaker, and it is **A4's real content**.
- ~~**Q5**~~ — German prose now, or English first? **English first.** Accepted as the
  sequencing for everything on this page, which also puts A2 before any German work: until
  the chrome is in the catalog there is nothing for a German string to be.

**For a native Japanese speaker, if you can find one:**

- **Q6.** Does `許可しますか? y はい  n いいえ` segment and speak correctly in a Japanese
  TTS — and does the space before `y` help or hurt?
- **Q7.** Register: is ですます right for a tool asking permission to edit your files, or
  is plain form normal for a CLI?
- **Q8.** Should the fullwidth `？` replace the ASCII `?` in the Japanese strings?
- **Q9.** For technical terms with ambiguous kanji readings, is kana preferable even
  though it looks less polished?
- **Q10.** Are the existing `ja` entries natural, or do they read as translated English?
  This one is the most valuable and the least specific — I want a native speaker's
  reaction, not a checklist.

**And one for both:** what does jichi say *first*, in your language, that would make you
close it? First impressions are chrome, and chrome is what this page is about.
