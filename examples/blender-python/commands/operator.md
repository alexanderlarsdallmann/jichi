---
description: Write a custom Blender operator — the bl_idname/execute/register pattern — the building block of any tool or add-on.
---
Write a custom Blender **operator**: a callable action the user (or an add-on
button) can invoke. Operators are the standard unit of a Blender tool, so getting
the pattern right matters.

Produce a script with the correct skeleton for the user's target Blender version:

1. A class subclassing `bpy.types.Operator` with:
   - `bl_idname` — MUST be `category.name` form (lowercase, e.g. `object.my_action`);
     a malformed id is a silent failure.
   - `bl_label` — the human name.
   - `execute(self, context)` — the work; return `{'FINISHED'}` (or `{'CANCELLED'}`).
   - optionally `poll(cls, context)` — when the operator is allowed to run (guards
     the context so it does not throw).
2. `register()` / `unregister()` functions that `bpy.utils.register_class` /
   `unregister_class` the operator — always as a pair.
3. A guard to run it directly for testing (register + call) when appropriate.

Explain each part as you go — what `context` gives you, why `poll` avoids the
context errors beginners hit, why register/unregister must match. Keep the actual
action small and testable. Then suggest running the `bpy-reviewer` to catch a
deprecated call or a context assumption, and `/run` to execute it in Blender.
