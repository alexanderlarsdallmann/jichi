# jichi in plain language

> **Also in German:** [Einfache Sprache](i18n/de/EINFACHE_SPRACHE.md) — the original
> of this page. *Einfache Sprache* is a defined German register with its own rules;
> this English page is its sibling, not its source.
>
> This page does **not** replace the others. It is a separate page. The other pages
> are dense. That is right for their readers. This page is plain. That is right for
> its readers.

## What is jichi?

jichi is a command-line program.

It helps you write software.

You type a task. jichi reads your files. jichi suggests changes. You say yes or no.

jichi works in the folder where you start it.

## What you need

You need three things:

1. A computer running Linux.
2. A language model. This is a service, on the internet or on your own machine.
3. A key for that service. The key works like a password.

## Getting jichi

jichi is one program file. You build it once from the source code:

```
make
```

This makes a file called `jichi` in the folder. You can copy it somewhere on your
`PATH` — that is the list of folders where your computer looks for programs. Or you
can run it as `./jichi` from that folder.

If `make` does not work, you are missing a build tool. See
[`INSTALL.md`](INSTALL.md).

## Starting for the first time

**Where you type all of this:** in a **terminal**, in the folder of the project
you want to work on. Every command on this page is typed there. The one exception
is a command starting with a slash (`/exit`, `/undo`) — those are typed *inside*
jichi, once it is running, and this page says so each time.

Type this:

```
jichi setup
```

jichi asks questions. You answer them. Then jichi writes a settings file for you.
You do not need to open that file.

Now check that everything works:

```
jichi doctor
```

`doctor` prints a list. A tick means that part is fine. A cross means something is
missing. Next to the cross, jichi says what is missing.

**When every line has a tick, jichi is ready.**

## Giving jichi a task

Start it:

```
jichi
```

Now you can type. For example:

```
Explain the files in this folder to me
```

(If you name a file, use one that exists. `src/main.c` only works in a project
that has that file.)

jichi answers.

## Three ways of working

jichi has three ways of working. They are called modes.

| Mode | What happens |
|---|---|
| **chat** | jichi asks before it changes a file. This is the normal way. |
| **plan** | jichi changes nothing. It only makes a plan. |
| **auto** | jichi works on its own. It does not ask. |

You switch like this:

```
/chat
/plan
/auto
```

**Take care with auto.** In this mode jichi changes files without asking. Use `auto`
only when you really want that.

## Useful commands

These commands start with a slash.

| Command | What it does |
|---|---|
| `/help` | shows all commands |
| `/status` | shows which model and mode are active |
| `/undo` | takes back the last changes |
| `/cost` | shows what this session has cost |
| `/exit` | stops jichi |

## When something goes wrong

jichi makes mistakes. That is normal.

You can take changes back:

```
/undo
```

jichi saves a copy before each change. `/undo` brings that copy back.

If jichi stops answering, press **Ctrl + C**. That stops the work.

## Having jichi read aloud

jichi can speak. This helps if you cannot read the screen.

```
/voice on
```

Then jichi reads its answers aloud. It also reads out its permission questions —
because if jichi is waiting and says nothing, you cannot tell why it is quiet.

You need two settings for this. If one is missing, jichi says so. It then stays
silent — but it tells you why first.

More: [VOICE.md](VOICE.md).

## Your data

- jichi sends your questions and parts of your files to the language model. This is
  needed for the model to answer.
- jichi saves your conversations on your own computer, in `~/.jichi.d/`.
- jichi also saves numbers about each run there: how long a call took, which
  tools were used. Not your words and not your code. jichi learns from these
  numbers. You can turn this off with `--log-level off`.
- jichi does not send data anywhere else.

## Take care

jichi can run commands on your computer. That is useful and it is dangerous.

So:

- Use **git** in your folder. git is a program that records every version of your
  files. With git you can see each change jichi makes, and undo it.
- Read what jichi suggests before you say yes.
- Be especially careful in `auto` mode.

Text from the internet is only data to jichi. jichi should not follow instructions
hidden in it. jichi marks such text. That helps. It is not a guarantee.

## Practice with marking

There are three small exercises written in this register. Each one has a check. The
check prints three lines, and the first one ends in **PASS** or **FAIL**. It does
not guess.

