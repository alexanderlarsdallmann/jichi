---
name: bpy-basics
description: The mental model of Blender's Python API — where code runs, data API vs operators, context, and how versions differ.
---
# bpy basics — the mental model

Before any specific code, hold these four ideas about `bpy`; they explain most of
what confuses beginners.

**1. `bpy` lives inside Blender.** It is not a `pip` package. `import bpy` only
works in Blender's own bundled Python — the Text Editor, the Python Console, or
`blender --background --python`. Plain `python script.py` gives `ModuleNotFoundError`.
That error means "wrong launcher," not "broken script."

**2. Two ways to touch the scene: `bpy.data` and `bpy.ops`.**

- `bpy.data` is the **data API** — the actual scene database. `bpy.data.objects`,
  `bpy.data.meshes`, `bpy.data.materials`. It is direct and predictable: you
  create and link things explicitly. Prefer it for scripting.
- `bpy.ops` is the **operator API** — it runs the same actions the UI buttons do
  (`bpy.ops.mesh.primitive_cube_add()`). Convenient, but each op depends on
  **context** (what is active, the mode, the area), so ops fail with poll/context
  errors when run from the wrong place. Use them, but know why they break.

**3. Context is king (and the usual culprit).** `bpy.context` is "what is
currently active/selected/in-focus." Operators read it. If an op errors, the
active object or mode is usually wrong. When context fights you, drop to
`bpy.data`, or override the context explicitly.

**4. Versions differ — pin yours.** The API changes across Blender releases;
2.8 was a big break, 4.x changed more. Check `blender --version`, and read the
**version-matched** docs (docs.blender.org has a version switcher). A snippet
written for a different version is the most common reason "the tutorial doesn't
work."

Hold these four and the specific commands (`/operator`, `/addon`, `/geometry-script`)
stop being magic incantations and start being obvious.
