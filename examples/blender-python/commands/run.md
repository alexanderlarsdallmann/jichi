---
description: How to actually RUN a bpy script — inside Blender, never with plain python. The first thing every beginner gets wrong.
---
Help the user run a Blender Python script correctly. This is the command to reach
for first, because the #1 beginner mistake is trying to run a `bpy` script with
plain `python`, getting `ModuleNotFoundError: No module named 'bpy'`, and thinking
the script is broken. It is not — `bpy` only exists **inside Blender**.

Explain and set up the right way for their goal:

1. **Learning / iterating** → Blender's **Text Editor**: open Blender, switch an
   area to the Text Editor, paste or open the script, press *Run Script*. Errors
   print to the *system console* (Window > Toggle System Console on Windows; the
   terminal Blender was launched from on Linux/macOS).
2. **Exploring the API** → Blender's **Python Console** (an area type): run one
   line at a time, inspect what each call returns. Best for figuring out the API.
3. **Automation / reproducible runs** → **headless**:
   `blender --background --python your_script.py` (add `-- arg1 arg2` to pass your
   own args after a bare `--`). No GUI; ideal for batch geometry generation.

Confirm which Blender version they have (`blender --version`) so the API matches.
Then help them get *one tiny script* running end-to-end (e.g. print the scene's
object names) before anything bigger — proving the run path works is worth more
than any code. Never suggest `python script.py` for a `bpy` script.
