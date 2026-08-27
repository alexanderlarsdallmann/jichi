# Start here — your first Krita Python script

*A numbered, first-session walkthrough. If you have never scripted Krita, do
exactly these steps once. The most important thing you will learn is **where the
code runs** — get that (and what a "Docker" is), and the rest follows.*

## Two things that trip up everyone

1. **Your script runs inside Krita, not with `python script.py`.** The `krita`
   module only exists in Krita's own Python. Run scripts in Krita's **Scripter**
   (Tools > Scripts > Scripter) or as an installed plugin — never plain `python`.
2. **A "Docker" is a panel, not a container.** In Krita, a Docker is a dockable
   UI panel (like Layers). If you search "krita docker," ignore everything about
   containers.

Get these two and you have skipped the confusion that costs most beginners their
first evening.

## Before you begin

You need **Krita** installed (krita.org — check the version under Help > About
Krita), **jichi** (you have it), and **a model** (copy `config.example.json`'s
`models` block into your config; a local one is free and private —
`docs/LOCAL_MODELS.md`; then `jichi doctor`).

## Your first script, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`) and, first, learn how
   to run things:

   ```
   /run
   ```

   jichi (the `krita-scripter` guide) confirms your Krita version and shows you the
   Scripter. **Get one tiny script running there first** — printing the active
   document's name:

   ```
   from krita import Krita
   doc = Krita.instance().activeDocument()
   print(doc.name() if doc else "open a document first!")
   ```

   Open an image in Krita, then Run it in the Scripter. Proving the run path works
   (and that a document is open) is worth more than any bigger script.

2. **Do something to a document.** Type:

   ```
   /action
   ```

   jichi helps you write a script that changes the active document — add a layer,
   make a selection, apply a step — through Krita's object model
   (`Document` > `Node`). Run each step in the Scripter and watch Krita respond.
   Remember `refreshProjection()` so the change appears.

3. **Automate the boring thing.** The best reason to script Krita is doing by code
   what is tedious by hand — batch-exporting or resizing a folder of images:

   ```
   /batch-export
   ```

   jichi builds the open → process → export → close loop. **Test on two or three
   files first**, and never overwrite your originals.

4. **Check it before you trust it.** Especially before enabling a plugin or
   running a big batch:

   ```
   Spawn the pykrita-reviewer agent to review my script.
   ```

   It reads (never edits) and flags a wrong run context, a change that will not
   stick, a batch that clobbers, or a broken plugin layout.

5. **Package it as a plugin** when it is ready to be a real tool:

   ```
   /plugin
   ```

   jichi scaffolds a minimal installable plugin — the `.desktop` manifest, the
   package, the `Extension` — that you enable in the Python Plugin Manager. See
   `skills/docker-plugin` for the layout (and how to add a Docker *panel* if you
   want one).

That is the arc: **run → act → automate → review → package.** Start tiny, run
everything in the Scripter, and remember: inside Krita, and a Docker is a panel.

## If you get stuck

- Import error on `krita` when you run it → you used plain `python`. Use the
  Scripter or a plugin. See `/run`.
- Your script "ran" but nothing changed → you likely forgot `refreshProjection()`
  / `waitForDone()` (`skills/krita-python-api`).
- `activeDocument()` is `None` → no image is open; open one first.
- Your plugin does not show up in the Python Plugin Manager → the `.desktop` name
  does not match the package, or the folder is not under `pykrita/`
  (`skills/docker-plugin`).
