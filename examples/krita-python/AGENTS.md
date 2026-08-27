# Krita Python (PyKrita) project conventions

*This file tells the agent how to behave in a Krita-scripting workspace. jichi
loads it automatically. Keep it short; edit it to match your setup.*

## What runs where — READ THIS FIRST

**Your code runs INSIDE Krita, not with plain `python`.** The `krita` module only
exists in Krita's embedded Python. `from krita import Krita` run with
`python script.py` will fail on the import — and that is **not a broken script, it
is the wrong launcher.** Run scripts one of two ways:

- **The Scripter** (Tools > Scripts > Scripter) — a built-in editor + Run button +
  output pane. Best for learning and quick tests.
- **An installed Python Plugin** — a folder under Krita's `pykrita` resource
  directory with a `.desktop` manifest, enabled in Settings > Configure Krita >
  Python Plugin Manager. This is how a script becomes a reusable tool.

There is nothing to `pip install` for the `krita` module — it ships with Krita.
Check your Krita version (Help > About Krita); the PyKrita API has grown across
releases.

## "Docker" means a panel, not a container

In Krita, a **Docker** is a *dockable UI panel* (like Layers or Tool Options) —
it has NOTHING to do with Docker containers. A plugin that adds a panel adds a
"docker" (a `DockWidget`). If you search "krita docker" you will get very confused;
this is the meaning here.

## The rules of this bench

1. **PyKrita runs in Krita; any extra library must be available to Krita's
   embedded Python**, not your system Python.
2. **`Krita.instance()` is the entry point** — the app, the documents, the
   actions, the resources. Guard `activeDocument()` for `None`.
3. **Make changes stick.** After editing a document call `refreshProjection()`
   (and `waitForDone()` where async) or the change may not appear/export.
4. **A plugin's `.desktop` name must match its package**, and the folder must be
   under `pykrita/` — a mismatch means Krita silently will not list it.
5. **Batch safely.** Never clobber originals; always `close()` opened documents;
   keep going on a single file's failure.
6. **Small, testable steps in the Scripter** before packaging a plugin.

## The workflow (commands)

- `/run` — how to actually run a PyKrita script (start here).
- `/action` — manipulate the active document (layers, selections, transforms).
- `/plugin` — scaffold a minimal installable plugin (`.desktop` + Extension).
- `/batch-export` — automate many files (open → process → export → close).

Before you enable a plugin or run a batch, have the read-only **`pykrita-reviewer`**
agent read it — it flags wrong run context, a broken `.desktop`/plugin layout, and
the PyKrita API mistakes beginners hit.
