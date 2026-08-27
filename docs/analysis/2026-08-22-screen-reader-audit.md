# Screen-reader audit: the manual protocol, run at last

*Record sheet for [`ACCESSIBILITY.md`](../ACCESSIBILITY.md) §"The manual
screen-reader test protocol". The environment below was measured 2026-08-22; the
per-step results are the human half and are filled in by whoever listens.*

**Why this file exists.** The eight automated checks in
`tests/smoke/accessible.sh` prove what jichi *emits* — role labels, a static
working line, zero SGR bytes under `NO_COLOR` + C locale, no escape bytes in
headless stdout. They cannot prove a reader **speaks it well**. That half has been
outstanding since the protocol was written, for one reason: no machine in the
matrix had a screen reader. The 2026-08-10 audit box had `espeak`, `spd-say` and
`brltty` and **no reader at all**; the 2026-08-20 bench had everything except
`espeakup` and `fenrir`.

That blocker is gone. This is the first sitting where the console path is live.

## Environment (measured, 2026-08-22)

| | |
|---|---|
| Reader | **fenrir 1.9.8-1ubuntu0.1** via speech-dispatcher (console path). **espeakup was tried first and abandoned** — see below |
| TTS | `libespeak-ng1` **1.51+dfsg-12build1** with `espeak-ng-data`, reached via **speech-dispatcher 0.12** (+ its `-espeak-ng` module). Note: **no standalone `espeak-ng` binary** — harmless for espeakup, which links the library, but it is exactly why fenrir's default `genericDriver` (which shells out to `espeak`) is silent here |
| Kernel modules | `speakup` **192512** and `speakup_soft` **16384**, both loaded |
| Service | `fenrir` **active + enabled** (`fenrir-daemon`), configured from the shipped `speech-dispatcher.settings.conf`. `espeakup` also active, and left configured — it speaks but truncates |
| Terminal | Linux virtual console (Ctrl-Alt-F3) |
| Also available | Orca **46.1-1ubuntu1** (desktop path, `gnome-terminal`), `brltty` **6.6-4ubuntu5**, `espeakup` **1:0.90-13build2**, `spd-say`, `sox` (fenrir chimes) |
| Audio | PipeWire **1.0.5** / wireplumber **0.4.17**, quantum forced to 1024 @48kHz. Four cards: HDMI, Behringer UMC204HD, onboard, Plantronics headset — the last two held by PipeWire. A `Dummy` sink acts as a hub whose monitor loops into the headset |
| jichi | **0.9.0**, build recorded at run time |
| Model | `jlu/qwen3-coder-next` via the HRZ gateway — verified reachable from the prepared session before the sitting |

**Why fenrir and not espeakup, measured rather than assumed.** Four obstacles stood
between a fully-installed stack and one spoken word; all four are now written up in
[`ACCESSIBILITY.md`](../ACCESSIBILITY.md) under *"Getting a reader to actually
speak"*. In order:

1. **espeakup is a root service doing its own audio** and never sees the desktop's
   PipeWire session. Fixed with `PULSE_SERVER=unix:/run/user/1000/pulse/native` in
   `/etc/default/espeakup` — it links `libpulse`, and the socket happened to be
   world-writable.
2. **`ALSA_CARD` was the wrong knob.** The unit ships it empty, which means ALSA's
   raw `default` — card 0, the GPU's HDMI. And the two cards actually in use were
   **held by PipeWire** (`fuser /dev/snd/pcmC*D0p`: one holder each), so a raw-ALSA
   client could not have opened them anyway.
3. **The `Dummy` loopback hub truncated it.** `pactl` reported
   `Latency: 0 usec, configured 0 usec`, and speech arrived **cut short** — still cut
   short at `PULSE_LATENCY_MSEC=500`. Half a second of client buffer rules out the
   client and implicates the hop.
4. **fenrir's shipped default was silent too**, for a different reason:
   `driver=genericDriver` shells out to `espeak`, and Ubuntu packages
   `libespeak-ng1` **without that binary**. Its own
   `speech-dispatcher.settings.conf` is the fix.

