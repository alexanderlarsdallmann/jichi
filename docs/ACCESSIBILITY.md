# Accessibility

jichi is a terminal program, so it inherits the accessibility of the user's
terminal + screen reader. This page audits how well it cooperates with assistive
technology and documents the **accessible mode** (M118) plus what remains.

## Plain language

Two pages describe jichi in a deliberately plain register, for readers for whom the
dense pages are the barrier:

- **[Einfache Sprache](i18n/de/EINFACHE_SPRACHE.md)** (German) — the original.
  *Einfache Sprache* is a defined register with its own rules: short sentences, one
  idea each, no metaphor, no nested clauses, concrete words, jargon explained.
- **[Plain language](PLAIN_LANGUAGE.md)** (English) — its sibling.

They are **separate pages, not simplifications** of the existing ones. The dense
pages stay dense: that density is a feature for their audience, and flattening them
would serve neither group. Scope is deliberately narrow — what jichi is, getting
started, the modes, undo, voice, and the safety warnings — not the whole corpus.

## Accessible mode

Turn on with config `accessible: true`, the `--accessible` flag, or (interactively)
`/accessible`. It reduces motion and makes output linear and screen-reader-friendly:

- **No animated spinner.** By default a "working…" line animates in place via
  carriage-return + erase-line while the model responds; a screen reader announces
  every frame. Accessible mode prints a single static `working...` line instead.
- **Role-labelled transcript (M184).** Each assistant message begins with a
  plain `assistant (<model> - <mode> - <time>):` line, tool activity reads
  `tool call: <name> <summary>` and `tool result: <name> ok|failed` — the
  role is spoken before the content, no glyph decoding required.
- Everything else stays plain and linear (see below).

`--no-color` / `NO_COLOR` compose with it (accessible mode does not force color off,
since a screen reader strips ANSI, but you can turn it off if your terminal reads
escape codes aloud).

## Audit

**Byte-level verification (2026-08-10):** the rows below stopped being prose
claims — `tests/smoke/accessible.sh` pins them from raw PTY captures on every
`make smoke`: the spinner animates by default (7 frames in the fixture
session) and is exactly one static line per model call in accessible mode;
the role labels are on the wire; the accessible transcript under `NO_COLOR` +
a C locale contains zero SGR sequences and zero UTF-8 glyph bytes while a
UTF-8 session does render glyphs (the presence/absence pair); and headless
`-p` output carries no escape bytes at all.

| Area | Status | Notes |
|------|--------|-------|
| Color-only meaning | Good, **pinned** | `✓`/`✗`, `▸`, and status are **glyph + text**, not color alone; ASCII fallbacks (`>`, `ok`, `x`) when the locale isn't UTF-8. |
| Motion / animation | **Fixed (M118), pinned** | The spinner is the one animation; accessible mode disables it. No other blinking/moving UI. |
| Keyboard-only | Good | Fully keyboard-driven; no mouse required. Single-key prompts (`y/n/a/e/v`) are documented on their own line. |
| Screen-reader linearity | Good in accessible mode | Output is appended top-to-bottom; the only in-place rewrite (the spinner) is off. |
| **Input echo** | **Fixed (M362), pinned** | The wrap-aware editor used to repaint the whole prompt+line (cursor-up, CR, `ESC[J`, reprint — measured ~39 bytes per keystroke, 37 erase-belows in a 28-character session) on *every* key, which a screen reader re-announces as a changed line each time. In accessible mode the common case (typing or erasing single-column ASCII at the end of a non-wrapping line) now emits just the character or `\b \b`, like a cooked terminal — 4 erase-belows in the same session, all at real boundaries. Wrap crossings, mid-line edits, UTF-8 input, and ghost overlays still take the full redraw. |
| Contrast | Terminal-controlled | jichi uses the terminal's palette; dim text is used only for de-emphasis, never to carry required meaning. |
| Headless / non-visual | Excellent, **pinned** | `-p` / `--output json`/`jsonl` produce plain, structured output with no UI at all — the most accessible way to drive jichi, and scriptable with any AT. |
| Magnification / reflow | Good | Relative widths; the input redraw is wrap-aware (`TIOCGWINSZ`). |

## Recommendations for impaired users

- **Screen-reader users:** run with `--accessible`, and consider `-p`
  (headless) for a fully linear transcript, or the **ACP** server driven by an
  accessible editor.
