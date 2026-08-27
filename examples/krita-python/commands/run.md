---
description: How to actually RUN a PyKrita script — inside Krita via the Scripter, never with plain python. The first thing beginners get wrong.
---
Help the user run a Krita Python script correctly. Reach for this first: the #1
beginner mistake is trying to run a PyKrita script with plain `python`, getting an
import error on `krita`, and thinking the script is broken. It is not — the
`krita` module only exists **inside Krita**.

Explain and set up the right route for their goal:

1. **Learning / iterating** → the **Scripter**: in Krita, Tools > Scripts >
   Scripter opens an editor with a Run button and an output pane. Paste or open
   the script, Run, read the output/errors in the pane. This is the fastest loop.
2. **A reusable tool** → an **installed Python Plugin**: a folder under Krita's
   `pykrita` resource directory (Settings > Manage Resources... shows the resource
   folder) with a `.desktop` file, enabled in Settings > Configure Krita > Python
   Plugin Manager. See the `docker-plugin` skill for the layout.

Confirm the Krita version (Help > About Krita) so the API matches. Then help the
user get **one tiny script** running in the Scripter first — e.g. print the active
document's name and size:

```
from krita import Krita
doc = Krita.instance().activeDocument()
print(doc.name() if doc else "no document open")
```

Proving the run path works (and that a document is open) beats any bigger script.
Never suggest `python script.py` for a PyKrita script.
