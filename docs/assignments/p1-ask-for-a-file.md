---
title: Ask for one file
audience: student
phase: implementation
difficulty: plain
points: 1
verify: "grep -qx 'I asked and it wrote this line' docs/assignments/p1-ask-for-a-file/note.txt"
hints:
  - You do not write the file. You ask the agent to write it. Then you read what it wants to do, and you say yes.
  - Say the exact path and the exact line. If you are vague, the agent guesses.
  - "Say: create the file docs/assignments/p1-ask-for-a-file/note.txt with this one line: I asked and it wrote this line"
---

> **Before you start.** Open a **terminal** in your project folder — the folder
> that holds `docs/`. Type `jichi` and press Enter: that is the agent, waiting for
> you. Never done this? [`PLAIN_LANGUAGE.md`](../PLAIN_LANGUAGE.md) starts from the
> beginning, with `jichi setup`. jichi always shows you a change and asks before it
> writes.
>
> The `jichi grade …` command below is different: you type that in the **terminal**,
> not inside jichi. Leave jichi with `/exit` first, or use a second terminal.
This is your first task. It is small on purpose.

## What you will learn

You will do one full turn of work with the agent:

1. You ask.
2. The agent shows you what it wants to change.
3. You read it.
4. You say yes.
5. A command checks the result.

Step 3 is the important one. It is the habit this whole course is about.

## The task

Ask the agent to make a new file. The file is:

```
docs/assignments/p1-ask-for-a-file/note.txt
```

The file must hold this one line, and nothing else:

```
I asked and it wrote this line
```

**Do not write the file yourself.** Ask the agent to do it.

## How to know you are done

Run this:

```sh
# in a terminal, in your project folder (the one holding docs/) -- not inside jichi
jichi grade docs/assignments/p1-ask-for-a-file.md
```

It prints three lines. This is what a pass looks like:

```text
Ask for one file: PASS
  verify: grep -qx 'I asked and it wrote this line' docs/assignments/p1-ask-for-a-file/note.txt (exit 0)
  score: 100%
```

The word on the **first** line is the answer: `PASS` or `FAIL`. The second line
shows the command that checked, and the third is your score. On a fail you get the
same three lines with `FAIL`, `(exit 2)` and `score: 0%`.

- **PASS** means the file is correct. You are done.
- **FAIL** means something is different. Read the file. Compare it to the line
  above, letter by letter. One extra space is enough to fail. You do not need to
  understand the `grep` on the second line — it is the check, shown so you can see
  there is no magic.

## One warning

The agent will show you a preview before it writes. Read it. It is easy to press
yes without looking. That habit will cost you later, on a task where the agent is
wrong.