- **Low vision:** set your terminal's font size/contrast; `--no-color` if ANSI is
  read aloud; accessible mode removes the moving spinner.
### Japanese: install a Japanese synthesizer, or the interface says "Chinese letter"

**If you read Japanese with a screen reader, do this before anything else.** Orca's default
synthesizer is `espeak-ng`, and **espeak-ng has no readings for kanji**. It announces each
ideograph as *"Chinese letter"* — so jichi's approval prompt is heard as *"Chinese letter,
Chinese letter, shimasuka"*, and the option **表示** (view the diff) is *"Chinese letter,
Chinese letter"* and nothing more. For 常に the kanji is not announced but **dropped**, so
"always" becomes *"ni"*. Kana is read correctly; jichi's Japanese catalog contains 46 kanji
characters. Measured 2026-08-23 —
[`analysis/2026-08-23-tts-japanese.md`](analysis/2026-08-23-tts-japanese.md).

**The fix is two packages and one line.** `speech-dispatcher` already ships the Open JTalk
module and its config; only the dictionary and voice are missing:

```sh
sudo apt install open-jtalk open-jtalk-mecab-naist-jdic \
                 hts-voice-nitech-jp-atr503-m001
```

then add to `/etc/speech-dispatcher/speechd.conf` (beside the other `AddModule` lines):

```
AddModule "openjtalk" "sd_openjtalk" "openjtalk.conf"
```

Then log out and back in, so speech-dispatcher restarts, and pick the Japanese voice in
Orca's preferences.

**Verified working, at the phoneme level** (2026-08-23, on the operator's machine after they
installed it). Open JTalk chooses the right reading for every word espeak-ng could not read:

| jichi's text | Open JTalk | reading |
|---|---|---|
| 表示 | `hy o o j i` | **hyōji** |
| 編集 | `h e N sh u u` | **henshū** |
| 常に | `ts u n e n i` | **tsuneni** |
| 許可しますか | `ky o k a sh i m a s U k a` | **kyoka shimasu ka** |

The capital `U` is a devoiced vowel — Open JTalk applies real Japanese phonology in ます
between voiceless consonants, which espeak-ng never attempted.

**Why this is a setup note rather than a jichi fix:** the readings were never in doubt. MeCab
has the correct, unambiguous reading for all four. Writing the catalog in kana instead was
considered and **rejected** — it would paper over a solvable configuration gap and make the
interface worse for sighted Japanese readers, who want kanji.

**Still unverified:** whether it *sounds* natural (phonemes say what will be spoken, not
whether it sounds like a person), and how NVDA, JAWS or VoiceOver behave. Both need someone
who reads Japanese.

- **Motor / keyboard-only:** everything is reachable by keyboard; approval prompts
  take a single key; `Ctrl-C` stops the current task without exiting (M107).
- **Cognitive load:** the beginner setup profile (`setup --profile beginner`, M116)
  minimizes options and turns off power-user subsystems.

## Accessibility of what you BUILD (M184)

jichi is also positioned to make *your project's* accessibility a
from-day-one concern instead of the usual post-release afterthought: every
scaffold pack (except the deliberately minimal `onboarding`) ships a
read-only **`accessibility-reviewer`** agent, the concrete
**`a11y-checklist`** skill it works from (per deliverable type: CLI output,
documentation, web UI, API errors), and an **`/a11y-review`** command — and
the scaffolded `AGENTS.md` states the rule: accessibility is reviewed
before release, not after. See docs/SCAFFOLDING.md.

## The manual screen-reader test protocol (what a machine cannot verify)

The byte-level pins above prove what jichi *emits*. Whether a screen reader
*speaks it well* can only be judged by a person listening, and the 2026-08-10
audit machine could not do that part: it has `espeak`, `spd-say`
(speech-dispatcher answers, rc 0) and `brltty` installed, but **no screen
reader** — no Orca (needs a desktop session), no fenrir, no `speakup` kernel
module — and no way to hear whether the emulated sound device actually
sounds. This section is the standing protocol for whoever runs the human
half.

**The two commands that get you there** (measured on the 2026-08-20 bench, where
everything except `espeakup` and `fenrir` was already installed):

```sh
scripts/a11y-session.sh          # readiness report + a throwaway workspace
```

