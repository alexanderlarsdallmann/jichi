# Natural language (answers + UI)

jichi has two localization layers on the program side, both phased like the
[localized docs](i18n/README.md):

1. **Answer language (M135)** — tell the model what language to answer in.
2. **UI messages (M137)** — the highest-traffic interactive strings (the
   tool-approval prompt, the working indicator) in en/de/es/ja/zh.

Non-ASCII *content* has always worked (UTF-8 pass-through, see
[ENCODING.md](ENCODING.md)); since M127 the line editor also handles wide
characters. This page is about the language jichi *speaks*, not the bytes it
carries.

## Answer language (M135)

Without a directive, most models answer in whatever language the prompt is in —
which for a learner reading English docs but thinking in Japanese usually means
English. The `language` setting injects one stable line into the system prompt:

> Respond in *X* unless the user writes in a different language or explicitly
> asks for another one. Code, identifiers, file paths, and command names stay
> as they are; translate only the surrounding prose.

Set it any of three ways (CLI > config; the TUI command changes the live
session):

```jsonc
{ "language": "Japanese" }        // config (free-form: "Deutsch", "zh", ...)
```

```sh
jichi --language German -p "explain make -j"
```

```
/language Japanese     # TUI: takes effect on the next message
/language              # show the current setting
/language off          # back to the model's default
```

Notes:

- The value is passed to the model **verbatim**, so any language the model
  knows works ("Swahili", "Latin", "Klingon") — it does not have to be one of
  the five doc languages. An English name ("Japanese") is the most reliable
  form across models; native names ("日本語") work on good ones.
- One stable line means the **prompt-cache prefix doesn't churn** (M31);
  changing `/language` mid-session invalidates the cached prefix once, like
  any system-prompt change.
- **Top-level, and every `subtask: true` command** (M597). A *spawned* delegate
  (`spawn_subagent`, `spawn_parallel`, `ask_for_help`) keeps its focused-task
  prompt: its answer is consumed by the main agent, which follows the directive
  itself. A command subtask has no main agent downstream -- its answer streams to
  you, or is written to a file -- so it receives the directive too. Until M597 it
  did not, and the bullet below was false for the mentor loop: a German
  self-learner's `/learn` drafted English lessons.
- **A command may pin its own language** with `language:` in its frontmatter
  (`.jichi/commands/*.md`, see [COMMANDS.md](COMMANDS.md)). It replaces the
  session language for that command's run. This is the **English-canonical
  lessons** option: `language: English` on `learn.md` keeps `memory.md` in one
  language while the session still tutors you in yours.
- Quality is the model's: a small local model may drift back to English on
  long answers. The directive is a strong nudge, not a guarantee.
- Learning surfaces inherit it for free: `/assign`, `/solve`, `/check`, the
  hint ladder, and -- since M597 -- the mentor loop are model-generated, so with
  `"language": "Deutsch"` an instructor gets German assignment briefs, learners
  get German feedback, and `/learn` drafts German lessons without any pack
  changes. The draft's five section headings stay English regardless (they are
  the machine format `learn apply` parses; the mentor is told so).

## UI messages (M137)

The reference surface (help text, subcommands, errors, docs) stays
English-canonical — same policy, and same reasoning, as the reference docs: an
out-of-date translation of a safety-relevant string is worse than an accurate
English one. What *is* translated is the small set a non-English speaker hits
constantly, starting with the **tool-approval prompt** (a safety surface — the
user must understand what they are approving) and the working indicator:

```
▸ write_file  main.c
  許可しますか? [y] はい  [n] いいえ  [a] 常に  [e] 編集  [v] 表示
```

Rules:

- **The keys are never localized.** `[y]/[n]/[a]/[e]/[v]` are the accepted
  keypresses in every language (a unit test enforces it), so muscle memory and
  PTY-driving scripts work everywhere.
- **Resolution order:** `$JICHI_LANG` (a UI-specific override — set `JICHI_LANG=en`
  to keep the UI English while answers are Japanese) → the config `language` →
  `$LANG`'s language prefix (`de_DE.UTF-8` ⇒ de) → English. Unrecognized
  values fall through to the next source.
- **Non-UTF-8 terminals fall back to English** rather than emit mojibake —
  the same policy as the `▸ ✓ ✗` glyph fallback.
- The catalog is compiled in (`src/util/jc_msg.c`) — no gettext, no locale
  data on disk, per the libcurl+cJSON-only dependency rule. An untranslated
  entry falls back to English, so tables can grow string by string.

### Adding a string or a language

1. Add the id to `enum jc_msg_id` (`include/jc_msg.h`) and the English text to
   `msg_en` (`src/util/jc_msg.c`); add translations to any of the other
   tables — missing entries just show English.
2. Non-ASCII text is UTF-8 written as `\xNN` escapes (the source stays ASCII,
   like the TUI glyphs), with the rendered string in a comment beside it.
3. A new language needs a `msg_*` table, a `jc_msg_lang` value, aliases in
   `match_name`, and a row in the `test_msg` language loop.

Machine-drafted translations follow the docs policy
([i18n/README.md](i18n/README.md)): honest about their status, native review
welcome — the ja/zh strings especially.

## What is deliberately not localized

- Headless output (`-p`, `--output json/jsonl`): consumed by scripts and
  agents; stable English/JSON is the contract ([SCRIPTING.md](SCRIPTING.md)).
- Log/telemetry/journal events: greppable, stable identifiers.
- `/help`, subcommand output, `doctor`: reference surface, English-canonical
  (phase 2 candidates if native maintainers appear).
