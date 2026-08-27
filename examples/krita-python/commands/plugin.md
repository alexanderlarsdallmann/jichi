---
description: Scaffold a minimal, installable Krita Python plugin — the .desktop manifest, the package, and the Extension/setup pattern done right.
---
Scaffold a minimal but correct Krita **Python plugin** the user can install and
enable. Beginners usually get the `.desktop` manifest or the folder layout wrong,
so build the skeleton carefully — Krita silently ignores a malformed plugin.

A plugin is a folder under Krita's `pykrita` resource directory with, at minimum:

1. **`your_plugin.desktop`** — the manifest Krita reads to list the plugin. It
   declares the library name (matching the Python package), the service type, and
   Python compatibility. A wrong `X-KDE-Library` or a mismatched name means the
   plugin never appears.
2. **`your_plugin/__init__.py`** — the Python package, whose name matches the
   `.desktop` library, importing and instantiating the extension.
3. **An `Extension` subclass** (`krita.Extension`) with:
   - `setup(self)` — called once when Krita starts.
   - `createActions(self, window)` — register menu/toolbar actions.
   - your action callbacks.
   And registering it with `Krita.instance().addExtension(...)`.

If the plugin adds a **Docker** (a dockable panel — NOT a container), that is a
`DockWidget` + a `DockWidgetFactory` registered with
`Krita.instance().addDockWidgetFactory(...)`; see the `docker-plugin` skill.

Explain the install flow (drop the folder in the resource `pykrita` dir, or use
Tools > Scripts > Import Python Plugin from File, then enable in the Python Plugin
Manager and restart Krita), and how to iterate. Keep it minimal — a tiny working
plugin the user extends beats a broad broken one. Suggest the `pykrita-reviewer`
before enabling.
