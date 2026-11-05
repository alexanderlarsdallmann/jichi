# The TUI's principles, on one page

*A contributor's page, written 2026-09-25 from the operator's decision on the twelve questions,
item 8: "one TUI principles page"
([`plans/2026-09-24-twelve-questions.md`](plans/2026-09-24-twelve-questions.md)). **It restates;
it does not decide.** Every rule below was decided somewhere else, and each says where, so the
rule and its reasons can be read together. Five documents made them:
[`INTERFACE_TUTORIAL.md`](INTERFACE_TUTORIAL.md), [`ACCESSIBILITY.md`](ACCESSIBILITY.md),
[`TUI_RENDER.md`](TUI_RENDER.md),
[`proposals/2026-08-tui-chrome-channel.md`](proposals/2026-08-tui-chrome-channel.md) and
[`proposals/2026-08-accessibility-by-default.md`](proposals/2026-08-accessibility-by-default.md).
The field these rules come from is [`BIBLIOGRAPHY.md`](BIBLIOGRAPHY.md) section 14. Where the five
disagree with each other or with the code, section 7 says so rather than choosing.*

A rule's status is marked when its source marks it: **shipped** (a milestone built it),
**proposed** (a design page states it and not all of it is built), or neither, which means the
page states it as standing practice.

## 1. Orientation

- **Two facts, all the time: where you are, and what you can do here.** *"Every interface owes
  its user those two facts, continuously."* ([INTERFACE_TUTORIAL](INTERFACE_TUTORIAL.md) section 1)
- **A warning names its fix**, *"in its own second line, not in a manual."* (section 2.2)
- **An unconfigured first run is a *"normal state, not an error."*** (section 2.2)
- **A check that passes still says so**: *"a check that passes still prints a line."* The
  tutorial calls this the design decision worth stealing (section 2.2).
- **An error has three obligations**: where you are, what happened, and what to do next.
  *"An error message is an interface, and it has the same three obligations."* (section 2.3)
- **Every refusal names a way forward** (the checklist, item 4).
- **A key people press does something, or says it does not.** Esc stopped nothing and said
  nothing until M753; it now stops a turn, as Ctrl-C does, and says so. **Shipped**, M753.

## 2. Content and chrome

- **The invariant: the model's words are content and are never altered. jichi's own lines are
  chrome, and may be rewritten, reordered or removed.** *"Never touch content."* (chrome-channel
  proposal, section 1). The accessibility-by-default proposal makes it the permanent boundary:
  *"Chrome may become prose. Content may not."* **Shipped** as a code split at M549.
- **Words, never symbols, in chrome.** *"Words, never symbols, in chrome."* (chrome-channel
  proposal, section 1) **Proposed**; M553 and M562 built parts of it.
- **Say each thing once, and put nothing that describes a reply before the reply.** *"Say each
  thing once."* (ibid.) The per-message token line is printed at `on_message_end`, after the text
  ([TUI_RENDER](TUI_RENDER.md)).
- **On demand beats always spoken, and a number is news only when it matters.** *"Available on
  demand beats always spoken."* and *"A context budget at 6% is not news. At 85% it is."*
  (chrome-channel proposal). The accessible prompt says how full the context is only from 80
  percent, where compaction fires: `chat > `, then `chat, 85 percent full > `. **Shipped**, M562.
- **Output to a pipe is raw text**: *"output is always raw text, so pipes and JSON stay
  clean."* Styling happens only in the TUI, with colour on ([TUI_RENDER](TUI_RENDER.md)).
- **Accessibility changes human-facing text only.** *"this is human-facing text only."* JSON,
  ACP and JSONL never change for it (accessibility-by-default proposal).

## 3. Prose and compression

- **The synthesis.** *"Prose where a human must understand. Compression where a human must
  scan."* (accessibility-by-default proposal) **Shipped**, applied at M560.
- **Linearise what is laid out, and do not say more.** Linearise data whose layout is its meaning,
  and prose is not a licence to say more (ibid.).
- **Decompose a flag into properties; do not flip it wholesale.** The bundle that was
  "accessible mode" is being retired one property at a time (ibid., plan step A3, partly done).

## 4. Accessible mode

- **No motion.** *"Accessible mode prints a single static `working...` line instead."* of the
  animated spinner ([ACCESSIBILITY](ACCESSIBILITY.md)). **Shipped**, M118.
- **The role comes before the content**, so a listener knows who is speaking before hearing it.
  **Shipped**, M184.
- **Colour and dimness never carry meaning alone.** *"dim text is used only for de-emphasis,
  never to carry required meaning"* (ACCESSIBILITY).
