# Start here — your first Blender Python script

*A numbered, first-session walkthrough. If you have never scripted Blender, do
exactly these steps once. The single most important thing you will learn is
**where the code runs** — get that, and the rest follows.*

## The one thing that trips up everyone

Your script runs **inside Blender**, using Blender's own Python — **not** with
`python script.py` in a terminal. If you try that, you will see:

```
ModuleNotFoundError: No module named 'bpy'
```

That does **not** mean your script is wrong. It means you used the wrong launcher.
`bpy` only exists inside Blender. Remember this and you have skipped the confusion
that costs most beginners their first evening.

## Before you begin

You need:

1. **Blender** installed (blender.org). Check the version: `blender --version` —
   the `bpy` API differs between versions, so you will target yours.
2. **jichi** (you have it) and **a model** (copy `config.example.json`'s `models`
   block into your config; a local one is free and private — `docs/LOCAL_MODELS.md`;
   then `jichi doctor`).

## Your first script, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`) and, first of all,
   learn how to run things:

   ```
   /run
   ```

   jichi (the `blender-scripter` guide) will confirm your Blender version and show
   you the three ways to run a script — the Text Editor, the Python Console, and
   headless. **Get one tiny script running first**, e.g. printing the names of the
   objects in the default scene. Proving the run path works is worth more than any
   fancy code.

2. **Explore the API live.** Open Blender's **Python Console** (change an area to
   it) and try one line at a time — `bpy.data.objects`, `bpy.context.active_object`.
   Ask jichi what each returns. This is the fastest way to build the mental model
   (`skills/bpy-basics`).

3. **Make something with code.** Type:

   ```
   /geometry-script
   ```

   jichi helps you write a small script that *creates* geometry — a generated mesh
   or a repeatable arrangement — using the predictable data API. Run it in Blender
   (via `/run`) and watch the mesh appear. Then change a parameter and re-run —
   that is the whole joy of scripting geometry.

4. **Write a reusable tool.** When you want an action you can trigger:

   ```
   /operator
   ```

   jichi builds a proper operator (the `bl_idname` / `execute` / `register`
   pattern). This is the building block of every Blender tool and add-on.

5. **Check it before you trust it.** Especially before installing an add-on:

   ```
   Spawn the bpy-reviewer agent to review my script.
   ```

   It reads (never edits) and flags a deprecated call for your Blender version, a
   context assumption that will throw, or a broken register/unregister pair — the
   exact things that cost a beginner an hour.

6. **Package it as an add-on** when it is ready:

   ```
   /addon
   ```

   jichi scaffolds a minimal installable add-on (the `bl_info` manifest, the
   register/unregister pair) you can enable from Preferences.

That is the arc: **run → explore → make → tool → review → package.** Start tiny,
run everything in Blender, and pin your version. The scripting is easy once you
stop fighting *where* it runs.

## If you get stuck

- `ModuleNotFoundError: No module named 'bpy'` → you ran it with plain `python`.
  Use Blender (Text Editor, Console, or `blender --background --python`). See
  `/run`.
- A `bpy.ops` call throws a context/poll error → the active object or mode is
  wrong; check them, or use the `bpy.data` equivalent (`skills/bpy-basics`).
- A tutorial's code fails → check its Blender version against yours; the API
  changed. Read the version-matched docs.
- Objects pile up as `.001`, `.002` on every run → your script is not cleaning up;
  reuse a named object or delete previous output first (`skills/mesh-ops`).