`spd-say` was the command that localised all of it: it spoke, which proved the audio
path sound and left only the reader to point at it. That is why fenrir won — it speaks
*through* speech-dispatcher, already wired into this machine's graph
(`speech-dispatcher-espeak-ng -> Dummy:playback -> headset`), while espeakup does its
own audio and fought the routing.

**Also corrected:** the protocol said `pip install fenrir-screenreader`, which PEP 668
refuses on Ubuntu 24.04 — and fenrir is in `apt` (1.9.8-1ubuntu0.1).

## The commands

```sh
# once, from the jichi checkout — prepares a THROWAWAY workspace, a config and a
# run.sh; touches nothing of yours
JICHI_A11Y_KEY=/path/to/keyfile sh scripts/a11y-session.sh

# then Ctrl-Alt-F3, log in, and:
/tmp/jichi_a11y.<id>/run.sh          # starts in CHAT mode: step 5 needs a real
                                     # approval prompt, which --auto would skip
```

Step 6 wants the same session **without** `--accessible`; there is no
`--no-accessible`, so run `jichi --config <ws>/config.json` plain.

## Results

Fill `pass` / `fail` / `note`. A "fail" here is a finding, not a defeat — the
protocol exists because nobody has heard this yet.

| # | What to listen for | Result | Notes |
|---|---|---|---|
| 1 | Typing echoes **one character at a time** (M362), not the whole line per keystroke. Backspace likewise, one deletion at a time | **PASS** | Verified against a *control*: with Orca's own key echo off, typing at the shell prompt was silent and Backspace spoke one character. jichi then sounded "roughly like the shell" — so its per-keystroke change is as small as a normal line editor's. **Note the mechanism:** Orca always does the speaking; what accessible mode changes is how much text changes per keystroke. M362 works. |
| 2 | In order: `assistant (model - mode - time):`, **one** `working...` (not a stream of frames), `tool call: <name>`, `tool result: <name> ok`, then the reply read linearly | **PARTIAL — finding** | Labels and order correct: `assistant (model · mode · time):`, one `working...`, `tool call: read_file <path>`, `tool result: read_file ok`. **But the tool result is followed by the ENTIRE file body, `cat -n` line numbers and all**, which Orca reads aloud line by line. See "The volume finding" below. |
| 3 | A fenced code answer arrives as **plain lines** (add `--no-color` if the reader speaks escape codes) | **PARTIAL — improved by M549** | Before the fix: unfollowable. The operator's words, with markdown both on and off, were "it read every single character... very annoying to follow along". Cause was NOT colour or syntax highlighting (identical with `/markdown` off) but **streaming granularity** — see M549 below. After the fix: *"more fluent, less stutter."* So jichi's half is fixed and a residue remains, most likely reader-side (Orca `speakBlankLines = True`, and punctuation level SOME, which speaks a lot in code where symbols *are* the content). |
| 4 | `Ctrl-G` on a half-typed line: is the ghost suggestion announced? | **FINDING — and the step's own expectation was stale** | The protocol said "visual-only today"; M462 had already shipped a spoken form. But it is guarded on `config.voice`, which was **off** in this session (no audio-role model), so it could not fire. What the operator actually heard was `"ware"` — the tail of the line, because `render_ghost` calls `render(e, prompt)` first: a **full line repaint** the reader announces. The suggestion itself is never conveyed. See "The voice-mode pattern" below. |
| 5 | An approval prompt in chat mode: the question **and** the `[y]/[n]/[a]/[e]/[v]` key list spoken **before** the editor waits; the chosen key acted on without echo | **PASS on the fence; two rounds of FINDING on the form** | *"I heard the questions, and the choices."* The safety assertion holds: both are spoken before jichi waits. **But**: *"every single character was read, too fast, and the options were unintelligible with the brackets `[a]`, and so on"* — and the prompt scrolled out of sight behind the tool output and the diff. Fixed as **M551**, then **re-tested by ear and found still wrong**: *"better, and more useful. The single vowels a, and e are difficult to make out when the options are read."* Fixed again as **M552**. See "The bracket finding" and "The second listening test" below. |
| 6 | The same as 1–2 **without** `--accessible` — spinner chatter and per-key line re-announcement. The honest baseline, and the proof the mode earns its keep | **Answered mechanically, not by ear** | `tests/smoke/accessible.sh` measures the differential from real PTY captures in both arms: **37 vs 4** `ESC[J` full-line repaints, **7 vs 2** `working` frames, and (M551) two different prompt renderings. That is the proof the mode earns its keep. It is **not** the listened baseline this step asks for, and it is recorded as the weaker thing it is: nobody has sat through the default mode with a reader running. |
| 7 | `/voice on` + `/listen` per [`VOICE.md`](../VOICE.md) — needs an audio-role model and `sound.play`. Its honest limits (no barge-in, fixed-length recording) are documented there | | |

