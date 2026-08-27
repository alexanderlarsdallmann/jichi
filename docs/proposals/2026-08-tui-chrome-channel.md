# The chrome channel: making the TUI's own noise readable aloud (design)

*Written 2026-08-23, after the second listening test of the screen-reader protocol.
**Design only — nothing in `src/` changes on this page.** The measurements are from real
PTY captures; the decisions carry their rejected alternatives.*

## The operator's framing, which is the whole design

> *"Within program code all symbols are important, and must be read. However, lines like
> these: `[tokens in=4,946 out=37]` / `[chat·chat·2%] ›` can be presented in a more
> readable way."*

That splits jichi's output into **two channels** and gives them different rules:

| channel | what it is | rule |
|---|---|---|
| **content** | the model's answer, file bodies, diffs, code | **never altered.** A symbol in code is information. `*p++` is not noise. |
| **chrome** | jichi talking about itself — prompt, token counts, headers, spinners, echoes | free to be rewritten, reordered, or removed |

**This distinction is the invariant the whole design rests on**, and it is worth stating
sharply because it is the thing a well-meaning "make it less chatty" change would break.
Every proposal below touches chrome only. Any change that would alter a byte of model
output, a file body or a diff is out of scope by construction — not deferred, *excluded*.

**jichi already has the seam.** M549 routed model text through `acc_out()` (line-buffered,
flushed at newlines); chrome goes to `printf` directly. So "content" and "chrome" are
already different code paths, which is why this is a tractable change rather than a
rewrite.

## The measurement: one ordinary turn, every line classified

Captured with `tests/tools/ptydrive` through a real PTY, `--accessible`, `LC_ALL=C`,
`NO_COLOR=1` — one prompt, one tool call, one approval, one reply, one `/exit`:

```
 1  jichi - interactive agent. Type /exit to quit, /help for commands.   once
 3  (mode: chat)                                                          once
 5  ^M^[[J[chat:m:6%] > ^M^[[14C^[[?2004h edit edit_me.txt ^M^[[J[chat:m:6%] > edit …
 6  assistant (m (mock) - chat - 11:01:20):
 7    working...
 9  [tokens in=20 out=5]
11  tool call: edit_file  edit_me.txt
12  @@ -1,1 +1,1 @@                                        <- CONTENT
13  -the quick brown fox                                   <- CONTENT
14  +the slow brown fox                                    <- CONTENT
16  > edit_file  edit_me.txt
17    Allow? Press y as in yes, n as in no, …              <- CONTENT (a question)
18    >   -> denied
19  tool result: edit_file failed denied
20  assistant (m (mock) - chat - 11:01:21):
21    working...
22  i was denied                                           <- CONTENT (the answer)
23  [tokens in=20 out=5]
24  ^M^[[J[chat:m:10%] > … /exit …
25  session total: 40 in / 10 out tokens                    once
```

**Four lines of content. Roughly twenty of chrome.** That ratio, not any single line, is
the defect. A sighted reader skips chrome with their eyes; a listener traverses all of it
at one word per word.

### Six findings from that capture

**F1 — the token line is pure punctuation, and it repeats per model call.**
`[tokens in=20 out=5]` is spoken roughly as *"bracket tokens in equals twenty out equals
five bracket"*. Five of those nine tokens are punctuation. It appears after **every**
model call, so a ten-call turn reads it ten times, and it is never the thing the listener
is waiting for.

**F2 — the prompt is the most-repeated string in the session, and it is read twice per
input.** `[chat:m:6%] >` is *"bracket chat colon m colon six percent bracket
greater-than"*. Line 5 shows it **twice**: once as the prompt, then again in the
committed echo after Enter. Around it sit `^[[J`, `^[[14C` and `^[[?2004h`
(bracketed-paste) — the operator has already reported one repaint being spoken aloud as
*"ware"* (step 4), so escape traffic here is not certainly inert.

**F3 — the tool name is announced three times.** Line 11 `tool call: edit_file
edit_me.txt`, line 16 `> edit_file  edit_me.txt`, then the approval prompt names it
again. One event, three announcements.

**F4 — a wall-clock timestamp on every assistant message.** `11:01:20`, spoken in full,
on every reply. M184 gave the header an accessible branch and kept the timestamp and the
middot separators (`·` reads as *"middle dot"* under a UTF-8 locale).

**F5 — `-> denied` is an arrow plus a word.** Line 18 is `  >   -> denied`: the
information is *denied*; the rest is a prompt glyph and an arrow.

