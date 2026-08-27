# jichi in Japanese, Chinese and Korean

*Setup, what works, what does not, and how to check it on your own machine.
Everything below was measured on 2026-08-21 (M523) unless marked otherwise. The
engine mechanics live in [`ENCODING.md`](ENCODING.md); this page is the practical
side and does not repeat it.*

## The short version

You do not need to configure anything for CJK **content**. jichi treats prompts,
files and answers as bytes and only cares about UTF-8 where it must — measuring
columns and moving the cursor. What you may want to configure is the **answer
language**, and what you should check is your **terminal and font**.

## 1. Locale: less than you expect

jichi does **not** need `LC_ALL=ja_JP.UTF-8` or any CJK locale to handle CJK text.
The locale is read for `LC_TIME` / `LC_NUMERIC` only (and for the thousands
separator in token counts — `jc_locale_group_sep`). Display width comes from a
built-in table, deliberately **not** from libc `wcwidth()` + `setlocale()`, so it
renders identically on every machine regardless of locale
([`ENCODING.md`](ENCODING.md) argues why).

The one requirement is that your terminal is in a **UTF-8** mode, which on any
current desktop it already is.

## 2. Terminal and font — the part that actually goes wrong

Almost every "jichi's display is broken with Japanese" report is one of these two,
and neither is jichi:

- **A font without CJK coverage.** Missing glyphs render as boxes or are silently
  substituted at a *different width* than the terminal reserved, so text drifts by
  a column and the cursor lands in the wrong place. Install a font with CJK
  coverage and set it in the terminal.
- **A terminal that disagrees about ambiguous-width characters.** jichi reserves
  **2 columns** for East-Asian Wide and Fullwidth codepoints. A terminal
  configured to draw those in 1 column will disagree with jichi by exactly one
  column per character. Most terminals get this right by default; if yours has an
  "East Asian ambiguous width" or "treat ambiguous-width as wide" setting, it must
  agree with the terminal's own rendering.

**Halfwidth katakana is narrow, and jichi knows it.** `ｱｲｳ` (U+FF61–FF9F) occupies
one column each, while `アイウ` (U+30A1–) occupies two. A tool that widened all of
the `FFxx` plane would misplace the cursor on text mixing the two; the boundary is
pinned by tests (`tests/test_utf8.c`).

## 3. Editing CJK at the prompt

The line editor is codepoint-aware, so ([`ENCODING.md`](ENCODING.md) for detail):

- **Backspace / Delete** remove one whole character — a 3-byte あ goes in one
  keystroke, never leaving an invalid partial sequence.
- **Arrows and Ctrl-B / Ctrl-F** land only on codepoint boundaries.
- **Alt-B / Alt-F / Ctrl-W** treat a run of non-ASCII as one "word", so word
  navigation and word-kill work across CJK.
- **Ctrl-T (transpose) is ASCII-only by design** — swapping the raw bytes of a
  multibyte character would corrupt it, so it no-ops when either side is
  multibyte. This is a deliberate refusal, not a gap.

**About your IME:** composition happens in the terminal and the input method, not
in jichi — jichi receives the committed text. So an IME that works in your shell
works at jichi's prompt. What jichi is responsible for is what happens *after*
commit: the column arithmetic and the cursor, which is section 2's business.

## 4. Getting answers in your language

Two ways, and they are not equally reliable:

```jsonc
// in your config — see LANGUAGE.md
{ "language": "Japanese" }
```

or `--language` on the command line. jichi puts the instruction in the system
prompt.

**Measured caveat, worth knowing before you debug the wrong thing.** With
`"language": "Korean"` a local 9B model (`qwen/qwen3.5-9b`) returned **empty
turns** — one tool call, then no answer text — twice in a row. The same prompt,
same file, same model, with the language option *removed* answered normally; and
asking for the language **in the prompt itself** worked:

