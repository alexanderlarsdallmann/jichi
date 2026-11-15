# Five defects in the harness port, and the course that found four of them

*2026-11-13. Milestone M1178. Written for self-learners, junior developers, and
agents. The harness is `jichi-harness`; its reading course is
[Tazuna](../reading/TAZUNA.md), and its reference is
[`docs/HARNESS.md`](../HARNESS.md).*

## In three sentences

The C port of the drive's host scripts had five defects that every test passed
over. Two were in the gate watcher's reading of its inputs, one in the
ledger's memory, one in how the gate builds shell commands, and one in a test
fixture that did not look like the program it stood in for. Four were found by
the agents writing the harness's reading course, because describing a function
honestly means reading every line and running every example; the fifth was
found by the supervisor replaying a real event.

## 1. How they were found

**Writing the course.** On 2026-11-13 the operator asked for a reading course
for the harness. The chapters were drafted by Claude agents, each told to quote
only what the code says and to run every command it published. The agent
writing chapters 7 (`watch`) and 8 (`gate run`) reported four places where the
code did something its comments and its reference script did not, each with a
measurement. The supervisor re-measured each one before calling it a defect.

**Replaying a real event.** The port ran for days as a dry shadow beside the
watcher script, and the two agreed on every tick. On the morning of 2026-11-13
the supervisor replayed one moment the shadow had missed (a regate tick fired
by hand while a gate ran). The two disagreed: the script waited for the running
gate, and the port would have started a second one.

Agreement on real events had not been enough, because no real event had
reached the code that was wrong. Section 6 comes back to this.

## 2. The watcher's busy filter could not see a running gate

**What it should do.** Before starting a gate, the watcher lists the gate
services that are running and waits if any is. Two `make ci` runs at once on
one machine is a standing rule broken.

**What it did.** It took the first space-delimited token of each line of
`systemctl --user list-units --no-legend`. But systemctl starts every line with
two spaces (a status column), so the token was always empty, and nothing was
ever busy:

    $ systemctl --user list-units ... --no-legend 'jhp-*' | od -c | head -1
    0000000           j   h   p   -   ...

The script it was ported from used `awk '{print $1}'`, and awk's first field
skips leading blanks. The port kept the idea and lost that detail.

**Why the tests were green.** The test's stand-in for `systemctl` printed the
unit names flush-left. The fixture did not behave like the real program, so the
parse was tested against a shape no machine produces.

**The fix.** Skip the blanks before taking the token. The parse also skips a
`●` and its blank, which systemd puts in that column for a failed unit
(measured afterwards with systemd 255: a transient unit that ran `/bin/false`
listed as `342 227 217`, a blank, then the name). The list asks only for active
and activating units, so the glyph should not reach the filter, but the parse
does not depend on that. The stand-in now prints the real shape: two
spaces, the unit, then the LOAD, ACTIVE, SUB and DESCRIPTION columns. With it,
three of cell 15's five cases fail on the old code and pass on the new.

**A second difference in the same line, found by the replay.** The
supervisor replayed the 07:37 moment once more with the fixed binary, against a
scratch copy of push.git whose `origin/master` was set back to where it stood
then. The pinned binary still said "would start" for systemd's shape, and the
fixed one waited. But its say line ended `(jhp-gate-86b52f09.service)`, while
the script's ended `(jhp-gate-86b52f09.service )`. The script joins the names
with `tr '\n' ' '`, which leaves a space after every name, the last one too.
The shadow compares say lines byte for byte, so the first real busy event after
a switch-over would have read as a disagreement. The port now prints the
reference's bytes, and cell 15 checks the whole line.

**What else parses a host command's output?** Only one other place: the
`pgrep` pids in the team-smoke wait. Real `pgrep` prints each pid flush-left
(measured: `2565696\n`), and the parse reads digits from the start of the line,
so it is right for the real program and was left alone.

## 3. A withdrawal check that read the rest of the file

**What it should do.** A READY line can be withdrawn by a later LOG line that
says `WITHDRAWN` and names that commit (and no other) in one sentence. The
script read the LOG one line at a time.

**What it did.** The tick walked the LOG buffer line by line and called
`jc_gate_withdrawn(p, c7)`, where `p` points at the start of a line **inside**
the buffer. The function measured its input with `strlen`, which runs to the
end of the buffer, not the end of the line. So each call read the whole rest
of the LOG:

- A newline is not a sentence boundary, so a line saying "the earlier plan is
  WITHDRAWN" and the next line naming the READY commit became one sentence,
  and the batch read as withdrawn when nobody had withdrawn it.
- The function treats 64 KiB or more as "not a withdrawal". With that much LOG
  after a genuine withdrawal, the withdrawal was never seen, and the batch was
  gated.

**The fix.** A new `jc_gate_withdrawn_n(t, n, c7)` reads the first `n` bytes
only, and the tick passes each line's own length. `jc_gate_withdrawn(t, c7)`
stays, as `jc_gate_withdrawn_n(t, strlen(t), c7)`.

