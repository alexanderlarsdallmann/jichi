# Autocomplete

Four pieces:

1. **Tab completion in the TUI** — instant, offline, deterministic completion of
   slash-commands and their arguments while you type.
2. **Ghost-text suggestion in the TUI** — press **Ctrl-G** to have the model
   suggest a continuation of your current prompt line (see below).
2b. **Prompt advice in the TUI** — press **Ctrl-Q** to be told, in one line,
   what is unclear about the request you are composing. Not a completion: it is
   *printed below your line*, never inserted (see below).
3. **The headless `complete` subcommand** — a one-shot LLM continuation of some
   text, using the `autocomplete`-role model, for scripts and editors.
4. **The headless `fim` subcommand** — fill-in-the-middle code completion (the
   editor "tab autocomplete" gesture): given the code before and after the
   cursor, return the code that belongs at the cursor.

## Ghost-text suggestion (Ctrl-G)

At the end of a non-empty input line, **Ctrl-G** asks the model to continue what
you've typed; the suggestion appears as **dim "ghost" text** after the cursor.
**Tab** accepts it (appends it to your line); **any other key dismisses** it and
is then handled normally. It's a manual, on-demand gesture — there is no
fetch-on-every-keystroke.

*Beginner view:* type the start of a request, press Ctrl-G, and if you like the
greyed-out continuation press Tab to keep it.

### What you actually get depends on the model (read this before relying on it)

jichi **asks** for a continuation and nothing else. The system prompt behind
Ctrl-G (`tui_suggest`, `src/tui/jc_tui.c`) says: *"Continue the user's partial
line naturally and briefly. Output ONLY the continuation that should follow it —
no preamble, no quotes, no repetition of their text."*

**The prompt now demonstrates rather than only instructs (M280).** Because an
instruction a model may ignore is a weaker signal than an example it can
pattern-match, the system prompt carries **three worked continuations** — one of
them the exact line reported below — and then names the failure explicitly:
*"Note what those outputs do NOT do: they do not answer the question and they do
not ask for clarification."* It also teaches the leading-space rule, since the
suggestion is appended verbatim and a missing space corrupts the line. The reply
is then passed through a small **cleaner** (`jc_suggest_clean`, pure and
unit-tested) that removes what models emit anyway: a leading blank line, an
`output:` label copied from the examples, surrounding quotes, and a verbatim echo
of your own text.

None of that makes compliance certain, which is why the rest of this section
stands:

**Whether the model obeys is a property of the model, not of jichi.** A
chat/instruct-tuned model often treats your half-finished line as a *question*
and answers it — or asks you to clarify. Observed 2026-08-04 with
`qwen3-coder-next` in the `autocomplete` role:

```
[chat·qwen3-coder-next·0%] › what is the name of this prCould you provide more
context or clarify which PR you're referring to?
```

The model read `pr` as "PR", decided it needed clarification, and answered. The
suggestion is then inserted **at the cursor, as if it were a continuation**,
which is why the line reads as garbled text rather than as a question.

Two things follow, and they are worth knowing up front:

- **A suggestion is a proposal, not an edit.** **Tab** accepts it; **any other
  key dismisses** it and is handled normally. A useless suggestion costs one
  keystroke, so the failure mode is cheap — but it is *not* silently correct
  either, and it will not always be a sentence continuation.
- **The `autocomplete` role exists precisely so this model can differ from your
  chat model.** If you want continuations, point that role at a model suited to
  continuation (a base or FIM-tuned model, or an instruct model that follows
  terse output constraints) rather than at whichever model answers your chat
  turns. See `docs/MODELS.md` for role assignment.

If what you actually want is *code* completion, use **`fim`** instead (below):
it frames the task with explicit `<BEFORE>`/`<AFTER>` markers and strips code
fences, which is far more robust than asking a chat model to continue prose.

*A clarifying question in the ghost slot is still a model-compliance artefact,
not a design intent* — jichi does not promise it, detect it, or format it. But
the wish behind it was real, so it now has its own gesture with its own key and
its own rendering: **Ctrl-Q**, below. That is the honest resolution of this
report — the ghost slot was not redefined to mean two things.

*Design decisions (advanced):*
- **Explicit trigger, not as-you-type.** jichi's line editor is single-threaded and
  reads a byte at a time; a per-keystroke async suggestion would mean either
  background threads or blocking the editor on every keypress. A manual Ctrl-G
  makes the one (brief, ~12 s-bounded, Ctrl-C-cancellable) model call predictable.
