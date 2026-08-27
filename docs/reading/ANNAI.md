# 案内（あんない）*Annai* — the guided tour

*The beginner edition of the jichi source reading guides: junior
developers and curriculum Stage 0–2 readers. The expert edition is
[深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md); the traced-run
edition, which starts from one recorded run instead of from the code, is
[追跡（ついせき）*Tsuiseki*](TSUISEKI.md) — read it after chapter 5 for the
same tool round with every real value in front of you. Design and
scope: [`docs/plans/2026-08-reading-guides.md`](../plans/2026-08-reading-guides.md);
the map of everything taught is [CURRICULUM.md](../CURRICULUM.md).*

You type a sentence; a file changes. This guide follows that sentence all
the way through the program, one chapter at a time — pseudo code before C,
a diagram before either, and something to *do* at the end of every chapter.
Every C idiom and every AI concept (tokens, context, reasoning, tool
calling) is introduced the first time it is needed, from zero.

**Assumption:** you have built jichi (curriculum Module 0) and can run
`make`, `jichi doctor`, and one `-p` turn. **Specifically the *checkout*
bench** — this guide reads jichi's own source, so the chapters open files
like `CLAUDE.md`, `src/main.c` and `tests/`, and compare `jichi map` against
a real `src/` tree. Module 0 offers two equivalent benches and the
separate-directory one does not have those files; `cd` into the jichi
checkout before starting. Reading without any bench — a repository browser,
a locked-down machine — is served by Appendix A, which gives every
experiment a read-only twin.

**A reading order, if you want one** (from
[`../curriculum/INSTRUCTOR.md`](../curriculum/INSTRUCTOR.md) §0.5, which is
the only place it was written down): chapters **1–2** alongside curriculum
Module 0, **3–5** across Modules 1–2, **6** whenever the wire interests you,
**7–8** with Module 8, and **9** as reinforcement for Modules 3 and 5.
Reading all ten up front also works; reading none of them is the option this
line exists to prevent.

**Conventions:** code anchors are written `file.c:function_name` — open the
file, search for the name. Never line numbers (they rot; the
`tests/smoke/reading_refs_lint.sh` lint holds every anchor in this guide to
that rule mechanically, and holds the count below honest too). How to navigate
by name, and where each command belongs, is the next section.

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

The chapters point at code **44 times**, in the form `file.c:function_name`, and
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
| 1 | [What you are holding](annai-01-what-you-are-holding.md) | **shipped** |
| 2 | [A turn, from the outside](annai-02-a-turn-from-the-outside.md) | **shipped** |
| 3 | [main() to the loop](annai-03-main-to-the-loop.md) | **shipped** |
| 4 | [Tool calling, the whole idea](annai-04-tool-calling.md) | **shipped** |
| 5 | [One tool, all the way down](annai-05-one-tool-all-the-way-down.md) | **shipped** |
| 6 | [The wire](annai-06-the-wire.md) | **shipped** |
| 7 | [Memory, the jichi way](annai-07-memory-the-jichi-way.md) | **shipped** |
| 8 | [When the conversation gets too long](annai-08-when-the-conversation-gets-too-long.md) | **shipped** |
| 9 | [How jichi knows it works](annai-09-how-jichi-knows-it-works.md) | **shipped** |
| 10 | [Where to go next](annai-10-where-to-go-next.md) | **shipped** |
| A | [Reading without a bench](annai-a-no-bench.md) | **shipped** |

All chapters ship (M222–M223). The status column stays: the
[Fukabori](FUKABORI.md)'s chapters are still commitments, and this table's
format is the promise they will be marked the same way.