## The volume finding (step 2)

**Captured, not inferred.** `tests/tools/ptydrive` drove the same turn through a real
PTY in both modes; this is what accessible mode handed the reader:

```
assistant (chat (jlu/qwen3-coder-next) · chat · 01:07:48 AM):
  working...
[tokens in=3,966 out=34]
tool call: read_file  /tmp/jichi_a11y.ndKGve/src/greet.c
tool result: read_file ok      1	#include <stdio.h>
     2	
     3	/* A deliberately small file: step 1 of the protocol reads it, step 3 asks for a
     4	 * code answer about it, and step 5's approval prompt offers to edit it. */
     5	int main(void)
     ...
```

The labels and their order are **exactly right** — that is M184 working. What follows
the label is the **entire tool output**, and for `read_file` that is the whole file
with its `cat -n` gutter. A nine-line fixture is survivable; a five-hundred-line
source file is five hundred numbered lines spoken aloud, with no way to skip ahead.

**This is NOT specific to accessible mode.** Default mode prints the same body
(measured: 7,234 bytes against accessible's 1,821 — accessible mode is far leaner
overall, having dropped spinner frames and colour, and it prints the body exactly
once, as default does). So the defect is not that accessible mode adds noise. It is
that:

> **A sighted user skims past tool output. A listener cannot.** Accessible mode fixed
> the repaint problem (M362) and the labelling problem (M184). It did not address the
> *volume* problem — and volume is the one that only matters to a listener.

**It is a defect against this project's own stated intent, not merely a preference.**
The protocol above asks the listener to hear *"`tool result: <name> ok`, then the
reply text read linearly"*. Full stop. Whoever wrote that expected a terse line. The
implementation emits the label plus the body.

**Why no automated check caught it.** All eight checks in `accessible.sh` pass. They
measure what jichi *emits* — role labels, frame counts, escape bytes, glyph fallback
— and none of them measures **how much a listener has to sit through**. That is not
an oversight in those checks; it is the half of accessibility a machine cannot judge,
which is the entire premise of this protocol. **This is the first finding it has ever
produced.**

**Not fixed here, because the fix is a design question.** The obvious move -- cap the
body in accessible mode and say `tool result: read_file ok (9 lines)` -- trades away
content a blind developer may genuinely want. The real difference is **control**: a
sighted reader chooses where to look, a listener receives serially. So the answer is
probably a cap plus a way to ask for the rest, and that belongs in a ROADMAP entry
with rejected alternatives rather than in a footnote here.

## The voice-mode pattern (three instances, one sitting)

**Every accessibility fix in this codebase has landed inside jichi's own `voice` mode,
and `voice` is not what a screen-reader user runs.** Three instances, found in one
morning:

| | the reasoning, already written down | where the fix landed |
|---|---|---|
| **M303** | `voice_buf` exists because *"synthesising each delta would stutter, and TTS needs a whole sentence to prosody it"* | jichi's own TTS. External readers still got 3.5-character fragments — **fixed as M549** |
| **M462** | *"Ghost text is DIM GREY OVERLAY TEXT, which is the one rendering a screen reader is least likely to convey"* | `if (... && c->app->config.voice) jc_voice_say(...)` — **behind the voice flag** |
| **M184/M362** | labels and incremental echo — the two that *did* land in accessible mode | and they are the two the protocol's step 1 and step 2 passed on |
| **M303 again** | the voice branch's own wording, *"Press y for yes, n for no, a for always"* — the speech-shaped form, written correctly | twenty lines **below** the `printf` that rendered `[y]es  [n]o  [a]lways` to the terminal, in the same function. **Fourth instance, fixed as M551** |

M462's comment states the general rule and then the patch violates it:

