# Text encoding & internationalization

## The model: UTF-8, byte-oriented, pass-through

jichi treats all text as **opaque UTF-8 bytes**, end to end. There is no codepage
conversion layer — no `iconv`, no Shift-JIS / EUC-JP / GBK handling, and no
`setlocale(LC_CTYPE)`-driven decode:

- The wire protocol to/from the model is UTF-8 JSON (both providers + the event
  log keep it valid UTF-8).
- Files are read/written as raw bytes; the terminal renders whatever bytes it is
  handed.
- The only locale check (`LANG`/`LC_ALL`/`LC_CTYPE` contains "utf-8") is cosmetic:
  it decides whether the TUI prints Unicode glyphs (`▸ ✓ ✗`) or ASCII fallbacks
  (`> ok x`), and (M137) whether a non-English UI message catalog may be used —
  on a non-UTF-8 terminal the localized approval prompt falls back to English
  rather than emit mojibake (see [LANGUAGE.md](LANGUAGE.md)). `setlocale` is
  otherwise used only for `LC_TIME`/`LC_NUMERIC`.

**Consequence:** non-ASCII *content* — Japanese and other languages in prompts,
model responses, files, search, `@`-references — works everywhere, including
headless (`-p`/`--auto`) and ACP, because it is just UTF-8 bytes flowing through.

### Design decision — why UTF-8 only, not legacy codepages

We deliberately **standardize on UTF-8** rather than support Shift-JIS/EUC-JP/etc.:
UTF-8 is the modern default on Linux/macOS terminals and editors; a conversion layer
(`iconv`) would add a dependency against the project's "libcurl + cJSON only" rule
and a large maintenance/edge-case surface. Users on a legacy-encoded terminal or
file should convert to UTF-8 (e.g. `iconv -f SHIFT-JIS -t UTF-8`).

## Base64 transport is input-only: base64 in ≠ base64 out (M129)

The `--config-json-b64` and `--prompt-b64` flags accept **base64** so an inline
config or prompt survives shell/SSH quoting (see docs/SCRIPTING.md,
docs/REMOTE_SSH.md). This is a *transport* encoding of the **input** only — not a
content encoding, and **not symmetric**:

- The base64 is decoded **once, at startup**, into plaintext UTF-8 and fed into the
  normal config/prompt path. From then on jichi has no idea the input ever was base64.
- The **answer is always plain UTF-8** — raw text on stdout (`--output text`), or a
  UTF-8 JSON object whose `text` field holds the UTF-8 answer (`--output json` /
  `jsonl`). There is **no** base64 response encoding and no flag for one (it was an
  explicit non-goal of the M129 design). Base64 input never causes base64 output.

If a downstream tool needs the answer base64'd (e.g. to carry it back over a hostile
transport), that is a trivial pipe on your side — keeping the concern at the
transport layer, consistent with jichi treating all content as opaque UTF-8 bytes:

```sh
# base64 IN (quoting-safe config), plain UTF-8 answer, base64 OUT (your pipe):
jichi --config-json-b64 "$B64" -q -p "…" | base64 -w0
```

## Interactive line editing (M127)

The one place bytes are not enough is the **TUI prompt line editor**: it must move
and delete by whole **codepoints** and measure **display width** correctly, or the
cursor and line-wrap drift on non-ASCII input. M127 makes the editor UTF-8-aware.

### Codepoint-granular editing

`jc_utf8_prev` / `jc_utf8_next` step over whole codepoints (skipping continuation
bytes), so:

- **Backspace** and **Delete** remove one whole character (a 3-byte あ goes in one
  keystroke, not three), never leaving a partial/invalid sequence.
- **Ctrl-B / Ctrl-F** and the **left/right arrows** land the cursor on codepoint
  boundaries, never inside a multibyte character.
- **Alt-B / Alt-F / Ctrl-W** treat a run of non-ASCII bytes as a "word", so word
  navigation and word-kill work across CJK / non-Latin text.
- **Ctrl-T** (transpose) is ASCII-only by design — swapping the raw bytes of a
  multibyte character would corrupt it, so it no-ops when either side is multibyte.

### Display width (CJK / fullwidth / combining)

`jc_utf8_width(cp)` returns the terminal column count for a codepoint: **2** for
East-Asian Wide / Fullwidth blocks and common emoji, **0** for combining marks and
zero-width/format controls, **1** otherwise. `jc_term_str_cols` uses it (still
skipping ANSI escapes) so the cursor position and the wrap-aware multi-row redraw
line up with what the terminal actually shows for Japanese, Chinese, Korean,
fullwidth forms, and emoji.

