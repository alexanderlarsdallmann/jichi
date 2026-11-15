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

Two agents implemented the tree, under the author's direction, across the
milestones recorded in [`docs/ROADMAP.md`](docs/ROADMAP.md):

**Claude (Anthropic)** — the implementing agent from the first commit
(2026-06-18) into October 2026. Most of the tree is still its work: at 0.13.0,
about 72% of the lines in `src/`, `include/`, `tests/` and `docs/` come from
commits Claude co-authored. Since October 2026 it mostly plans and supervises
the drives in which jichi implements itself.

**jichi** — this program, implementing itself since 2026-10-05. In a drive, a
team of jichi agents works on jichi's own source (set up and driven today by
`jichi-harness`): a lead plans and reviews, workers write the code, tests and
documentation, and nothing reaches master without a green `make ci` on the
exact commit. Its models are
ones the university hosts itself: `jlu/qwen3.8-27b` as the lead and
`jlu/qwen3-coder-next` as the workers. At 0.13.0, 1,524 of master's 3,337
commits carry jichi's co-author trailer, 77% of the commits since 0.12.0, and
about a quarter of the tree's lines come from them.

Both are credited as tools and collaborators, deliberately **not** as copyright
holders: copyright generally requires human authorship, so naming a program or
a model as a holder would be both wrong and unenforceable. The project record
does not hide this — see [`docs/ANECDOTES.md`](docs/ANECDOTES.md), which keeps
the agents' mistakes in the same file as their successes.

## Origins and design

**Continue** (<https://github.com/continuedev/continue>, Apache-2.0) — the
starting point. For jichi's first phase, from the first commit on 2026-06-18 to
about the end of June (roughly M1–M80: the agent loop and its tools,
configuration, retrieval, MCP, LSP and ACP), Continue's feature set, interaction
model, configuration format and tool names served as the specification. **No
code is shared.** Continue is roughly 39k lines of TypeScript/React/Node; jichi
is C89 throughout, written from the observed behaviour rather than from the
source, so Continue's licence imposes nothing on this tree. What remains of it
today is a migration path (`jichi-convert` still reads Continue
configurations), some of the vocabulary, and a debt acknowledged with thanks.

**The cJSON API** (<https://github.com/DaveGamble/cJSON>, MIT) — the shape of the
JSON interface. `src/json/cJSON.c` is an original C89 implementation of the
subset jichi uses, so that the real library can replace it as a drop-in pair.
Again: no code taken, and no MIT obligation inherited.

**Other projects** were studied along the way, and each taught jichi something
specific. Open source, read from its source: **opencode**
(<https://github.com/anomalyco/opencode>) — the shape of the slash commands, and
`AGENTS.md` as the rules file; **qwen-code**
(<https://github.com/QwenLM/qwen-code>) — neutralising a repository's own git
configuration, keeping the head and the tail of a long command's output, and
evidence on how long a stream from a slow local model may stay silent. Open
source, read from their documentation: **Gemini CLI** and **Codex** — how far to
trust a project's own configuration files. Not open source, so only its
published behaviour: **Claude Code** (Anthropic) — compaction, `CLAUDE.md` as the
fallback rules file, and an early band of features matched against it
(M24–M27). As with Continue, no code was taken.
[`docs/COMPARED.md`](docs/COMPARED.md) records what was read, from which tree and
when, with a wider survey of 373 command-line agents and harnesses.

**The design since** has been shaped by jichi's author: the author's vision,
requirements and experience in software development, worked out in discourse
with Claude and, since October 2026, with jichi itself. Most of what jichi is
today has no counterpart in Continue — the autonomy envelope, the learner
curriculum and reading tracks, the observability stack, the platform matrix and
its gates, the self-driving harness. The decisions, with the alternatives they
rejected, are recorded in [`docs/DECISIONS.md`](docs/DECISIONS.md),
[`docs/proposals/`](docs/proposals/), [`docs/ROADMAP.md`](docs/ROADMAP.md) and
[`docs/dialogues/`](docs/dialogues/).

## Standing on

The C89 standard library, and the operating systems the platform matrix is
measured on rather than assumed about — Linux (glibc and musl, from Debian 5 to
current, on physical x86-64, Arm and RISC-V machines and on emulated
architectures), FreeBSD, OpenBSD, NetBSD, illumos, GNU/Hurd, Haiku, macOS and
Windows (WSL2, Cygwin and MSYS2). macOS was marked **Never compiled** until
**Dima Mellnik** built, verified and drove jichi on a MacBook Air M4 (M817,
2026-09-26); it is **Partly verified**, because `make ci` has not run on a Mac.
FreeMiNT and FreeDOS have only been cross-compiled, and are marked **Never
compiled**. Each row in [`docs/PLATFORMS.md`](docs/PLATFORMS.md) exists because
someone ran the tier there.

## Institutional context

Developed at Justus-Liebig-Universität Gießen (HRZ). The licensing question was
answered at M619 (2026-08-27): **Apache-2.0**, copyright Justus-Liebig-
Universität Gießen, and the tree has been public since. See
[`docs/LICENSING.md`](docs/LICENSING.md).