```sh
jichi -p "Read 설명.txt and answer in Korean, in one sentence, what it is."
# -> 설명.txt 는 시험 파일을 위한 문서입니다.
```

So: the config option is honoured by jichi, but a small model may respond to a
system-prompt language instruction by producing nothing. **If your answers go
empty, put the language in the prompt** — or use a larger model for non-English
output. This is a model-compliance limit, not an encoding one.

## 5. CJK file and symbol names

Both work, and one of them only started working at M523:

- **Filenames** — `read_file`, `list_files` and the repo map all handle them.
  Measured: `説明.txt`, `说明.txt` and `설명.txt` in one workspace, read by name.
- **Identifiers** — `def 계산(a, b)` and `def 加算(a, b)` now appear in the repo
  map as symbols. Before M523 the identifier predicate was ASCII-only, so such a
  file was listed with **no symbols at all**: it paid for its path in every
  request and contributed nothing. Python, Zig, JS, Java, Ruby and Elixir all
  permit non-ASCII identifiers, so this mattered to anyone writing code in their
  own language.

## 6. Check it yourself

Every command below was run in this form. Expected output is given so a
disagreement is visible rather than a matter of taste.

```sh
# in the jichi checkout
make test 2>&1 | tail -1
#   -> "NNNNN checks, 0 failures"  -- includes the width table and its boundaries
```

```sh
# in a throwaway directory
mkdir /tmp/cjk && cd /tmp/cjk
printf 'これはテストです。\n' > 説明.txt
printf 'def 계산(a, b):\n    return a + b\n' > 계산기.py

jichi map
#   -> lists 계산기.py: 계산      (a CJK filename AND a CJK symbol)

jichi -p "Read 説明.txt and say in one sentence what it is." < /dev/null
#   -> a sentence about the file; the CJK filename resolved
```

For the display side there is no substitute for looking: start `jichi`, type a
line of CJK text, and move the cursor through it with the arrow keys. If the
cursor tracks the characters, your terminal and jichi agree. If it drifts by one
column per character, re-read section 2 — that is a terminal or font setting.

## 7. Localized documentation

`docs/i18n/` carries translated pages: `ja/` 日本語, `zh/` 中文（简体）,
`ko/` 한국어, plus `de/` and `es/`. Every translation carries a banner saying it
is a **machine draft** and pointing at `en/` for terminology, because an
unreviewed translation that presents itself as authoritative is worse than an
honest draft. Native review is welcome and is the fastest way to improve them.

Coverage is uneven and stated rather than implied: `ja/` and `zh/` have
`GETTING_STARTED`, `PHILOSOPHY`, `JOURNEY`, `PROJECT_TIMELINE` and
presentations; `ko/` currently has `GETTING_STARTED` only. The deeper guides
(`TUTORIAL_*`, the subsystem pages, the curriculum) are **English only**.

## 8. What has not been verified

Named rather than implied, because "supports CJK" is the kind of claim that
quietly means "we typed one Japanese word once":

- **No CJK terminal-rendering matrix.** The width table is unit-tested and the
  column arithmetic is exercised, but jichi has not been run against a grid of
  terminal emulators and fonts to confirm what each draws.
- **Vertical text, right-to-left, and complex shaping** are not handled and are
  not planned; the line editor is a single-row-per-line column model.
- **The width table is a pragmatic subset** of the well-known wcwidth ranges. It
  covers common CJK, Hangul, kana, fullwidth, emoji and combining marks. A rare
  script may be off by a column — a cosmetic cursor nuance, and the table is easy
  to extend ([`ENCODING.md`](ENCODING.md)).
- **Answer quality in CJK depends on the model**, not on jichi, and this project
  has measured only that a local 9B answers Japanese and Korean correctly when
  asked in the prompt — nothing about fluency at length.
- **IME behaviour was not tested across input methods.** The reasoning in
  section 3 (composition is the terminal's business) is sound but is an argument,
  not a measurement.
