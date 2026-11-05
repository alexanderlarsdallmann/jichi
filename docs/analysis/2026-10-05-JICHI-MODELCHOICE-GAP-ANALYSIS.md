# jichi gap analysis — guiding a self-learner / junior / agent to choose the right model, and test-prompt batteries for model fit

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

The question asks whether jichi is badly short of a way to check a model's fit,
and specifically whether there is a set of test prompts for programming-language
or natural-language proficiency. **The honest answer is: the second half is
already true, and the gap is narrower than the suspicion.** jichi *does* ship
two model-fitness test batteries, both written for learners, both cited with
measured results:

1. **The version probe** (`docs/CHOOSING_A_MODEL.md` §4, driven by
   `tests/bench/version_probe.py`, M374) — a *concrete, runnable* set of test
   prompts for **programming-language proficiency**. It sends the model a
   self-report question ("what is the newest released version of Zig you have
   reliable knowledge of?") and a behaviour probe ("write a minimal program that
   appends three integers to a `std.ArrayList`…"), then lets the *installed
   compiler* arbitrate and prints a record block to paste into the docs. It is
   proven two-sided offline (`--mode self-test`).
2. **The eight questions** (`docs/WORKBENCH.md` §11.2) — a *concrete* set of
   test prompts for **reasoning / natural-language proficiency**, put to two
   free models side by side (`--readonly`), each with a *source to check the
   answer against* (a diff, a PDF, a 404 page, a log, a man page, a Makefile).
   It measures the thing that *cannot* be checked by a compiler.

So the "is there a set of test prompts" question has a **yes** for both
proficiencies. The one thing *definitely lacking* is not the existence of a
battery but **its coverage and its reach**: the version probe, the only
*executable* one, tests **exactly two languages — Zig and C89 — while jichi
scaffolds ~20** (C, C++, Rust, Go, Python, GDScript/Godot, Perl, R, Scheme,
Racket, Clojure, Haskell, Elixir, Erlang, Emacs Lisp, TS/JS, …). For any of the
other eighteen, "probe the model for language fit" has no runnable instrument;
the learner has only the *prose recipe* in §4 (copy the two commands, swap the
language) and `c89` is explicitly the *control*, not a target. And **neither
battery is reachable by an agent in-loop** — there is no `jichi model-check`
command and no agent tool; the model-choice guidance is entirely *ex-ante*
(probe before you start) and *reactive* (routing escalates to the strong model
*after* a verifier fails, `escalateOnVerify`, ROUTING.md:16), never a
*mid-session "is this model fit for this task?"* check. Those are the real gaps:
**a version probe that covers the languages jichi actually scaffolds, and a
reasoning battery that is a portable fixture rather than prose bound to jichi's
own corpus.**

## 1. What exists — and why it is genuinely strong (the user is partly right)

- **A learner-facing model-choice tutorial, not a config reference.**
  `CHOOSING_A_MODEL.md` is 399 lines titled "…and the question you must ask it
  first," with the thesis "choose by task shape and knowledge fit first; size
  is the third question, not the first — and never rely on a model for a
  language before probing what version it knows." Four axes (capability,
  knowledge, cost/latency, context), tiers (tiny/small/large) with *what each
  is for*, and a verification-asymmetry section (§3). (Verified.)
