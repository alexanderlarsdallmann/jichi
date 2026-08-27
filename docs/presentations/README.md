# jichi presentations

Text-first slide decks in [Marp](https://marp.app) markdown. Each `NN-*.md` has a
`marp: true` front-matter block and renders to HTML / PDF / PPTX with `marp-cli`,
or previews live in the Marp VS Code extension. They are plain markdown, so they
diff and review like any other doc.

> **Localized decks** live under `docs/i18n/<lang>/presentations/` (de · es · ja
> · zh; these English decks are canonical). `make slides` renders each language
> into `docs/presentations/out/<lang>/`. See [../i18n/README.md](../i18n/README.md).

| Deck | For |
|---|---|
| `00-super-features.md` | The headline capabilities in a dozen slides. |
| `01-introduction.md` | What jichi is, why C89/POSIX, the architecture at a glance. |
| `02-using-jichi.md` | Day-to-day use: TUI, headless, modes, tools, editors. |
| `03-roadmap.md` | Where it's been, where it's going, and how it was built. |
| `04-university.md` | Research, coursework, reproducibility, low-resource. |
| `05-school.md` | Classroom use and the guardrails that make it safe. |
| `06-building-with-ai.md` | The build retrospective: numbers, phases, and the four delivery models (incl. one dev + AI). |
| `07-the-release.md` | **The release argument** (M307): the four claims, and how a reviewer checks each one. The deck to open first. |

> **`07` is English-only for now.** The four localized sets (de · es · ja · zh) carry
> decks `00`–`06`; translating `07` waits for the same trigger as the rest of the
> translation work (see the curriculum note in `docs/ROADMAP.md`) — a confident wrong
> translation of a *claims* deck would be worse than an absent one.

## Rendering

```sh
# One-off, no install (needs Node):
npx @marp-team/marp-cli docs/presentations/00-super-features.md -o out.html
npx @marp-team/marp-cli docs/presentations/00-super-features.md --pdf
npx @marp-team/marp-cli docs/presentations/00-super-features.md --pptx

# Or via the Makefile target (renders every deck to docs/presentations/out/):
make slides
```

`make slides` is a no-op with a friendly note when `marp`/`npx` isn't available,
so it never breaks a build or CI (same pattern as the Emacs/elisp targets).

## Real-life use-cases referenced across the decks

- **zigodot** — jichi driving a large, autonomous Godot→Zig rewrite (the north-star
  dogfood): `--auto` runs, the autonomy envelope, subagents, telemetry.
- **Teaching** — instructors + learners using the assignments feature
  ([TEACHING_ASSIGNMENTS.md](../TEACHING_ASSIGNMENTS.md)).
- **Remote ops** — driving jichi headless over SSH under tmux on a GPU/CI box
  ([REMOTE_SSH.md](../REMOTE_SSH.md), [TMUX.md](../TMUX.md)).
- **Autonomous loops & robotics** — a supervisor over a task queue
  ([AUTONOMOUS_LOOPS.md](../AUTONOMOUS_LOOPS.md)) and jichi as a robot's
  deliberative layer ([ROBOTICS.md](../ROBOTICS.md)).
- **Build retrospective** — the timeline, phases, and delivery-model comparison
  ([PROJECT_TIMELINE.md](../PROJECT_TIMELINE.md)).

## Editing conventions

- Keep one idea per slide; prefer a short code block or a small mermaid diagram
  over a wall of bullets.
- Speaker notes go in HTML comments (`<!-- note -->`) — Marp shows them in
  presenter mode and keeps them out of the slide body.
- Mermaid isn't rendered by Marp by default; the decks keep diagrams small and
  ASCII-friendly, or link to the doc that has the full diagram.