> *"It is not enough to ship a voice mode and leave the visual-only surfaces visual:
> that is how a feature becomes inaccessible without anyone deciding it should be."*

That sentence is correct, was written by the person fixing it, and the fix is guarded
on `config.voice`. **The rule was known, quotable, and violated by the line it was
written about** -- the same sentence this project already recorded about `craft_ab`'s
priced default at M544.

**The distinction that keeps being missed:** `voice` is jichi *speaking*; accessible
mode is jichi *being readable by someone else's reader*. They are different consumers
with different needs, and a fix for one is not a fix for the other. `--accessible` is
the flag a screen-reader user passes; `voice` is a feature they may never enable, and
on this bench could not (no audio-role model).

**The fix M549 used, generalised:** turn the invisible thing into ordinary labelled
text, which is exactly what M184 did for tool calls. For Ctrl-G that means accessible
mode emitting `suggestion: <text>` as a real line rather than dim overlay. Not done
here -- it touches the line editor during editing, which is riskier than M549's output
path, and it deserves its own milestone rather than a third change in one morning.

## The bracket finding (step 5), and why eleven automated checks missed it

The approval prompt read:

```
  Allow? [y]es  [n]o  [a]lways  [e]dit  [v]iew
```

**That is a visual affordance: the key is shown *inside* the word it names.** A sighted
reader sees `y` highlighted within `yes` and needs no explanation. Read aloud it is not
a prompt but a spelling exercise — a reader announces roughly *"bracket y bracket e s,
bracket n bracket o, bracket a bracket l w a y s"*. The operator's report, unprompted
and exact:

> *"every single character was read, too fast, and the options were unintelligible
> with the brackets `[a]`, and so on"*

**Both halves of that sentence are one defect.** "Every single character was read" is
not a granularity problem left over from M549 — it is the literal consequence of a
string built out of single letters and punctuation. Fixing the form fixes both.

**It was three prompts.** The population was measured rather than assumed — lines in
`src/` naming both `[y]` and `[n]`, which is **11 lines in exactly 2 files**:

| site | a keypress there authorises | state after the first draft of the fix |
|---|---|---|
| `jc_msg.c` catalog | an edit to a file | fixed |
| `jc_tui.c:1251` | **a `sudo`** | still spelling itself out |
| `jc_tui.c:1280` | **a physical actuation** | still spelling itself out |

The first draft fixed the site that was *reported* and left the two that outrank it —
inside the milestone whose whole subject is a rule applied in one place and not the
neighbouring one. All three are fixed now. The two red ones branch inline rather than
through the catalog: they are not localized today, and putting them in the five language
tables is a separate decision, not a side effect of this one.

**What M551 changed:** a second catalog entry, `JC_MSG_ALLOW_PROMPT_ACC`, selected when
`--accessible` is on:

```
  Allow? Press y for yes, n for no, a for always, e to edit, v to view.
```

The sighted rendering is untouched — this is a branch, not a replacement, and
`tests/smoke/accessible.sh` check 11 exists to keep it that way. German, Spanish,
Japanese and Chinese get the **existing reviewed translation with the brackets
removed and nothing else changed**: same vocabulary, same order, each key letter
space-delimited. That is a limit, stated rather than glossed — no screen-reader test
has been run in those languages, so re-punctuating a reviewed string is a change that
can be defended and inventing a new sentence is not.

### Twenty-one checks across three drivers, and not one had ever rendered a prompt

This is the part worth keeping. Three drivers covered these three surfaces, all three
are *good* drivers, and none of them had ever displayed the prompt it was about:

| driver | checks | why the prompt was never reached |
|---|---|---|
| `accessible.sh` | 8 | every arm passed `--auto` |
| `privileged.sh` | 5 | every arm headless `--auto`; the confirm needs posture `ask` **and** no `--auto` |
| `kinetic.sh` | 8 | same — six scenarios, every one unattended |

`--auto` in a fixture reads as a *convenience*: it makes the run deterministic and
removes a place the harness would have to answer something. What it actually removes is
a fence. Twenty-one checks about a file edit, a privilege escalation and a physical
motion, and the keypress that authorises each was never displayed to anything.

**Thirteen checks were added**, each perturbed independently, and **three of mine were
wrong first** — every one caught by a perturbation, a restore, or a denominator, and
none by reading:

