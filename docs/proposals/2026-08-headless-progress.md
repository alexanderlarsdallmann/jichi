# Headless progress: minimal, meaningful, and bounded

*Design note written before the change, per the M299 craft section.*

---

## The question that prompted it

> *"Is there a quiet mode for output that is minimal but meaningful during work with jichi?"*

**In the TUI, yes** — `-q` at launch or `/quiet on` mid-session drops the per-message
model/mode header, the token lines, the pre-edit diff preview and the wisdom note, while
**keeping** the `▸ tool_name  summary` activity lines. That is exactly the shape being asked
for, and it already ships.

**In headless, no.** Measured against the mock model, one turn with three tool calls:

| | stdout | stderr |
|---|---|---|
| default | the answer | `[tool] list_files {"path":"."}` and `[tool list_files -> ok]` per call, plus a token line |
| `-q` | the answer | **0 bytes** |

There is no middle setting. The choice is a firehose or silence, and the middle one is
precisely what the TUI already has.

## Two defects found while measuring

**1. The headless tool line is unbounded.** It is literally

```c
fprintf(stderr, "\n[tool] %s %s\n", name, args);   /* args = the RAW JSON */
```

so a `write_file` of a 200 KB file writes 200 KB to stderr. Three lines below it, the jsonl
path bounds its tool-result preview with the comment *"A bounded preview so an agent can
react without the full blob."* Same file, same author, opposite treatment — and
`jc_tool_arg_summary`, the short renderer the TUI uses for this exact purpose, is never
called here.

**2. stdout is not clean.** Under `-q`, stdout for that run is `\n\n\nAll done.`: one blank
line per tool round, emitted by `hl_message_end` for every assistant message including the
tool-only ones that produced no text. `docs/EMBEDDING.md` states *"stdout is the answer;
stderr is diagnostics"*, and three leading newlines in a captured artifact is a small
violation of the first half.

## Decisions

### D1 — Fix the default; do not add a flag

The default becomes the bounded, summarised line. `-q` stays silent. `-v` gets the raw
arguments.

*Rejected: a new `--progress` / `--tool-lines` flag.* It would leave the unbounded default in
place and add a hundred-and-somethingth flag to opt out of a defect. An unbounded write to
stderr is not a preference to be configured; it is a bug.

*Rejected: leave the default and document it.* Documentation does not stop a large
`write_file` from flooding a log.

**This is permitted by the project's own contract**, which is why it can be done at all:
`docs/EMBEDDING.md` lists **stderr text** under *"not an interface"* and says *"never parse
stderr."* Checked before deciding: **no test asserts on the current format** (`grep` over
`tests/` returns nothing), and the one match anywhere in the tree —
`examples/web-bridge/bridge.py` — builds a similar-looking string from **jsonl** events,
which this change does not touch.

### D2 — Share the renderer, not the decoration

Both surfaces call `jc_tool_arg_summary`. The TUI keeps `▸ name  summary` with colour and
per-depth indentation; headless keeps `[tool] name  summary`.

*Rejected: making headless byte-identical to the TUI.* The TUI's decoration exists for an
interactive terminal — colour, indentation by subagent depth, a spinner it erases on the
first tool. A log file wants a stable greppable prefix and no ANSI. What must not differ is
the **content**, and sharing one renderer is what guarantees that.

*Rejected: leaving headless with its own ad-hoc formatting.* That is how the two drifted into
"the surface with less room shows more bytes" in the first place.

### D3 — The raw arguments move to `-v`, not to nothing

They are genuinely useful: a malformed tool call is diagnosed by seeing what the model
actually sent (the M148 repair path exists because models emit broken argument JSON). Losing
that would remove a debugging affordance to fix a formatting one.

`-v/--verbose` is already documented as *"Enable debug logging"*, so raw arguments belong to
it by meaning rather than by convenience. *Rejected: a dedicated `--tool-args`.*

### D4 — jsonl is untouched, and the asymmetry becomes deliberate

The jsonl `tool_call` event keeps the complete raw `args`. An agent parsing events needs the
real arguments, and unlike stderr the jsonl schema **is** a stable documented interface.

So after this change the two surfaces differ on purpose, and the rule is worth stating:
**the machine surface is complete; the human surface is bounded.** Before the change they
differed by accident, in the direction that helped nobody.

### D5 — stdout emits its separator only when the turn produced text

`hl_message_end` writes `\n` unconditionally. It will write one only if that assistant
message actually streamed text.

*Rejected: trimming leading whitespace before printing the answer.* Output is streamed, not
buffered — there is nothing to trim at the point the first byte is written.

**Known consequence, accepted:** a turn whose answer is genuinely empty now emits nothing at
all on stdout rather than a bare newline. That is more faithful to "stdout is the answer"
(an empty answer is empty), and the empty-answer *warning* is a separate stderr message that
`tests/smoke/empty_answer.sh` asserts on and this does not touch.

### D6 — `-q` keeps meaning silence

Unchanged. Anyone using `-q` to get a clean capture keeps exactly what they had.

## The resulting three levels

| | stderr during work | for |
|---|---|---|
| `-q` | nothing | a clean capture; a supervisor that parses stdout or jsonl |
| *(default)* | `[tool] write_file  out.txt` — one bounded line per call | a human watching an `--auto` run |
| `-v` | the same, plus the raw argument JSON and debug logging | diagnosing a malformed call |

## How this could be wrong

- **Someone may be grepping stderr despite the instruction not to.** The `[tool] ` prefix and
  the `[tool NAME -> ok]` result line are kept precisely so that a habit built on the prefix
  still works; what changes is the payload after the name.
- **The summary can be empty.** `jc_tool_arg_summary` picks the first of
  `path`/`command`/`query`/`pattern`/`symbol`/`url`/`name`/`file`/`task`; a tool with none of
  them renders as `[tool] name` alone. That is still the useful half — *which* tool ran — and
  the raw form is one `-v` away.
- **It is a default change**, so the first run after upgrading looks different. Recorded in
  the ROADMAP entry and the decision register rather than left to be discovered.

## Verification

- A smoke driver pinning all three levels against the mock model: `-q` silent, default
  bounded and *not* containing a large argument blob, `-v` containing it.
- The stdout-cleanliness assertion is the one that must be shown failing first: today stdout
  begins with one `\n` per tool round.
- Existing drivers re-run whole, since this changes output several of them read.