That script prepares the *jichi* side — a scratch workspace, a config pointing at
a local model, and a `run.sh` that starts in **chat** mode (step 5 needs a real
approval prompt, which `--auto` would skip). It deliberately does **not** touch
your session: enabling accessibility and starting a reader are one-liners it
prints for you to run, because a tool that reconfigures someone's desktop without
asking is the opposite of an accessibility feature.

*Desktop path:*

```sh
gsettings set org.gnome.desktop.interface toolkit-accessibility true
orca &            # add its own replace switch if one is already running
```

Then open a **new** `gnome-terminal` — GTK apps read that switch at startup, and
xterm is not accessible either way. Set it back to `false` to undo.

*Console path — read the safety notes under "What is necessary" first; this path
can strand you on a TTY, and it did on 2026-08-22:*

```sh
sudo apt install fenrir            # NOT pip: see "getting a reader to speak" below
sudo modprobe speakup_soft         # only for the espeakup route
```

Then switch to a text console (Ctrl-Alt-F3) and log in.

**What is necessary:**

- **Desktop path (do this one first).** A GNOME session with **Orca** enabled and
  an AT-SPI-accessible terminal — **gnome-terminal or another VTE terminal**
  (xterm is not accessible). Orca reads new output as it appears. Two commands in,
  two commands out, and you never leave your session:

  ```sh
  gsettings set org.gnome.desktop.interface toolkit-accessibility true
  orca &                  # add its own replace switch if one is already running
  # then open a NEW gnome-terminal -- GTK reads that switch at startup
  # undo, completely:
  pkill orca; gsettings set org.gnome.desktop.interface toolkit-accessibility false
  ```

- **Console path (only if you need it).** A Linux *virtual console* with
  **fenrir** (`apt install fenrir`; needs root for `/dev/vcsa`) or the **speakup**
  modules (`modprobe speakup_soft` + `espeakup`), plus a TTS engine and audio.

  **READ THIS BEFORE YOU SWITCH.** It is the more representative path — no
  terminal emulator between jichi and the reader — and it is the one that can
  strand you. It cost a hard reboot on 2026-08-22, and **not** for the reason you
  would guess: the operator knew that `Ctrl-Alt-F2` led back. They mistyped their
  **username at the login prompt**, and could get neither forward nor out.

  - **`login` cannot be cancelled.** It ignores SIGINT by design, so `Ctrl-C` at
    the username prompt does nothing. A mistyped username must be *completed* —
    press Enter, let the password prompt fail — before you get a fresh one. There
    is no going back to edit it.
  - **A keyboard-grabbing reader may swallow the way out.** fenrir grabs the
    keyboard through its evdev driver, and it was running at the time. `evdev`
    intercepts combinations before the kernel's VT switch sees them, so
    `Ctrl-Alt-F<n>` can stop working — the documented escape route is not
    guaranteed while a console reader is live.
  - **Therefore: have a second way in, before you switch.** ssh from another
    machine, or a phone on the LAN, so `sudo systemctl stop fenrir` never requires
    the console you are stuck on. This is the one that would have prevented the
    reboot.
  - **Know the way back anyway.** `loginctl list-sessions` gives your graphical
    session's TTY; that is your `Ctrl-Alt-F<n>`, usually **tty2** (tty1 is the
    display manager). Check rather than assume.
  - **It survives reboots.** The package enables the service, so it starts again
    on the next boot. `sudo systemctl disable --now fenrir` when you are done.

  None of this is a reason to avoid the console path. It is the reason to do the
  desktop path first, and to come here only when a finding needs the emulator out
  of the picture.
- **Braille (optional):** `brltty` with a display, paired with either path.
- A build of jichi, a mock or real model endpoint, and this page open.

## Getting a reader to actually speak (measured 2026-08-22)

**The packages are the easy part, and this section exists because nothing here was
written down.** On the 2026-08-22 bench every package was already installed —
orca 46.1, fenrir 1.9.8, espeakup 0.90, brltty 6.6, speakup 3.1.6 with the soft
synth, libespeak-ng 1.51 with its data, speech-dispatcher 0.12 with its espeak-ng
module, PipeWire 1.0.5, sox. **Four separate obstacles still stood between that and
one spoken word**, and an earlier revision of this page compressed all of them into
*"that contention is the first thing to check"* — true, and unactionable.

