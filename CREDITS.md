# Credits

Who and what jichi owes. None of this is a licence condition — see
[`docs/LICENSING.md`](docs/LICENSING.md) for the terms and
[`NOTICE`](docs/licenses/NOTICE.Apache-2.0) for the short form that travels with
redistributions.

## Authorship

**Alexander-Lars Dallmann** — the author. The copyright is held by
**Justus-Liebig-Universität Gießen** (§ 69b UrhG: the employer exercises the
economic rights in software written in the course of employment; authorship
stays with the author). Every source file carries both lines, and
`jichi --version` prints them.

**Claude (Anthropic)** — the implementing agent for essentially all of the code,
tests and documentation in this tree, under the author's direction, across the
milestones recorded in [`docs/ROADMAP.md`](docs/ROADMAP.md). Credited as a tool
and collaborator, deliberately **not** as a copyright holder: copyright generally
requires human authorship, so naming a model as a holder would be both wrong and
unenforceable. The project record does not hide this — see
[`docs/ANECDOTES.md`](docs/ANECDOTES.md), which keeps the agent's mistakes in the
same file as its successes.

## Specified against

**Continue** (<https://github.com/continuedev/continue>, Apache-2.0) — the
*specification*: feature set, interaction model, configuration format, tool
names. **No code is shared.** Continue is roughly 39k lines of
TypeScript/React/Node; jichi is C89 throughout, written from the observed
behaviour rather than from the source. Its licence therefore imposes nothing on
this tree; the debt is real and acknowledged with thanks anyway.

**The cJSON API** (<https://github.com/DaveGamble/cJSON>, MIT) — the shape of the
JSON interface. `src/json/cJSON.c` is an original C89 implementation of the
subset jichi uses, so that the real library can replace it as a drop-in pair.
Again: no code taken, and no MIT obligation inherited.

## Standing on

The C89 standard library, and the six operating systems the platform matrix is
measured on rather than assumed about — Linux, FreeBSD, OpenBSD, NetBSD,
illumos and Windows (Cygwin and MSYS2). macOS is on the matrix too, as the one
row marked **Never compiled**: no Mac has ever built this tree, and the matrix
says so rather than assuming it would work. Each row in
[`docs/PLATFORMS.md`](docs/PLATFORMS.md) exists because someone ran the tier
there.

## Institutional context

Developed at Justus-Liebig-Universität Gießen (HRZ). The licensing question was
answered at M619 (2026-08-27): **Apache-2.0**, copyright Justus-Liebig-
Universität Gießen, and the tree has been public since. See
[`docs/LICENSING.md`](docs/LICENSING.md).
