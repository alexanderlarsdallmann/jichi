# Copyright and licensing

Two questions, and since **2026-08-27** both are answered: the copyright is held
by Justus-Liebig-Universität Gießen with Alexander-Lars Dallmann as the author,
and the licence is **Apache-2.0**.

## Current state

*This is the one section that changes when the decision changes. `license_lint`
check 10 fails if it describes a state the tree has left.*

| | |
| --- | --- |
| Copyright holder | **Justus-Liebig-Universität Gießen** |
| Author | **Alexander-Lars Dallmann** |
| SPDX identifier in force | `Apache-2.0` |
| `LICENSE` file | present -- the verbatim Apache-2.0 text |
| Status | decided 2026-08-27; a deliberate switch (MIT is under consideration) is one `set-license.sh` command |

While the question was open (2026-07-27 to 2026-08-27) every source said
`LicenseRef-UNDECIDED` -- valid SPDX syntax for an identifier not on the SPDX
list. It was chosen over three alternatives on purpose:

* **No SPDX line at all.** Silence is not neutral. An unmarked public repository
  reads to most people as "presumably permissive", which is the one thing that
  must not be assumed here.
* **"All rights reserved".** True, but it invites nobody to ask, and it would have
  to be swept out of 476 files anyway.
* **Naming the leaning early** (`Apache-2.0` before the decision). That is a grant.
  A licence, once genuinely offered, is not easily withdrawn -- so a placeholder
  that cannot be mistaken for a licence name is the only honest option, and it is
  one greppable token when the sweep comes.

## Where the licence is stated, and only there

One source of truth, `include/jc_license.h`:

```c
#define JC_COPYRIGHT    "Copyright (c) 2026 Justus-Liebig-Universität Gießen"
#define JC_AUTHOR       "Author: Alexander-Lars Dallmann"
#define JC_LICENSE_SPDX "Apache-2.0"
```

Everything else derives from it or is checked against it:

| Surface | What it shows |
| --- | --- |
| every `.c`/`.h` in `src/`, `include/`, `tests/` | a three-line SPDX + copyright + author header |
| `jichi --version` | the holder, and `licence: <spdx-id>` |
| `jichi describe --output json` | the `copyright` and `license` fields |
| `LICENSE` at the tree root | the verbatim licence text -- the file that legally matters |
| `NOTICE` | installed only if the chosen licence propagates one |

`docs/licenses/` holds **candidate** texts. Nothing there grants anything; see its
README.

## Releasing, once the decision is in

    scripts/set-license.sh Apache-2.0        # -n first, to see the plan
    make clean && make -j4 WERROR=1 && make ci
    ./jichi --version                        # must print licence: Apache-2.0
    # edit the "Current state" table above, then commit

The script rewrites the SPDX line in every source and the define, copies the
verbatim text to `LICENSE`, installs `NOTICE` when the licence calls for one, and
then lists the prose that still mentions the placeholder. It does not commit: a
change touching every file in the tree gets read first.

It refuses an identifier with no verbatim text in `docs/licenses/`, rather than
writing a `LICENSE` whose contents it invented.

## What the lint holds down

`tests/smoke/license_lint.sh` (11 checks) is two-state, switching on whether
`LICENSE` exists:

* **no `LICENSE`** -- every source *must* say `LicenseRef-UNDECIDED`, and a source
  claiming a real licence with no `LICENSE` file present is a failure: a licence
  named only in a comment grants nothing.
* **`LICENSE` present** -- *no* source may still say `UNDECIDED`, `LICENSE` must be
  byte-identical to the candidate text for the identifier the headers name, and the
  `NOTICE` that licence propagates must be installed.

Both states also require: one identifier across all sources (never a mix), the
holder spelled identically in every file, `JC_LICENSE_SPDX` agreeing with the
headers, the candidate texts matching their checksums, `--version` and `describe`
reporting what the tree says, and `set-license.sh` still working.

## Third-party code: there is none

jichi vendors no third-party source. The one file that looks like it does,
`src/json/cJSON.c`, is an original C89 implementation of the *cJSON API* and
carries a PROVENANCE note saying so; it lived under `third_party/cjson/` until
M171, which was misleading in both directions. So there are no inbound licence
obligations to carry, no third-party notices to reproduce, and the `NOTICE`
template says as much.

Runtime dependencies are the C89 standard library and, optionally, a TLS
implementation the operator supplies; neither is distributed with the source.

## If Apache-2.0 is chosen: what it obliges

Not advice -- just the clauses that impose work on this project, so nothing is a
surprise later:

* **section 4(a)-(d)** -- redistributions carry the licence, state changes, keep
  attribution notices, and propagate `NOTICE`. `set-license.sh` installs the
  `NOTICE`; `make-snapshot.sh` already refuses to create the first public commit
  from a tree with no `LICENSE` in it.
* **section 3** -- an express patent grant from contributors, with a termination
  clause for anyone who sues over patents. This is the main reason Apache-2.0 is
  preferred over MIT/BSD for institutional software.
* **section 5** -- contributions are licensed inbound under the same terms unless
  stated otherwise, so no separate CLA is needed for ordinary patches.
* **section 6** -- no trademark rights are granted; the name "jichi" is not covered
  by the licence either way.
* **appendix** -- the boilerplate header. The three-line SPDX form used here
  (identifier, holder, author) is the now-standard short equivalent and is what
  SPDX tooling reads.

## The answer, and what it followed from

The institutional answer landed **2026-08-27**, and it is the outcome this page
anticipated: under German law (§ 69b UrhG) the economic rights in software
written by an employee in the course of their duties are exercised by the
employer -- hence the holder line names **Justus-Liebig-Universität Gießen** --
while authorship, and the author's name in the notice, remain with
**Alexander-Lars Dallmann** -- hence the author line. The licence chosen is
**Apache-2.0**; the operator has noted that the review may yet switch it to
**MIT**, and `docs/licenses/MIT.txt` stands ready so that switch is the same
one-command sweep (`scripts/set-license.sh MIT`), gated by the same lint.
Applying the answer cost one command, as prepared.