The chain, and where it breaks:

```
console (Ctrl-Alt-F3)
  └─ speakup, synth=soft                    [kernel module — fine]
       └─ /dev/softsynth
            └─ espeakup   [a ROOT service]
                 └─ libespeak-ng
                      └─ audio out ──►  its OWN ALSA/Pulse client,
                                        NOT your desktop session
```

**1. espeakup is a root service doing its own audio, so it never sees your
session.** On a PipeWire desktop that is the whole problem. It links `libpulse`, so
it *can* be pointed at the session — the socket is typically world-writable:

```sh
# /etc/default/espeakup  (the unit reads it via EnvironmentFile=-)
PULSE_SERVER=unix:/run/user/1000/pulse/native
PULSE_SINK=<your sink name from `pactl list short sinks`>
```

**2. `ALSA_CARD` is the wrong knob here, and the unit ships it empty.** With no
`/etc/asound.conf` an empty `ALSA_CARD` means ALSA's raw `default` — card 0, which
on this bench is the GPU's HDMI output. Worse, the two cards actually being listened
through were **held by PipeWire** (`fuser /dev/snd/pcmC*D0p` showed one holder
each), so a raw-ALSA client could not have opened them at all. The free cards went
nowhere anyone was listening.

**3. A zero-latency loopback hub truncates espeakup, and more client buffer does not
fix it.** This bench routes `Dummy:monitor → headset`, and `pactl` reported the hub
at `Latency: 0 usec, configured 0 usec`. espeakup's speech arrived **cut short**, and
stayed cut short at `PULSE_LATENCY_MSEC=500` — half a second of client buffer, far
more than any plausible drain. **That rules out the client and implicates the hop.**
Worth knowing before spending an evening on latency tuning.

**4. fenrir's shipped default is silent on a system with no `espeak` binary.**
`settings.conf` uses `driver=genericDriver`, whose `genericSpeechCommand` shells out
to `espeak` — and Debian/Ubuntu package the **library** (`libespeak-ng1`) without
that binary. So fenrir starts, runs, reports `active`, and says nothing. It ships the
fix next to the problem:

```sh
sudo cp -n /etc/fenrirscreenreader/settings/settings.conf \
           /etc/fenrirscreenreader/settings/settings.conf.orig
sudo cp /etc/fenrirscreenreader/settings/speech-dispatcher.settings.conf \
        /etc/fenrirscreenreader/settings/settings.conf
sudo systemctl restart fenrir
```

### The one command that localises all of this

```sh
spd-say "speech dispatcher is audible"
```

**If that speaks, your audio path is sound and only the reader needs pointing at
it** — which makes **fenrir the better choice on a PipeWire desktop**, because it
speaks *through* speech-dispatcher rather than doing its own audio. espeakup fights
the routing; fenrir works with it. If `spd-say` is silent, fix that before touching
any reader.

And to prove speakup itself is alive, independently of any reader:

```sh
sudo sh -c 'echo "speakup is working" > /sys/accessibility/speakup/synth_direct'
```

### What generalises, and what does not

**Generalises:** a root service does not inherit a user's audio session; a packaged
library is not a packaged binary; `pip install` is refused on PEP-668 distributions
(Ubuntu 24.04) and the reader is in `apt` anyway; and `spd-say` is the cheapest way
to bisect "is it the audio or the reader".

**Does not generalise:** the specific card numbering, the `Dummy` loopback topology,
and `/run/user/1000`. Those are one bench. The *shape* of the failure is what to
carry: **the reader is usually fine and the audio routing usually is not.**

## The steps, keyed to what the driver already pins

*Setup first (the section above); a reader that cannot speak fails every step below
for a reason that has nothing to do with jichi.*

1. Start `jichi --accessible` in the reader's terminal. Type a prompt —
   **listen:** each character should be echoed once as typed (M362), not the
   whole line re-read per keystroke. Then backspace a few characters —
   the same, one deletion at a time.
2. Submit a prompt that triggers a tool call. **Listen for, in order:**
   `assistant (model - mode - time):`, one `working...` (once, not a stream
   of frames), `tool call: <name> ...`, `tool result: <name> ok`, then the
   reply text read linearly.
3. Ask for a code answer. **Listen:** fenced code should arrive as plain
   lines (with `--no-color` if the reader speaks escape codes).