| version | what it matched | how it failed | caught by |
|---|---|---|---|
| first | a bare `denied` | the mock's own closing reply is the text *"i was denied"* — with the tool swapped for a read-only one, no prompt rendered and the denominator still passed | perturbation D |
| second | `tool result: edit_file failed denied` | that string exists only in the **accessible** log; the role-labelled transcript is itself an accessible-mode feature, and default mode renders the same event as `x error denied` | the restored run going red while 10 and 11 were green |
| third | `Allow?` **and** `-> denied` in both arms | — | verified against both recorded captures plus two negative controls |

The second failure is worth naming on its own: **a pattern lifted from one arm's capture
and applied to both**. Checks 10 and 11 exist precisely because the two arms render
differently, and I then wrote a denominator that assumed they render the same.

**The third mistake was in `privileged.sh`, and it is the worst of them.** `jc_agent.c`
says the privileged check is *"evaluated BELOW the verdict"*; I read that as *the
ordinary tool approval prompts first* and wrote a script that sends `y` then `n`. The
order is the reverse — the **privileged prompt comes first** — so that `y` **granted the
sudo escalation**, and what refused the command was the tool prompt afterwards. Two of
the three new checks were green, because the prompt *had* rendered correctly in both
modes; only the denominator could see that it had been answered wrong. A driver that
grants privilege in order to test refusing it is a bad driver even when it passes, and
the fix was to read the capture instead of the comment.

**A tool-level trap, recorded because the symptom pointed nowhere.** Python's
`io.open(path, encoding='utf-8')` reads in universal-newline mode, so a lone `CR` in a
file is rewritten as `\n` when the text is written back. That silently split a ptydrive
script's `send "run it\r"` across two lines; `pd_script_parse` rejected it, and what I
saw was not a parse error but a **missing log file** and three red checks. Verified with
a two-line probe rather than guessed at, and every edit since passes `newline=''`. The
cheap habit that catches it: after editing a fixture, `cat -A` the bytes you changed.

## The second listening test (step 5, round two) — and the reason this page exists

M551 shipped, the operator installed it, listened, and reported:

> *"better, and more useful. The single vowels a, and e are difficult to make out when
> the options are read."*

**Two mechanisms. The first was predictable; the second I should have caught and did
not.**

1. `y`, `n` and `v` are consonants and survived. `a` and `e` spoken alone are weak
   sounds.
2. **"a for always" is ambiguous with English grammar.** A listener cannot separate the
   letter *A* from the indefinite article, so the phrase is heard as *"…for always"* and
   the key disappears. The remedy's own wording concealed the key it was written to
   expose.

**The fix is a relation, not a phrasing:** make the key recoverable from the word beside
it, so that mishearing the letter costs nothing.

```
Allow? Press y as in yes, n as in no, a as in always, e as in edit, v as in view.
```

It costs no new vocabulary because **the keys already are the option words' initials** —
which is precisely why the bracket form worked visually. Applied to all three prompts,
and to jichi's own voice string, which said `"a for always"` through the same
synthesiser: the ambiguity is in the phrase, not in who is speaking it. That last one is
**not verified by ear** — voice mode needs an audio-role model this bench does not have —
and it is marked in the source as riding on the reasoning rather than on a test.

**It cannot be translated, and the limit is in the source rather than glossed over.**
The keys are never localised, so they are the *English* words' initials. German's
options are ja/nein/immer/bearbeiten/ansehen — `a wie immer` would be **false**, since
`a` is not the first letter of `immer`. No cue exists in de/es/ja/zh that is both correct
and in that language, and no screen-reader test has been run in any of them to design
one against. Those four keep M551's bracket-free form.

**Two corrections to my own instruments came out of this, both in the same direction —
pin the property, not the prose:**

| instrument | first draft | why it was wrong |
|---|---|---|
| `test_msg.c` | would have asserted the sentence | it asserts `" a as in a"` — the key followed by a word starting with that letter. The relation is the fix; the prose is free to change. Perturbed: reverting only the `a` cue reddens exactly 1, the whole entry exactly 5 |
| `prompt_keys_lint.sh` | anchored on the literal phrase `Press y as in yes, n as in no` | it would have gone red on a legitimate rewording. **A lint that fails when the product improves teaches people to edit the lint.** Now `Press y .*no\.` — wording-agnostic, still anchored on the period so it excludes the voice string |

