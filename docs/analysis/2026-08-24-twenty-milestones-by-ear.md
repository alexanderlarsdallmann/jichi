# Twenty milestones found by listening

*2026-08-24. M551–M570 — the accessibility arc, written up honestly: which defects a person
found, which the test suite found, which I found by running my own published commands, and the
eleven mistakes I made along the way. The headline number is uncomfortable and worth stating
first: **of the defects that mattered, the test suite found none.***

---

## 1. Where the findings came from

Twenty milestones. Attributing each to whoever actually surfaced it:

| # | milestone | surfaced by |
|---|---|---|
| M551 | the approval prompt spelled its own key list aloud | **operator, listening** |
| M552 | the fix for the unreadable prompt was itself unreadable | **operator, listening** |
| M553 | the chrome channel speaks in sentences | **operator** ("we need phrases, and sentences") |
| M554 | how wide is the chrome, really | me — a consequence of M553 nobody had measured |
| M555 | a separator inside a number is read as a separator | **operator, listening** |
| M556 | what a screen reader actually says for jichi's Japanese | **operator** (asked the question) |
| M557 | the chrome sentences enter the catalog | me — structural, to make M553 translatable |
| M558 | a test that passed for the wrong reason | me — while investigating M557 |
| M559 | one setting explains ten milestones | me — reading Orca's config, *after* nine fixes |
| M560 | without colour, the role has to be a word | **operator** (accessibility-by-default request) |
| M561 | A3 closed: the bundle retired, one property at a time | me — a principle extracted from M560 |
| M562 | the prompt, the most-repeated string in a session | **operator** (asked for A7) |
| M563 | the accessible token line was dead code under colour | **operator, running the shipped build** |
| M564 | digits for the approval keys | **operator** (proposed them outright) |
| M565 | a stray key is not an answer | **operator** ("any other key can be hit accidentally") |
| M566 | the headless front-end had none of it | me — running a command I had just published |
| M567 | a terminal has no language channel | **operator, listening** |
| M568 | the German catalog is complete | **operator** (native speaker, wrote the verdict) |
| M569 | advice that cannot be followed | **operator** (asked one question about `Enter`) |
| M570 | a human denial never reached the loop-breaker | **operator** ("0 does not abort") |

**Twelve of twenty came from a person using the software.** Six were mine and every one of those
was *derived* — a consequence of a fix, a structural prerequisite, or a discovery made while
verifying something else. Two (M559, M561) were the most valuable things I contributed, and both
were **explanations of defects already reported**, not discoveries of new ones.

**The test suite, at 12,888 unit checks and 250 smoke drivers when this began, found zero of
them.** Not one. That deserves a section of its own.

## 2. Why 12,888 checks could not see any of it

Three distinct blindnesses, each of which had to be fixed before the tests could see the class
of defect at all:

**The tier exported the wrong environment.** `tests/smoke/_smoke.sh` exports `NO_COLOR=1` for
every driver. So accessible mode had never once run **with colour on** — the configuration a
real user is most likely to have. That is exactly where M563 lived: `print_token_line` read
`if (c->color) … else if (c->accessible)`, so the prose branch was unreachable for any
accessible user with a colour terminal. Twenty-one accessible checks, all green, none of them in
that combination.

**The tier tested one front-end.** `src/main.c` called `jc_msg()` **zero** times against
`jc_tui.c`'s twenty-five. Every accessible sentence and every translation reached the
interactive renderer only, and `jc_msg_set_lang` was called from the TUI alone — so a headless
run never resolved a language at all. Found (M566) by running a liveness command I had written
into a hand-off document, not by any test.

**The instrument could not perceive the defect.** No automated check can hear "unintelligible".
`espeak-ng -q -x` prints phonemes and settled several questions for free, but it cannot answer
what Orca does, because **Orca performs punctuation verbalisation before the synthesizer sees
the text** (M559). The one measurement that explained nine milestones came from reading
`~/.local/share/orca/user-settings.conf`, and I read it on the tenth.

**The generalisable form:** *an instrument that exists and never covers the combination is
indistinguishable from no instrument.* This project recorded that shape three times in three
days — M551 (eight checks, every arm `--auto`), M558 (`redraw.py`, one mode), M563 (every
accessible arm `NO_COLOR`).

## 3. The two ideas worth keeping