4. Press `Ctrl-G` on a half-typed line. **Listen:** the ghost suggestion is
   *visual-only* today — note whether your reader announces it at all (it
   likely will not; Tab still accepts it). This is a known gap, not a bug.
5. Trigger an approval prompt (a mutating tool in chat mode). **Listen:** the
   question and the key list must be spoken before the editor waits; the chosen
   key is acted on without echo. **In accessible mode the key list is
   bracket-free** — `Allow? Press y for yes, n for no, a for always, e to edit,
   v to view.` — because the sighted form shows the key *inside* the word it
   names (`[y]es`), and a reader announces that as *"bracket y bracket e s"*.
   Measured 2026-08-23; the operator's report was *"the options were
   unintelligible with the brackets"* (M551). The sighted rendering is
   unchanged: this is a branch on `--accessible`, and
   `tests/smoke/accessible.sh` checks 10 and 11 pin both forms.

   **The same applies to the two prompts that outrank it**, and both were fixed
   in the same milestone: `Run this with elevated privilege?` (a `sudo`) and
   `Allow this physical actuation?` (a kinetic tool) also read
   `Press y for yes, n for no.` under `--accessible`. There is deliberately no
   `a` on either: no `always` grant may satisfy a privileged or kinetic confirm,
   which is the point of both gates ([`AGENT_MODES.md`](AGENT_MODES.md)). If you
   can trigger one of these,
   listen to it: they are pinned by `tests/smoke/privileged.sh` checks 6–8 and
   `tests/smoke/kinetic.sh` checks 9–11, but nobody has heard them.
6. Repeat 1–2 **without** `--accessible` to hear the difference the mode
   makes (spinner chatter, per-key line re-announcement) — the honest
   baseline, and the proof the mode matters.
7. For a screen-less workflow, test `/voice on` + `/listen` per
   [VOICE.md](VOICE.md) (needs an audio-role model + `sound.play`; its honest
   limits — no barge-in, fixed-length recording — are documented there).

**Record the result** as a dated note in `docs/analysis/` naming the reader,
its version, the terminal, and per-step pass/notes — the same register
discipline as every other measurement in this project. The sheet for the
2026-08-22 sitting, with the environment pre-filled, is
[`analysis/2026-08-22-screen-reader-audit.md`](analysis/2026-08-22-screen-reader-audit.md).

## Answering the approval prompt, and how a run ends (M573)

*Written because the operator asked the question that showed nobody knew the answer: **"The
user, or agent has to know about the three refusals rule. When is the count reset?"** It was
recorded only in `ROADMAP.md` and `ANECDOTES.md` — engineering records, not somewhere a user or
a model reads. A rule that ends your turn belongs where you can find it **before** it happens.*

**The keys.** Every one works in every language, and each decision has a letter and a digit:

| | | |
|---|---|---|
| `y` `1` | yes | run this call |
| `n` `0` | no | refuse this call |
| `a` `8` | always | allow this tool for the rest of the session |
| `e` `3` | edit | change the arguments, then decide |
| `v` `5` | view | show the whole call, then ask again — **decides nothing** (`tests/smoke/view_key.sh` check 5) |

English advertises the letters (`y`/yes explains itself); German advertises the digits (`a` is
not the first letter of `immer`, so no "as in" cue could ever be correct — M564/M568). Both sets
are accepted regardless of which is announced.

**Any other key does nothing** and the prompt is read again, up to three times, then refuses
(`tests/smoke/approval_keys.sh` check 2). A slip is not an answer (M565): *"if 0 means no, 0
means no"*. That the digits actually authorise a call is `approval_keys.sh` check 3.

**Three ways a run ends, in increasing deliberateness:**

1. **Three refusals in a row** end the turn by themselves (`tests/smoke/deny_stops.sh` check 3),
   and jichi says so (`deny_stops.sh` check 2):
   *"Denied repeatedly, so this run is stopping. Nothing was changed."* The count is
   **independent of which tool asked** — a model that varies its request between `edit_file`,
   `apply_patch` and `run_terminal_command` does not get a fresh allowance for each (M572; before
   that fix, one report ran to **ten** prompts for a single rename).
2. **Ctrl-C** at an approval prompt ends the run at once (`deny_stops.sh` check 4). It used
   to refuse just the one call,
   which meant it could never reach the input line — so there was no way out of a retry loop
   except to out-wait the model.
