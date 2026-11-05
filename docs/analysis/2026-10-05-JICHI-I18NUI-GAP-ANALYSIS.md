# JICHI I18N UI CATALOG — gap analysis (the inverse: what is guarded well)

## TL;DR

*Read-only survey of the M137 UI-message catalog (`src/util/jc_msg.c`,
`include/jc_msg.h`) and its documentation (`docs/LANGUAGE.md`,
`docs/i18n/README.md`) at HEAD `1b71247d`, 2026-10-05. The one thing
definitely lacking: **the program-layer coverage number lives nowhere a
reader can see it.** The test (`tests/test_msg.c`) pins per-language
untranslated counts — en 0, de 0, **es/ja/zh 18 of 30 slots** — and *any*
count change fails the build (M380); but the figure exists only in the
test's stdout and in code comments. The docs' promise — "the
highest-traffic UI strings (the tool-approval prompt, the working
indicator) are translated into five languages" — is *true and
understated*: the two highest-traffic strings are indeed in all five,
and *12 further strings* are in all of es/ja/zh (the M557 "chrome"
sentences) — but no document says how complete any language is. A reader
of LANGUAGE.md cannot tell that 40% of the UI in Japanese is English
fallback (by design, by pin, by documented phase) without reading
`tests/test_msg.c`.

## 1. The defense (measured, not assumed)

The catalog is `enum jc_msg_id` (30 slots, `JC_MSG__COUNT`) × five
`const char *const` tables in `jc_msg.c` (en 17, de 108, es 194, ja 219,
zh 265 — C positional initialization). Lookup is `jc_msg(id)`
(`jc_msg.c:450`): a NULL/empty slot **falls back to English** — the
documented phased policy ("tables can grow string by string",
LANGUAGE.md:100).

The guard is `tests/test_msg.c`'s M380 sweep, and it is better than most
lints in this register:

- it asserts through `jc_msg_raw()` — the pre-fallback table — so a
  forgotten translation *cannot* hide behind English (the defect M380
  itself fixed: "the assertion matched, but not the thing it named");
