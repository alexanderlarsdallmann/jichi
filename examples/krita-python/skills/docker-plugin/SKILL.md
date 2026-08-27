---
name: docker-plugin
description: The anatomy of a Krita Python plugin — the .desktop manifest, the package layout, the Extension, and (a Krita) Docker panel.
---
# The plugin layout — .desktop, package, Extension, Docker

A Krita Python plugin is a folder Krita can discover, list, and enable. Two things
make it valid, and beginners usually get one wrong — Krita then just silently does
not show the plugin.

**1. The folder lives under Krita's `pykrita` resource directory** (Settings >
Manage Resources... reveals the resource folder; `pykrita/` is inside it), and
contains two matching things:

```
pykrita/
  my_plugin.desktop      <- the manifest
  my_plugin/             <- the package (name matches the .desktop library)
    __init__.py
    my_plugin.py
```

**2. `my_plugin.desktop` — the manifest.** A KDE-style desktop file Krita reads to
list the plugin. The essentials: the service type, the library name (which MUST
match the package folder), a human name/comment, and Python compatibility. A
wrong library name or a typo here means the plugin never appears in the Python
Plugin Manager — with no error.

**3. The `Extension`.** In the package, subclass `krita.Extension`:

- `setup(self)` — runs once when Krita starts.
- `createActions(self, window)` — register your menu/toolbar actions here.
- your callbacks doing the work.

Register it at import time: `Krita.instance().addExtension(MyExtension(Krita.instance()))`.

**4. A Docker (a panel), if you want one.** "Docker" here = a *dockable panel*
(not a container). Subclass `krita.DockWidget`, build your Qt UI in it, and
register a `DockWidgetFactory`:

```python
Krita.instance().addDockWidgetFactory(
    DockWidgetFactory("myDocker", DockWidgetFactoryBase.DockRight, MyDocker))
```

**Installing / iterating:** drop the folder in `pykrita/` (or Tools > Scripts >
Import Python Plugin from File), enable it in Settings > Configure Krita > Python
Plugin Manager, and restart Krita. During development you re-enable / restart to
reload. Start with a tiny working plugin and extend it.
