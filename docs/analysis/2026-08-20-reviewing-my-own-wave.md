# Reviewing my own wave, and the defect it found in the product

**Date:** 2026-08-20 · **Milestone:** M505 · **Corpus:** the four pages and five
sections added at M499/M502 (`TOOL_DECISIONS.md`, `STATE.md`, `VOCABULARY.md`,
`TEACHING.md`, plus "your first hour" / "your first bounded run" / "your first
skill" / "the smallest config that works" / "a check failed — now what") ·
**Instrument:** `DOC_REVIEW.md`

---

## 0. The weakness of this pass, stated first

`DOC_REVIEW.md` §3 says reviewers should be **read-only** and independent, and
§5 says **verify every finding before acting on it**. Here the author and the
reviewer are the same agent, which invalidates the independence the four-persona
method buys. Nothing in this note pretends otherwise.

What that leaves is the half a single reader can still do honestly: **execute
every copy-pasteable command, and check every factual claim against the source.**
That is the mechanical half, and it is where M392's sharpest findings came from
anyway — *"run the command, read the function, check the default"*.

The trigger for running it at all is §5's own: *after a wave of new pages*. The
previous wave shipped with **seven of about fifteen** copy-pasteable commands
broken, which is why this instrument exists.

## 1. Every command, executed

24 command lines across the four pages (VOCABULARY has none by design). Excluding
placeholders (`/path/to/...`, `<id>`) and the two slash commands that need a TUI
session, six were directly runnable:

| Command | Result |
| --- | --- |
| `jichi ls --all` | exit 0 |
| `jichi checkpoints gc` | exit 0 |
| `jichi context` | exit 0 |
| `jichi prune --keep 20 --older-than 30d --dry-run` | exit 0 |
| `jichi assignments` | exit 0 |
| `jichi assign docs/assignments/01-find-the-setting.md` | exit 0 |

Plus the whole `TEACHING.md` phase sequence, which had already been walked end to
end before that page was written (`--expect-fail` → `assign` → `hint` → solve →
`grade --record` → `assignments`), and `jichi doctor`, run many times.

**Nothing broken.** The reason is worth naming rather than claimed as virtue:
those commands were run *while* the pages were written, which is the M502 rule
("walk it before you assign it") applied to documentation.

## 2. Claim checks — one page overstated what a command does

**Finding 1. `TOOL_DECISIONS.md` claimed too much of `doctor`.** The closing
paragraph said *"`doctor` reports the effective mode, the permission lists,
whether hooks are active, and whether the privileged audit sink is disabled"*.
Measured: `doctor`'s output contains **none** of the mode, the permission lists,
or hook activity — `grep -iE 'mode|permission|hook'` over a real run matches only
model lines — and `jc_doctor_add` has no such check in the source. The audit-sink
warning exists, but only when the sink is off.

This is the rubric's *"How"* question failing: I sent a reader to a command for
four answers it does not give. Fixed with a table naming what actually answers
each — `jichi status` for the mode, `context` for what the model sees, the config
plus `sysmsg` for the permission lists, `runs` for what happened while away —
and a sentence saying plainly that `doctor` is a health check on the
*environment*, not a description of the *posture*.

**Verified and left alone:** the nine-step chain order (read from `jc_agent.c`,
including that the scope fence and the hook run *after* the approval prompt), the
`load_skill` tool line in SKILLS (`jc_tool_arg_summary`'s key list includes
`name`, so `▸ load_skill  commit-style` is what renders), the `learner`/
`instructor` preset diff, the `make install` manifest, the fifteen `~/.jichi.d/`
subtrees, and `jichi models`' role listing.

## 3. And then the review found a product defect

Checking one sentence in the DOCTOR page — *"`jichi config validate` names the
line"* — meant feeding it a broken config. It answered:

    $ jichi --config bad.json config validate
    OK: bad.json
      1 model(s); active: claude-opus-4-8

for a config that reads, in full, `{"models":[{"name":"a"}]}`.

Two separate things there, and only one is a documentation bug:

1. The trailing comma I planted parsed fine, because jichi's reader is
   **JSONC-tolerant** by design. So "names the line" was the wrong claim, not a
   broken tool. Corrected.
2. **The active model became `claude-opus-4-8` — a priced frontier id nobody
   configured** — and `doctor` rendered it as a green *"configuration loaded"*
   line, indistinguishable from a config that named that model.

`default_model(provider)` fills a missing `"model"` field: `gpt-4o` for provider
`openai`, an Anthropic id otherwise. The substitution is deliberate and defensible.
**Its invisibility is not**, for two reasons this project has already paid for:

* It reaches for a **priced** model. `ANECDOTES.md` #63 is about ~$10 spent on a
  priced model nobody authorised, and `CLAUDE.md` now carries a local-models-only
  rule. A config that omits an id and silently gets Opus is that hazard wired in.
* A hardcoded model id in a fallback is a **stale claim by construction** — newer
  ids exist already.

**The fix is provenance, not resolution** — the same argument M503's
`verify_source` makes. `jc_model_cfg` gains `model_defaulted`, and `doctor`
reports: *"active model id was DEFAULTED, not configured — the config names no
`model` for the active entry, so 'claude-opus-4-8' was substituted from the
built-in default for provider 'anthropic'."* **WARN interactively, FAIL under
`--unattended`**, joining M158b's escalation set: a supervisor starting a loop
against a model nobody chose is a posture problem, and the substituted id may be
billable.

The default itself is untouched. Removing it would change behaviour for configs
that rely on it, and that is a separate decision from making it visible.

## 4. What is already good, and should not be sanded off

§3 step 4 of the instrument asks for this explicitly, because without it the next
pass flattens the best writing:

* **`TEACHING.md`'s phase-3 table** ("what you are measuring, and it is not
  whether you can solve it") is the part that does work no lint can: it turns
  "sit your own assignment" from advice into five checkable questions.
* **`STATE.md`'s last column** — *what you lose by deleting it* — is what makes
  that page an operational tool rather than an inventory.
* **`VOCABULARY.md`'s decision to define the *testing* words** (TAP, floor,
  two-sided proof, vacuous check) alongside the product words. A reader of this
  project's tests needs both, and no other page defines either set.
* **The `GLOSSARY.md` disambiguation header.** Two sentences that stop a
  predictable wrong turn.

## 5. What this page mentions but does not fix

Per the instrument's step 6 — fix the blockers, register the rest:

* **The independence problem.** A real four-persona pass over this corpus is
  still owed; this note is the mechanical floor under it, not a substitute.
* **`config validate`'s silence on an incomplete model entry.** It now has a
  doctor warning beside it, but `config validate` itself still prints `OK`. Whether
  that command should surface posture warnings, or stay a pure parse check, is a
  scope decision for that subcommand — registered, not guessed.

## 6. Lessons

1. **The trigger earned its place.** A wave of new pages produced exactly one
   overstated claim — and finding it required running the command, not re-reading
   the sentence.
2. **A documentation claim is a test of the product.** The sharpest finding here
   was not a doc bug: it was the product defect that checking a doc sentence
   exposed. That is the third time this month.
3. **Reviewing your own work has a knowable ceiling.** The mechanical half is
   fully available and worth doing immediately; the judgement half needs someone
   else, and saying which is which is the only honest way to publish a self-review.
