# Review 8 — The interactive faces: the terminal, the setup wizard and the editor protocol

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the report
for `src/tui/`, `src/setup/` and `src/acp/`. It was paused at the operator's
request, but every section had been filled. Its six code excerpts were checked
against the source: five matched as cited, and one matched at a corrected range,
given below. Written by Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to understand how a C
program owns a terminal: raw mode, escape sequences, pasted text, Ctrl-C. It
also covers how jichi lets an editor drive it over a JSON protocol.

**After reading it you will be able to:**

- explain cooked mode, raw mode and jichi's third mode, the *hold*, and why the
  hold keeps Ctrl-C working;
- cut UTF-8 text without splitting a character, using one bit mask;
- read a line-framed protocol reader that runs in linear time;
- explain why a size cap must also say what happens to the excess;
- recognise where untrusted bytes must be cleaned before they reach a terminal.

**What you need first:** [TUI_PRINCIPLES.md](../TUI_PRINCIPLES.md),
[TYPE_AHEAD.md](../TYPE_AHEAD.md) and [ACP.md](../ACP.md) are the references.

---

## 1. Three ways to talk to jichi

**The terminal layer** (`src/tui/jc_term.c`). A terminal normally runs in
**cooked** mode: the kernel collects a whole line, echoes the keys, handles
Backspace, and turns Ctrl-C into the signal SIGINT. A line editor needs **raw**
mode instead, in which every key reaches the program at once and nothing is
echoed, so the program draws the line itself.

The settings live in a `struct termios`, as flags:

| Flag | What it does |
|---|---|
| `ECHO` | the kernel echoes typed keys |
| `ICANON` | the kernel delivers a whole line at a time |
| `ISIG` | Ctrl-C becomes a signal |
| `OPOST` | output is post-processed, for example LF becomes CR LF |

jichi uses three modes:

- **raw,** only while it edits a line;
- **the hold** (M254), while the agent works: no echo and no line buffering, so
  typed keys can be collected (type-ahead, Esc to stop), but `ISIG` and `OPOST`
  are kept, so Ctrl-C still aborts and streamed text still breaks lines
  correctly (§3.1);
- **the user's own mode,** restored in between.

**The REPL** (`src/tui/jc_tui.c`). `jc_tui_run` opens or resumes a session,
then loops:

1. build the prompt: the mode, the model tier, the context percentage, the cost;
2. read a line;
3. run a slash command (about 60 of them), or send the line to the agent;
4. save the session after every turn.

The agent reports back through callbacks: streamed text, a tool's start and
result, the approval question, progress, type-ahead.

**The setup wizard core** (`src/setup/jc_setup.c`). Pure functions with no I/O:

- a table of 19 presets;
- a builder that turns the answers into a configuration; it never writes a
  literal key, only the *name* of the environment variable that holds it;
- a gap-filling merge into an existing configuration;
- the text of a start script.

The interactive questions live in `main.c` ([Review 3](2026-10-08-review-03-main-and-the-run-modes.md)).

**The ACP server** (`src/acp/`). ACP, the Agent Client Protocol, lets an editor
(Zed) drive jichi with JSON-RPC 2.0, one JSON object per line on stdin and
stdout.

- **The editor asks:** `initialize`, `session/new`, `session/prompt` and
  `session/cancel`.
- **jichi streams back** `session/update`.
- **jichi also asks the editor:** for approval, and, when the editor offers it,
  to read and write files and run terminals *through the editor*.

```
 keyboard --> jc_term: raw while editing a line; "hold" while a turn runs
                 | line                        ^ poll_key: type-ahead, Esc
                 v                             |
 jc_tui_run: "/..."? --yes--> handler (/compact /context /clear /resume ...)
                 | no
                 v
       jc_agent_run_turn(app, hist, &cb) --> model + tools
                 | cb: text, tool start/result, confirm, progress, input
                 v
       render to stdout; save the session after every turn

 editor --JSON-RPC lines--> jc_acp_serve --> dispatch --> handle_prompt
        <-- session/update, request_permission, fs/*, terminal/* --+
```

