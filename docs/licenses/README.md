# Candidate licence texts -- NOT the licence of this tree

**Nothing in this directory grants you any licence.** These are verbatim copies of
licence texts kept here so that applying (or deliberately switching) the licensing
decision is a one-command step rather than an afternoon of editing ~500 file
headers by hand.

The licence of this tree is whatever the file `LICENSE` at the repository root
says -- since **2026-08-27** that is the verbatim Apache-2.0 text, and the SPDX
header in every source file agrees:

    /* SPDX-License-Identifier: Apache-2.0
     * Copyright (c) 2026 Justus-Liebig-Universität Gießen
     * Author: Alexander-Lars Dallmann */

(While the decision was pending, the identifier was the deliberate placeholder
`LicenseRef-UNDECIDED` -- valid SPDX for "not on the SPDX list", unmistakable for
a grant, and one greppable token for the sweep that then came.)

## What is here

| File | What it is |
| --- | --- |
| `Apache-2.0.txt` | The verbatim Apache License 2.0 text. **In force since 2026-08-27.** |
| `MIT.txt` | The verbatim MIT (Expat) text, holder filled in -- ready should the review switch to it. |
| `NOTICE.Apache-2.0` | The `NOTICE` file Apache-2.0 section 4(d) propagates, installed only if that licence is chosen. |
| `SHA256SUMS` | Checksums of the texts above. `license_lint` verifies them, so a candidate cannot drift or be edited unnoticed. |

## Release, once the decision is in

    scripts/set-license.sh Apache-2.0

That copies the text to `LICENSE`, installs `NOTICE` if the licence calls for one,
and rewrites every `LicenseRef-UNDECIDED` in the tree to the chosen identifier.
`tests/smoke/license_lint.sh` then flips from "no LICENSE, so every file must say
UNDECIDED" to "a LICENSE exists, so nothing may still say UNDECIDED" -- the tier
fails if the sweep left anything behind. See `docs/LICENSING.md`.

## Adding another candidate

Two steps, in this order:

1. Put the **verbatim** text at `docs/licenses/<spdx-id>.txt`. Do not reflow it, do
   not add a banner, do not fill in the appendix brackets -- a modified licence text
   is not that licence.
2. Append its checksum to `SHA256SUMS` (`cd docs/licenses && sha256sum <file> >> SHA256SUMS`).

`set-license.sh` refuses any identifier with no text file here, on purpose: it will
not write a `LICENSE` whose contents it had to invent.