### What this round is actually evidence of

**The accessibility fix had its own accessibility defect, and only a second listening
test found it.** Nothing in the gate could have: 12,791 unit checks and 1,409 smoke
checks all passed on the wording that hid the key. A fix designed by someone who cannot
hear the result is a hypothesis, and it stays one until somebody listens.

That is the operator's thesis from the sitting before, evidenced a second time — and
this time against my own remedy rather than against the original code.

## One root cause, found last (M559)

**Ten milestones treated these as separate wording problems. They are one setting.**

The operator asked a clarifying question about M555 — *was I asking how the screen reader
speaks `4.946`, or how a German would read it aloud?* — which sent me to measure the
synthesizer directly:

```
$ espeak-ng -v de -q -x 4.946
fˈiːɾ tˈaʊzənt nˈɔønhˈʊndɜt zˈɛks ʊntfˈɪɾtsɪç      # "viertausendneunhundertsechsundvierzig"
$ espeak-ng -v de -q -x 4946
fˈiːɾ tˈaʊzənt nˈɔønhˈʊndɜt zˈɛks ʊntfˈɪɾtsɪç      # identical
```

**espeak-ng reads a grouped number correctly**, in German and in English, including
`1.234.567`. So the mechanism I had written into six files — *"a punctuation mark inside a
numeral defeats the reader's number parser"* — is false of the synthesizer.

The layer is one line of Orca's configuration:

```
speakNumbersAsDigits        False
verbalizePunctuationStyle   1        <- SOME
```

At **SOME**, Orca voices embedded punctuation. It speaks the dot, splitting `4.946` into
fragments that reach espeak as `4` and `946`. The synthesizer never sees a number.

### And that one setting explains every defect in this audit

| milestone | what jichi printed | the symbol Orca voiced |
|---|---|---|
| M551 | `Allow? [y]es  [n]o  [a]lways` | `[` `]` |
| M553 | `[tokens in=20 out=5]` | `[` `]` `=` |
| M553 | `  -> denied` | `->` |
| M553 | `session total: 40 in / 10 out` | `/` |
| M554 | `[chat·chat·2%]` | `·` `%` |
| M555 | `4.946` | `.` |

**Not six findings. One:** jichi used punctuation for visual compression, and the operator's
reader was configured to speak punctuation. Every fix in this audit was the same fix —
replace a symbol with a word — arrived at six times without noticing.

### Why this matters more than the correction

It **predicts**. Any remaining punctuation-dense chrome is a defect waiting for a listener,
and the list is now enumerable rather than discovered by ear one item at a time. It also
sets the honest scope: these are defects **at punctuation level SOME**, which is not
universal — a listener at NONE would have heard none of them, and one at MOST or ALL would
hear more. Six fixes were shipped without anyone establishing which level was in play.

**And it changes what should be asked of a new tester.** Not *"does this read well?"* but
*"what is your punctuation level?"* first — because the answer determines which half of the
findings apply to them at all.

### Confirmed by ear, and how the confirmation had to be redone

**espeak-ng is innocent, and the operator verified it.** `espeak-ng -v de "4.946"` *"speaks
the complete number"* — their words, listening to it.

Getting there took a correction worth recording. My first evidence was `-x` phoneme output:

```
f'i:r t'aUz@nt_! n'OYnh'Und3t z'Eks _|Untf'IrtsIC
```

I read that as *viertausendneunhundertsechsundvierzig* and presented it as proof. The
operator's reply — *"produces gibberish"* — was correct: **that is espeak's internal
notation, and I was asking them to trust my transcription of it.** An instrument's raw output
is not evidence to someone who cannot read the format; it is a claim wearing a costume.

Two better forms, in increasing order of worth:

1. **A substring test**, machine-checkable by anyone: the output for `4.946` contains
   `t'aUz@nt` (tausend), `h'Und3t` (hundert) and `f'IrtsIC` (vierzig), and does **not**
   contain `p'UNkt`. Those words appear only if espeak parsed a number.
