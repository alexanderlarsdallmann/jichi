---
description: Write a script that creates or manipulates mesh geometry procedurally — clean, re-runnable, using the data API where it's predictable.
---
Write a Blender Python script that creates or manipulates **geometry**
procedurally — a generated mesh, a modification across objects, a repeatable
scene. This is where scripting shines: doing by code what would be tedious by hand.

Guide the approach:

1. **Prefer the data API (`bpy.data`) over `bpy.ops`** where you can — creating a
   mesh with `bpy.data.meshes.new()` + `bpy.data.objects.new()` + linking to a
   collection is more predictable than operator calls that depend on context and
   selection. For true mesh editing, use `bmesh`.
2. **Link to a collection the right way for the version** (`collection.objects.link()`
   on 2.8+, not the removed `scene.objects.link()`).
3. **Make it re-runnable.** Either name and reuse objects, or clean up previous
   output at the start, so running twice does not leave `.001` duplicates.
4. **Parameterize it.** Counts, sizes, seeds as variables at the top, so the user
   can vary the result — that is the point of scripting geometry.

Keep each step small and runnable so the user can watch the mesh appear. Explain
the mesh data model (vertices/edges/faces, `from_pydata`, `update()`), and set a
seed if anything is random. Then suggest `/run` to execute it and the
`bpy-reviewer` to check for a version-mismatched API call.
