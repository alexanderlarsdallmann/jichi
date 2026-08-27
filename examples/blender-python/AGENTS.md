# Blender Python (bpy) project conventions

*This file tells the agent how to behave in a Blender-scripting workspace. jichi
loads it automatically. Keep it short; edit it to match your setup.*

## What runs where — READ THIS FIRST

**Your code runs INSIDE Blender, not with plain `python`.** The `bpy` module only
exists in Blender's own bundled Python. If you run a `bpy` script with
`python script.py`, you get `ModuleNotFoundError: No module named 'bpy'` — and
that is **not a broken script, it is the wrong launcher.** Run scripts one of
three ways:

- **Blender's Text Editor** — paste/open the script, press *Run Script*. Errors
  print to the system console (Window > Toggle System Console on Windows; the
  launching terminal on Linux/macOS). Best for learning.
- **Blender's Python Console** — one line at a time, to explore the API.
- **Headless**: `blender --background --python your_script.py`. Best for
  automation. jichi runs *this*, never plain `python`, for a `bpy` script.

jichi will tell you the command it is about to run — read it, and never approve a
plain `python …` for a `bpy` script.

## Pin your Blender version

The `bpy` API **changes between Blender versions** (2.8, 4.x each broke things —
collection linking, `bmesh`, context, operators). Find yours with `blender
--version` and target it. Write API against the **version-matched** docs
(docs.blender.org has a version switcher). A snippet from a tutorial for a
different version is the #1 reason code "doesn't work."

## The rules of this bench

1. **`bpy` runs in Blender; there is nothing to `pip install` for `bpy`.** Any
   extra library must be available to Blender's bundled Python, not your system
   Python.
2. **Prefer the data API (`bpy.data`) over operators (`bpy.ops`)** where you can —
   it is predictable and does not depend on context. Reach for `bmesh` to edit
   geometry.
3. **Mind the context.** Operators fail when the active object/mode/area is wrong;
   most `bpy.ops` errors are context errors. Check the active object and mode.
4. **Make scripts re-runnable.** Clean up or reuse — do not pile up `.001`
   duplicates on every run.
5. **Small, testable steps.** Run each change in Blender and watch the scene
   before adding more.

## The workflow (commands)

- `/run` — how to actually run a script in Blender (start here).
- `/operator` — write a custom operator (the tool building block).
- `/addon` — scaffold a minimal installable add-on.
- `/geometry-script` — create/manipulate mesh geometry procedurally.

Before you install an add-on or trust a script, have the read-only **`bpy-reviewer`**
agent read it — it flags deprecated/version-mismatched API, wrong run context, and
the add-on register/unregister mistakes beginners hit.