2. **Their ear.** `espeak-ng -v de "4.946"` with no `-q`, speaking to the audio device. One
   command, no notation, no trust required.

The second is what settled it, and it was available from the first minute.

**And the isolating test failed for an unrelated reason.** `spd-say "4.946"` was silent
because **speech-dispatcher was not running** (`pgrep -x speech-dispatcher` finds nothing;
the default sink is a headset and is `RUNNING`, so audio is fine). So that test was
*inconclusive, not negative* — a distinction worth making, since a silent result reads like
a null result.

**Where the attribution stands:** espeak-ng reads the number correctly (measured twice, heard
once). The operator heard a reader say the dot. Therefore the layer is above the synthesizer,
and `verbalizePunctuationStyle` at SOME is the strong hypothesis — still not *isolated*,
because the test that would separate the layers did not run. The fix is unaffected either
way; the attribution matters for predicting which other symbols are at risk, and the table
above gives that regardless.

## Verdict

**The operator's own conclusion is the finding, and it outranks every row in the table
above.** Written after two sittings, roughly three hours, having never used a screen
reader before:

> *"I'm not used to using a screenreader, because I can see well enough. However, this
> is a lesson for how important accessibility design is for software development, and
> that it needs to be there from the beginning: with user tests. Not as an
> afterthought."*

Everything this audit found supports it, and the shape is consistent enough to state
plainly: **every defect found here was an afterthought's residue.**

**And then it was proven a second time, against the fix.** M551's replacement wording
was itself unlistenable — *"the single vowels a, and e are difficult to make out"* —
which the gate could not see and a second listening test found in one minute. The
argument for user tests is not that authors are careless. It is that **an author cannot
perceive the channel they are not using**, and no amount of care substitutes for
somebody who is.

| defect | the code was written | for whom |
|---|---|---|
| M549 — 266 deltas for 926 characters | for a terminal, where instant echo is correct | a screen reader re-announcing each one |
| M550 — `" /exit"` became a prompt | for an eye that sees the gap after the arrow | a listener, for whom a space is nothing |
| M551 — `[y]es  [n]o`, in **three** prompts (a file edit, a `sudo`, a physical actuation) | for an eye that sees `y` highlighted in `yes` | a voice, which spells it |
| M462 — the ghost suggestion | for dim grey overlay text | a reader that conveys neither dimness nor overlay |

Not one of them is a hard problem. Every one is *the visual design, shipped unexamined
into a mode whose entire premise is that nobody is looking.* And each was found within
minutes of somebody listening — after passing a gate of 247 drivers and 1,396 checks,
including eleven written about accessible mode specifically.

**What the audit is worth, stated honestly.** Four steps passed or partly passed, three
produced findings, three of those are fixed. Against that: the protocol had never been
run at all, its own step 4 expectation was **stale**, and an eight-check driver had no
opinion about the safety prompt. A test suite written by the person who wrote the
feature tests what that person thought of — which is the argument for user tests, made
by the artefact rather than about it.

**What remains, and it is not a footnote:**

1. **The ghost suggestion** (step 4 / M462) — accessible mode should emit
   `suggestion: <text>` as labelled text. Riskier than M551: it touches the line editor
   during editing.
2. **The volume finding** (step 2) — the whole file body follows `tool result: ok`, and
   the approval prompt then scrolls out of reach behind it. The operator: *"it scrolls
   out of sight in the terminal, I have to scroll back and forth."* A design question,
   not a small fix: how much of a tool result belongs in a linear transcript.
3. **`ask_user`'s numbered options** — the same class as the brackets, unexamined:
   `1) /* Greets the user by printing 'hello'. */` is C comment syntax read aloud as
   punctuation. Not yet measured.
4. **Step 6 by ear**, and step 7 at all.
5. **A tester who actually uses a reader.** The operator does not, said so, and that is
   the honest ceiling on everything above.

## What was fixed to make this sitting possible

`scripts/a11y-session.sh` hardcoded the institutional gateway, whose config needs
a key. **Requiring an institutional key in order to verify accessibility excludes
exactly the tester who has a screen reader and no institutional account** — so it
now honours `JICHI_A11Y_CONFIG=<path>` and copies a config you already have. A
local OpenAI-compatible server needs no real key, which makes that the shortest
path to a session for an outside contributor.
