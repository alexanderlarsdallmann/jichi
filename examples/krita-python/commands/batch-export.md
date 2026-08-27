---
description: Automate exporting or processing many files with PyKrita — open, transform, export in a loop, cleanly and without clobbering.
---
Write a PyKrita script that processes **many files** — the automation Krita
scripting is great for: batch-exporting `.kra` files to PNG, resizing a folder of
images, applying a step across a set.

Guide the approach:

1. **Iterate a real list of files** (a folder glob), not hard-coded names, so it
   scales and re-runs.
2. **Open, process, export, close** each in turn:
   - `Krita.instance().openDocument(path)` to load (headless-ish; still runs
     inside Krita).
   - do the work (see `/action`), calling `doc.refreshProjection()` /
     `doc.waitForDone()` so changes are applied before export.
   - `doc.exportImage(out_path, InfoObject())` (or `saveAs`) with the right
     parameters for the format.
   - `doc.close()` so documents do not accumulate and eat memory.
3. **Do not clobber silently.** Write to a separate output folder, or confirm
   before overwriting an existing file. A batch that quietly overwrites
   originals is a disaster.
4. **Report progress and errors per file**, and keep going on one failure rather
   than aborting the whole batch — print which files succeeded and which did not.

Test on **two or three files first**, in the Scripter, before running the whole
folder. Set any randomness with a seed. See the `batch-processing` skill for the
open/process/export/close pattern in full. Then run the `pykrita-reviewer` to
catch a clobber or a missing `close()`.
