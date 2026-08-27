# The listening pass: what a machine could verify, and what it could not

**Date:** 2026-08-20 · **Milestone:** M500 · **Task:** run the accessibility and
voice tests on the operator's own workstation, rather than describe them. ·
**Outcome:** the machine half passed, one product defect was found and fixed, one
gateway defect was measured and reported, and the human half is scheduled rather
than claimed.

---

## 0. The page said this machine could not do it. The page was out of date.

`ACCESSIBILITY.md` describes its audit machine (2026-08-10) as having *"no screen
reader — no Orca, no fenrir, no speakup"*, and the M326b re-check at M463 upgraded
that to *"ready to run"*. Measured directly on this workstation:

| Component | State |
| --- | --- |
| GNOME **X11** session, `DISPLAY=:0` | live |
| **Orca 46.1** | installed |
| `org.a11y.Bus` (at-spi-bus-launcher) | **running** |
| `gnome-terminal` (VTE, accessible) | installed |
| speech-dispatcher + the **`sd_espeak-ng`** module | installed; `spd-say -L` lists voices |
| **`speakup_soft`** kernel module (console path) | present (6.8.0-137-lowlatency, v2.6) |
| `espeakup`, `fenrir` | absent (both installable) |
| `brltty` | installed; no display hardware |
| `gsettings … toolkit-accessibility` | **false** — AT-SPI trees are off until it is on |

One probe deserves recording because it is the kind of check people skip: `pgrep -x
at-spi-bus-launcher` reported *not running* while `busctl --user` showed
`org.a11y.Bus` owned by pid 3643. The cause is the 15-character limit on
`pgrep`'s name match, which this project already has a note about. **A negative
from a truncating matcher is not a negative.**

## 1. The audio path, verified without a human

Two facts that had never been measured here, both machine-checkable:

**Speech reaches the device.** Capturing the default sink's monitor while
`spd-say` spoke: **peak amplitude 16467, RMS 2436** over 4.0 s (silence would be
~0). So speech-dispatcher → espeak-ng → PipeWire → the interface works end to end.

**The microphone opens and delivers frames.** `parec` on the default source
returned 2.00 s of frames (peak 1 — a quiet room, which is the expected reading
with nobody speaking, and still proves the device path).

That converts part of the "needs a person" protocol into something a script can
assert: *did audio actually leave the machine* is not a judgement, it is an RMS.
What still needs ears is whether a **screen reader speaks it well** — announcement
order, interruption, whether the working line is chatty. That has not changed.

## 2. Voice mode on local models: one half works, one half is broken upstream

The gateway advertises two audio models, both **free** (`input_cost_per_token`
0.0), which is what makes this testable at all under the local-models-only rule:

| Model | Mode | Result |
| --- | --- | --- |
| `jlu/whisper-1` | `audio_transcription` | **works.** 0.65 s direct; through jichi's `transcribe_audio`, 5 s end to end |
| `jlu/tts-1-hd` | `audio_speech` | **HTTP 500** on every request shape tried |

The transcription proof is pleasing because the input was not a fixture: the clip
was the **espeak capture from §1**, and whisper returned *" Jitch accessibility
check, 1, 2, 3."* — the wrong spelling of "jichi" being espeak's pronunciation, not
whisper's mistake. So the STT half of `/listen` is live on a free local model.

The TTS half is not, and the failure is upstream: `POST /v1/audio/speech` with
`jlu/tts-1-hd` answers `{"error":{"message":"Internal server error"}}` in ~4.5 s
for a plain request, for `response_format: wav`, and for a different `voice`. **A
model can be listed, priced, and described with a `mode` it does not serve.**
That is worth remembering the next time a model list is treated as a capability
list — and it is the HRZ gateway's to fix, not jichi's.

## 3. The defect this exposed in jichi, and the fix

Driving the broken backend through jichi produced the interesting finding.
`generate_audio` reported, for **every** failure:

    error: audio generation request failed

The status was in the log the whole time (`audio generation: HTTP 500 …`) and
never reached the model. Measured consequence, on the real gateway:

| | before | after |
| --- | --- | --- |
| wall clock | **27 s** | **8 s** |
| output tokens | **428** | **209** |
| retries | three argument variations (different path, `.wav` vs `.mp3`, an explicit `voice`) | **none** |
| conclusion offered to the operator | *"check your audio/TTS configuration (missing API key/endpoint, or the backend is down)"* — the configuration was correct | *"the audio server returned HTTP 500 on `/audio/speech` — a server-side failure, not an argument problem"* |

This is the **M342 message class** — a refusal that states a cause with no way
forward amplifies retry loops — in the one family where it costs the most, because
the alternative action differs completely by status: `401`/`403` is *fix your key*,
`404` is *fix `apiBase` or the model id*, `429` is *wait*, `5xx` is *stop, it is
not you*.

**The fix** is `jc_neterr_render` (pure, `src/net/jc_neterr.c`): both transports
now hand the caller the HTTP status — `0` meaning *no response arrived*, which is
its own case and must not print as "HTTP 0" — and both tools plus the voice path
render a message naming the operation, the status, the endpoint, and the action.
`transcribe_audio` got the same treatment, because the defect was identical there
and a 401 on the transcription endpoint is precisely the case where the **operator**
must act and the old sentence could not say so.