**Invariants worth knowing before you change anything here:**

- **Modes:** raw only while editing; the hold only during a turn.
- **Approvals cannot be answered by type-ahead.** An approval key is read with
  pending input flushed, so keys typed earlier never answer it.
- **Control bytes are stripped.** The model's text and tool results are cleaned
  of terminal control bytes before printing (§3.6; but see F2).
- **Signal handlers only set flags.**
- **ACP's stdout carries only protocol,** and every child process gets
  `/dev/null` as stdin, so a model's shell command cannot swallow the editor's
  next message (M844).

---

## 2. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 2.1 A terminal mode is saved state; change only what you mean — `src/tui/jc_term.c:200-218`

```c
int jc_term_hold_begin(struct jc_term *t)
{
    struct termios h;
    if (t == NULL || !t->is_tty || t->holding) {
        return -1;
    }
    if (tcgetattr(t->in_fd, &t->hold_saved) != 0) {
        return -1;
    }
    h = t->hold_saved;
    h.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
    h.c_cc[VMIN] = 0;    /* a read() returns 0 immediately when idle */
    h.c_cc[VTIME] = 0;
    if (tcsetattr(t->in_fd, TCSANOW, &h) != 0) {
        return -1;
    }
    t->holding = 1;
    return 0;
}
```

**What it teaches.** The idiom is **read, copy, clear exactly the bits you need,
write**. The saved copy is what the matching `hold_end` restores, so the hold
composes with whatever mode the user's terminal was in.

**Notice what is *not* cleared.** `ISIG` stays, so Ctrl-C still aborts, and
`OPOST` stays, so streamed text still gets its line breaks.

**The details:**

- **`TCSANOW`, not `TCSAFLUSH`.** The flushing variant would throw away exactly
  the keys the hold exists to collect.
- **No nesting.** Refusing a second `hold_begin` keeps the restore unambiguous.
- **No partial state.** Every failure returns before anything changes.

**What it does not cover** is F1: a restore that depends on the process reaching
`hold_end` is only as good as every way out, signals included.

### 2.2 Cutting UTF-8 from the tail — `src/tui/jc_tui.c:271-282`

```c
static jc_size queue_echo_off(const char *s, jc_size len, jc_size max)
{
    jc_size off;
    if (s == NULL || len <= max) {
        return 0;
    }
    off = len - max;
    while (off < len && ((unsigned char)s[off] & 0xc0) == 0x80) {
        off++;
    }
    return off;
}
```

**What it teaches.** In UTF-8, every *continuation* byte has the bit pattern
`10xxxxxx`, so `(b & 0xC0) == 0x80` finds one without a decoder.

Starting at the naive cut and stepping *forward* to the next lead byte keeps the
visible tail within `max` bytes, and never prints half a character. The cast to
`unsigned char` matters: `char` may be signed, and masking a negative value is
where helpers like this usually go wrong.

Compare [Review 1](2026-10-08-review-01-the-agent-loop.md) §4.2, where the
compaction code solves the same problem with the project's shared helpers.

### 2.3 Framing lines in linear time — `src/acp/jc_acp.c:119-143`

*(The report cited lines 119–147; the excerpt below is lines 119–143.)*

```c
    scanned = 0;
    for (;;) {
        char *nl;
        ssize_t n;

        /* A complete line already buffered? Scan only what is new. */
        nl = (s->inbuf.data != NULL && scanned < s->inbuf.len)
             ? (char *)memchr(s->inbuf.data + scanned, '\n',
                              s->inbuf.len - scanned) : NULL;
        if (nl == NULL && s->inbuf.data != NULL) {
            scanned = s->inbuf.len;
        }
        if (nl != NULL) {
            jc_size linelen = (jc_size)(nl - s->inbuf.data);
            jc_size rest;
            char *line = (char *)malloc(linelen + 1);
            if (line == NULL) {
                return 0;
            }
            memcpy(line, s->inbuf.data, linelen);
            line[linelen] = '\0';
            /* Shift the remainder (after the newline) to the front. */
            rest = s->inbuf.len - linelen - 1;
            memmove(s->inbuf.data, s->inbuf.data + linelen + 1, rest);
            s->inbuf.len = rest;
```

