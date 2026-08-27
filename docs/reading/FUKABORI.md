# 深掘り（ふかぼり）*Fukabori* — the deep dive

*The expert edition of the jichi source reading guides: advanced
developers, experts, curriculum Stage 3+ readers. The beginner edition is
[案内（あんない）*Annai* — the guided tour](ANNAI.md); the traced-run edition is
[追跡（ついせき）*Tsuiseki*](TSUISEKI.md), which argues nothing and quotes a
recorded run instead — chapter 4's state machine, as bytes. Design and scope:
[`docs/plans/2026-08-reading-guides.md`](../plans/2026-08-reading-guides.md).*

Where the Annai follows one request through the system, this edition takes
one **architectural decision per chapter** and defends it against its
alternatives: the invariants first, then the code that carries them, then
the failure that taught them (the anecdotes ship with this repository for
exactly that use). C idioms go unremarked; trade-offs get argued.

**Conventions:** as the Annai — anchors are `file.c:function_name`, never
line numbers, held by the `tests/smoke/reading_refs_lint.sh` lint. Where to type
each command, and what to have open, is the next section.

## Before you start: where to type, and what to have open

Two questions the chapters used to leave you to infer. Both are answered here
once, and every command block repeats the answer on its first line.

### You will be working in two places

| Place | What belongs there | How the guides mark it |
|---|---|---|
| **The jichi checkout** — the directory you cloned and ran `make` in | anything with `make`, `./jichi`, or a relative path into the tree (`src/`, `tests/`) | `# in the jichi checkout` |
| **A throwaway directory** — `/tmp/something`, empty, yours to wreck | every exercise where the agent *edits files* | the block creates and enters its own: `mkdir /tmp/x && cd /tmp/x` |

**Never point an editing agent at the jichi checkout.** Not because it is
dangerous to jichi — it is your copy — but because you cannot tell your reading
from its writing afterwards, and a chapter's exercise should leave the source you
are studying byte-identical. Where a chapter *must* read the real tree (grepping
`src/`, or chapter 7's parallel summaries), it either only reads or it fences the
writes, and it says which.

**Two terminals is the comfortable setup**: one parked in the checkout, one in the
scratch directory. With one terminal, `cd -` jumps back to where you were.

### `jichi` or `./jichi`?

Both appear, and the difference is not a typo:

- **`./jichi`** — run the binary sitting in the checkout. Always works right after
  `make`, and is unambiguous about *which* build you are running.
- **`jichi`** — the one on your `$PATH` (after `make install`, or a symlink). The
  chapters use this in `/tmp` exercises, where `./jichi` would not resolve.

If a `/tmp` block says `jichi` and your shell says *command not found*, either
install it or use the absolute path: `~/src/jichi/jichi` (wherever you cloned).

### Have the source open — this is a reading guide, not a manual

The chapters point at code **57 times**, in the form `file.c:function_name`, and
each of those is an instruction: *open that file and go to that function.* Reading
the prose without the code in front of you is possible and about half as useful.

Line numbers are deliberately never used — they rot, and a lint enforces their
absence — so you navigate by **name**:

- in an editor: open the file, search for the function name;
- in a terminal: `grep -n jc_agent_run_turn src/chat/jc_agent.c` gives you the
  line, and `less +/jc_agent_run_turn src/chat/jc_agent.c` opens straight at it;
- with `ctags`/an LSP set up, jump to definition as usual.

Any editor is fine — this project has no house editor and needs no plugin. If you
have **no editor and no checkout** (a browser, a locked-down machine), the Annai's
[Appendix A](annai-a-no-bench.md) gives every exercise a read-only twin.

## Chapters

| # | Chapter | Status |
|---|---|---|
| 1 | [Why C89, and what it cost](fukabori-01-why-c89.md) | **shipped** |
| 2 | [The provider abstraction](fukabori-02-the-provider-abstraction.md) | **shipped** |
| 3 | [The three-arena lifetime model](fukabori-03-the-three-arena-lifetime-model.md) | **shipped** |
| 4 | [The agent loop as a state machine](fukabori-04-the-agent-loop-as-a-state-machine.md) | **shipped** |
| 5 | [Context economics](fukabori-05-context-economics.md) | **shipped** |
| 6 | [The autonomy envelope](fukabori-06-the-autonomy-envelope.md) | **shipped** |
| 7 | [Fork-based parallelism](fukabori-07-fork-based-parallelism.md) | **shipped** |
| 8 | [Streaming and the no-buffering invariants](fukabori-08-streaming-and-the-no-buffering-invariants.md) | **shipped** |
| 9 | [Sessions, snapshots, and the two histories](fukabori-09-sessions-snapshots-two-histories.md) | **shipped** |
| 10 | [The test architecture as a system](fukabori-10-the-test-architecture-as-a-system.md) | **shipped** |
| 11 | [AI-supported coding, examined](fukabori-11-ai-supported-coding-examined.md) | **shipped** |
| 12 | [The migration road](fukabori-12-the-migration-road.md) | **shipped** |

All twelve chapters ship (M224–M225). Each routes into the primary
sources rather than restating them — [`docs/ROADMAP.md`](../ROADMAP.md)
(every milestone's design), `docs/analysis/` (the measured
investigations), and [`docs/ANECDOTES.md`](../ANECDOTES.md) (the war
stories) — adding the *reading order* and the argument, not the facts,
which live there.