### Design decision — a self-contained width table, not libc `wcwidth()`

`jc_utf8_width` uses a **built-in East-Asian-Wide / Fullwidth / zero-width range
table** instead of libc `wcwidth()` + `setlocale()`. Reasons:

- **Deterministic** — `wcwidth()` results vary by libc version and the active
  locale; a fixed table renders identically everywhere.
- **No locale state** — no `setlocale(LC_CTYPE, "")` dependency (which would also
  interact with the `LC_NUMERIC` juggling the config parser does).
- **Pure + unit-testable offline** (`tests/test_utf8.c`), C89-clean, no new
  dependency.

The table is a pragmatic subset of the well-known wcwidth ranges — it covers the
common CJK, Hangul, kana, fullwidth, emoji, and combining-mark cases. Rare scripts
may be off by a column; that is a cosmetic cursor-alignment nuance, not a
correctness or safety issue, and the range table is easy to extend.

## Input methods (IME)

IME composition (e.g. typing Japanese via a Japanese IME) happens in the
**terminal / OS layer before bytes reach jichi**: jichi receives the *committed* UTF-8
bytes and inserts them. Combined with the codepoint-granular editing above, typing,
editing, and submitting IME text all work. (Pre-edit/composition rendering is owned
by the terminal, not jichi.)

## What is verified

- `tests/test_utf8.c`: decode, prev/next stepping, per-codepoint width (ASCII /
  hiragana / Hangul / fullwidth / emoji / combining), `str_cols`, and safe handling
  of truncated/invalid sequences.
- A PTY smoke under ASan/UBSan drives real Japanese input plus backspace, arrows,
  word ops, undo, and clear — clean exit, no memory errors.
- **End-to-end in the real TUI (2026-07-13):** driven through the zigodot `./jichi`
  wrapper over a PTY in a UTF-8 locale, `こんにちは` typed into the prompt echoed
  back intact, one backspace left `こんにち` (a whole codepoint removed, not a
  partial byte), no `U+FFFD` replacement characters appeared, and the session
  exited cleanly. Column *alignment* for wide characters is driven by the
  unit-tested `jc_utf8_width` (CJK/fullwidth = 2) but is best eyeballed by a human
  in a terminal.

> Note: the `jichi` / `jichi-convert` binaries are **git-ignored build
> artifacts** — never committed. Rebuild with `make` (they're what the zigodot
> `./jichi` wrapper resolves to via `../jichi/jichi`).

### Multiline paste (M156)

Pasting text with embedded newlines used to collapse to the first line: the
editor reads byte-at-a-time and treated a pasted `\n` exactly like a typed
Enter, so the first newline submitted and the rest spilled into the next
prompt. M156 fixes it two ways, on mutually exclusive byte paths:

- **Bracketed paste** — the editor emits `ESC[?2004h` while reading (and
  `ESC[?2004l` at teardown), so a terminal wraps pasted content in
  `ESC[200~ … ESC[201~`. Bytes inside that region — newlines included — are
  captured as *content*, spliced into the buffer at the cursor, and never hit
  the submit branch. A paste does **not** submit; a subsequent real Enter does.
- **Burst fallback** — for a terminal (or tmux config) that doesn't send the
  markers: at a bare newline, if input is *immediately pending* (`select()`
  with a zero timeout), the newline is treated as a paste line break, not
  Enter. A pasted block arrives as one burst so its interior newlines all see
  pending input; a typed Enter (nothing buffered behind it) still submits.

Pasted text reuses the M69 trailing-`\` continuation machinery: each line but
the last is committed and echoed, the last stays editable, and Enter submits
the whole block. The buffer stays one logical line per row (no newline-aware
render rewrite), so pasted rows are shown but not re-editable in place. The
splice/normalize core (`jc_paste_splice`: CRLF and lone CR → LF, cursor
tracking) is pure and unit-tested; the end-to-end paste is PTY-tested
(`tests/e2e/paste.py`).

## Deferred / future

- A fuller wcwidth range table (rarer scripts, Unicode 15 emoji sequences).
- Grapheme-cluster awareness (combining sequences, ZWJ emoji) — today each codepoint
  is edited/measured independently, which is correct for the common cases.
- Full in-buffer multiline *editing* (arrow/edit freely across pasted rows) —
  M156 preserves & submits pasted multiline text but keeps one logical line per
  row; richer editing would need a newline-aware render/cursor model.

See also: docs/TUI_RENDER.md, docs/ACCESSIBILITY.md.
