# Type-ahead: typing while jichi works (M254, opt-in since M257)

**Off by default. Turn it on per run with `--type-ahead`, or per project with
`"typeAhead": true`.**

With it on, you can keep typing while the agent works. What you write is
collected, echoed while you type it, and applied at the agent's next step — as a
mid-turn course correction if the turn is still going, or as your next message if
it has ended.

## Why it is off by default

Because jichi **cannot promise your input is visible the whole time**, and
invisible input is not a cosmetic problem:

- you cannot correct a typo you cannot see;
- you cannot compose a thought you cannot read back;
- you cannot tell "my keypresses are being captured" from "my terminal is wedged".

At best that is a nuisance; often it is a risk: a queued line gets *sent*, and
while **Ctrl-K** can now un-queue one, you have to know it went wrong to press it.
Where the visibility gap comes from is architectural, not an oversight (see
[Design decisions](#design-decisions) D3), so the honest response is to make the
feature something you choose, knowing the tradeoff, rather than something you
discover mid-run.

Two of the three gaps have since been closed — colour-off terminals (M257) and
shell commands, the minutes-long case (M258) — so what remains is narrow and
listed below. The default stays off regardless: "narrow" is not "none", and this
is a choice worth making deliberately rather than inheriting.

## When your typing is visible

| While… | Your typing is |
|---|---|
| **waiting for the model** — most of a turn's wall-clock | **visible, live**, beside the working indicator |
| **a shell command runs** — a build, a test suite (M258) | **visible, live** — the command runner ticks the display while it waits on output |
| assistant prose is streaming | captured, not shown |
| another long tool runs (a big `codebase_search`, an MCP call) | captured, not shown |
| `--accessible` is set | captured, not shown — deliberately (see D9) |
| *after you press Enter* | **always** echoed back: `▸ queued for the next step: …` |

So you always find out what you sent. In the remaining gaps you find out *after*
committing rather than before — and **Ctrl-K un-queues** if that turns out to be
the wrong line (see D10).

The two windows that used to matter most are closed: colour-off terminals (M257)
and shell commands (M258). Prose streaming is the one that remains by design —
`cb_text` erases the indicator on the very next delta, so an echo there would
flicker between paragraphs rather than inform. Streaming is also the *short* case:
the echo returns the moment the next model call begins.

## What it looks like

```
  ⠹ working… 12.4s  » also check the tests
```

Press **Enter** and it is queued, confirmed on its own line:

```
  ▸ queued for the next step: also check the tests
```

At the agent's next tool-call boundary it reaches the model, and jichi says so:

```
  queued input applied
```

The model receives it as one user message prefixed `[operator]` — the same shape
a supervisor's `jichi control … inject` produces (see [`CONTROL.md`](CONTROL.md)).

## The two ways it lands

| When you press Enter | Where it goes |
|---|---|
| the turn is still running and will call another tool | mid-turn, at the next tool-call boundary, as one `[operator]` message the next model call sees |
| the turn ends first (or made no tool calls at all) | it becomes your **next message**, echoed behind the prompt as if you had just typed it, and recorded in the editor's recall history |

Both drain the same queue, so a line is never applied twice and never silently
dropped. Several lines typed before a boundary are joined into one message.

## Keys while the agent works

| Key | Effect |
|---|---|
| printable text | appended to the line being typed (UTF-8 safe) |
| **Enter** | queue the line |
| **Backspace** | delete one character |
| **Ctrl-U** | clear the line being typed |
| **Ctrl-K** | un-queue: drop lines already committed but not yet sent |
| **Ctrl-C** | unchanged — aborts the turn |
| arrows, function keys | ignored (this is a queue, not the line editor) |

**Enter is what queues.** A line typed but never committed is not sent — and not
silently thrown away either: when the turn ends jichi prints it back, labelled
unsent, so you can see what did not go.

## Turning it on

```sh
jichi --type-ahead            # this run
jichi --no-type-ahead         # explicitly off, overriding a config that enables it
```

```jsonc
{ "typeAhead": true }         // this project
```

Or from inside a session, with no restart:

```
/typeahead on          # also: /typeahead off, or bare /typeahead to see the state
```

Turning it off drops anything still queued — "off" should not mean "off, but that
one line you typed will still be sent".

Headless (`-p`), ACP, and non-TTY sessions ignore it entirely: they install no
keyboard front-end, nothing holds the terminal, and the command runner's idle tick
has no consumer — so those paths behave exactly as they did before this feature
existed.

## What it was fixing

Before M254 the human at the keyboard was the only operator with **no** mid-run
channel at all. A supervisor could steer a headless run over the control socket;
an editor could cancel and answer permission prompts over ACP; the person sitting
in front of the TUI had Ctrl-C — abort the whole turn — and nothing else.

Worse, the keystrokes were actively destroyed. Raw mode was entered only while
editing an input line, so during a turn the terminal sat in its normal cooked mode
with echo on: anything typed was **echoed into the middle of the streamed
answer**, and then the `TCSAFLUSH` at the next raw-mode entry **discarded** it.
Garbled the transcript, then threw the work away, with no message. The lesson
users learned was to sit on their hands — for minutes at a time on a long run.

## Design decisions

**D1 — Opt-in, off by default.** Decided at M257 after the visibility gap was
mapped, on operating experience: visible input is what makes correction and
composition possible, so a feature that cannot guarantee visibility must be
chosen, not defaulted. Note this is *not* a resource decision — it is on even in
`--lite` when you ask for it, because holding the tty costs nothing.

**D2 — Enter commits; nothing else sends.** No timer, no idle-flush, no
send-on-boundary of whatever happens to be half-typed. A queued line is a line
you decided to queue.

**D3 — The echo lives *in* the working-indicator line.** That line already owns
the last terminal row and redraws it with CR + erase, so nothing in the scrolling
transcript can be overwritten by it. *Rejected:* a sticky bottom line (the obvious
"always visible" answer). jichi is a scrolling transcript, not a full-screen TUI:
a sticky line needs either the alternate screen or an output chokepoint that
erases and repaints around **every** write — the markdown renderer, tool lines,
diff previews, nested subagent output, the approval prompt. Worse, when streamed
text ends mid-line, a `\r\x1b[K` erases *that text*; the `at_bol` tracking exists
precisely to keep notices off half-printed lines. The refactor is large, it
reintroduces flicker, and it re-opens a failure mode currently designed out. This
decision is the direct cause of the visibility gaps in the table above, and it is
a deliberate trade, not an accident.

**D4 — The indicator's *existence* does not depend on colour (M257).** It used to,
so `NO_COLOR` terminals typed blind for the whole turn. But the mechanism is CR +
erase-line — cursor control, not styling, and the line editor already emits
`\r\x1b[J` and `\x1b[<n>C` on the same fd with `NO_COLOR` set. So existence now
depends on having something to show, and only the SGR codes depend on colour.
Kept narrow on purpose: **without** type-ahead a `NO_COLOR` session gets exactly
the output it always got.

**D5 — The hold keeps `ISIG` and `OPOST`, and uses `TCSANOW`.** Three
load-bearing differences from the editor's raw mode. `ISIG`: Ctrl-C must still
raise SIGINT — a new channel must not take the interrupt away. `OPOST`:
everything printed during a turn ends lines with a bare `\n` and relies on the
tty's LF→CRLF, so clearing it would stair-step the whole transcript. `TCSANOW`
rather than `TCSAFLUSH`: flushing pending input at the *fix site* would discard
the very keystrokes the feature exists to keep — the original bug, reintroduced.

**D6 — Reuse the control channel's convention, do not invent one.** Text lands as
one `[operator]`-prefixed user message at a tool-call boundary, via the shared
`jc_history_add_operator` that `jc_control_boundary` also calls. One meaning for
"text that arrived mid-turn", whether it came off a socket or a keyboard — and the
M31 cached prefix stays byte-stable either way. Boundary-only also means steering
can never interleave with a running tool, and top-level-only means it never lands
in a subagent's private history.

**D7 — No path may end in silent loss.** That was the defect. Uncommitted text is
printed back labelled unsent; an over-long queue says so and names the dropped
line; escape sequences are swallowed rather than becoming text.

**D8 — The approval prompt stays un-answerable by type-ahead.**
`jc_term_read_key` still enters raw mode with `TCSAFLUSH`, which is what stops a
stray `y` in your queued text from approving a tool you never saw. jichi drains
the queue *before* prompting, so the text is kept **and** the prompt stays
un-answerable by it.

**D9 — `--accessible` gets no live echo.** M118 suppresses the animated indicator
on purpose; repainting a line per keystroke would spam a screen reader with
frames. Confirmation-on-Enter is the feedback in that mode. If you use a screen
reader and want the queue, expect to type blind and rely on the confirmation.

**D10 — Un-queue is a key, not a UI (M258).** Ctrl-U clears the line you are
*typing*; **Ctrl-K** drops what you already committed but that has not yet been
sent. Two separate scopes, two separate keys, because "undo my half-typed line"
and "cancel the instruction I queued" are different intentions. Ctrl-K takes the
sting out of committing blind — the original limitation here was that a typo
committed while the echo was invisible could not be recalled at all. It stays a
buffer, not an editor: you cannot edit a queued line or drop one of several
selectively; Ctrl-K clears the pending queue whole, and says so. (The key mirrors
the line editor's own kill-to-end-of-line: same finger, same meaning — "discard
what is pending".)

**D11 — The display gets ticked from inside the command runner (M258).** A
foreground shell command blocks the agent loop, so no front-end callback fired
while it ran: a four-minute test suite was four minutes of blind typing, the worst
remaining window. `jc_app_run_command_ex` now calls `jc_app_tick` whenever it is
idle waiting on output, forwarding the same `on_progress` tick libcurl drives
during a model call. Two consequences worth stating: the default `popen` read loop
became a `select` loop (with a **NULL** timeout when nothing wants ticks, so a
headless run blocks exactly as the old `fread` did — identical bytes, identical
waiting); and the TUI arms the indicator lazily, only when a tick arrives *while a
tool is running* and only at a line start. *Rejected:* hooking `jc_proc_capture`
too. It has no front-end consumer today (CLI verify, `@diff`, PDF extraction,
notify), and a hook nothing calls is dead surface — ANECDOTES #28's lesson.

## Verification

`tests/smoke/typeahead.sh` drives a real PTY against a mock model whose first
call stalls, and asserts four things — two of them negative:

1. a line typed into that window and committed is confirmed queued, and the run
   ends `STEER_OK`, which the mock returns only when the `[operator]` message
   really reached the wire;
2. it reached the **second** model call, not the first (`req.1` clean, `req.2`
   carrying it) — captured mid-turn, applied at the next boundary;
3. the typing was **visible while being typed**, asserted on the rendered working
   line under `NO_COLOR` — the case that was blind before M257 (observed failing
   with the fix reverted: *0 working lines rendered*);
4. **without** `--type-ahead` the typed text never ships (`STEER_MISSED`), so the
   default cannot drift back on unnoticed.

`tests/smoke/typeahead_live.sh` covers the M258 controls, each proven red-first by
reverting its fix:

1. typing stays visible **while a foreground tool runs** — asserted structurally,
   that a working line carrying the typed text appears *after* the tool-start line
   (fails with the tick removed: "the tick never arrived");
2. **Ctrl-K un-queues** — the notice appears and the dropped line never reaches
   the wire (fails with the key removed);
3. **`/typeahead on` works mid-session**, started without the flag. This one
   earned its keep immediately: it caught that `take_input` was installed only at
   startup, so the toggle enabled capture but not injection and a queued line fell
   through to the next prompt instead of steering the running turn.

The original M254 defect was observed first, and the recorded transcript was the
diagnosis: it showed the typed text echoed into the assistant's output and the run
finishing without it.