**What it teaches.** A reader that searches the whole buffer for `\n` after every
`read()` is *quadratic* in the length of a long line: each read rescans
everything before it. Remembering how far it has already looked (`scanned`)
makes it linear.

The remainder after the newline moves to the front with `memmove`, because the
two regions overlap, and `memcpy` on overlapping memory is undefined. The caller
receives a heap copy it must free. A few lines further on, the same function
refuses a line beyond a maximum length *and logs why*: a cap that says why it
fired.

### 2.4 A bound must say what happens to the excess — `src/tui/jc_term.c:957-981`

```c
            char win[6];
            jc_size d = 0;      /* bytes drained after the cap */
            memcpy(win, out->data + out->len - endlen, endlen);
            out->len = cap;
            out->data[out->len] = '\0';
            for (;;) {
                if (read(fd, &ch, 1) <= 0) {
                    return;
                }
                d++;
                memmove(win, win + 1, endlen - 1);
                win[endlen - 1] = (char)ch;
                if (memcmp(win, END, endlen) == 0) {
                    /* Stream tail = kept straddle + the 1 dropped byte +
                     * the d drained bytes; trim the straddle from content. */
                    jc_size straddle =
                        (d + 1 < endlen) ? (endlen - 1 - d) : 0;
                    if (straddle > out->len) {
                        straddle = out->len;
                    }
                    out->len -= straddle;
                    out->data[out->len] = '\0';
                    return;
                }
            }
```

**What it teaches.** *Bracketed paste* makes the terminal wrap a paste in
`ESC[200~ … ESC[201~`, so a pasted newline is not taken as Enter.

- **The first version of this cap** simply stopped at 1 MB. That left the rest of
  the paste in the input queue, where it was replayed as *keystrokes*: the very
  injection bracketed paste exists to prevent.
- **The fix** keeps the capped content and *drains* the excess up to the end
  marker. It uses a six-byte rolling window, seeded from the buffer's tail, so a
  marker that straddles the cap is still found.

**Two lessons.** A bound is a policy for the excess, not just a size. And a
search for a terminator must survive a split across every boundary you
introduce.

### 2.5 Derive the explanation from the behaviour's own data — `src/setup/jc_setup.c:808-823`

```c
    if (p->mode != NULL && p->mode[0] != '\0') {
        n += (jc_size)jc_snprintf(buf + n, cap - n, "\"mode\": \"%s\"",
                                  p->mode);
    }
    for (i = 0; KEYS[i].key != 0 && n + 2 < cap; i++) {
        if ((p->features & KEYS[i].bit) == 0) {
            continue;
        }
        if (n > 0) {
            n += (jc_size)jc_snprintf(buf + n, cap - n, ", ");
        }
        n += (jc_size)jc_snprintf(buf + n, cap - n, "%s", KEYS[i].key);
    }
    if (n == 0) {
        jc_snprintf(buf, cap, "nothing beyond the model itself");
    }
```

**What it teaches.** The wizard explains each preset by listing the config keys
it will write. The explanation is generated from the *same* feature bits the
preset applies, so the explanation cannot drift from the behaviour. A
hand-written second list is the thing that rots.

**Read the loop condition as part of the idiom.** `n += snprintf(buf + n,
cap - n, …)` relies on `jc_snprintf` returning the length it *would* have
written. After a truncation, `n` can exceed `cap`, and `cap - n` then wraps
around to a huge unsigned size. The condition `n + 2 < cap` is what stops the
next call from writing past the buffer. Remove it and the idiom becomes an
overflow.

### 2.6 Clean untrusted bytes where they enter — `src/tui/jc_tui.c:717-741`