**F6 — the volume finding, restated with its consequence.** `tool result: … ok` is
followed by the entire file body (step 2 of the protocol), which pushed the approval
question out of reach: *"it scrolls out of sight in the terminal, I have to scroll back
and forth."* This is the one item here that is a **design** question rather than a
wording question, and it is treated separately in §4.

## 1. Principles

1. **Words, never symbols, in chrome.** If a listener must hear it, spell it as prose.
   Brackets, `=`, `·`, `->`, `%` are visual compression and cost more than they save
   when linearised.
2. **Say each thing once.** Three announcements of one tool call is three times the
   duration and no extra information.
3. **Content before metadata.** Nothing that describes a reply should be read before the
   reply. Token counts are the clearest case: they are an accounting detail read at the
   exact moment the listener wants the answer.
4. **Available on demand beats always spoken.** jichi already has `/context`, `/cost`
   and `/tokens`-shaped commands. Chrome that exists "so you can see it without asking"
   is a *visual* affordance — it costs a glance and it costs a listener a sentence.
5. **Threshold, don't narrate.** A context budget at 6% is not news. At 85% it is. Speak
   the number when it changes something.
6. **Never touch content.** §Principles ends where the model's bytes begin.

## 2. Per-line design

Each row is a decision, with what it rejects.

### 2.1 The token line (F1) — **a sentence.** *(This section was wrong; the operator corrected it.)*

```
now:  [tokens in=4,946 out=37]
new:  4,946 input tokens used, and 37 output tokens used.
```

**What this section said first, and why it was wrong.** It proposed *suppressing* the
line and exposing the numbers through a command. The operator's answer:

> *"I think we need to use phrases, and sentences: 100 input tokens used, and 30 output
> tokens used."*

That is a better principle than the one I had, and the difference matters. I had
diagnosed a **volume** problem — the line repeats after every model call — and prescribed
removal. The reported problem was a **grammar** problem: a listener gets no sentence to
parse, only a label and two `=` signs. Prose fixes the reported defect *and keeps the
information*, where suppression would have solved a complaint nobody made by discarding
something somebody might want.

The general lesson, which belongs in this document rather than in a commit message:
**when a reader reports that something is hard to understand, the first move is to make
it comprehensible, not to remove it.** Removal is a volume decision and needs a volume
measurement.

**Still rejected: keeping it behind a config flag defaulting on.** A default that must be
turned off is the same defect with an extra step.

**Rejected: keeping it behind a config flag defaulting on.** A default that must be
turned off is the same defect with an extra step, and the population that would find the
switch is exactly the population being harmed.

**How the information stays reachable:** the running total is already in the prompt's
status segment and in `/cost`; the per-call figure lands in the session JSONL and in
`--output json`, which is where a person who wants per-call accounting is already
looking. **Open question O1:** should a `/tokens` command print the last call's figures?
Cheap to add, and it converts an always-spoken line into an on-request one.

### 2.2 The prompt (F2) — **DONE at M562**, with two corrections to what follows

Shipped as `chat > `, and `chat, 85 percent full > ` at the threshold. Two places where the
sketch below was adjusted once it met the code:

- **The threshold is 80%, not the amber point.** 80 is where compaction actually fires and
  rewrites the conversation, and it is the figure the red badge already uses — so this is
  existing policy expressed for a listener rather than a new one.
- **The arrow is kept.** It is the readiness cue, and it is one symbol against the five
  removed. Whether even that earns its place is a listening question.

It also nearly broke `tests/e2e/redraw.py`, which counts `[chat` to assert the prompt appears
once and had just been extended to run in both modes: a bracket-free prompt counts **zero**.
The marker is now passed in per mode. *A test that greps for one mode's rendering covers one
mode.*

### 2.2 The prompt (F2) — the original sketch

```
now:  [chat·chat·2%] ›
new:  chat >                       (the common case)
      chat, 85 percent full >      (only at or above the warn threshold)
```

