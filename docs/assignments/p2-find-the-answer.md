---
title: Find the answer, then write it down
audience: student
phase: implementation
difficulty: plain
points: 1
verify: "grep -qx 'timeout = 30' docs/assignments/p2-find-the-answer/answer.txt && grep -q 'timeout = 30' docs/assignments/p2-find-the-answer/settings.txt"
hints:
  - Ask the agent to read settings.txt first. Do not ask it to change anything yet.
  - One line in settings.txt starts with the word timeout. Copy that whole line.
  - "Say: read docs/assignments/p2-find-the-answer/settings.txt, then write the timeout line into docs/assignments/p2-find-the-answer/answer.txt, replacing what is there."
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

Read first. Change second.

An agent can change a file without understanding it. You can too. This task
separates the two steps, so you feel the difference.

## The task

There is a file with settings in it:

```
docs/assignments/p2-find-the-answer/settings.txt
```

One line in it sets a **timeout**.

1. Ask the agent to **read** the file and tell you the timeout line.
2. Then ask it to write that line into this file:

```
docs/assignments/p2-find-the-answer/answer.txt
```

The answer file must hold **only** that one line when you are finished.

Copy the line exactly as it appears. Do not tidy it up.

## How to know you are done

```sh
# in a terminal, in your project folder (the one holding docs/) -- not inside jichi
jichi grade docs/assignments/p2-find-the-answer.md
```

The check looks for two things:

- the answer file holds the timeout line, and
- **the timeout line is still in settings.txt.**

The second check is there on purpose. You were asked to read that file, not to
edit it. A task that only checked your answer would let you get it right for the
wrong reason.

Being exact about the second check: it looks for the line, not for the whole file
being untouched. So it catches the mistake people actually make — editing the file
you were asked to read — and would not notice a line added somewhere else in it.
A check that says what it does is worth more than one you have to trust.
