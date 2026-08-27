---
title: Change one line and nothing else
audience: student
phase: implementation
difficulty: plain
points: 2
verify: "grep -qx 'The speed is 80 steps.' docs/assignments/p3-change-one-line/notes.txt && grep -qx 'Line one must not change.' docs/assignments/p3-change-one-line/notes.txt && grep -qx 'Line three must not change.' docs/assignments/p3-change-one-line/notes.txt && [ \"$(wc -l < docs/assignments/p3-change-one-line/notes.txt)\" = 3 ]"
hints:
  - Only the middle line changes. The other two lines must stay exactly as they are.
  - Name the old text and the new text in your request. Then read the preview and check that only one line is marked as changed.
  - "Say: in docs/assignments/p3-change-one-line/notes.txt, change the line 'The speed is 50 steps.' to 'The speed is 80 steps.' Change nothing else."
---

> **Before you start.** Open a **terminal** in your project folder — the folder
> that holds `docs/`. Type `jichi` and press Enter: that is the agent, waiting for
> you. Never done this? [`PLAIN_LANGUAGE.md`](../PLAIN_LANGUAGE.md) starts from the
> beginning, with `jichi setup`. jichi always shows you a change and asks before it
> writes.
>
> The `jichi grade …` command below is different: you type that in the **terminal**,
> not inside jichi. Leave jichi with `/exit` first, or use a second terminal.
## What you will learn

A small change should stay small.

An agent that is asked to fix one thing will sometimes tidy other things at the
same time. That is how a one-line fix turns into a change nobody reviewed. This
task is graded on **what you did not change**.

## The task

There is a file with three lines:

```
docs/assignments/p3-change-one-line/notes.txt
```

The middle line says the speed is 50 steps. It should say **80** steps.

Ask the agent to change that one line.

When it shows you the preview, look at it before you answer. Check that only one
line is marked as changed.

## How to know you are done

```sh
# in a terminal, in your project folder (the one holding docs/) -- not inside jichi
jichi grade docs/assignments/p3-change-one-line.md
```

The check tests four things:

1. the middle line now says 80 steps,
2. the first line is unchanged,
3. the third line is unchanged,
4. the file still has exactly three lines.

Number 4 catches a common surprise: a file can gain a blank line at the end
without anybody meaning it.

## If it fails

Do not guess. Read the file and compare it to the four rules above. Then ask the
agent to fix only the thing that is wrong.

You can also undo everything and start again. Type this **inside jichi**, at
its prompt — it is not a terminal command:

```text
/undo
```