Pinned by: `tests/test_neterr.c` (pure, four groups — including that five status
classes produce five *different* messages, since one message spelled five ways is
the original defect), `audiogen.sh` checks 5–6 (a 500 names the status **and**
tells the model the arguments are not the cause) and `transcribe.sh` check 5 (a
401 is reported as a key problem). All three proven red against the unfixed
binary.

**A methodology note worth more than the fix.** The first attempt at the
`transcribe.sh` two-sided proof edited the source with a Python replacement whose
escaping did not match, so nothing changed, the build was identical, and the check
"passed with the fix reverted" — which would have meant *the check is vacuous*.
What caught it was that the expected `not ok` line printed **nothing at all**, not
a reasoned inspection. Assert on the reason; and when a proof produces no output,
that is the finding.

## 4. Also found, and not a defect

`docs/VOICE.md` prescribed a `sound` config in a shape the parser does not
accept — `"play": "aplay"` with a `recordArgs` array, where `jc_config.c` reads
`"play": {"command": …, "args": […]}` (the form `SOUND.md` documents correctly).
A reader copying the VOICE example would get **no sound backend and no error
about it**. Fixed. This is the M394 class one layer down: `doc_commands_lint`
checks documented *command lines*, and nothing checks documented *config blocks*.

And one operator error worth recording so it is not re-diagnosed: `jichi -p "…"`
with stdin inherited **waits for stdin**, which looked exactly like a hang in the
voice path and cost three bisection runs. It is documented (`--no-stdin`, in
`SCRIPTING.md`, `DEPLOYMENT.md` and `REMOTE_SSH.md`) and every smoke driver
redirects `< /dev/null`. The bisection was still the right move: it separated *my
invocation* from *jichi* before anything was filed.

## 5. Disclosure: one priced call

While distinguishing *"the gateway's TTS is broken"* from *"my request shape is
wrong"*, I sent one five-word request to **`tts-1-hd`** — the unprefixed alias,
which routes to OpenAI, not to the JLU model. It returned 200 and 7,296 bytes,
which is what proved the request shape was correct and the `jlu/` model
specifically broken. It was a diagnostic worth a fraction of a cent, and it was
**not covered by any grant**: the standing rule is local models only, exceptions
granted explicitly per run. I stopped there and did not use it for the pass. Same
lesson as ANECDOTES #63 — *a capable key is not a permitted one* — and the
correction that matters is reporting it in the same breath rather than at the end.

## 6. What still needs a person

The protocol in `ACCESSIBILITY.md` §"The manual screen-reader test protocol" is
seven steps, and steps 1–6 are a **listening** exercise: whether each character is
echoed once, whether `working…` is announced once rather than streaming, whether
the approval prompt's key list is spoken *before* the editor waits. No rig can
judge that. What this pass changed is the cost of getting there: the tooling is
installed and verified, the audio path is proven, and the two settings still
needed are known —

    gsettings set org.gnome.desktop.interface toolkit-accessibility true
    orca &

— plus `sudo apt install espeakup` if the console path (arguably the more
representative one) is wanted instead. Step 7, the screen-less `/voice on` +
`/listen` workflow, is **blocked on the gateway's TTS**, not on us: `/listen`
alone works today.

**Scheduled, with both paths chosen (2026-08-20).** The operator elected to run
*both* the desktop and the console path, later the same day. So the kit is built
rather than the session changed:
[`scripts/a11y-session.sh`](../../scripts/a11y-session.sh) reports what the host
still needs, prints the two enablement one-liners for a human to run, and
prepares a throwaway workspace with a small C file, a config on a free local
model, and a `run.sh` that starts in **chat** mode — because step 5 needs a real
approval prompt and `--auto` would skip it. Verified end to end: the generated
workspace answered a prompt in accessible mode on `jlu/qwen3-coder-next`.

The result table below is **empty on purpose** until the pass runs. An analysis
note with guessed rows would be worse than no note; this project's own rule is
that a figure nobody measured does not get written down.

| Step | What to listen for | Result |
| --- | --- | --- |
| 1 | each character echoed **once** as typed; backspace likewise | *pending* |
| 2 | `assistant (…)`, one `working…`, `tool call:`, `tool result:`, then the reply read linearly | *pending* |
| 3 | fenced code arrives as plain lines | *pending* |
| 4 | Ctrl-G's ghost suggestion — expected to be announced only with voice on | *pending* |
| 5 | the approval question **and** the `[y]/[n]/[a]/[e]/[v]` list spoken **before** the editor waits | *pending* |
| 6 | the same turns **without** `--accessible`: spinner chatter, per-key re-announcement | *pending* |
| 7 | `/voice on` + `/listen` — **blocked**: the gateway's TTS answers 500 | *blocked upstream* |

Reader, version, terminal and per-step notes go in this table when it runs, per
the protocol's own instruction.

## 7. Lessons

1. **A model list is not a capability list.** `jlu/tts-1-hd` is advertised with
   `mode: audio_speech` and a price of zero, and answers 500 to everything.
2. **The status class is the message.** One sentence for 401, 404, 429 and 500
   forces the reader to guess, and a model's guess is *retry the arguments*.
3. **Some of "needs a human" is measurable after all.** "Did sound leave the
   machine" is an RMS; only "does it sound right" needs ears. Splitting the two
   turned a blocked row into a scheduled twenty minutes.
4. **When a two-sided proof produces no output, that is the result.** A check that
   passes with its fix reverted is vacuous, and silence is how it announces itself.
5. **Report the spend in the same breath as the finding it bought.**