- it **prints** `catalog: <lang> has N of 30 entries untranslated`;
- it **pins** the counts: en 0 (line 215), de 0 (line 224, "GERMAN IS
  COMPLETE as of M568", with the operator's approval quote), es/ja/zh
  **18** (line 246) — so *gaining* a translation fails the build until
  the pin is raised with a sentence, and *losing* one fails it directly;
- the pin carries a changelog of every raise: M565 (11→12), M570 (→13,
  "JC_MSG_DENIED_STOP landed in English and German only"), M571 (→14),
  M572 (→15), M578 (→16), M753 (16→18, "Esc stops a running turn" —
  verified: M753 added `JC_MSG_ESC_STOP`/`JC_MSG_TYPEAHEAD_OFF` in
  English+German only and raised the pin in the same commit).

(One micro-drift inside the otherwise-tight guard: the changelog *comment*
ends at "M578 to 16", while the *check* says `== 18` — M753 raised the
value but not the trailing comment. It is a comment, not a gate, so it
fails nothing; noted for completeness.)

## 2. What the documentation actually says — and does not

**LANGUAGE.md (the M137 section):** "the highest-traffic interactive UI
strings (the tool-approval prompt, the working indicator) in
en/de/es/ja/zh" and "An untranslated entry falls back to English, so
tables can grow string by string." True, and the fallback is disclosed.
But it carries **no coverage number**: a reader learns *that* partial
translation exists, not *how much* of Japanese or Chinese is English.

**i18n/README.md (the program-side note):** "the highest-traffic UI
strings (the tool-approval prompt, the working indicator) are translated
into five languages (Korean is documentation-only so far)." Also true,
also silent on the 12/30. Its coverage **table** is the model for what the
program layer lacks: it has a per-language row with explicit ⚠️ cells
("draft, figures behind", "4 decks short a slide") — the *exact*
"be honest about the gap" device this directory's own policy demands.
That table covers the *docs* (Philosophy/Journey/Timeline/Slides); the
*program catalog* has no row in it at all.

**The two highest-traffic strings, verified present in all five:**
`JC_MSG_WORKING` (slot 0) and the tool-approval prompt (slot 1) are the
two most-frequent UI lines; `msg_en`/`de`/`es`/`ja`/`zh` all initialize
slots 0 and 1 (ja: 作業中 / 許可しますか?; zh: 处理中 / 允许吗?; es:
trabajando / ¿Permitir?). So the docs' specific claim is not just
true — it understates the real state, which is that **all of es/ja/zh
translate 12 of 30 slots** (the M557 "chrome" sentences + the
unrecognised-key notice), not merely two.

## 3. The one thing definitely lacking

**A coverage fact a reader can see without reading the test.** The
number (12/30 for es/ja/zh, 30/30 for en/de, and the per-raise
changelog) is the authoritative, build-enforced state of the program's
localization — and it lives only in `tests/test_msg.c`'s printed output
and comments. The two documents that *promise* the localization carry
the fallback *policy* but not the *coverage*, so a learner opening
LANGUAGE.md to find out whether jichi will speak to them in their
language gets an answer one sentence short of the truth: "some
strings, fallback to English" instead of "12 of 30 UI sentences,
fallback for the other 18."

This is the mirror of analyses 38–40 (the *denominator* rot: plans,
deferred anchors, deck coverage). Here the code is the strong side and
the **prose is the weak side** — the gap is a disclosure, not a
mechanism. The fix is one small addition, and it should point at the
source of truth rather than restate a number that will drift:

## 4. The correction set (ready to apply)

1. **`docs/LANGUAGE.md`, M137 section** — add one line under the
   fallback disclosure: the catalog is **30 UI sentences**; English and
   German are complete, es/ja/zh carry the original "chrome" set
   (12/30) with English fallback for the rest, *as pinned by
   `tests/test_msg.c`* (name the pin, not the number, so it cannot
   rot). Mirrors the i18n/README table's honesty device.
2. **`docs/i18n/README.md` coverage table** — add a **UI (program)**
   column: en ✅ · de ✅ · es ⚠️ 12/30 · ja ⚠️ 12/30 · zh ⚠️ 12/30 ·
   ko —, with the cell footnote "count pinned in `tests/test_msg.c`;
   raised per M565/M570/M571/M572/M578/M753."
3. **`tests/test_msg.c` pin comment** — append the M753 raise
   (16→18: `JC_MSG_ESC_STOP`/`JC_MSG_TYPEAHEAD_OFF`, en+de only), so
   the in-code changelog matches the value.

## 5. Limitations (honest)

- The coverage counts come from the **pin** in `tests/test_msg.c`
  (authoritative, build-enforced), not from my re-count of the tables;
  I verified N=30 slots and that slots 0–1 (the two docs-named strings)
  are initialized in all five languages. I did not run the test binary
  this session (no build in the read-only checkout's current state);
  the pin *is* the record of its last passing state.
- "Highest-traffic" is the docs' word, not a measured frequency; I
  accept it as the intent (working indicator + approval prompt are the
  per-turn and per-tool lines, the most repeated UI text).
- The *answers* layer (M135) is out of scope here: it is the model's
  output, quality-is-the-model's, and the doc says exactly that.

## 6. Appendix (reproducible, from the jichi checkout @ 1b71247d)

    # the guard: pin values + print
    grep -nE 'untranslated == (0|18)' tests/test_msg.c      # 215, 224, 246
    sed -n '205,247p' tests/test_msg.c | grep -E 'catalog:|MSGL|complete|raised'
    # the slots and the two highest-traffic ids
    awk '/JC_MSG_WORKING = 0/{f=1} f&&/^    JC_MSG_/{c++} f&&/JC_MSG__COUNT/{print c; exit}' include/jc_msg.h   # 30
    grep -nE 'JC_MSG_WORKING = 0|JC_MSG_TOOL|approve|permit' include/jc_msg.h | head -3
    # fallback is by design
    sed -n '450,462p' src/util/jc_msg.c                     # s == NULL -> msg_en
    # docs-named strings in all five (slots 0,1)
    for L in 17 108 194 219 265; do sed -n "${L},$((L+4))p" src/util/jc_msg.c | cut -c1-50; done
    # the promise, and the missing number
    sed -n '8,9p' docs/LANGUAGE.md
    sed -n "$(grep -n 'program side' docs/i18n/README.md | cut -d: -f1),+5p" docs/i18n/README.md
    grep -cE 'untranslated|12/30|18 of 30|coverage' docs/LANGUAGE.md   # 0
    # the M753 pin raise (value moved, comment did not)
    git show 53ed13d4 -- tests/test_msg.c | grep -E '^[+-].*untranslated'

## Verdict

The i18n UI catalog is **the opposite of the last four findings**: its
code is one of the most tightly guarded surfaces in the register
(raw-table assertion, printed + pinned per-language counts, a
self-updating changelog the gate *forces* you to write). The one thing
it lacks is a **disclosure**: the coverage number a reader needs
("12 of 30, fallback for the rest, es/ja/zh; en/de complete") lives in
the test's output and comments, not in either document that promises
the localization. Three small additions (§4) move the fact where the
i18n README's own table already teaches it to live.
