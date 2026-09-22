# 自治の八句 — eight haiku for jichi

Eight small poems about what this program does and what building it has taught.
Each is given in Japanese, with its reading in hiragana, its mora count, and an
English rendering. **The provenance of every line is at the foot of the page**,
because a document that will not say who wrote it has no business being in this
tree.

---

### 1. 一巡 — one turn of the loop

> 問い一つ　道具は動き　また静か
>
> といひとつ / どうぐはうごき / またしずか — 5 · 7 · 5

*One question asked —*
*the tools stir, and then*
*quiet again.*

---

### 2. 場 — the arenas

> 場が閉じて　借りし記憶の　みな還る
>
> ばがとじて / かりしきおくの / みなかえる — 5 · 7 · 5

*The scope closes;*
*all the memory it borrowed*
*returns at once.*

Three arenas, freed by lifetime rather than one object at a time —
[`CLAUDE.md`](../CLAUDE.md)'s oldest invariant.

---

### 3. 柵 — the fence

> 柵閉ざす　誰も見ずとも　なお拒む
>
> さくとざす / だれもみずとも / なおこばむ — 5 · 7 · 5

*The fence stays shut —*
*with no one watching*
*it still refuses.*

A fence refuses rather than warns, and unlike a cap it stays on
([`VOCABULARY.md`](VOCABULARY.md)).

---

### 4. 空の緑 — the hollow green

> みな緑　落ちぬ試験は　何も見ず
>
> みなみどり / おちぬしけんは / なにもみず — 5 · 7 · 5

*All of it green —*
*but a test that cannot fail*
*has looked at nothing.*

The rule this project states most often, in seventeen mora.

---

### 5. 知らぬ機 — the first run on a strange machine

> 知らぬ機に　初めて載せて　古き傷
>
> しらぬきに / はじめてのせて / ふるききず — 5 · 7 · 5

*Carried for the first time*
*onto an unknown machine:*
*an old wound.*

Written the day illumos found a `uname()` call that had been wrong on every
non-Linux kernel since it was typed.

---

### 6. 記録 — the record

> 通りしを　書かず調べぬ　ことを書く
>
> とおりしを / かかずしらべぬ / ことをかく — 5 · 7 · 5

*Not what passed —*
*what went unexamined:*
*that is what I write.*

---

### 7. 古き文字 — C89

> 古き文字　選びて書けば　遠く行く
>
> ふるきもじ / えらびてかけば / とおくゆく — 5 · 7 · 5

*Choose the old letters*
*to write with, and the writing*
*travels further.*

---

### 8. 門 — waiting for the gate

> 夜更けまで　門の開くを　待ちにけり
>
> よふけまで / もんのひらくを / まちにけり — 5 · 7 · 5

*Late into the night,*
*waiting for the gate*
*to open.*

門 is a gate in the ordinary sense and **the** gate in this project's sense —
`make ci`, which nothing merges without.

---

## Provenance, stated plainly

**The Japanese is Claude Opus 5's, not a native speaker's, and not the local
model's.** A native reader's corrections are welcome and expected; this page is
offered in the same spirit as `docs/i18n/`'s machine drafts, which say what they
are.

**The 5-7-5 is measured, not judged by ear.** Mora are counted mechanically:
every kana is one, the small ya/yu/yo and small vowels attach to the preceding
kana and count zero, and 「っ」「ん」「ー」 each count one. That is why every poem
above carries its reading — *kanji cannot be counted without one*, which is the
whole reason a hiragana line sits under each haiku rather than being tidied away.
All eight verify at 5 · 7 · 5.

**The local Japanese model was asked to write these first, and could not.**
`llm-jp-4-8b-thinking` on a local LM Studio, the model
[`analysis/2026-09-17-local-japanese-translation.md`](analysis/2026-09-17-local-japanese-translation.md)
ranked best of five for translation, was given the same eight themes through two
different pipelines — an English prompt, then a Japanese system prompt with a
worked example and a stricter parser — for about 130 requests in total. It
produced **two conforming haiku out of sixteen theme-attempts**, and both ignored
their theme: one was a spring-rain poem offered for *"the test suite passes but a
vacuous check proves nothing"*, and the other coined 「緑紙」 (*green paper*),
which is not a word. Across 84 attempts in the second run the rejections were 27
wrong mora counts, 25 malformed haiku lines, 25 readings that were not pure
hiragana, and 5 replies with no parseable output at all.

That is a measurement rather than a complaint. Constrained-form composition in a
second language is a different task from translation, and the same model that is
*"fit to draft, for a reviewer"* at one is not usable at the other.

**The local model did do the job it is good at.** All eight were then sent back
to it for review — a judging task, not a generating one — and it answered on
three axes: naturalness, whether the reading matches the kanji, and any single
unnatural word. It rated **five of eight fully 自然 (natural)**, confirmed **all
eight readings match**, which is an independent corroboration of the mora counts
above, and flagged nothing as an unnatural word. Its two substantive notes are
kept because they are fair:

- **§6 記録** — *"語順と助詞が古典的で現代俳句には馴染みにくい"*: the word order
  and particles are classical and sit oddly in a modern haiku.
- **§7 古き文字** — 「古き文字」 reads 文語的, literary and a little stiff.

Both are true. The classical register (`書けば`, `待ちにけり`, `通りしを`) is a
deliberate choice and a defensible one in haiku, but a reader should know it was
a choice and not an accident — and a native reviewer may well disagree with it.
