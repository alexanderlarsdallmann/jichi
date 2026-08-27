# Voice: speaking and listening

> **Also read:** [`ACCESSIBILITY.md`](ACCESSIBILITY.md) (screen readers, reduced
> motion, the transcript mode), [`SOUND.md`](SOUND.md) (the `sound` backends),
> [`MEDIA_GEN.md`](MEDIA_GEN.md) (`generate_audio`),
> [`TRANSCRIBE.md`](TRANSCRIBE.md) (`transcribe_audio`).

Turn it on with `--voice`, `"voice": true`, or `/voice` in the TUI. Off by
default; `--no-voice` overrides a config that turned it on.

## Why it is a mode and not a tool

Every primitive shipped long before M303: `generate_audio` (TTS), `transcribe_audio`
(STT), `play_audio`/`record_audio`, and `@audio:` / ACP audio blocks already routed
speech *into* a turn. What did not exist was an **interaction mode** — nothing spoke
a reply, and nothing listened for one.

A tool could not have filled that gap, because a tool has to be *chosen by the
model*. The things a screen-less user most needs spoken are exactly the ones the
model never narrates:

- a **tool-approval question** — jichi is waiting for a keypress;
- an **error**;
- the **answer** itself.

If the screen is not being read, anything jichi *waits on* must be audible, or the
session simply stops with no explanation. That is the requirement accessibility
sets, and it is stricter than "read the answer aloud".

## What you need

Two things, and jichi tells you in words which one is missing rather than falling
silent:

1. **A model with the `audio` role** — speech synthesis (see
   [`MEDIA_GEN.md`](MEDIA_GEN.md)).
2. **A `sound.play` command** — how to play it (see [`SOUND.md`](SOUND.md)):

```json
{
  "models": [
    { "name": "tts", "provider": "openai", "model": "kokoro",
      "apiBase": "http://localhost:8080/v1", "roles": ["audio"] }
  ],
  "sound": {
    "play":   { "command": "aplay",   "args": ["-q"] },
    "record": { "command": "arecord", "args": ["-q", "-f", "cd"] }
  },
  "voice": true
}
```

`/voice on` **refuses loudly** when either half is missing:

```
voice: unavailable -- voice needs a model declaring the "audio" role to
synthesize speech; none is configured
  (leaving it off: silence would be worse than this message)
```

That refusal is deliberate. For the users this exists for, "on but silent" is
indistinguishable from a hung session — the worst possible failure mode.

## What gets spoken

| Surface | Spoken |
|---|---|
| the assistant's reply | yes, once complete, after it is on screen |
| a tool-approval question | yes, before the keypress is awaited |
| errors | yes |
| streamed text, token counts, spinners | no |
| a subagent's progress stream | no — it is progress, not the answer |

The reply is accumulated and spoken as **one utterance**. Synthesising each
streaming delta would stutter, and a synthesiser needs a whole sentence to place
prosody.

### Prose is reduced before it is spoken

A screen reader can skim; a speaker cannot. So the text is deliberately lossy
(`jc_voice_speakable`, pure and unit-tested):

- a fenced code block becomes **"(code block, 12 lines)"** — reading C aloud line by
  line is not accessibility, it is punishment;
- markdown decoration (`` ` ``, `*`, `_`, `#`, list bullets, blockquote markers) is
  dropped, because "star star important star star" is noise;
- whitespace runs collapse;
- the utterance is capped (~1200 chars) at a **sentence boundary** where possible,
  with a spoken "(truncated)".

That cap is a safety property, not a nicety — see the limits below.

## Listening

```
/listen          # a 5-second window
/listen 12       # a 12-second window
```

It records, transcribes, **echoes what it heard** (and speaks it back when voice is
on, so a misheard prompt can be caught before it becomes a turn), then submits the
transcript as an ordinary prompt — so `@`-references, slash commands and history all
behave exactly as if you had typed it.

Needs `sound.record` and a model with the `transcribe` role.

## The honest limits

These are stated rather than worked around, because each one would otherwise look
like a bug:

- **No silence detection.** `/listen` is a **fixed window**. `record_audio` takes a
  `seconds` argument enforced by a timeout, and jichi has no voice-activity
  detection — so it will not stop when you stop talking. A user expecting auto-stop
  will think it is broken; it is not, it is unimplemented.
- **No barge-in.** Playback is a child process run to completion. You cannot
  interrupt a spoken reply mid-sentence except with Ctrl-C. This is *why* the
  utterance is capped: with no escape, an unbounded monologue is a trap.
- **Latency.** A turn produces no speech until it finishes, and synthesis itself
  takes time. There is no spoken "working on it" yet, so a long turn is a long
  silence. Worth fixing; not fixed.
- **No bundled TTS.** jichi shells out and links no audio library (the M42
  external-extractor pattern), so voice depends entirely on your configured
  backend and its quality.
- **A backend that answers but fails is a real state, and now it says which
  failure it was (M500).** Measured against a gateway that advertises a TTS model
  and answers `HTTP 500` for it: the log line now reads *"speech synthesis failed
  -- the server answered HTTP 500 on /audio/speech: this is a SERVER failure, not
  the arguments"*. That distinction matters most here, because the person this
  mode exists for cannot read the screen to work it out: `401`/`403` means fix
  your key, `404` means fix `apiBase` or the model id, `429` means wait, and a
  `5xx` means the server is broken and retrying will not help. Before M500 all
  four printed the same sentence, and a model given that sentence retries the
  arguments — measured: three variations, 428 output tokens, then advice to fix a
  key that was correct.
- **Not in headless or ACP.** Voice is a TUI mode. Headless output is for machines,
  and an ACP client owns its own presentation.
- **Speech files are transient.** Synthesised audio is written under
  `~/.jichi.d/voice/` and deleted after playing — outside the workspace, so it never
  lands in your tree or inside a snapshot's blast radius
  ([`ANECDOTES.md`](ANECDOTES.md) #1).
