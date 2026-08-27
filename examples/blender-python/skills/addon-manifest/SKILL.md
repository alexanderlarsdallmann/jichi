---
name: addon-manifest
description: The anatomy of a valid Blender add-on — bl_info, class registration, and the register/unregister pair Blender depends on.
---
# The add-on manifest and registration

A Blender add-on is Python that Blender can install, list in Preferences, and
enable/disable. Two things make it valid; beginners usually get one of them wrong.

**1. `bl_info` — the manifest.** A dict near the top that Blender reads to list
the add-on. The essentials:

```python
bl_info = {
    "name": "My Add-on",
    "author": "You",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),   # MINIMUM Blender version it supports -- match yours
    "category": "Object",
}
```

If `bl_info` is missing or malformed, Blender simply will not show the add-on —
no error, it just is not there. The `"blender"` tuple is the minimum version;
set it to the version you are targeting.

**2. `register()` / `unregister()` — always a matched pair.** Blender calls
`register()` when the add-on is enabled and `unregister()` when disabled. Every
class you add (operators, panels, property groups) must be registered on enable
and **unregistered on disable, in reverse order**:

```python
classes = (MyOperator, MyPanel)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
```

**Why the pair matters:** if `unregister` does not undo everything `register`
did, disabling the add-on leaves classes registered — you get "already registered"
errors on reload, duplicate menu entries, and an add-on that will not cleanly turn
off. The reverse order avoids dependency issues on teardown.

**Installing:** a single `.py` file or a zipped package, via Preferences >
Add-ons > Install. During development, keep the file where Blender can reload it,
and re-run register/unregister rather than restarting Blender each time.