The mode is kept because it is the one segment that changes what a keypress *does* —
`auto` acts without asking. The model name is dropped from the accessible prompt: it is
announced on every reply already (F4's header), so the prompt is the second place it is
said and the more frequent one. The percentage appears only when it crosses the
threshold the amber/red colour already uses — the colour *is* the sighted threshold, so
this is the same rule expressed for a listener rather than a new policy.

**Rejected: keeping the model in the prompt.** It is the answer to "which model am I
talking to", which the header answers every turn.

**Rejected: dropping the percentage entirely.** It is the only warning a listener gets
before compaction fires, and compaction changes the conversation.

**Rejected: a spoken "ready" word.** Readline already signals readiness by reading the
prompt; adding a word makes the most frequent string longer.

**Not decided here — the committed echo.** Line 5 reads the prompt a second time after
Enter, because the editor repaints the committed line. That is the same repaint
mechanism as the step-4 ghost finding and it belongs with that work, not here.
**Open question O2.**

### 2.3 The tool announcement (F3) — **one line, no duplicate**

```
now:  tool call: edit_file  edit_me.txt          … then …
      > edit_file  edit_me.txt
      Allow? Press y as in yes, …
new:  tool call: edit_file  edit_me.txt
      Allow? Press y as in yes, …
```

The `> edit_file  edit_me.txt` line exists to head the approval block visually. In
accessible mode the approval question names the tool in prose already, so the glyph line
is a third announcement. **Rejected: dropping the first line instead** — `tool call:` is
the M184 role label, and the role label is what makes the transcript linear.

### 2.4 The assistant header (F4) — **drop the timestamp, separators to words**

```
now:  assistant (m (mock) · chat · 11:01:20):
new:  assistant, chat:
      assistant, chat, using qwen3-coder-next:   (only when the model CHANGED)
```

**Rejected: keeping the timestamp.** It is the strongest candidate for "useful to
someone", and it is read on every single message. A transcript timestamp belongs in the
session log, which has one. **Open question O3** — a config knob for listeners who *do*
want it, defaulting off.

**Rejected: dropping the model unconditionally.** Model identity matters exactly when it
changes — routing, fallback, `/model` — and a fallback the listener never hears about is
a fence they cannot audit. Announced on change only.

### 2.5 The decision echo (F5) — **the word, not the arrow**

```
now:    >   -> denied
new:  denied
```

Same for `allowed`, `allowed (always this session)`, `allowed (edited)`. The glyph and
arrow are alignment, and alignment is a visual service.

### 2.6 The wisdom line — **suppress in accessible mode**

`print_wisdom` prints a rotating proverb before each prompt. It has no accessible branch
today. It is decoration by its own description, and it sits directly in front of the
thing the listener is waiting to interact with. **Rejected: keeping it** — a listener who
wants it can have it via a config knob; the default should not spend a sentence per turn
on ornament.

### 2.7 The `/status` command — **the counterweight**

Everything above removes something. One command should put it all back:

```
/status  ->  chat mode, model qwen3-coder-next, context 6 percent of 128 thousand,
             12,480 tokens in, 3,105 out, no cost recorded, 2 background jobs
```

**This is what makes the removals honest.** Suppressing a number is only acceptable if
asking for it is one short command. The name may want to be `/state` or fold into the
existing `/context`; **open question O4.**

## 3. What is deliberately NOT touched

- **Model text, file bodies, diffs, code, tool output bytes.** The operator's constraint,
  and the reason this design is safe to make aggressive elsewhere.
- **The approval prompt's wording.** Settled by M551/M552 and measured by ear twice.
- **Sighted rendering.** Every change is branched on `accessible`, as M551 was, with a
  control check proving the sighted form survived. That pattern is now three drivers
  deep (`accessible.sh`, `privileged.sh`, `kinetic.sh`) and should be four.
- **Colour, glyphs, the spinner.** Already handled by M118/M310.

## 4. The volume finding (F6) — a separate, harder question

`tool result: read_file ok` followed by the file's entire body is not a wording problem
and does not belong in the same milestone. The options, none of them free:

**First, a correction to this section's premise.** The body is **already bounded**:
`cb_tool_result` prints results up to 360 bytes in full, and larger ones as head 240 +
tail 100 with a byte count between. The operator's step-2 finding was a nine-line file
*under* that bound, so "the entire file body" is what happens below 360 bytes, not in
general. The open question is therefore narrower and better posed: **is 360 bytes the
right bound for a listener**, when it is roughly sixty spoken words?

| option | cost |
|---|---|
| announce a summary (`tool result: read_file ok, 9 lines`) and suppress the body | the listener loses the ability to hear what the model saw — and *"what did it actually read"* is a real question |
| print the body but bound it (first N lines, then `… 240 more lines, /last to hear it`) | a bound is a policy; N is arbitrary and will be wrong |
| keep it, add `/quiet-tools` | opt-in, so the default stays painful |
| reorder: the model's *reason* before the tool output | helps, does not reduce volume |

**Recommendation: measure before choosing.** Instrument how many lines of tool output a
typical session emits, and how often the operator actually needed the body. That is a
`docs/analysis/` measurement, not a design decision to make from a chair — and this
project's own rule is *measure the population before building a gate.*

## 4b. What shipped as M553, and the two extras the transcript revealed

Implemented: the token line, the session total, the assistant header, the tool-call
line, the tool-result line, and the decision echo — all as prose, all branched on
`accessible`, all with a sighted control check.

**Two additions came from reading the resulting transcript rather than from this
design**, which is the argument for looking at output instead of only at code:

| found | fix |
|---|---|
| `  >   denied.` — the approval prompt's **input arrow** was still there, spoken as *"greater than"* in front of the answer | no arrow in accessible mode; the question already says jichi is waiting |
| the tool was announced **three** times (role line, glyph line, approval question) | the glyph line goes in accessible mode; the M184 role label is the one worth keeping, because it is what makes the transcript linear |

**And a defect in my own new test**, caught by perturbing it rather than by reading it.
Check 13 asserts the *sighted* token line is unchanged, and read only the colour arm —
`accessible.sh` arm `a` unsets `NO_COLOR` on purpose so check 2 can watch the spinner
animate. `print_token_line` has **three** branches (colour, accessible, plain), so
rewriting the *plain* branch to prose left check 13 green: a sighted user on a
`NO_COLOR` terminal would have silently received the accessible rendering. The check now
reads two captures and covers all three branches. **A control arm that exercises one of
three code paths is not a control.**

## 5. Staging

Each stage is independently shippable, gated, and reversible. Ordered by
**benefit ÷ risk**, and deliberately not as one commit.

| stage | change | risk | why here |
|---|---|---|---|
| **S1** | token line suppressed; session total worded; decision echo worded (§2.1, §2.5) | very low — pure output, one branch each | the two loudest repeaters, no interaction with the line editor |
| **S2** | assistant header: timestamp out, separators to words, model on change (§2.4) | low | per-message, still pure output |
| **S3** | duplicate tool line removed; wisdom suppressed (§2.3, §2.6) | low | needs care that the approval block still reads as a block |
| **S4** | `/status` (§2.7) | low, additive | must land **with or before** S1–S3 in the docs, since it is what justifies them |
| **S5** | the prompt (§2.2) | **medium** — this string is what the line editor repaints, so it touches the editing path M362/M549 tuned | biggest single win, highest chance of a regression; last on purpose |
| **S6** | the committed-echo repaint (O2) and the ghost suggestion (step 4) | medium | one milestone, since both are `render()` repaints |
| **later** | the volume finding (§4) | — | blocked on a measurement, not on a decision |

## 6. How each stage is gated

The pattern is already established and should be copied rather than reinvented:

1. **A behavioural check per change, in both arms** — accessible *and* a sighted control
   proving the visual form survived. `accessible.sh` checks 10/11 are the template.
2. **Perturb per check**, not per driver. Three of the thirteen checks written for
   M551/M552 were wrong first, and every one was caught by a perturbation, a restore, or
   a denominator — none by review.
3. **A denominator per driver**, naming something only the surface under test can
   produce. Two of those three failures were denominators satisfied by the mock's own
   reply text or by the other arm's rendering.
4. **Extend `prompt_keys_lint.sh`, or write its sibling.** The lint that pins
   "every bracket prompt has a spoken counterpart" is the right shape for "every chrome
   line has an accessible branch". Measure the population first: the count is small
   (this page enumerates it) so a lint fits.
5. **Assert the property, not the prose** — `test_msg.c` pins `" a as in a"` rather than
   the sentence, precisely so a future rewording is not a test failure.
6. **And then somebody listens.** This is the one that cannot be automated and the one
   this whole document exists because of: M551's fix passed 12,791 unit checks and 1,409
   smoke checks while hiding the key it was written to expose. **A fix designed by
   someone who cannot hear the result is a hypothesis.**

## 7. Open questions for the operator

| # | question | my recommendation |
|---|---|---|
| **O1** | Should a `/tokens` command print the last call's counts, or is the session total enough? | add it — it costs a few lines and converts an always-spoken line into an on-request one |
| **O2** | The committed echo reads the prompt a second time after Enter. Fix it here, or with the step-4 ghost repaint? | with the ghost — same mechanism, same risk profile |
| **O3** | A config knob to keep the per-message timestamp for listeners who want it? | yes, defaulting **off**; it is cheap and it makes the removal reversible without a rebuild |
| **O4** | `/status` as a new command, or fold into `/context`? | new command. `/context` is about the budget; this is about the session |
| **O5** | Should any of this apply **outside** accessible mode? | no. The sighted rendering is measured, liked, and not the thing that hurts |
