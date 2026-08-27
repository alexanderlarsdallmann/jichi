---
name: krita-python-api
description: The mental model of PyKrita — where it runs, the Krita.instance entry point, the Document/Node object tree, and actions.
---
# The PyKrita mental model

Before any specific code, hold these ideas about PyKrita; they explain most
beginner confusion.

**1. It runs inside Krita.** `from krita import Krita` only works in Krita's
embedded Python — the Scripter (Tools > Scripts > Scripter) or an installed
plugin. Plain `python script.py` fails on the `krita` import. That is the wrong
launcher, not a broken script. There is no `pip install krita`.

**2. `Krita.instance()` is the door.** Everything hangs off it:

- `Krita.instance().activeDocument()` — the open `Document` (or `None`; always
  guard for none).
- `Krita.instance().action("id")` — a menu/toolbar action you can `.trigger()`.
- `Krita.instance().addExtension(...)` / `addDockWidgetFactory(...)` — how a
  plugin plugs in.
- documents, windows, resources, notifiers all reachable from here.

**3. The object tree: Document > Node.** A `Document` has a tree of `Node`s
(layers and groups). You create nodes (`doc.createNode("Layer 1", "paintlayer")`),
add them (`parent.addChildNode(node, above)`), read/write pixel data, and make
selections. The active node is `doc.activeNode()`; the root is `doc.rootNode()`.

**4. Make changes stick.** After editing a document, call
`doc.refreshProjection()` so the composite updates, and `doc.waitForDone()` where
an operation runs asynchronously. Forgetting these is the usual "my script ran but
nothing changed."

**5. A "Docker" is a panel.** In Krita, a Docker is a *dockable UI panel* (Layers,
Tool Options are dockers) — NOT a Docker container. A plugin that adds a panel
adds a docker via `DockWidget` + `DockWidgetFactory`. Do not let a web search for
"krita docker" send you to containers.

Hold these five and the specific commands stop being magic and start being
obvious.
