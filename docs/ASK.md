# Asking the user (`ask_user`)

`ask_user` lets the agent pause mid-task and put a focused clarifying question to
the user — with optional suggested answers — instead of guessing when a choice
materially changes what it should do. It is the single most-requested agent UX
in the sibling tools, and it cuts wrong turns on ambiguous requests.

## Behaviour

`ask_user(question, options?)` routes through an optional front-end *ask
delegate* (`app->ask`, mirroring the ACP fs/terminal delegates):

- **Interactive TUI** — installs the delegate, so the question (and any numbered
  suggestions) is printed and jichi blocks on a line of input. A bare number
  selecting a suggestion expands to that option's text; anything else is taken
  verbatim. The answer is returned to the model as the tool result.
- **Headless / ACP / `--auto`** — no delegate is installed, so the tool does
  **not** block. It returns a note that no interactive user is available and the
  model should proceed on its best judgment. An unattended run therefore never
  hangs waiting for input.

The tool is **read-only** (it changes nothing on disk), so it isn't
permission-gated — there is no point prompting to approve a prompt.

```jsonc
// the model calls:
{ "question": "Which database should I target?",
  "options": ["postgres", "sqlite"] }
```

```
? Which database should I target?
  1) postgres
  2) sqlite
  answer > 1          # -> the model receives "postgres"
```

## When the agent should use it

The tool description steers the model to reserve it for **genuinely blocking
decisions** — an irreversible choice, a fork in scope, a missing fact it can't
infer — not for routine confirmation (that's what tool-approval prompts and plan
mode are for). In a non-interactive run it should assume sensible defaults and
state them rather than lean on `ask_user`.

## The run's record (M359)

In a **bounded** run, every `ask_user` call is journaled as an `ask` event —
the bounded question text plus `answered:true|false` (the answer text is
never recorded; it reaches the history anyway). The unattended no-op and a
declined prompt journal `answered:false`, and the `runs` reader renders the
count as `unanswered=N` beside M161's `steered=N` (also in `runs --output
json`): the dual of operator steering. A reviewer triaging a run learns "the
model judged N decisions blocking and guessed" without replaying the
transcript — the strongest signal an unattended task needs a sharper brief or
an attended re-run.

## Internals

- **`struct jc_ask_delegate`** (`include/jc_app.h`) — `ask(ctx, question,
  options, noptions, &out)` appends the answer to a caller-owned `jc_sb` and
  returns `JC_OK`; `JC_ERR_*` (or a NULL delegate) means "no answer", and the
  tool tells the model to proceed.
- **`ask_user`** (`src/tools/jc_tool_ask.c`) — a builtin tool; builds the option
  list from the optional `options` array (capped) and calls the delegate.
- **TUI** (`src/tui/jc_tui.c`) — `tui_ask` prints the question + suggestions and
  reads a line via `jc_term_readline`; installed on `app->ask` for the session.
- e2e: `tests/e2e/ask.py` drives the no-delegate (headless) path and asserts the
  run completes without hanging and the proceed note reaches the model.

## Answering with a posture instead of text (M304)

When `ask_user` fires in **auto** mode the TUI adds a line:

```
? Which approach should I take?
  1) rewrite the parser
  2) patch the existing one
  (or /plan or /chat to narrow the posture from here)
  answer >
```

This is the moment the human is **already in the loop** — and the moment they are
most likely to want the run reined in rather than answered: *"stop working
unattended, show me the plan first."* Before M304 the only options were to answer
the question or to Ctrl-C the run.

- **The slash is required.** `/plan` narrows; `plan` is an answer. An `ask_user`
  question could easily be *"which approach?"* with `plan` among the options, so a
  bare word must stay a bare word.
- **Narrowing only** — the same one-way rule as the control channel
  ([`CONTROL.md`](CONTROL.md)). A widening is refused with a reason.
- **The question is then re-asked**, because the model still needs its answer — and
  because a narrowed posture may well change what you want to say.
- The change **persists past the turn**, and is spoken when
  [voice](VOICE.md) is on.