**One setting explained ten milestones (M559).** Six reported defects looked like six wording
problems. They were one root cause: Orca configured with `verbalizePunctuationStyle 1` (SOME),
speaking the punctuation jichi used for visual compression. `[tokens in=4,946 out=37]` is heard
as *"bracket tokens in equals four nine four six out equals thirty seven bracket"*. Nine fixes
had already shipped before anyone measured the setting that caused all of them.

**Colour-carried information versus punctuation-carried information (M561).** The distinction
that turns "make it accessible" from taste into a decidable question:

- information carried by **colour** *vanishes* without it → restoring it is a **defect fix**
- information carried by **punctuation** *does not vanish*, only reads badly → converting it is
  a **preference**

That single test yields "yes, the role must become a word" for M560 and "no, don't reflow this"
for prose chrome, from one principle rather than two judgements. It is also what kept the
**content channel** inviolate: the operator's rule — *"within program code all symbols are
important, and must be read"* — means model text, file bodies, diffs and code are never
altered. Only chrome, where jichi talks about itself, is free to be reworded.

## 4. The digits, which dissolved four milestones of workarounds

The operator proposed: `1` yes, `0` no, `8` always, `3` edit, `5` view — accepted in every
language alongside `y n a e v`.

M552–M559 had worked *around* a problem the digits **removed**. The letters are English
initials, so `a wie immer` is simply false — `a` is not the first letter of `immer` — which
meant the "as in" cue could not be translated, DIN 5009 was needed to name letters instead, and
the German prompt measured ~108 columns against a 78-column budget. Digits have no language.

The measured outcome, after M568 completed the German catalog:

```
approval prompt, accessible form:   English 75 columns   German 57 columns
```

**German ended up shorter than English.** The open question about DIN 5009 closed as
*obsolete* rather than answered — the best kind of closure, and it came from the user, not from
the person who had written four milestones of workarounds.

And a property nobody predicted, discovered by the M567 voice mismatch: **digits degrade
better under the wrong voice.** `1 ja` read by an English voice is "one ya" — a wrong-language
number, still a number. `a immer` is "ay immer", where the cue is simply lost.

## 5. Consent, which is where the real defects were

Three of the last four milestones are about the approval fence, and together they say something
the accessibility framing almost hid: **the fence had defects that harm everyone, and a screen
reader is what made them visible.**

- **M565** — a stray keypress *denied*. Safe, and still wrong: it resolved a fence with a byte
  the user never meant and told the model "denied by the user" for a decision nobody made. The
  operator's reasoning was better than mine: *"any other key can be hit accidentally. if 0
  means no, 0 means no."* I had checked the mechanism, found no observable difference, and
  missed the point entirely.
- **M569** — the type-ahead notice said *"unsent, press Enter to queue it"* at the exact moment
  the text had already been freed. Advice that could not be taken, and a listener cannot see
  that the line is gone.
- **M570** — a human denial jumped past the tool-loop detector via a `goto`, so refusing the
  same call produced **seven prompts for one rename**. The detector already had the right
  advice and the right threshold; it had simply never seen a person's refusal.

None of these is an accessibility bug. All three were found because a screen reader makes the
cost of bad interaction design impossible to ignore — a sighted user glances past a doubled
prompt; a listener sits through it.

## 6. My mistakes, all of them

Listed because the register is the point, and because several repeat.

**Spending money on the operator's key without asking (~$0.007).** `jlu/tts-1-hd` returned
HTTP 500; I stripped the namespace prefix, got a 200, and sent ~19 requests to **OpenAI's
priced TTS**. The repository had already written down that the bare alias is priced. Recorded as
ANECDOTES #68; `CLAUDE.md` gained the mechanical rule — *an id that starts working when you
strip a prefix is not fixed*. **The deeper error cost more than the money:** I reached for a
neural TTS to answer "what does a screen-reader user hear". Orca speaks through `espeak-ng`,
which was installed the whole time, free and deterministic.

**Reporting a defect that did not exist.** I told the operator about a 200-milestone bug harming
screen-reader users. The cause was my own VT emulator not consuming `ESC[?2004h`. ANECDOTES #69.

**Wrong advice, twice, in the same week.** I told them to pass `LANG=de_DE.UTF-8` to hear German
— which sets the locale for *jichi's process* while Orca was already running with `en-US`, so it
produced the incoherent pairing they then reported. And I told them Ctrl-C twice would interrupt
a turn; at an approval prompt Ctrl-C is a **denial**, so it never reaches the input line.

