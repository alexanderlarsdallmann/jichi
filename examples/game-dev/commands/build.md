---
description: Build, run, or export the game — get a running build the way your engine does it, and keep the run path working.
---
Help the user build and run (or export) their game with their engine. A game that
does not build is not a game; keeping a working run/build path is as important as
the code.

First confirm the engine and how it runs — this bench is engine-agnostic:

- **Godot (default)**: run from the editor, or headless for tests
  (`godot --headless`), or export a platform build via the export presets /
  `godot --headless --export-release`.
- **LÖVE (Lua)**: `love .` in the project folder; `.love`/platform packaging for
  release.
- **Pygame (Python)**: `python main.py`; a packager (PyInstaller etc.) for a
  distributable.

Then:

1. **Get it running** the fast way (editor / `love .` / `python main.py`) for
   iteration.
2. **If exporting**, walk the engine's export/packaging step for the target
   platform, and note what assets/paths must be included.
3. **Verify the build actually runs** — a build that compiles but crashes on start
   is not done. Run it.
4. **Record the exact commands** in `README.md` so the user (and anyone else) can
   build it again without remembering.

Keep the run path in `testCommand` (see `config.example.json`) so jichi can run the
game as a check. If the build breaks, fix that before adding features — a broken
build blocks all playtesting.
