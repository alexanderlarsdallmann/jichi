---
description: Write a script that manipulates the active document — layers, selections, transforms — via the PyKrita object model.
---
Write a PyKrita script that does something to the **active document** — add or
merge layers, make a selection, apply a change, read pixel data. This is the core
of most Krita scripting.

Guide the approach through the object model:

1. **Entry point.** `Krita.instance()` — the running app. `.activeDocument()` for
   the open document (guard for `None`: a script that assumes a document is open
   will fail with no document).
2. **Document > Node.** A `Document` has a tree of `Node`s (layers/groups). Get
   the active node (`doc.activeNode()`), the root (`doc.rootNode()`), create paint
   layers (`doc.createNode(name, "paintlayer")`) and add them
   (`parent.addChildNode(node, above)`).
3. **Make changes stick.** After editing, call `doc.refreshProjection()` (and
   `doc.waitForDone()` where an operation is async) so Krita and the UI reflect
   the change — a common "nothing happened" cause.
4. **Menu actions when needed.** `Krita.instance().action("id").trigger()` runs
   the same action a menu item does, when there is no direct API for it.

Keep it small and run it in the Scripter (`/run`) after each step, watching Krita
respond. Make it re-runnable (do not add a duplicate layer every run). Then suggest
the `pykrita-reviewer` to catch an API-change-that-does-not-stick or a
no-document assumption.