| Exercise | Points | What you learn |
|---|---|---|
| [p1 — ask for a file](assignments/p1-ask-for-a-file.md) | 1 | one whole turn: ask, read the preview, say yes, check |
| [p2 — find the answer](assignments/p2-find-the-answer.md) | 1 | read first, change second |
| [p3 — change one line](assignments/p3-change-one-line.md) | 2 | a small change stays small |

This is how you start one:

```
jichi assign docs/assignments/p1-ask-for-a-file.md
```

This is how you check your work:

```
jichi grade docs/assignments/p1-ask-for-a-file.md
```

They are marked **exactly as strictly** as every other exercise. An easier mark would
be kind and would teach you nothing.

German editions of these three are in
[`docs/i18n/de/assignments/`](i18n/de/assignments/).

## Using jichi in a web browser (JupyterHub)

Some schools and universities run a service called **JupyterHub**.

It gives you a computer you use through your web browser. You do not install
anything. You log in, and it is there.

If your school does this, jichi may already be installed for you.

### How to start

1. Log in to JupyterHub in your browser.
2. Look for a tile called **Terminal**. Click it.
3. A black window opens. This is a terminal. It works like the terminal on your
   own computer.
4. Type this and press Enter:

```
jichi
```

That is all. jichi now works the same way as everywhere else.

### If jichi is not there

Type `jichi` and the terminal may answer `command not found`.

That is normal. jichi is not a program you download ready-made. Somebody has to
**build it from the source code** and put it on the computer. In a JupyterHub
that somebody is the administrator.

So: ask your administrator. Do not try to work around it.

If you want to try building it yourself in your own folder, type this first:

```
cc --version
```

If that prints a version, you have the tool needed to build programs, and you
can ask your administrator or teacher for the next steps. If it says `command
not found`, you cannot build it yourself, and that is not your fault.

### Check where you are

The terminal may not start in the folder you expect.

Type this first:

```
pwd
```

It prints the folder you are in. If it is the wrong one, use `cd` to change it.

### Your files are yours

Other people on the same JupyterHub cannot see your files.

They cannot see your key. They cannot see your chats with jichi. Every person
has their own home folder.

### Two keys work differently in a browser

Some keys belong to the browser, not to jichi.

- **Ctrl-R** reloads the web page. In jichi it normally searches your history.
- **Ctrl-G** does something else in Firefox.

If a key does something strange, that is why. Use the arrow keys instead.

### If jichi says it has no key

You may have put your key in a file called `~/.bashrc`. That is the normal
advice, and in a JupyterHub terminal it may not work.

The reason: the terminal in your browser starts your shell in a different mode
than a normal terminal does. In that mode it reads a file called `~/.profile`
and **not** `~/.bashrc`.

**What to do:** put the same line in `~/.profile` instead:

```
[ -f "$HOME/.jichi.env" ] && . "$HOME/.jichi.env"
```

Then close the terminal and open a new one.

If that does not help, ask your administrator. They can set the key for
everybody, and then you do not have to do anything.

### jichi cannot read your notebooks well

A Jupyter notebook is a file ending in `.ipynb`.

To jichi, that file is not code. It is a long text full of technical details.
A notebook with one picture in it can be **250 times** more work for jichi than
the same code in a normal `.py` file. jichi also cannot see your results and
pictures.

**What to do:** keep your code in normal `.py` files, and let the notebook use
them. Ask your teacher about `jupytext` if you want the notebook and the `.py`
file to stay the same automatically.

### If something goes wrong

- **Ctrl-C** stops what jichi is doing.
- `/undo` puts your files back the way they were.
- `/exit` closes jichi.

## Where to read more

- [`TUTORIAL_BEGINNER.md`](TUTORIAL_BEGINNER.md) — a longer tutorial.
- [`AGENT_MODES.md`](AGENT_MODES.md) — the modes, explained exactly.
- [`ACCESSIBILITY.md`](ACCESSIBILITY.md) — screen readers and reduced motion.
- [`CONFIG_TUTORIAL.md`](CONFIG_TUTORIAL.md) — every setting.
- [`JUPYTERHUB.md`](JUPYTERHUB.md) — the dense page about JupyterHub, for
  administrators and developers. It has the measurements behind the section
  above.

## About this page

This page follows plain-language rules:

- short sentences,
- one idea per sentence,
- no metaphors or comparisons,
- no nested clauses,
- concrete words,
- jargon explained.

It is a **separate page**, not a shortened version of another one. The dense pages
stay dense. Both kinds of writing have their readers.