- **A real, runnable programming-language probe.** `version_probe.py`
  (13 KB, Python-stdlib-only) with a per-language corpus; `zig` uses
  discriminator prompts targeting the 0.15 Io rewrite (arraylist, stdout);
  `c89` is the control ("has not changed since 1990, so a model failing it
  indicts the probe, not the model"). It "can FAIL a model, never certify one"
  (a toy compiling certifies nothing) and its own gate is proven two-sided
  offline. It prints a `MODEL_KNOWLEDGE` record block ("project requires /
  self-reported newest / observed dialect / suggested verdict / mitigation")
  for the operator to paste. (Verified.)
- **A real reasoning / NL battery.** WORKBENCH §11.2, "Eight questions, two
  models," each with a checkable source; §11.3 "what each difference teaches"
  (the diff: right bug, slightly wrong *reason*; the PDF: right quote, reversed
  summary; the 404: answered from a site it chose *and did not say so*; the log:
  a cause the timestamps rule out). This is a measured demonstration that a
  *coding* model and a *reasoning* model fail in different, checkable ways.
  (Verified.)
- **Measured campaign behind the guidance.** CHOOSING §3's two-day bake-off
  (eleven runs, four tasks, both models in both roles) with the lessons
  "assign models to *crafts* (prose · surgical edit · implement-to-gate), not a
  strong/weak axis," "the fence is part of the model choice" (fenced run 504k
  tokens vs unfenced 5,215k — the same model editing the gate test), and "an
  author and solver sharing a model share its blind spots." (Verified.)
- **A decision checklist for a task family** (CHOOSING §5) and a "start fast,
  let jichi escalate" operational rule (WORKBENCH §11.1). (Verified.)
- **A *deliberate, argued* design decision (D1)** to keep the version probe a
  documented ritual, **not** an automated `doctor` check: "a `doctor` printing
  a green checkmark next to a model's unaudited claim about itself would
  launder weak evidence into apparent fact." That is the register's own
  honesty standard, and it is *correct* — it means the gap below is not a
  "you forgot to automate it" defect but a scope/reach question. (Verified.)

## 2. The one thing definitely lacking

**Not the existence of a test battery (there are two), but (a) the
version probe's *language coverage* — two languages against ~20 scaffolded —
and (b) the *reach* of both batteries — neither is invocable by an agent
mid-session, and the reasoning battery is prose, not a portable fixture.**

1. **The executable probe covers only `zig` and `c89`.**
   `version_probe.py`'s corpus is literally two entries (lines 59 `zig`, 75
   `c89`); c89 is the *control*. jichi's scaffold ships AGENTS.md conventions
   for ~20 languages (`FILE_AGENTS_{C,CPP,RUST,GO,PY,GODOT,PERL,R,GUILE,RACKET,
   CLOJURE,HASKELL,ELIXIR,ERLANG,ELISP,WEBTS,…}`). A self-learner on a **Python,
   Rust, Go, or Godot** project — the common cases, and godot is precisely the
   case analysis #30 found *lacking a gate* — has **no runnable** version probe.
   The §4 prose gives the *shape* of the prompt (self-report + "write a minimal
   program that uses X") but the learner must author their own behaviour probe
   and find their own compiler; the two-sided self-test that makes the probe
   honest (a bad fixture, a good fixture) does not exist for those languages.
   The probe's whole value is that the *compiler is the arbiter* and the gate is
   *proven to catch a stale dialect* — and that value is currently Zig-only,
   with C89 as the control.
2. **Neither battery is agent-reachable.** There is no `jichi model-check` /
   `jichi probe` command and no agent tool (`grep model_fit|check_model|
   probe_model|fit_for` over `src/` is empty). The guidance is **ex-ante** (the
   human runs the probe *before* starting — "would have exposed the requirements
   gap before the project started") and **reactive** (`escalateOnVerify` moves a
   turn to the strong model *after* the verifier fails). A self-learner or an
   *agent* that is, mid-task, unsure whether the current model is fit for the
   next step has no instrument: the routing decision is made by jichi's policy,
   not by a fitness check the model or a junior can request. The "eight
   questions" and the "version probe" are tools a *human operator* reaches for;
   they are not in the loop a *learner in a session* or an *autonomous agent* is
   in.
3. **The reasoning battery is prose bound to jichi's own corpus.** §11.2's eight
   questions are embedded as `jichi --readonly -p "…"` invocations in the
   WORKBENCH doc (the PDF, the log, the Makefile, the 404 are *jichi's*
   fixtures), and §11.3's "teaches" is an *interpretation*, not a score. There
   is no fixture file, no runner, no rubric, no "N/8 with these are
   source-checkable" — so a self-learner cannot *run the set*, cannot *score*
   their own model against it, and cannot port it to a different corpus. The
   programming probe, by contrast, *is* runnable and *does* print a verdict; the
   NL battery is a demonstration, not an instrument. The two proficiencies the
   user asks about are thus asymmetric: **programming-language fit has an
   executable (Zig-only) instrument; natural-language fit has a demonstration,
   not an instrument.**

## 3. Why this is the *right* "definitely lacking" (and what it is NOT)

- **It is NOT "there is no test-prompt set" — that would be false, and
  answering the user honestly requires saying so.** Both proficiencies have
  named, measured, learner-facing test batteries. The gap is their *coverage*
  (one language against ~20) and *reach* (operator-side, ex-ante/reactive, not
  in-loop).
- **It is NOT "automate it into `doctor`."** D1 *argued against* exactly that
  and was right (a self-report laundered into a green checkmark). The probe is a
  ritual by design. I do not recommend undoing D1; I recommend *extending the
  corpus* and *adding a learner-facing runner*, which D1 does not forbid (it
  forbids a `doctor` *certification*, not a `--lang rust` probe or a
  self-scored NL fixture).
- **It is NOT "the reasoning battery is missing."** §11.2 is a real, measured,
  checkable set. It is *demonstration-grade*: bound to jichi's corpus, no
  runner, no rubric. The honest framing is "exists as a worked example; lacks a
  reusable fixture" — which is a different and smaller claim than "lacks the
  battery."
- **"The version probe is Zig-only" is the sharpest, most falsifiable core,
  because jichi *itself* made the language a first-class concern** (~20 packs,
  the whole §4 thesis is "never rely on a model for a language before probing
  what version it knows"). The instrument for that thesis covers the one
  language that is *not* the project's own implementation language — a
  consequence of the probe growing out of the zigodot incident, not of a
  general design.
- **The "agent-reach" gap is real but partly by-design** (an agent probing
  *itself* has the same self-report-trust problem D1 rejects). The honest
  version is narrower: *a learner in a session* (not the agent, the human
  driving it) has no way to invoke the probe for the language they're in; and
  the *reactive* escalation signal (verifier failed) is a poor proxy for
  "language knowledge is stale" — which is exactly the zigodot failure the probe
  exists to catch *ex-ante*. So the in-loop absence is a genuine seam, but I do
  not claim an agent should self-audit.

## 4. Evidence

- **Two runnable/prose batteries exist.**
  - Version probe (executable): `tests/bench/version_probe.py` — corpus at
    59 (`zig`) and 75 (`c89`); c89 is the CONTROL
    ("has not changed since 1990, so a model failing it indicts the probe, not
    the model", 56-57); "can FAIL a model, never certify one" (18); two-sided
    offline self-test (217-273); prints a `MODEL_KNOWLEDGE` record block
    (198-214) with "suggested verdict: DOES NOT MEET / not refuted … WEAK
    positive evidence" (204-212). CLI: `--url … --model … [--lang zig]
    [--lang c89] [--trials 3]` or `--mode self-test` (26-28). (Verified.)
  - The prose recipe: `docs/CHOOSING_A_MODEL.md` §4 "The probe, concretely"
    (208-260) — the literal self-report prompt, the literal behaviour prompt
    (`sed '/^```/d'`, "the compiler is the arbiter"), and the context-free
    `--config-json` form. (Verified.)
  - Eight questions (prose, checkable): `docs/WORKBENCH.md` §11.2 (the
    table) and §11.3 (the "teaches"); the prompts appear as `jichi
    --readonly -p "…"` invocations (e.g. 438 Makefile, 525 ls, 547 man tree,
    576 the MIME PDF). (Verified.)
- **jichi scaffolds ~20 languages; the probe tests 2.**
  `grep -oE 'FILE_AGENTS_[A-Z0-9_]+' src/scaffold/jc_scaffold.c | sort -u` →
  C, CPP, RUST, GO, PY, GODOT, PERL, R, GUILE, RACKET, CLOJURE, HASKELL,
  ELIXIR, ERLANG, ELISP, WEBTS, plus role packs. `version_probe.py` corpus →
  `zig` (59), `c89` (75) **only**. (Verified — the coverage gap is direct.)
- **No agent-reachable model-check exists.**
  `grep -rnE "model_fit|check_model|probe_model|fit_for|model_probe" src/
  --include=*.c` → **empty**; no `jichi probe`/`model-check` subcommand in
  `main.c`. (Verified.)
- **The guidance is ex-ante and reactive, not in-loop.**
  CHOOSING §4: the probe "would have exposed the requirements gap **before the
  project started**, instead of after days of confusing compile failures"
  (183-186). ROUTING.md:16/37: `escalateOnVerify` (default on) "the fast tier
  couldn't produce a passing result — the clearest 'this is hard' signal" —
  escalation is *after* a gate fails, not a language-fit check. (Verified.)
- **The reasoning battery has no runner or rubric.** No test file references
  the eight-question set or WORKBENCH §11.2 (`grep` over `tests/` finds no
  such runner); the "teaches" in §11.3 is interpretation, not a score.
  (Verified by search.)
- **The design decision D1 (ritual, not `doctor`) is argued and correct.**
  CHOOSING §6, D1 (328-337): "a `doctor` printing a green checkmark next to a
  model's unaudited claim about itself would launder weak evidence into
  apparent fact." (Verified.)
- **The motivating failure is real and Zig-specific.** zigodot's model "produced
  internally-consistent Zig 0.11–0.13 code, repeatedly, against [the] rules"
  and, asked directly, "answered that its training data covers Zig 0.11.0"
  (CHOOSING §4, 180-186) — the exact case the probe's Zig corpus targets.
  (Verified.)

## 5. Implications

- **A self-learner is *well* guided for the common "is this model strong
  enough to reason / for prose" question, and for Zig specifically** — but for
  the *other eighteen languages jichi will actually scaffold*, the "does this
  model know my language?" question (the §4 thesis) has **no instrument**: the
  learner has the two-command *shape* and must write their own behaviour probe
  and find their own compiler, without the two-sided self-test that keeps an
  honest probe from a false one. The register's own standard — "a gate never
  seen failing has never been seen working" (version_probe.py:22) — is applied
  to the Zig gate but not to the Rust/Python/Go/Godot ones that do not exist.
- **The two proficiencies the user asks about are asymmetric in
  machine-checkability.** Programming-language fit has an *executable* instrument
  (compiler-arbitrated, two-sided, prints a verdict) — for one language.
  Natural-language fit has a *worked example* (eight questions, checked by eye
  against a source) — for jichi's own corpus. A learner who wants "how good is
  this model at reasoning, on *my* task" has to reconstruct §11.2 by hand; a
  learner on a Rust project has to reconstruct §4 by hand. The strongest
  existing artifact is the least general.
- **In an autonomous / junior session there is no mid-flight "is my model fit
  here?"** The routing policy will move to the strong model when a *verifier*
  fails, but a stale-language model often *passes* a weak verifier (or there is
  none — the godot case, #30) and writes plausibly-wrong old-API code. The
  *ex-ante* probe is the correct instrument for that failure, and it is the
  one a junior is least likely to have run, and the one not reachable once
  they are in the tree. The guidance says "probe first"; nothing makes it
  probe-able *in the moment* for the language in front of them.
- **D1 is the reason this should be fixed by *corpus + runner*, not by
  *automation into doctor*.** The register already decided the probe must stay
  a human-judged ritual. Respecting that, the honest extensions are: (1) add
  per-language discriminator prompts (the Zig ones were chosen because they
  target a *measured* rewrite; the others need the same evidence, which is why
  this is a corpus effort, not a copy-paste), and (2) give §11.2 a fixture +
  runner + rubric so the NL battery is as machine-usable as the Zig probe is
  for Zig. Both are inside D1's boundary; neither launders a self-report into
  a checkmark.

## 6. Recommendation

1. **Extend `version_probe.py`'s corpus to the languages jichi scaffolds, with
   *measured* discriminators — one at a time, each proven two-sided.** The Zig
   prompts target the 0.15 Io rewrite because that is the version a stale model
   gets wrong; the Rust/Python/Go/GDScript prompts must target an *equally
   measured* break (a std change, a removed API, a GDScript 3→4 difference —
   the godot case is the obvious first candidate, pairing with analysis #30).
   A language with no measured discriminator should be *left out and said so*
   (as `c89` is the named control), not filled with a prompt nobody proved the
   gate catches on. This is a corpus effort; the first honest addition is
   `gdscript`/Godot 3-vs-4, which the register has already named as "a model's
   usual mistake" (def-294).
2. **Give the eight questions a portable fixture + runner + rubric** so
   natural-language fit is machine-usable like the Zig probe: a `tests/bench/
   reasoning_battery.{py,json}` holding the eight (or N) prompt+source pairs, a
   runner that sends them `--readonly`, and a *scoring rubric* (each answer is
   checked against its source; "right bug, wrong reason" is a distinct grade
   from "right and right-reason," per §11.3). The corpus is *portable* (the
   sources are supplied files, not jichi's checkout) so a learner can point it
   at their own diff/log/spec. This closes the asymmetry: programming fit and
   NL fit both become *runnable and scored*.
3. **A learner-facing `jichi model-check <lang>` that wraps the probe** (or,
   respecting D1, prints the *exact* two commands and the record block for the
   language in the current project) — not a `doctor` green check, but "here is
   how to probe *this* language, and here is the block to paste." That makes the
   §4 thesis actionable for the eighteenth language, not just the first.
4. **Document the reach honestly**: in CHOOSING §4 and WORKBENCH §11, name that
   (a) the executable probe currently covers Zig (C89 as control) and the rest
   via the prose recipe, and (b) neither battery is in-loop — escalation is
   reactive (`escalateOnVerify`), so a *stale-language* model that passes a weak
   gate is the case the *ex-ante* probe exists to catch, and it must be run
   before, not discovered after. That is the requirement the learner most
   easily skips.

Rejected, in the register's spirit:

* *Add a `doctor --live` model-knowledge check.* D1 already rejected this and
  was right — it would launder a self-report into a checkmark. The fix is
  corpus + learner runner, inside the ritual D1 preserves.
* *Claim the NL battery is "missing."* It is not — §11.2 is a real, measured,
  source-checked set. The honest claim is "demonstration, not fixture";
  overstating it to "absent" would be exactly the fabrication this register
  guards against.
* *Have an *agent* probe its own fit mid-session.* The agent probing itself has
  the same self-report-trust problem D1 rejects, and the *human* (learner or
  operator) is the one who must judge. I recommend the *learner-facing* runner,
  not an agent self-audit.

## 7. Limitations and honest counterarguments

1. **The user's suspicion is substantially *wrong* and I say so in the TL;DR.**
   There *is* a set of test prompts for both proficiencies. Framing this as
   "jichi has no way to check model fit" would be false. The finding is
   narrower: coverage (1 executable language vs ~20), reach (operator/ex-ante,
   not in-loop), and the NL battery's format (prose, not fixture).
2. **Zig is not a trivial "one language."** The probe grew out of a *measured*
   failure on zigodot — the operator's own Zig project (a Godot in Zig, driven
   by jichi), whose rules state Zig 0.16.0 while the model wrote 0.11-0.13
   (CHOOSING_A_MODEL.md:180-190) — and the Zig discriminators target a
   *specific, measured* std
   rewrite (0.15 Io). That is exactly the right way to build a probe; the
   gap is that the *method* has not yet been applied to the other languages,
   not that the Zig probe is weak.
3. **"The reasoning battery has no runner" may be *deliberate*.** §11.2 is
   written as a *teaching* artifact (a tutorial comparing two models for a
   learner), and a scored fixture could read as a "model IQ test" the register
   is wary of. I claim only that, for the *proficiency-check* purpose the user
   asks about, it is not currently *usable as an instrument* — the runner
   recommendation is for that use, not to replace the tutorial.
4. **I have not run `version_probe.py` against a live endpoint** (read-only;
   it needs a server + local compiler). The "zig + c89 only, c89 control,
   can-fail-never-certify, two-sided self-test" claims are verified by reading
   the script (lines 18-28, 55-75, 198-214, 217-273); the *behaviour* (it
   actually discriminates) rests on the register's own claim that its self-test
   is proven offline, which I read but did not execute.
5. **The "~20 languages" is the scaffold's AGENTS.md count**, a reasonable
   proxy for "languages jichi claims to support." It is not a claim that all
   20 need probes simultaneously; the recommendation is *measured, one at a
   time*, with godot/GDScript the first (pairing with #30).

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the executable programming-language probe
    sed -n '1,28p' tests/bench/version_probe.py        # CLI, self-test, can-fail-never-certify
    sed -n '55,90p' tests/bench/version_probe.py       # corpus: zig (59) + c89 CONTROL (75)
    sed -n '198,214p' tests/bench/version_probe.py     # the record block + suggested verdict
    sed -n '217,275p' tests/bench/version_probe.py     # two-sided offline self-test

    # the prose recipe (self-report + behaviour prompt, context-free form)
    sed -n '178,260p' docs/CHOOSING_A_MODEL.md         # §4 the version probe
    sed -n '328,337p' docs/CHOOSING_A_MODEL.md         # D1: ritual, not doctor

    # the reasoning / NL battery (eight questions + what each teaches)
    grep -n "### 11.2\|### 11.3" docs/WORKBENCH.md
    sed -n '438p;525p;547p;576p' docs/WORKBENCH.md      # the questions as --readonly -p invocations

    # jichi scaffolds ~20 languages; the probe tests 2
    grep -oE 'FILE_AGENTS_[A-Z0-9_]+' src/scaffold/jc_scaffold.c | sort -u
    grep -oE '^\s+"(zig|c89|python|rust|go|gdscript)":' tests/bench/version_probe.py   # zig, c89

    # no agent-reachable model-check; guidance is ex-ante + reactive
    grep -rnE "model_fit|check_model|probe_model|fit_for|model_probe" src/ --include=*.c   # (none)
    sed -n '16,18p;37p' docs/ROUTING.md                   # escalateOnVerify (reactive)
    sed -n '180,186p' docs/CHOOSING_A_MODEL.md            # "before the project started"

    # the motivating failure (Zig-specific, measured)
    sed -n '180,198p' docs/CHOOSING_A_MODEL.md            # zigodot 0.11-0.13 vs required 0.16

**Verdict line for the register this could seed:** the suspicion that jichi
lacks a way to check a model's fit is **substantially wrong, and this is the
honest headline**: jichi ships *two* learner-facing model-fitness test
batteries. (1) A **programming-language** one — the version probe,
`docs/CHOOSING_A_MODEL.md` §4, executed by `tests/bench/version_probe.py`
(M374): a self-report prompt plus a behaviour prompt ("write a minimal
`std.ArrayList` program…") arbitrated by the *installed compiler*, two-sided
self-test offline, "can fail a model, never certify one," printing a record
block to paste. (2) A **reasoning / natural-language** one — `WORKBENCH.md`
§11.2, eight questions put to two models `--readonly`, each with a *source to
check the answer against* (a diff, a PDF, a 404 page, a log, a man page, a
Makefile) and §11.3's "what each teaches" (the coding model's four
source-revealed flaws vs the reasoning model's). Both are cited with measured
results and a 399-line model-choice tutorial. The one thing *definitely
lacking* is therefore narrow, and it is not "no test prompts": **(a)
coverage** — the only *executable* probe tests **exactly two languages, `zig`
and `c89` (c89 the named control), while jichi scaffolds ~20**
(`FILE_AGENTS_{RUST,GO,PY,GODOT,PERL,R,…}`), so a learner on Rust/Python/Go/
Godot has no runnable instrument, only the §4 prose shape; **(b) reach** —
neither battery is *agent- or learner-in-loop*: no `jichi model-check`, no
agent tool (`grep` over `src/` empty), the guidance is *ex-ante* ("would have
exposed the gap before the project started") and *reactive*
(`escalateOnVerify` moves to the strong model *after* a gate fails) — and a
stale-language model that passes a weak gate is exactly the case only the
ex-ante probe catches; **(c) format** — the NL battery is a worked example
bound to jichi's own corpus (no fixture, runner, or rubric), while programming
fit has an executable one; the two proficiencies are thus asymmetric in
machine-checkability. The fix is inside the register's own D1 (which rightly
keeps the probe a *ritual*, not a `doctor` green check): extend
`version_probe.py`'s corpus with *measured, two-sided* discriminators one
language at a time (godot/GDScript 3-vs-4 first, pairing with the
analysis-#30 finding that the godot pack ships no gate), give §11.2 a portable
fixture + runner + source-checkable rubric so NL fit is as machine-usable as
Zig fit is for Zig, and add a learner-facing `jichi model-check <lang>` that
prints the exact commands + record block for the language in front of them.
Revisit when: a non-Zig language has a measured two-sided probe, the NL set
runs and scores itself, and a learner can probe the language they are in —
at which point the §4 thesis ("never rely on a model for a language before
probing what version it knows") is *executable* for the languages jichi
actually scaffolds, not just the one the incident taught it.