```c
static void cb_text(void *user, const char *delta, jc_size n)
{
    struct tui_ctx *c = (struct tui_ctx *)user;
    char *ctrl_free = NULL;
    /* M472: strip C0 controls and DEL from the MODEL's bytes here, once, before
     * any of the three write paths below -- and it has to be here rather than at
     * the writes, because the markdown renderer INSERTS jichi's own SGR colour,
     * so a strip after it would erase jichi's escapes along with the model's.
     * Stripping the delta on the way in leaves jichi's own output untouched.
     *
     * A terminal executes some of these bytes: OSC 52 writes the user's system
     * clipboard, ESC[2K erases the line jichi just printed. Measured before this:
     * an assistant message's OSC 0 and OSC 52 reached stdout byte-for-byte. The
     * voice buffer benefits too -- there is no point speaking a control byte.
     *
     * One byte per control character, so unlike UTF-8 it cannot straddle two SSE
     * chunks. Nothing is allocated in the common case (jc_ctrl_sanitize returns
     * 0), and the buffer is freed at every return below. */
    if (delta != NULL && n > 0) {
        jc_size clean_n = 0;
        if (jc_ctrl_sanitize(delta, n, &ctrl_free, &clean_n)) {
            delta = ctrl_free;
            n = clean_n;
```

**What it teaches.** A terminal *executes* some byte sequences. One kind (OSC 52)
writes the user's clipboard; another erases the line on screen. A model's text,
or a file it reads, can contain them.

**Where to clean.** The cleaning happens where the model's bytes *enter*, before
jichi adds its own colour codes. Cleaning at the write would also erase jichi's
own escapes. *Lesson:* sanitise untrusted input at the boundary, before trusted
content is mixed in.

The finding F2 below is that some other print paths of the TUI still skip this
step.

---

## 3. What the review found

Seventeen findings: three high, four medium, nine low. Status is the
reviewer's.

### High

- **F1 — the second Ctrl-C kills jichi and leaves the terminal without echo.**
  The same root cause as [Review 3](2026-10-08-review-03-main-and-the-run-modes.md)
  R03-F1: `signal()` in a strict C89 build gives one-shot handlers. In the TUI
  the effect is worse, because the process dies inside the hold, and nothing
  restores the terminal. The fix is one `sigaction`-based installer in the
  platform layer, which fixes the TUI, headless runs and the daemon at once.
  (F9 is the matching test gap: nothing can see F1 today.)
- **F2 — control bytes from the model or from files still reach the terminal on
  some of the TUI's other print paths,** including the approval prompt (a
  terminal-injection class). The TUI writes through about 580 direct `printf`,
  `fputs` and `fwrite` lines, and §2.6's filter guards two paths. *The reviewer's
  suggestion:* two functions, one for jichi's own text and one for untrusted
  text, and a lint that bans raw writes of non-literal strings.