- **Overlay, not a rewrite of `render()`.** The ghost is drawn by a small
  `render_ghost` overlay *on top of* the normal wrap-aware `render()` — the
  editor's redraw core is left untouched, so the multi-row redraw fix can't
  regress. The ghost is only offered with the cursor at end of line, kept to a
  single line, and self-heals on the next keystroke (which erases to screen end).
- **It completes the *prompt*, not code.** For code completion in an editor, use
  `fim` over ACP — that's the higher-value path; ghost text is a convenience for
  composing a prompt in the REPL.
- **Cleaned, but never rejected.** Both options this bullet used to list as
  hypothetical were taken in M280: the prompt gained few-shot examples, and a
  non-continuation reply got its own gesture and rendering (Ctrl-Q). What is
  *still* deliberately absent is **rejection**: `tui_suggest` does not decide
  whether the reply "is a continuation" before showing it. Every cheap test for
  that — starts with a capital? ends in `?`? doesn't lexically extend the line?
  — also rejects legitimate continuations, and an engine that silently drops
  good suggestions is worse than one that occasionally shows a bad one you
  dismiss with a keystroke. `jc_suggest_clean` removes *artefacts* (labels,
  quotes, echoes, extra lines); it does not judge *content*. That line is worth
  keeping where it is.
- **Layering.** `jc_term` exposes a generic `jc_suggest_fn` callback (like the
  Tab completer); the TUI implements it with a one-shot `autocomplete`-role model
  call (`tui_suggest`), so the terminal code stays model-agnostic.

## Prompt advice (Ctrl-Q)

**Ctrl-Q** asks: *is what I have typed clear enough to act on?* The model
replies with **one short line**, which is printed on its own dim, labelled line
above a redrawn prompt:

```
[chat·gemma-4·2%] › add caching to the loader
  advice: which loader, and cache what — parsed results or raw bytes?
[chat·gemma-4·2%] › add caching to the loader
```

Your line is **never touched**. That is the whole difference from Ctrl-G, and it
is asserted by a test (`tests/smoke/advice.sh` fails if the advice is spliced
in), because splicing a clarifying question into the input is precisely the
defect that motivated this gesture.

If the request is already specific, the model is told to answer exactly
`looks clear` — so you get a definite verdict rather than invented nitpicking.

*Beginner view:* before you press Enter on something vague, press Ctrl-Q. If it
names something you had not decided, decide it and type it in. If it says
`looks clear`, send it.

*Design decisions (advanced):*
- **A different model from Ctrl-G, deliberately.** Continuation is the
  `autocomplete` role's job (a small or FIM-tuned model is ideal). Judging
  whether a request is answerable is what you are already paying the **active
  chat model** for, and a 0.5B completion model would invent nitpicks. So
  `tui_advise` uses `config.model`, not the role.
- **Printed, not spliced.** It reuses the mechanism Tab already uses to list
  candidates: write on a fresh line, reset the row bookkeeping, redraw the
  prompt. No new rendering machinery, and the wrap-aware `render()` core is
  untouched.
- **One line, enforced twice.** The prompt asks for ~100 characters;
  `jc_advice_clean` then keeps only the first line and strips a leading
  `advice:`/`hint:` label or surrounding quotes. A printed paragraph would push
  the prompt around, so the cap is not cosmetic.
- **Why Ctrl-Q is free to bind.** jichi's line editor runs in raw mode with
  `IXON` cleared, so Ctrl-Q is not flow control here. If a terminal multiplexer
  grabs it before jichi sees it, that is the one environment where this key will
  not arrive — nothing in jichi can help, and the gesture simply does nothing.
- **Manual, like Ctrl-G.** One bounded (~20 s), Ctrl-C-cancellable model call
  per press. Nothing happens automatically, and nothing happens on an empty
  line.

## Tab completion (TUI)

Press **Tab** in the interactive prompt to complete the token under the cursor.
Behavior is classic readline:

- **0 candidates** → nothing happens (Tab inserts no character).
- **1 candidate** → the token is replaced with it.
- **>1 candidates** → the token is extended to their longest common prefix; if
  that adds nothing, the candidates are listed on a fresh line and the prompt is
  redrawn.

What completes depends on the line:

| You type | Tab completes |
| --- | --- |
| `/re` (at line start) | command names — built-ins **and** custom `.jichi/commands/*` (here: `/review`, `/resume`) |
| `/resume <prefix>` | saved session ids (from `~/.jichi.d/sessions`) |
| `/model <prefix>` | configured model names |
| `/mode <prefix>` | `chat` / `plan` / `auto` |
| `@<prefix>` | file paths under the workspace (directories get a trailing `/`), plus the `@diff` / `@url:` / `@sym:` providers |

Completion only replaces the span from the token start to the cursor; any text
after the cursor is preserved.

## Headless `complete`

```sh
jichi complete "the capital of France is"      # text as an argument
echo 'def add(a, b):' | jichi complete         # text on stdin
```

`complete` resolves the model with the `autocomplete` **role** via
`jc_app_model_for_role(JC_ROLE_AUTOCOMPLETE)`, falling back to the active model
when no model declares that role. It makes a single **non-streaming** call (the
same path as the compaction summarizer) with a system prompt instructing the
model to output only the continuation, and prints the result to stdout.

Give a model the role in config:

```json
{
  "models": [
    { "name": "qwen", "provider": "openai", "model": "...",
      "roles": ["chat", "autocomplete"] }
  ]
}
```

It honors the usual `--model` / routing / fallback flags. On no model or a
network failure it prints nothing and exits non-zero (bounded by the 60 s HTTP
timeout — it never hangs).

## Fill-in-the-middle (`fim`)

`fim` is the editor "tab autocomplete" gesture: given the code immediately
**before** the cursor (the prefix) and immediately **after** it (the suffix),
return the code that belongs at the cursor.

```sh
# Split a file at a byte offset (the cursor position):
jichi fim path/to/file.py 137

# Or pass the two sides as JSON on stdin (for unsaved editor buffers):
echo '{"prefix":"def add(a, b):\n    return ","suffix":"\n"}' | jichi fim
```

It prints **only the code to insert** — no surrounding prefix/suffix, and any
markdown code fence the model adds is stripped, so the output is ready to splice
into the buffer at the cursor.

Like `complete`, it resolves the `autocomplete`-role model (falling back to the
active model) and makes one non-streaming call. jichi speaks chat/messages APIs
(not a raw FIM-token completion endpoint), so the prompt is model-agnostic: the
prefix and suffix are wrapped in `<BEFORE>`/`<AFTER>` markers and the system
prompt asks for only the bridging code. Each side is bounded to the
cursor-nearest ~4 KB so a huge file stays within the model's context.

An editor integrates inline completion by shelling out to `jichi fim` (writing the
cursor's prefix/suffix as JSON on stdin). The Agent Client Protocol has no
standard inline-completion method, so this is exposed as a subcommand rather than
an ACP request; a custom ACP method could wrap it later if a client speaks one.

## Internals

- **Pure core** — `src/util/jc_complete.c` (`include/jc_complete.h`):
  `jc_complete_token` finds the whitespace-delimited token ending at the cursor;
  `jc_complete_common_prefix` computes the longest common prefix of the
  candidates. Both are pure and unit-tested (`tests/test_complete.c`).
- **Line editor hook** — `src/tui/jc_term.c` gains a `jc_completer_fn` callback
  (`jc_term_set_completer`) and a Tab branch that applies the readline logic via
  `ed_replace`. The completer mallocs each candidate string; `jc_term` frees them
  (no per-Tab arena growth, no leak). With no completer set the editor is
  unchanged, so other callers are unaffected.
- **TUI completer** — `tui_complete` in `src/tui/jc_tui.c` classifies the line
  and draws candidates from the built-in command list + `app->commands`,
  `jc_session_list`, the config model list, and `jc_list_dir`.
- **Headless `complete`** — `run_complete` in `src/main.c`, dispatched in the
  early-exit block (config + network, no tool registry).
- **FIM core** — `src/util/jc_fim.c` (`include/jc_fim.h`): `jc_fim_bound` (the
  per-side context window), `jc_fim_build_user` (the `<BEFORE>`/`<AFTER>` user
  message), and `jc_fim_strip_fences` (de-fence the model output). All pure and
  unit-tested (`tests/test_fim.c`); `run_fim` in `src/main.c` wires them to the
  one-shot call (splitting a file at an offset, or reading prefix/suffix JSON).
