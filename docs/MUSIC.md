# Music development with jichi

Sheet-music engraving, composition assistance, MIDI rendering, and working
beside a DAW — with the same discipline as every other jichi domain: external
tools driven through the shell (**never** a linked audio/notation library —
the M42/M163b rule), a mechanical verify gate, and honest limits stated up
front.

```sh
jichi setup --preset composer     # or: jichi init music
```

The `music` pack ships:

| Asset | What it does |
|---|---|
| `composer` agent | writes/edits LilyPond from *stated* musical intent; proves every change engraves; never invents tempo/key/phrasing — asks |
| `engraver` agent (read-only) | reviews the *printed page* for the player: collisions, beaming vs meter, page turns, rehearsal marks — findings only |
| `arranger` agent | transposition (`\transpose`, never manual note surgery), voicing within stated ranges, part extraction |
| `lilypond-notation` skill | the source-hygiene subset: variables per voice, bar checks, `\relative`, reviewable diffs |
| `midi-workflow` skill | `.ly` → MIDI (`\midi{}`) → audio (fluidsynth/timidity) → `play_audio` |
| `ardour-project` skill | what jichi may touch in a `.ardour` session, honestly (see limits) |
| `/engrave` `/hear` `/transpose` | compile-and-report, render-and-play, transpose-and-prove |
| `config.example.json` | the engraving gate as `verify`, `sound.play`, a `render_midi` user tool |

## The loop

**Sources vs artifacts** (the scaffolded `AGENTS.md` enforces the
vocabulary): `.ly`, MIDI captures, `.ardour` sessions and lyrics are
sources; PDFs and rendered audio are artifacts — regenerated, never
patched.

**The mechanical floor is "it engraves":** `lilypond --loglevel=ERROR`
exits non-zero on any error, which makes it a real `verify` — agent work on
scores is gated exactly like agent work on code. What the floor cannot
judge — whether the music is any *good*, whether the page is kind to a
sight-reading player — is the engraver agent's review plus your ears: the
curriculum's three-layer model, transposed.

**Hearing it:** with `sound.play` configured (`aplay`, `ffplay`, …) the
`play_audio` tool plays rendered audio from the chat; `/hear` runs the
whole chain (engrave → MIDI → fluidsynth/timidity → play). A General MIDI
soundfont (e.g. FluidR3_GM) is fluidsynth's one requirement; `timidity`
needs none.

`doctor` checks `lilypond` is on PATH whenever your `verify`/`testCommand`
names it (the shipped example does), so a missing engraver is a visible
warning, not a mystery failure.

## Honest limits

- **jichi cannot hear.** No audio analysis, no tuning or mix judgment;
  rendering proves the *pipeline*, ears prove the *music*.
- **Ardour owns its sessions.** `.ardour` files are XML jichi may read
  freely (answer questions about tracks, routing, tempo maps) and patch
  only trivially (a name, a comment) — structural edits happen in Ardour,
  and never while the session is open there. Composition/notation is agent
  territory; recording, mixing, mastering are human territory; MIDI is the
  bridge.
- **`midi2ly` output is a sketch**, not a source — clean it per the
  notation skill before treating it as one.
- Engraving *quality* (spacing, breaks) beyond error-free compilation is
  review territory, not gate territory — the same honesty as prose floors.

*See also: [SOUND.md](SOUND.md) (the play/record tools),
[USER_TOOLS.md](USER_TOOLS.md) (wrapping more of your toolchain),
[SCAFFOLDING.md](SCAFFOLDING.md), [SETUP_WIZARD.md](SETUP_WIZARD.md).*