**Vacuous checks — the running count is eleven.** Seven in one session; then an `env -u`
misuse that exited 127 so an absence-assertion held in empty output; then an `expect "] "`
anchor copied from a driver that only runs the sighted prompt; then four in one driver
(`deny_stops`). Every single vacuous pass was an **absence** assertion, and an absence holds
trivially in nothing.

**A false sentence in my own test header.** Check 1 of `headless_accessible.sh` claimed checks
2–5 "are all clean on a pair of empty files". I measured it: they redden. The check still earns
its place — it converts four misleading failures into one true sentence — but the justification
I first wrote for it was itself the species of defect the project keeps finding.

**Process damage.** Killed `make` mid-build and corrupted binaries, then diagnosed the
resulting `rc=6` as a product defect. Ran `git add -A` during a build and staged `*.o.tmp`.
Wrote a perturbation that did not compile, while my helper sent `make`'s output to `/dev/null`
— so **the driver silently ran the previous binary** and a check appeared to pass while the
behaviour was absent.

**And the one that keeps recurring in a different costume:** attributing an anomaly to the most
interesting available cause. An HTTP 200 meant "found it" rather than "who answered?". A wrapped
line meant "a bug" rather than "my parser". A retrying model meant "a missing feature" rather
than "a `goto`".

## 7. What changed structurally

| | before | after |
|---|---|---|
| unit checks | 12,888 | 12,931 |
| smoke drivers / checks | 250 / 1,429 | 254 / 1,449 |
| catalog entries | 22 | 24 |
| languages complete | English | English, **German** |
| front-ends using the catalog | 1 (TUI) | 2 (TUI + headless) |
| `doctor` language checks | 0 | 3 |

New instruments that did not exist: `tests/test_width.c` (every entry measured in columns per
language, UAX #11); the per-language coverage count printed on every build, so a deliberate gap
is visible and an accidental one fails; `group_sep_lint`, `prompt_keys_lint`,
`headless_accessible`, `doctor_language`, `queue_notice_glyph`, `deny_stops`.

## 8. What this says about doing it again

**Accessibility cannot be retrofitted by reading code.** Twelve of twenty defects required a
person to hear them. The six I found were derived from those twelve. Had the arc been run
without a user, the product would today have prose chrome in the TUI, brackets in the headless
path, a fence that could be worn down by retries, and a German interface nobody could listen
to — with a fully green gate.

**The operator said this before any of it was fixed**, and it is the most accurate sentence in
the record:

> *"this is a lesson on how important accessibility design is for software development, and that
> it needs to be there from the beginning: with user tests. Not as an afterthought."*

**What the tests are for, then.** Not discovery — regression. Every driver written here exists
to keep a defect a person already found from coming back, and several of them promptly caught
*my* mistakes rather than the product's. That is a real return, and it is a different job from
finding the defect in the first place. The mistake would be to read a green gate as evidence
that the interface works for anyone in particular.

**Three habits that paid.** Measure with the instrument the user actually has (`espeak-ng -q -x`
answered for free what a paid API got wrong, in the opposite direction). Perturb **per check**,
not per driver — every vacuous assertion above was found that way and none by re-reading.
And prefer a lint to an audit, but **state its universe and floor its extraction**: three of
these drivers caught a fixture bug in their own denominator before they ever tested the product.

## 9. Still open

- **A6, Japanese** — needs native speakers; the protocol is written and `doctor` now tells a
  Japanese user which packages they need (measured at M556: espeak-ng says "Chinese letter" for
  46 kanji in the `ja` catalog, or drops them).
- **M571** — `jc_tool_arg_summary` does `(void)name;` and reads only top-level scalars, so
  `ask_user` (`question`) and `apply_patch` (path nested in `edits[]`) announce themselves as
  raw JSON; `v`/`5` prints `args` verbatim for every tool, so **the view key never shows a
  diff**; and a refusal is rendered *"The tool X **failed**. denied"*, reporting a decision as a
  malfunction.
- **A5** — UAX #14 line breaking, newly relevant now that German chrome ships (its widest entry
  measures 76 columns against a 78 budget).
- `/status` and the wisdom line under `--accessible`; the ghost suggestion, which is riskier
  because it touches the line editor mid-edit; and whether the line-buffering delay is
  perceptible to a sighted user.
