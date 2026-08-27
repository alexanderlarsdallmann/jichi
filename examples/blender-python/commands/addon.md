---
description: Scaffold a minimal, installable Blender add-on — the bl_info manifest, operators, panel, and register/unregister done right.
---
Scaffold a minimal but correct Blender **add-on** the user can install and enable
from Blender's Preferences > Add-ons. Beginners usually get the manifest or the
register/unregister pair wrong, so build the skeleton carefully.

Produce an add-on (`__init__.py` for a single-file add-on, or a package) with:

1. **`bl_info`** — the manifest dict Blender reads: `name`, `author`, `version`,
   `blender` (the MINIMUM supported version tuple — match the user's Blender), and
   `category`. A missing or malformed `bl_info` means Blender will not list the
   add-on.
2. **The functionality** — at least one operator (see `/operator`) and, if it has
   UI, a `Panel` subclass placing a button in a known space/region.
3. **`register()` / `unregister()`** — registering every class on enable and
   unregistering every class on disable, in reverse order. Mismatched
   register/unregister is the classic "it won't disable / duplicates on reload"
   bug.

Explain the install flow (zip the package or point Preferences at the file),
where the panel will appear, and how to reload during development. Keep it
minimal — a working tiny add-on the user extends beats a broad broken one. Suggest
the `addon-manifest` skill for the details and the `bpy-reviewer` before install.
