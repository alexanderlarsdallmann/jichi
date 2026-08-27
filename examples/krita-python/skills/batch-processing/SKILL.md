---
name: batch-processing
description: The open-process-export-close pattern for automating many files in Krita, without clobbering originals or leaking memory.
---
# Batch processing in Krita

Automating a repetitive task across many files is where PyKrita shines — batch
export, bulk resize, apply-a-step-to-a-folder. The reliable shape is always the
same loop, and the mistakes are always the same too.

**The pattern, per file:**

```
1. open    -> doc = Krita.instance().openDocument(in_path)
2. process -> ... edits ...; doc.refreshProjection(); doc.waitForDone()
3. export  -> doc.exportImage(out_path, InfoObject())   # or doc.saveAs(...)
4. close   -> doc.close()
```

Wrap that in a loop over a real file list (a folder glob), not hard-coded names.

**The mistakes to avoid:**

- **Clobbering originals.** Write to a *separate output folder*, or check for an
  existing file before overwriting. A batch that silently overwrites the inputs
  is a genuine disaster — treat the originals as read-only.
- **Not closing documents.** Every `openDocument` that is not `close`d stays in
  memory; a big batch will balloon and slow to a crawl. Always close.
- **Changes not applied before export.** If you edit then export without
  `refreshProjection()` / `waitForDone()`, you can export the *un*-changed image.
- **All-or-nothing failure.** One bad file should not abort the whole run. Wrap
  each file in try/except, keep going, and print which succeeded and which failed.

**Before running the whole folder, test on two or three files** in the Scripter,
and confirm the output looks right. Report progress as you go (`N of M`), so a
long batch is not a silent black box. Still runs inside Krita — batch does not
mean plain `python`.