- **F3 — ACP runs a prompt against whatever session the process holds,** and a
  second `session/new` discards the first session unsaved. It was *already
  recorded* (the desktop design's item D4, parked under def-297), and was
  re-verified here, with one new detail: the discarded session's id can never be
  loaded again.

### Medium

- **F4** — ACP: a cancel, or the editor's refusal, during a delegated file write
  falls back to writing the file on disk anyway, behind the editor's open
  buffer. The terminal delegate already does the right thing; the file delegate
  should match it.
- **F5** — unrecognised escape sequences leak their tails into the input line.
- **F6** — ACP messages that arrive while jichi waits for the editor are
  dropped, and the cancel poll sees only the first line.
- **F7** — an ACP turn that failed is reported as a normal `end_turn`, with no
  error text.

### Low (selected)

- **F11 and F12** — `/clear`, `/resume` and `/fork` keep the previous
  conversation's read records, and `/clear` keeps the session id, so the stored
  conversation is overwritten at the next save.
- **F14** — the setup wizard's start script puts the config path into a shell
  command without quoting.
- **F16** — Ctrl-C on a multi-line input clears only the last row, and the rows
  above are still sent.

---

## 4. The design, seen whole

**Size.** `jc_tui.c` is 5,431 lines, and `jc_tui_run` alone about 2,180: one
loop, about 60 slash commands as a chain of `strcmp`s. Four lists describe the
same commands: the chain, a name table, the help text and the completion code.
Lints hold them together.

**What the reviewer would refactor first, with the cost:**

1. **One session switch.** `/resume` and `/fork` each swap the session, the
   history and the todo list by hand, `/clear` mutates in place (F12), and none
   resets the per-conversation tables (F11). A `tui_session_switch()` would make
   them one operation. It is small, three call sites, and it matters for the
   relay: a relay is a *fourth* kind of switch.
2. **A command table** `{name, takes_arg, handler, help}`: commands become
   enumerable, testable without a pseudo-terminal, and usable over ACP, where
   slash commands do not travel today. It is wide and mechanical; the risk is in
   the handlers' shared `continue`/`free(line)` discipline.
3. **A pure key decoder,** bytes in and key events out, so escape handling is
   unit-testable like everything else in this file.

**What is clean.** `jc_setup.c` is the cleanest file in the slice: pure,
table-driven, every explanation derived from data, unit-tested.

**A reading note.** Many functions carry more milestone narrative than code; the
approval callback has 131 comment lines to 107 code lines. For a learner that is
gold. For a maintainer, the rule is to *read the comment before changing the
line*, because most of the odd-looking choices are load-bearing and explained
there: `TCSANOW`, the 100 ms Esc wait, the order of the accessibility branches.

---

## 5. What this means for the context relay

- **A relay in the TUI is a fourth kind of session switch**
  ([proposal](../proposals/2026-10-context-relay.md) §6.12). It should be built
  on the single switch of §4 above, so that it cannot inherit F11 and F12.
- **What the offer must not lose:**
  - the type-ahead queue;
  - the transcript on screen;
  - the link from the old session to the new one.
- **Over ACP, a relay is a `session/new` seeded from the notes.** It waits for the
  ACP contract work the desktop design recorded (F3), and is designed later.

---

## 6. Something to do

**Without a bench (the read-only twin):**

1. In §2.1, list the four `termios` flags of §1's table and say which ones the
   hold clears. Then explain what the user would see if `OPOST` were cleared too.
2. In §2.2, apply `queue_echo_off` by hand to the UTF-8 bytes of `"aé"` (`61 C3
   A9`) with `max = 1`. Which offset does it return, and what is printed?
3. In §2.5, set `cap = 10` and walk the loop with three long key names. At
   which step does `n + 2 < cap` stop it, and what would happen on the next call
   without it?

**With a bench:** watch the hold at work. Start `jichi` in a terminal, give it a
task that takes a few seconds, and type a sentence while it works. The sentence
appears as type-ahead after the turn; it did not echo while you typed, and
Ctrl-C still stopped the turn. That is §2.1's mode. Try Ctrl-C twice only in a
terminal you can close: F1 is real.

## 7. Read further

- [TUI_PRINCIPLES.md](../TUI_PRINCIPLES.md), [TUI_RENDER.md](../TUI_RENDER.md),
  [TYPE_AHEAD.md](../TYPE_AHEAD.md), [ACP.md](../ACP.md),
  [SETUP_WIZARD.md](../SETUP_WIZARD.md): the references. *Primary for jichi.*
- The POSIX specification, "General Terminal Interface" (`termios`): the flags of
  §1. *Primary.*
- ECMA-48, *Control Functions for Coded Character Sets*: what CSI and OSC
  sequences are. *Primary.*
- The Agent Client Protocol specification (agentclientprotocol.com). *Primary.*
- Michael Kerrisk, *The Linux Programming Interface*, chapters 62 (terminals)
  and 64 (pseudoterminals). *Secondary.*

---

## Completed on 2026-10-17 (at e5d832a4)

*This section finishes the review above. The chapter was written from a report that was paused
before its findings could be checked against later code; about 45 milestones (M914 to M959) have
landed since `17bbb895`. The text above is left as it was, because people may already have acted on
it; this section says what is true now. Written by Claude Opus 5.5, 2026-10-17, at master
`e5d832a4`.*

### How this was checked

The first question for a re-check is what changed. For this slice the answer is short:
`git diff 17bbb895 e5d832a4` touches **one** of its files, `src/tui/jc_tui.c`, and only to remove one
`free()` after `/tutor` (M926, Review 4's F2). `jc_term.c`, `jc_setup.c`, `jc_acp.c` and
`jc_acp_proto.c` are byte-identical. So every earlier finding in them still describes the same code,
and the work was to re-measure the important ones, finish what the paused reviewer left untraced, and
look at how *new* features elsewhere reach these front ends.

Measurements used a private build, the repository's scripted mock model (`tests/tools/mockmodel`), and
the repository's pseudo-terminal driver (`tests/tools/ptydrive`) to type into the real TUI. No API key,
no network, no timeout around a jichi run. Marks: **[V]** read end to end, **[M]** measured, **[R]**
read but not traced to the end.

### Words for this section

- **Pseudo-terminal (PTY).** A pair of devices that lets one program play the keyboard and screen of
  another. A test that drives the TUI through a PTY sees exactly the bytes a terminal would receive,
  including escape sequences.
- **Memcheck (valgrind).** A tool that runs a program on a simulated CPU and tracks, for every byte,
  whether it was ever written. "Conditional jump depends on uninitialised value" means the program made
  a decision based on a byte nobody set.
- **`sigaction` versus `signal`.** Both install a signal handler. `signal()`'s behaviour depends on the
  C library and its feature macros; under this build's strict C89 flags glibc gives the old System V
  meaning, where the handler is removed after one delivery. `sigaction()` states the behaviour
  explicitly in its flags.
- **The context relay** (built since the review, off by default; see [RELAY.md](../RELAY.md)). When a
  headless run's context window is exhausted, the run ends at a round boundary (a **handover**) and
  writes notes for a fresh session to continue from.

### 1. The earlier findings now

| Finding | Then | Now (at `e5d832a4`) |
|---|---|---|
| F1 the second Ctrl-C kills jichi | high | **Fixed (M918).** One installer, `jc_signal_install`, uses `sigaction` with no reset-on-delivery flag, at every install site; a PTY driver proves the process survives a second interrupt and exits cleanly. One side effect of the same flags remains and is harmless: a signal sent *from another process* while the TUI waits at its prompt ends the TUI as Ctrl-D would. |
| F2 control bytes reach the terminal on other print paths | high | **Still open, measured again** (terminal-injection class). A harmless marker sequence inside a tool-call argument reached the terminal raw on the tool-start line. The parallel-board rows, which the paused report had not traced, are a further path: they print a child's tool name or error text unfiltered. |
| F3 ACP ignores the prompt's `sessionId` | high | **Still open** (recorded under def-297). The neighbouring `session/load` path has its own finding in the hardening review (D-07). |
| F4 ACP falls back to a disk write after a cancel or refusal | medium | **Still open.** |
| F5 unknown escape sequences leak into the input line | medium | **Still open.** |
| F6 ACP drops messages while it waits; the cancel poll sees one line | medium | **Still open,** and the same poll has a second defect (N2 below). |
| F7 a failed ACP turn reads `end_turn` | medium | **Still open;** a relay handover reads `end_turn` too (N1). |
| F8 one terminal field is never initialised | low | **Still open, now measured on the real path:** valgrind reports the uninitialised read on every TUI start. In the measured run the stray value happened to be zero, so the false "your input was discarded" notice did not print; on another run it can. |
| F9 no test could see F1 | low | **Fixed (M918)** by the new PTY driver. |
| F10 to F16 | low | **All still open.** The setup items F14 and F15 were re-verified by the hardening review, which also found that F15's code copies a literal API key into the project (D-24) and that `jichi setup` replaces a commented project config (D-22). |
| F17 smaller items | low/note | **Still open.** The `volatile int` flags are the hardening review's E-13. |

In numbers: of 17 earlier findings, 2 are fixed and 15 stand, two of them now measured at `e5d832a4`.

### 2. New findings

**N1 (medium, [M]) -- with the relay on, an interactive turn under pressure ends with no answer.** The
relay was designed for headless runs, and only the headless code writes its notes. But the decision to
hand over is made inside the agent loop, which every front end shares, and nothing limits it to
headless. Measured in the TUI with `"relay": true` and a small window: after one tool round the turn
ended, the log said *"the next leg continues from the notes"*, no notes were written, the model's
answer was never requested, and the TUI's session total left out the extra summary call the relay made.
Over ACP (`--acp`) the editor received `end_turn` and no text. Because the relay's keys can currently be
switched on by an untrusted project (Review 1's completion, N8), this is not only an opt-in problem.
*Fix:* let only the headless front end arm the relay, and say in RELAY.md that the key does nothing in
the TUI, ACP and the daemon until an interactive offer is designed.
*Test first:* drive the TUI through a PTY with the relay on and assert that the final answer appears.
*Lesson:* a feature that is "headless only" in its documentation must be headless only in its gate. A
decision placed in shared code reaches every caller of that code.

**N2 (low, [V]) -- ACP's mid-turn cancel poll skips the line-length cap.** While a model call runs,
jichi polls the editor's input for a cancel. That poll appends what it reads without the 8 MiB line cap
the main reader enforces, and searches for a newline from the start of the buffer on every tick: the
same quadratic rescan that M664 removed from the MCP reader. The editor is a trusted peer, so this is
memory and CPU, not an attack. *Fix:* the same cap and a remembered scan offset in the poll.

### 3. What changed in the design picture

- **"Signals belong in the platform layer" was taken.** The paused report's recommendation is now the
  code: one installer, every site. That is the cheapest kind of fix, one helper that makes a whole class
  of call site correct.
- **The other refactors are not started:** a command table, one session switch, a pure key decoder,
  and a single function for printing untrusted text. N1 adds weight to the session switch: the relay
  now exists, and the TUI has no switch through which to hand a session over.

### 4. Lessons

1. **An unchanged file can still have changed behaviour.** None of the ACP code changed, yet a turn
   can now end in a new way (N1), because the loop it calls did change. Re-reviewing a slice means
   re-reading its callees' new exits, not only its own diff.
2. **An open question is cheapest to answer with the right instrument.** F8's question, "does the
   uninitialised field ever get read?", took one valgrind run to settle; reasoning about stack contents
   could not.
3. **A filter protects only the paths that pass through it.** F2 is still the chapter's §2.6 lesson: list
   every write to the terminal a second way and check each one.

### 5. Recommendations, in order

1. Gate the relay decision on the headless front end (N1), together with Review 1's reclassification of
   the relay keys.
2. One chokepoint for untrusted terminal text, with a lint, covering the tool-start line, the approval
   prompt, the diff preview, the ghost text and the board rows (F2).
3. Initialise the terminal structure completely (F8: one `memset`, and the valgrind run above as its
   test).
4. Give the ACP file delegate the terminal delegate's cancel rule (F4), and parse each line in the cancel
   poll with the cap applied (F6, N2).
5. Then the remaining items in the original order: F3 with the desktop work, F5's key decoder, F11/F12's
   session switch, and the setup items with the hardening review's D-22 and D-24.

### 6. Something to do

**Without a bench.** jichi's interrupt handler sets `abort_flag` and returns. Read `jc_signal_install`
in `src/platform/jc_platform_posix.c` and list the three things it sets in `struct sigaction`. Which
of them decides that the handler survives its first delivery? Which flag would make an interrupted
`read()` restart instead of failing, and why might the TUI *not* want that while it waits at its
prompt?

**With a bench.** Run the TUI under `valgrind`, type `/help` and then `/exit`, and read the report. Find
the field of `struct jc_term` that `jc_term_init` never sets, and the line in `jc_term_readline` that
reads it.