- **Accessible wins over colour.** *"accessible mode does not imply `NO_COLOR`"*: the accessible
  form must hold with colour on ([INTERFACE_TUTORIAL](INTERFACE_TUTORIAL.md) section 5.3).
- **The question and its keys are spoken before the editor waits**, with no brackets to spell
  aloud. **Shipped**, M551.
- **Every decision has a letter and a digit, and a stray key is not an answer.** *"A slip is not
  an answer"* (ACCESSIBILITY). **Shipped**, M564, M565 and M573.
- **A limit is stated once, not counted down after every refusal.** **Shipped**, M573.
- **Digit grouping is for sighted readers**: a thousands separator is read aloud as a separator,
  so accessible mode leaves it out. **Shipped**, M555.
- **An untranslated entry stays a visible gap**, because *"text that looks translated and is not
  is worse than a gap"* (accessibility-by-default proposal). **Shipped**, M557.

## 5. The terminal

- **Ask the terminal its size, and have two fallbacks.**
- **Both ends have to be a terminal** before anything that assumes one is done.
- **Colour is meaning, and the rule is one line**, and honour `NO_COLOR` correctly, which is
  harder than honouring it at all.
- **Wrap to a width you chose, not to the terminal's.** (These four are
  [INTERFACE_TUTORIAL](INTERFACE_TUTORIAL.md) section 5.2, each with the failure that produced
  it. The widths the documents choose disagree; see section 7.)
- **The transcript that scrolls is never overwritten.** Only the working line, on the last row,
  is redrawn: *"so the scrolling transcript can never be overwritten"* ([TUI_RENDER](TUI_RENDER.md)).
  **Shipped**, M254.
- **A paste never submits**: *"the paste never submits; a real Enter does."* (ibid.) **Shipped**,
  M156 and M363.
- **The prompt keeps the short tier name, and the reply names the model** (ibid.). **Shipped**,
  M296.

## 6. Testing an interface

- **A pty tests the logic; only a terminal tests the terminal**
  ([INTERFACE_TUTORIAL](INTERFACE_TUTORIAL.md) section 5.4).
- **Know what a script can check and what it cannot** (ibid., section 4).
- **Watch one person use it, without coaching.** The checklist's last item has never been
  recorded as done. [`LEARNER_STUDY.md`](LEARNER_STUDY.md) is how to do it.

The tutorial's checklist, as it states it (section 7):

1. Can a person arriving mid-task tell **where they are**?
2. Can they tell **what is available**?
3. After acting, can they tell **what happened**?
4. Does every refusal name **a way forward**?
5. Does a check that passes still **say so**?
6. Does every diagram answer a question the prose cannot?
7. Does every step have an **escape route** when it fails?
8. Have you watched **one person** use it without coaching them?

## 7. Where the five documents disagree, or have not caught up

Found while writing this page, on 2026-09-25. This page does not settle any of them: each is a
decision for whoever owns the page it is in, and the list is in [`DEFERRED.md`](DEFERRED.md).
Where the code settles a question, that is said. **The code is what runs, and a page that
disagrees with it is the one that is out of date.**

- **The accessible reply header.** ACCESSIBILITY describes `assistant (<model> - <mode> -
  <time>):`, and the chrome-channel proposal shows a model named only when it changes. The code
  prints *"Model %s responds with the following:"* and names the model every time, having dropped
  the mode and the time on purpose (`src/tui/jc_tui.c`, where the header is written).
- **The approval key list.** ACCESSIBILITY quotes the spoken prompt as *Press y for yes, n for
  no, a for always, e to edit*, and both proposals as *Press y as in yes*. The catalog says
  *Press y for yes, n for no, a as in always, e as in edit, v for view.*
  (`src/util/jc_msg.c`), which matches neither.
- **The width to wrap to.** The documents choose 76 columns (INTERFACE_TUTORIAL section 5.2;
  ACCESSIBILITY, pinned by a check), a 78-column budget and 80 columns (accessibility-by-default
  proposal).
- **What is still to do.** The accessibility-by-default proposal lists as remaining items that
  have shipped since: the prompt line (M562), the wisdom line hidden in accessible mode (M574)
  and the ghost suggestion announced there (M578). It also promises part of an item it records as decided *no* (its section 3,
  item f).
- **Smaller drift**: an example sentence with digit grouping that M555 removed in accessible
  mode; ACCESSIBILITY's "input echo" line for a flag M558 deleted; and the Japanese kana question,
  rejected on one page and open on the other.

Resolve one, and remove its line here and in DEFERRED in the same change.