**The C lesson.** A pointer into a buffer is not a string. If the bytes you
mean end before the NUL, pass their length; a function that calls `strlen`
cannot know where your line ends.

## 4. A LOG of 1 MiB read as no LOG

**What it did.** The file reader refused any file of 1 MiB or more, and the
tick treats an unreadable LOG as "no LOG": a quiet tick, exit 0, nothing said.
The drive's LOG only grows. The live one had reached 474,487 B, so on some day
the watcher would have stopped gating, silently, for good.

**The fix.** The reader has no size cap now. It reads the whole file into the
arena, which gives an oversized request its own block. The 1 MiB constant still
bounds the output of a shimmed host command, which is what it was for.

**The lesson.** A cap on an append-only file is not a limit. It is a date.

## 5. The ledger freed a pointer it never set

**What it did.** `jc_hp_gateledger_run` declares `struct jc_vec accept` and
initialises it only inside the function that reads the accept-list file, which
runs only when an accept list is configured. Without one, the walk read the
vector's length and data, and the cleanup passed its data pointer to `free()`.
valgrind on the real binary, with no accept list:

    Conditional jump or move depends on uninitialised value(s)
       at jc_hp_gateledger_accept_listed
    Conditional jump or move depends on uninitialised value(s)
       at free / jc_vec_free / jc_hp_gateledger_run

The output was correct. The stack happened to hold harmless values there, so
every test passed, and a different compiler or a different call before this one
could have crashed it.

**The fix.** `jc_hp_gateledger_load_accept(path, a, accept)` initialises the
vector first and then reads the file if there is one. The run calls it
unconditionally. The unit test fills a vector with `0xA5` bytes before calling
it with no path, so a loader that skipped the initialisation fails a check on
every machine, not only under valgrind.

## 6. The gate pasted its argument into a shell

**What it did.** `gate run <SHA8>` builds commands like
`cd '<push repo>' && git rev-parse --verify -q <SHA8>^{commit}` and runs them
with `sh -c`. The `<SHA8>` was pasted bare, and nothing checked that it was a
commit id. A value containing `;` ran the rest of it on the host. The same
value also named the gate's record directory. The push repo's path was quoted
with `'...'`, so a path containing a `'` broke the `cd`, and every commit read
as "not in push.git".

The watcher only ever passes 7 to 40 hex characters, so the drive's normal path
was safe. A hand-typed `gate run` was not. The script it was ported from
passed `"$s^{commit}"` in double quotes, so git received the value whole.
The port lost that by building a string. (The script did name its record
directory from the value too, unchecked.)

**The fix.**
- `gate run` refuses a `<SHA8>` that is not 7 to 40 lowercase hex, with exit 2,
  before it touches the config, the gate home or the state file. These are the
  same words the `--stage ci` refusal uses.
- `gf_git` quotes every word, the repo path included, with the helper the stage
  commands already used (`gf_cmd_word`, from an earlier fix of the same class,
  M1159).

## 7. The proof

| defect | test | pre-M1178 binary | fixed |
|---|---|---|---|
| busy filter | `harness_gate_watch` cell 15, with the stand-in printing the real shape; the 07:37 replay on real data | letters a, c, d fail; the replay says "would start" | green; the replay waits, byte for byte the script's line |
| withdrawal across lines | `harness_gate_watch` cell 23; `test_watchgate` (16) | read as withdrawn | green |
| withdrawal with 64 KiB after it | `harness_gate_watch` cell 24 (about 108 KB after the line) | the gate started | green |
| LOG of 1 MiB or more | `harness_gate_watch` cell 25 (1,388,920 B) | quiet tick, no start | green |
| ledger accept list | `test_gateledger` (9b); valgrind on the binary | 3 checks fail; valgrind: uninitialised reads, one inside `free()` | green; valgrind clean |
| `<SHA8>` in the shell | `harness_gate_run` cell 96 | the injected `touch` ran, and a state line was written | refused, exit 2, nothing written |
| a `'` in the push repo path | `harness_gate_run` cell 97 | red-setup | red-not-ff (the setup reached the repo) |

Each unit check was also shown to fail with only its own fix reverted: the
loader's initialisation moved below the NULL check (3 failures), and the
length bound replaced by `strlen` (1 failure).

## 8. Lessons

1. **A fixture that does not behave like the real peer tests nothing about the
   peer.** The busy filter's stand-in printed a shape `systemctl` never prints.
   Look at the real output, byte by byte (`od -c`), before writing the stand-in.
2. **Agreement on real events is only as good as the events.** The shadow and
   the script agreed for days because no real event reached the wrong code
   while a gate ran. A replay of a chosen moment found what days of watching
   did not.
3. **Writing about code is a review of it.** Four of the five were found by
   agents whose task was to explain the code, not to test it. An explanation
   that must quote the code and run its examples cannot skip a line.
4. **"The output is correct" is not "the program is correct".** The ledger gave
   the right verdict while reading uninitialised memory. valgrind saw it; the
   tests could not.
5. **When you port a shell script, list what the shell did for free.** Its
   quoting, awk's field splitting and its line-at-a-time reading were all lost
   in the port without anyone deciding to lose them.
