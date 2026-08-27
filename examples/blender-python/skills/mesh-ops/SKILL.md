---
name: mesh-ops
description: How to create and edit mesh geometry in code — from_pydata, bmesh, object vs edit mode — cleanly and re-runnably.
---
# Mesh operations in code

Generating geometry is where `bpy` earns its keep. Two levels, for two jobs:

**Building a mesh from scratch — `from_pydata`.** For creating a mesh out of raw
geometry:

1. Make a mesh datablock: `mesh = bpy.data.meshes.new("MyMesh")`.
2. Fill it: `mesh.from_pydata(vertices, edges, faces)` — lists of coordinate
   tuples and index tuples.
3. `mesh.update()` to recalculate.
4. Wrap it in an object and link it to a collection:
   `obj = bpy.data.objects.new("MyObj", mesh)` then
   `bpy.context.collection.objects.link(obj)` (2.8+; the old
   `scene.objects.link()` was removed).

**Editing existing geometry — `bmesh`.** For modifying a mesh (extrude, subdivide,
select-and-transform), `bmesh` is the code-side edit-mode:

1. `import bmesh`, create/get a bmesh from the mesh.
2. Operate on `bm.verts`, `bm.edges`, `bm.faces` (call `ensure_lookup_table()`
   before indexing).
3. Write back with `bm.to_mesh(mesh)` and `bm.free()`.

**The gotchas:**

- **Mode matters.** Some operations assume object mode, some edit mode. If an edit
  fails, check the active mode.
- **Re-runnability.** Clean up or reuse — a script that links a new object every
  run leaves `Cube.001`, `Cube.002`, … Delete previous output at the top, or reuse
  a named object.
- **Prefer data API over ops for structure.** `bpy.data.meshes.new` beats
  `bpy.ops.mesh.primitive_*` when you want control and predictability.

Build small, run in Blender, watch the mesh appear, then add complexity.
