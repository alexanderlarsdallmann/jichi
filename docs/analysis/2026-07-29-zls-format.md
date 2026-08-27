# M195: the `format_file` / zls reproduction

*2026-07-29. The investigation M195 scoped, run against a real `zls`. Deliberately
an investigation and not a fix: three data points were too thin to write code
against, and a wrong guess on this path silently rewrites the user's source files.*

## The question

The 2026-07-28 zigodot log recorded **`format_file` 0/3**, every call returning
`error: the server returned malformed formatting edits`. Is the reply shape one
jichi mishandles, or one the server gets wrong?

## Setup

`zls 0.16.0` (matching `zig 0.16.0`), prebuilt release artifact, signature verified
against zigtools' published minisign key (`RWR+9B91GBZ0zOjh...`) and the installed
binary confirmed byte-identical to the signed tarball. A deliberately misformatted
`main.zig`, with `zig fmt --stdin` as ground truth.

## What actually happens

**1. `format_file` works.** Driven through the real tool with the HRZ model, jichi
reformatted the file and the result is **byte-identical to `zig fmt`**. zls's reply
is a clean `TextEdit[]` — 7 edits, mostly zero-width ranges (`start == end`, pure
insertions) plus two deletions:

```json
{"range":{"start":{"line":2,"character":11},"end":{"line":2,"character":11}},
 "newText":" "}
```

jichi's applier handles those correctly: `ee < s` skips a reversed range, `ee == s`
is kept, and the single-pass splice skips only genuinely overlapping edits.

**2. The observed failure does not reproduce.** Not with a misformatted file, not
with an already-formatted one, not with an unparseable one.

**3. The transport is sound.** `jc_lsp_framer_pop` is a proper accumulating framer —
it returns 0 until `len >= hdr + 4 + Content-Length`, so it cannot deliver a
truncated body. Worth stating explicitly because M201 had just found sixteen
*Python* readers with exactly that bug; jichi's own is not one of them.

**4. An error reply produces a different message.** A JSON-RPC error response has
no `result` member, so `lsp_request` leaves `*result_out` NULL, `jc_lsp_format`
returns NULL, and the tool says *"formatting failed (no matching language server or
no response)"* — not "malformed".

## So what would produce the observed message?

`jc_lsp_apply_text_edits` returns −1, and therefore that message, only when the
`result` is **present and is neither `null` nor an array**. Verified by direct call
on the pure function:

| `result` | applied | user sees |
|---|---|---|
| `null` | 0 | "Already formatted (no changes)." |
| `[]` | 0 | "Already formatted (no changes)." |
| `{"edits":[]}` (an object) | **−1** | **"malformed formatting edits"** |

So the log's 3/3 failures required a server reply of a non-array, non-null shape.
zls 0.16.0 does not produce one. The `null` case — the obvious candidate — was
already fixed on **2026-07-08** (`6c6dc89`), three weeks *before* the failing log,
so it cannot be the cause either.

**Conclusion: not reproducible, and the cause remains unidentified.** The honest
options are a different zls version in that program's environment, or a shape
produced under conditions this reproduction did not recreate. **No fix was
written.** A defensive "accept an object too" would be a guess on the one code path
that rewrites source files, and the whole reason M195 was scoped as an
investigation is that guessing there is the expensive mistake.

## What the investigation did find: a misleading success message

For a file with a **syntax error**, zls replies `result: null` — correct, it cannot
format what it cannot parse. jichi's null handling then reports:

> `Already formatted (no changes).`

That is wrong in a way that matters to an agent. The file is broken, not tidy. A
model that has just introduced a syntax error, calls `format_file`, and is told
"already formatted" has been told its file is fine. The M198 lesson applies exactly:
*an invisible failure is a correctness boundary when the consumer is the model.*

The 2026-07-08 fix was right to stop treating null as an error — that produced a
false failure on an already-formatted file. But it collapsed two distinct states
into one message: "nothing to change" and "I could not read this well enough to
change anything". They deserve different words.

This one **is** safe to fix, because it changes only the message, never the edits.
The distinguishing signal is available for free: an unparseable file has
diagnostics, and jichi already collects `publishDiagnostics` for every opened
document (`jc_lsp_diagnostics`).

## Recommendation

1. **Distinguish the two null cases** in `format_file`: when the reply is null *and*
   the document has error-severity diagnostics, say so — "cannot format: the file
   has syntax errors (N diagnostics)" — instead of "already formatted". Message
   only; no change to the applier.
2. **Leave the malformed path alone** until a reply that triggers it is actually
   captured. Add the observed `result` shape to the error message so the next
   occurrence arrives with evidence — the M201 lesson about making failures
   self-documenting, applied here.
3. **Re-check with the zls version that program used**, if it can be identified.
   Absent that, M196's drive is the next chance to catch it in the wild.