3. **`/exit`** ends the session.

**When the refusal count resets** — the whole question, and the answer is that its lifetime is
**one turn**:

| event | count afterwards |
|---|---|
| you send a new message | **0** — every turn starts fresh |
| you **approve** anything mid-turn | **0** — accepting breaks the streak (`deny_stops.sh` check 7) |
| three refusals end the turn | the next message starts at **0** |
| Ctrl-C ends the turn | the next message starts at **0** |
| end of session | never reached — it cannot outlive a turn |

It is a local in `run_agent_loop`, which runs once per turn, so nothing carries over between
things you type. A subagent gets its own count for the same reason.

**Both the person and the model are told.** After the *first* refusal in a turn the run prints
the rule once (`deny_stops.sh` check 5) — *"This run stops after 3 refusals in a row. Control C stops it now."* — rather
than a countdown after every refusal, which would be chatty for a listener and would need plural
agreement in every translation. The **model** is told in the tool result: how many refusals have
been used, what the limit is, and to ask what you want instead of trying another way
(`deny_stops.sh` check 6 — asserted on the captured request, since what the model receives is
invisible on screen). Before
M573 it received the bare sentence *"Tool call denied by the user."* and discovered the limit the
expensive way — the transcript that prompted this section shows it reaching `ask_user` on its own
only after ten prompts.

**What this is not.** None of it is a substitute for `--auto` being off, for `--edit-scope`, or
for `pathFence`. Those bound what a call *can* touch; this bounds how many times you are asked.

## Deferred / future

- ~~A screen-reader-optimized transcript mode that labels roles~~ shipped
  at M184 (above).
- ~~Incremental input echo in accessible mode~~ shipped at M362 (above).
- Configurable, higher-contrast glyph/label sets.
- Audit the ACP/editor integration surface with a specific screen reader.
- ~~A spoken/announced form of the Ctrl-G ghost suggestion~~ **shipped at
  M462.** Ghost text is dim grey overlay text — the one rendering a screen
  reader is least likely to convey — so for the users voice mode exists for,
  Ctrl-G produced no observable output at all. With `voice` on, the suggestion
  is now spoken through the same `jc_voice_say` path as replies and approval
  prompts. It speaks only on the explicit keypress and only when there is
  something to say, so it cannot become chatty. **The general lesson is worth
  more than the fix:** shipping a voice mode does not make the visual-only
  surfaces accessible, and each one has to be found and closed deliberately.
- Run the manual protocol above with fenrir and Orca; record the note.
  **Checked, 2026-08-17 (the M326b rule), and the reason had rotted:** this was
  carried as if no screen reader were available. On the current bench
  `orca`, `speech-dispatcher`, `spd-say` and `brltty` are all **installed**;
  only `fenrir` is absent. So the blocker is not tooling — it is that the
  protocol is a *listening* exercise and needs a person for twenty minutes.
  That is a materially different state from "we have no screen reader", and
  the row should be read as **ready to run**, not blocked.

See also: docs/TUI_RENDER.md, docs/SCRIPTING.md (headless), docs/ACP.md.


## The setup wizard (M326n)

Before M326n, `--accessible` **did nothing** for `jichi setup`: the subcommand is
dispatched before the flag is applied to the config, and the wizard never
consulted it. Five of its lines ran to 118 columns, and the no-echo key prompt
gave no feedback at all.

What it does now:

- **`--accessible` reaches the wizard.** Menus become plain `option N: name` /
  `writes: …` / `why: …` lines instead of column-aligned ones, which a screen
  reader announces cleanly and which do not depend on a monospace grid.
- **Every line wraps to 76 columns or fewer**, pinned by a check in
  `tests/smoke/setup_keyfile.sh`. 80 columns is often the whole working width
  under screen magnification.
- **The no-echo key prompt reports a character count** (`read 51 characters`)
  once the key is stored. The secrecy is unchanged; what was missing was any
  confirmation that the paste arrived intact.

What was already right, and stays: numbered menus, no colour in the wizard, no
spinner or animation, and every prompt naming the default that Enter takes.

Known gap: the wizard is the only subcommand audited this way. `--accessible`
reaching it was a dispatch-order fix, so other subcommands that print aligned
columns have not been checked against a screen reader.
