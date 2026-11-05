# The terminal workbench: jichi with an editor, a browser, a PDF reader and a file manager

*This page is for three readers who work without a commercial IDE:*
- *a **self-learner** who is teaching themselves programming on a Linux machine;*
- *a **junior developer** who wants to use jichi on real projects;*
- *a **junior administrator** who looks after Linux machines and wants a careful helper.*

*It shows how to arrange the tools a Linux desktop already has around jichi: a terminal, an
editor (nano, Vim or Emacs), a web browser, a PDF reader, a file manager such as ranger or
Thunar, several windows, several desktops and a second monitor. It also shows how to pass text
between them, and which use cases need which arrangement.*

*After reading it you can:*
- *arrange windows and desktops for a task, and move between them with the keyboard;*
- *get text from an editor, a web page, a PDF, a log or a man page into jichi, and back;*
- *follow one change from your request to a commit, and take it back if it was wrong;*
- *pick a jichi pack and a layout for your use case (§10);*
- *check what the model tells you against its source (§11).*

*What you need first: jichi installed and configured ([INSTALL.md](INSTALL.md),
[SETUP_WIZARD.md](SETUP_WIZARD.md)), with `jichi doctor` passing ([DOCTOR.md](DOCTOR.md)); a Linux
desktop; and the first session of [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md). No IDE is
needed, and none is used on this page.*

*Where it fits: [WORKFLOWS.md](WORKFLOWS.md) routes you by role, and this page is about your desk.
[EDITORS.md](EDITORS.md), [EMACS.md](EMACS.md) and [VIM.md](VIM.md) are the editor integrations.
[TMUX.md](TMUX.md) and [SCREEN.md](SCREEN.md) keep a session alive across machines.
[REFERENCES.md](REFERENCES.md) lists every `@` reference. [TOOL_DECISIONS.md](TOOL_DECISIONS.md)
says what jichi may do to your machine, and who decides.*

*How it was checked, on 2026-09-29: jichi built from `master` at M839 (`build: e0d8a840`), on
**Xubuntu 24.04** with Xfce 4.18. That machine was installed from Xubuntu 22.04.2 media and
upgraded since. What "out of the box" means below comes from its installer's own package list.
GNOME's defaults were read on a second machine running GNOME 50.1 on Ubuntu 26.04. Eight
questions were put to two free, locally hosted models through the university's gateway:
`jlu/qwen3-coder-next`, and `jlu/qwen3.8-27b`, a stronger reasoning model. Their answers are
compared in §11, mistakes included. What was not run is marked "manual" (from the tool's own
manual) or "not run".*

---

## 0. The answer first

1. **The file system is the integration.** jichi reads and writes files in one directory. Your
   editor, file manager and git see the same files. No plugin is needed for them to work
   together.
2. **Start jichi in the project's directory** (`cd ~/project && jichi`). Its repository map and
   its session list are tied to that directory. Its file tools are not: they can read any file you
   can, unless you turn on the path fence (`--path-fence`, or `"pathFence": true`).
3. **Give each kind of work its own desktop:** one for jichi and the editor, one for reading
   (browser and PDF), and one for the system (§3).
4. **Hand jichi text, instead of asking it to fetch:** `git diff | jichi --readonly -p "…"`,
   `man -P cat tree | jichi …`, `@notes.pdf`, `@url:…` (§6, §7).
5. **Use `--readonly` for questions.** With it, jichi cannot run the shell or change a file (§12).
6. **In a session, read every request before you press `y`.** `/fences` shows what bounds the
   next turn, and `/diff` shows what changed (§4).
7. **Install git before you rely on `/undo`.** Checkpoints need it, and a fresh Xubuntu install
   did not have it (§2).
8. **Check the answer against its source:** quote, page, timestamp. In §11 both models found a
   real bug, and neither stated the reason cleanly.
9. **Choose the model by the job.** The fast coding model wants small, exact tasks and your eyes
   on every request. The reasoning model is slower, and does not hand work to a faster model by
   itself (§11.1).
10. **What you pipe in leaves your machine.** It goes to the model's server, and into the saved
    session (§13).

---

## 1. The idea: jichi is one window among several

jichi is a program in a terminal. It reads files, proposes changes and makes them, and runs
commands when you allow it. It does not own your project. Everything it does lands in ordinary
files, which every other tool on your desktop can read:

```text
browser, PDF viewer
        |
        |  a URL, a quote
        v
you --> jichi, in a terminal -- reads, writes --> files --> git
 |                                                  ^
 +----> editor ------------- reads, writes ---------+
```

In words: you talk to jichi in a terminal and edit in an editor. Both read and write the same
project files, and git records them. From the browser and the PDF viewer, you bring jichi a URL
or a quote.

That has three consequences:
- **Any editor works**, including one jichi has never heard of. It only has to notice that a
  file changed on disk (§5).
- **Your tools are the check on jichi.** The editor shows the file, `git diff` shows the change,
  and `make test` shows whether it works. None of them depends on what jichi says about itself.
- **The directory is where it starts, not a boundary.** Start jichi where the project is. Its
  file tools can still read any file you can read; only the path fence (`--path-fence`, or
  `"pathFence": true`) bounds them to the directory, and by default it is on only under `--auto`
  and for a delegate that runs without asking (§12).

One change, from request to commit, runs through all of them:

```text
You            jichi              Files           Editor
 |               |                  |               |
 |-- describe -->|                  |               |
 |               |-- read what ---->|               |
 |               |   it needs       |               |
 |<-- allow? ----|                  |               |
 |----- y ------>|                  |               |
 |               |-- checkpoint, -->|               |
 |               |   then write     |               |
 |               |                  |<-- reload ----|
 |--- /diff ---->|                  |               |
 |- make test ---+----------------->|               |
 |- git commit --+----------------->|               |
 |  or /undo     |                  |               |
```

In words: you describe the change, and jichi reads the files it needs. Before it edits, it
shows you the edit and asks. You allow it, and jichi takes a checkpoint, then writes. Your editor
reloads the file. You read the change with `/diff`, run the tests, and then either commit or
take the change back with `/undo`.

Drawn by Mermaid, where your viewer supports it:

```mermaid
sequenceDiagram
    participant Y as You
    participant J as jichi
    participant F as Files
    participant E as Editor
    Y->>J: describe the change
    J->>F: read what it needs
    J->>Y: an edit: allow? y / n / e / v
    Y->>J: y
    J->>F: checkpoint, then write
    E->>F: reload the file
    Y->>J: /diff
    Y->>F: make test
    Y->>F: git commit, or /undo
```

---

## 2. The tools, and what a fresh system gives you

A Linux desktop comes with more than it seems to. This table lists what this page uses and
whether it was there after a fresh install. The "fresh" column is the package list that
Xubuntu 22.04.2's installer recorded (`/var/log/installer/initial-status.gz`) on the machine this
page was written on. Other distributions and desktops ship other sets.

| Job | Program on this page | Fresh Xubuntu? | Package |
|---|---|---|---|
| terminal | `xfce4-terminal`, `xterm` | yes | `xfce4-terminal`, `xterm` |
| simple editor | `nano` | yes | `nano` |
| vi | `vi` (a small Vim) | yes | `vim-tiny` |
| full Vim | `vim` | no | `vim` |
| Emacs | `emacs` | no | `emacs` |
| graphical file manager | Thunar | yes | `thunar` |
| terminal file manager | `ranger` | no | `ranger` |
| PDF viewer | Atril | yes | `atril` |
| PDF to text | `pdftotext`, `pdfinfo` | yes | `poppler-utils` |
| web browser | Firefox | a snap; see below | `firefox` (snap) |
| text web browser | `lynx`, `w3m` | no | `lynx`, `w3m` |
| pager, manuals | `less`, `man`, `info` | yes | `less`, `man-db`, `info` |
| directory tree | `tree` | yes | `tree` |
| open a file in its program | `xdg-open` | yes | `xdg-utils` |
| desktop notification | `notify-send` | yes | `libnotify-bin` |
| version control | `git` | **no** | `git` |
| build tools | `make`, `gcc` | **no** | `build-essential` |
| session multiplexers | `tmux`, `screen` | no | `tmux`, `screen` |
| JSON on the command line | `jq` | no | `jq` |
| process viewer | `htop` | no | `htop` |

Firefox is a snap on Ubuntu, and the installer's package list does not record snaps. It was
installed on this machine as one, and that row is the only one not read from the list. The
"minimal installation" option leaves Atril out as well.

**`git` matters most.** jichi's checkpoints, and so `/undo` and `/rewind`, need `git` on `PATH`
([SNAPSHOTS.md](SNAPSHOTS.md)). Without it they switch themselves off, and nothing warns you at
the moment of an edit. `jichi doctor` reports it, and `/status` says `snapshots: on` or `off`.

To see what your machine has, ask the shell:

```sh
# anywhere
for p in nano vim emacs ranger atril pdftotext lynx w3m git tmux screen jq htop; do
  command -v "$p"
done
```

`command -v` prints the path of each program it finds, and nothing for the ones that are
missing. To install the rest on Debian or Ubuntu (not run for this page; `sudo` asks for your
password):

```sh
# anywhere
sudo apt install git build-essential vim emacs ranger lynx w3m tmux screen jq htop
```

---

## 3. Layouts: windows, desktops, and a second monitor

### 3.1 One terminal, split

The smallest workbench is one terminal window split in two: jichi on one side, a shell or an
editor on the other. tmux and GNU screen do this inside any terminal, over ssh too, and keep it
running when the connection drops ([TMUX.md](TMUX.md), [SCREEN.md](SCREEN.md)). Terminal tabs are
the other simple option. `xfce4-terminal` opens a new tab with `Ctrl-Shift-T` (from the
program's own key list).

### 3.2 Two windows side by side

Put jichi's terminal on one half of the screen and the editor on the other. The desktops ship
keys for this:

| Action | Xfce 4.18 (shipped default) | GNOME 50.1 on Ubuntu 26.04 (default) |
|---|---|---|
| tile a window to the left half | `Super` and keypad `←` (the 4 key) | `Super-Left` |
| tile a window to the right half | `Super` and keypad `→` (the 6 key) | `Super-Right` |
| maximise or restore | `Alt-F10` | `Alt-F10` |
| switch windows (GNOME: applications) | `Alt-Tab` | `Alt-Tab`, `Super-Tab` |

Xfce's tiling keys are on the **numeric keypad**, so a laptop without one has no default key for
them. You can drag a window to a screen edge instead, or set your own key.

### 3.3 One desktop per kind of work

A **workspace** (a virtual desktop) is a whole screen of windows that you switch to at once. Give
each kind of work its own:

```text
+----------------+   +--------------+   +----------------+
| 1: work        |   | 2: reading   |   | 3: system      |
|                |   |              |   |                |
| terminal:jichi |<--| browser      |   | file manager   |
| editor         |<--| PDF viewer   |   | htop, logs     |
+----------------+   +--------------+   +----------------+
        ^                                       |
        +---------------- a path ---------------+
```

In words: workspace 1 holds jichi's terminal and the editor, and is where you work. Workspace 2
holds the browser and the PDF viewer, for reading. Workspace 3 holds the file manager and the
system tools. From the reading and system desktops you carry a URL, a quote, a page range or a
path back to jichi.

Drawn by Mermaid, where your viewer supports it:

```mermaid
flowchart LR
    subgraph W1["Workspace 1: work"]
        T["terminal: jichi"]
        E["editor"]
    end
    subgraph W2["Workspace 2: reading"]
        B["browser"]
        P["PDF viewer"]
    end
    subgraph W3["Workspace 3: system"]
        FM["file manager"]
        H["htop, logs"]
    end
    B -. "a URL, a quote" .-> T
    P -. "a page range" .-> T
    FM -. "a path" .-> T
```

The reason is attention, not tidiness. A change you are about to approve deserves a screen
without a web page on it, and a manual deserves a screen without a streaming reply beside it.

| Action | Xfce 4.18 (shipped default) | GNOME 50.1 on Ubuntu 26.04 (default) |
|---|---|---|
| go to workspace 1, 2, 3 … | `Ctrl-F1`, `Ctrl-F2`, `Ctrl-F3` … | `Super-Home` goes to the first |
| previous / next workspace | `Ctrl-Alt-Left` / `Ctrl-Alt-Right` | `Super-PageUp` / `Super-PageDown` |
| move the window to the previous / next | `Ctrl-Alt-Home` / `Ctrl-Alt-End` | `Super-Shift-PageUp` / `-PageDown` |
| show the desktop | `Ctrl-Alt-D` | not checked |

GNOME 50.1 also switches workspaces with `Ctrl-Alt-Left` and `Ctrl-Alt-Right`, so that pair works
on both desktops.

**These are the defaults, and yours may differ.** Anyone can change them, including a
distribution. The Xfce column is from `/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/`. The GNOME
column was read with `GSETTINGS_BACKEND=memory`, which returns the defaults without your own
changes. To see what **your** desktop does:

```sh
# on Xfce
xfconf-query -c xfce4-keyboard-shortcuts -p '/xfwm4/custom/<Primary>F2'
# on GNOME
gsettings get org.gnome.desktop.wm.keybindings switch-to-workspace-right
```

On Xfce the first command printed `workspace_2_key`. On GNOME the second printed
`['<Super>Page_Down', '<Super>KP_Next', '<Super><Alt>Right', '<Control><Alt>Right']`. Xfce's
settings dialog (Settings → Window Manager → Keyboard) shows the same list.

### 3.4 A second monitor

With two monitors, keep the work where you look straight ahead: jichi and the editor on the main
screen, and the manual, the browser or the log on the other. On GNOME, `Super-Shift-Left` and
`Super-Shift-Right` move a window to the monitor on that side. In a GNOME session, only the
primary monitor switches workspaces by default: GNOME Shell overrides
`workspaces-only-on-primary` to `true` (the schema default, read with `GSETTINGS_BACKEND=memory`,
is `false`). So a manual on the second screen stays where it is when you switch, and setting it
`true` changes nothing. To make the second monitor switch too, set it to `false` (not run for
this page, because it changes the setting):

```sh
# on GNOME
gsettings set org.gnome.mutter workspaces-only-on-primary false
```

Xfce's behaviour with two monitors was not checked for this page.

### 3.5 A tiling window manager, if you want one

The machine this page was written on also offers i3 and xmonad at the login screen. A tiling
window manager arranges every window side by side without overlap, driven entirely from the
keyboard. It suits this way of working well, and it is a project in itself. Learn the desktop you
have first. Neither was used for this page.

---

## 4. jichi in the terminal

### 4.1 Starting, and what the prompt tells you

```sh
# in the project directory
cd ~/project
jichi
```

jichi answers `jichi - interactive agent. Type /exit to quit, /help for commands.` and then shows
its prompt:

```text
[chat·qwen·7%] ›
```

That is the mode (`chat`), the model's name from your config (`qwen`), and roughly how much of
the model's context window is already in use. `/context` gives the breakdown. You type after
`›` and send with Enter.

### 4.2 The commands you will use every day

All of these are in `/help` (grouped there by topic). `/help` needs no model call, so it is safe
to try.

| You type | It does |
|---|---|
| `/status` | model, mode, whether snapshots are on, the working directory |
| `/fences` | what bounds the next turn: mode, read-only, scopes, and the shell |
| `/diff` | what changed since the last checkpoint |
| `/undo` | put the files back as they were at the last checkpoint |
| `/rewind [n]` | put back files **and** conversation to an earlier turn (`--dry-run` first) |
| `/plan` | investigate and propose, change nothing (`/plan off` to leave) |
| `/chat` | back to the normal mode, where every change asks first |
| `/context` | where the context window went |
| `/compact` | summarise older history to make room |
| `/export [--html]` | write the conversation to a file (HTML with `--html`) |
| `/sessions`, `/resume` | list and reopen earlier conversations |
| `/exit` | leave |

This is what `/status` and `/fences` showed in a fresh session on the test machine, with the
directory's name shortened:

```text
[chat·qwen·7%] › /status
model:       qwen (jlu/qwen3-coder-next)
mode:        chat
snapshots:   on
self-review: off
path-fence:  off
vision:      no
tokens:      0 in / 0 out
cwd:         /home/you/project
[chat·qwen·7%] › /fences
what bounds the next turn:
  mode:           chat, tools that change things ask first
  read-only:      no
  path fence:     off here; on for a delegate that runs without asking
  edit scope:     none
  constraints:    none
  privileged:     sudo and doas are asked every time
  physical:       kinetic tools are asked every time
  approvals:      none remembered
  budgets:        none in this session
  the shell:      NOT fenced. run_terminal_command runs as you, past the
                  path fence and the edit scope, without your API keys
```

The last line is the one to remember. When you allow a shell command, it runs as you, with your
permissions, and it can reach any file you can reach. That is why the approval prompt matters
(§12).

### 4.3 Keys

| Keys | What they do |
|---|---|
| `Tab` | complete a command, a model name, a session id, or an `@path` |
| `Ctrl-R` | search what you typed before |
| `\` then Enter | continue the message on a new line |
| `Esc` or `Ctrl-C` | while jichi works: stop the turn |
| `Ctrl-D` | on an empty line: leave |
| `Ctrl-A` / `Ctrl-E` | start / end of the line |
| `Ctrl-G` | suggest how to finish the line (asks the model) |

When jichi asks permission, `y` allows, `n` refuses, `a` allows this whole tool for the rest of
the session, `e` lets you edit the call first, and `v` shows the whole call. Any other key asks
again. Three refusals in a row stop the run. Because `a` remembers the tool by its name, allowing
one shell command with `a` lets every later shell command run without asking (`sudo`, `doas`,
`pkexec`, `su` and `run0` still ask each time).

**Two of these keys collide with the session tools.** GNU screen uses `Ctrl-A` as its prefix,
so inside screen you type `Ctrl-A a` to reach jichi's start of line ([SCREEN.md](SCREEN.md) §7).
tmux uses `Ctrl-B`, which is jichi's "one character left". Inside tmux, use the arrow key
instead, or type `Ctrl-B Ctrl-B` to send it through ([TMUX.md](TMUX.md) § Gotchas).

### 4.4 Copy and paste between windows

- In `xfce4-terminal`, **`Ctrl-Shift-C`** copies and **`Ctrl-Shift-V`** pastes (from the
  program's own key list). `Ctrl-C` without Shift goes to the program, which for jichi means
  "stop".
- Selecting text with the mouse and clicking the **middle button** pastes it elsewhere, without a
  copy step. That is the X selection, and it is separate from the clipboard (manual).
- Pasting several lines into jichi is safe. It treats a paste as one message and does not run
  it line by line ([TMUX.md](TMUX.md) § Mouse mode).

### 4.5 One question, without a session

For a question with an answer and nothing to change, the headless form is simpler than a
session:

```sh
# in the project directory
jichi --readonly -p "What does the Makefile's check-target target run?" < /dev/null
```

Run in jichi's own checkout, `jlu/qwen3-coder-next` answered with the Makefile's line,
`check-target: all test smoke`, and explained the three parts, in five seconds.
`jlu/qwen3.8-27b` took 15 seconds and three tool calls, and also explained why the target
includes `all`, which it had read in the Makefile's comment. `-p` prints the answer and exits,
and `--readonly` keeps it from changing anything. On stderr, jichi then says what it checked and
what it did not: `checked: 1 tool call, 0 errors`, and `not checked: no envelope armed`, meaning
that nothing tested the answer. `-q` leaves those lines out. `< /dev/null` matters when jichi's
stdin is not a terminal -- in a script, a cron job or a pipeline -- because a headless run then
waits for input that never comes; typed at a terminal it does not wait, and `--no-stdin` does the
same as the redirect. Piped text
becomes context for the question: `command | jichi --readonly -p "question"`
([SCRIPTING.md](SCRIPTING.md)). `-c` continues the most recent session for this directory, so a
follow-up question keeps the context.

---

## 5. jichi and your editor

### 5.1 The file changes on disk, and your editor has to notice

When jichi edits a file you also have open, the editor's copy is out of date. Each editor handles
that differently, and this table is from their manuals:

| Editor | It warns you | You reload with | Automatic reload |
|---|---|---|---|
| nano | when you save: `File was modified since you opened it; continue saving?` | close and reopen | no |
| Vim | after a shell command (`:!…`), on `:checktime`; the GUI also on focus | `:e` (`:e!` drops yours) | `:set autoread`, at the same moments; off by default |
| Emacs | when you edit the buffer | `M-x revert-buffer` | `M-x global-auto-revert-mode` |

**Save before you ask jichi to edit a file you have open.** Otherwise two versions exist, yours
in the editor and jichi's on disk, and saving one throws the other away. If you answer yes to
nano's question, nano's copy wins and jichi's change is gone.

### 5.2 The integrations

jichi ships small integrations for three terminal editors. Each one only runs jichi's headless
form for you. You never need them, but they save typing:

- **Emacs**: `editors/emacs/jichi.el` streams answers into a buffer, and can run a task on the
  project ([EMACS.md](EMACS.md)).
- **Vim and Neovim**: `editors/vim/jichi.vim` provides `:JichiAsk`, `:JichiRegion` for a
  selection, and `:JichiTask` ([VIM.md](VIM.md)).
- **nano**: nano has no plugins, so `editors/nano/jichi-nano` is a shell wrapper with `ask`,
  `explain` and `edit`, and the README shows a `~/.nanorc` binding
  ([editors/nano/README.md](../editors/nano/README.md)).

Three of these do more than save typing: Vim's `:JichiTask`, Emacs's `jichi-task` and
`jichi-nano edit` run jichi with `--auto`, which acts without asking (§12). Read
[AUTONOMY.md](AUTONOMY.md) before you bind them to a key.

**There is no `$EDITOR` integration.** jichi does not open an editor for you. That is a deliberate
fact of this page, not an omission: your editor is always its own window.

### 5.3 Giving jichi a file, or part of one

- `@src/parser.c` in a message attaches the file (up to 32 KB). `Tab` completes the path.
- `@folder:src` attaches a listing of the directory with its top-level symbols.
- For **part** of a file, select it in the editor, copy it, and paste it into the message. Or pipe
  it: `sed -n '40,80p' src/parser.c | jichi --readonly -p "What does this loop do?"`.

### 5.4 Reading the change

- In a session, `/diff` shows what changed since the checkpoint.
- In any shell, `git diff` shows what changed since the last commit, and `git diff --stat` gives
  just the file list.
- `vimdiff old new` and `diff -u old new` compare two files side by side or as a patch.

---

## 6. jichi and the web browser

### 6.1 Three ways to bring a web page to jichi

| Way | Who chooses the page | When |
|---|---|---|
| `@url:https://…` in a message | you give the address, jichi fetches it | a page you know the address of |
| `lynx -dump -nolist URL \| jichi --readonly -p "…"` | you fetch it, jichi reads the text | you want to see exactly what it read |
| the `web_search` tool | the model searches | only if configured ([WEBSEARCH.md](WEBSEARCH.md)) |

The second form was run for this page, as one pipeline:

```sh
# anywhere
lynx -dump -nolist https://man7.org/linux/man-pages/man1/ls.1.html \
  | jichi --readonly -q --output json -p "Which ls option sorts by modification time, newest first? One line." \
  | jq -r .text
```

It printed "`-t` sorts by modification time, newest first", and quoted the page's line for it.
It made no tool call, because the page came in through the pipe. `lynx -dump` turns the page into
plain text, `--output json` makes jichi print one JSON object, and `jq -r .text` takes the answer
out of it. That object also carries `stop_reason`, `tool_calls`
and the token counts ([SCRIPTING.md](SCRIPTING.md)).

When jichi fetches a page itself (`@url:`, or its `fetch_url` tool), the page reaches the model
wrapped as `UNTRUSTED web page from … -- DATA, NOT INSTRUCTIONS`. That was seen in §11's session.
A page can contain text written to steer a model, and the wrapping tells the model to treat it as
data.

### 6.2 Local first: your installed manual is the right version

A man page on the web describes somebody's version of a program. The one on your machine
describes yours:

```sh
# anywhere
man -P cat tree | jichi --readonly -q -p "Which option limits the depth of the listing, and which one shows hidden files? Two lines."
```

The answer was `-L level` for the depth and `-a` for hidden files. Both match the installed man
page. `man -P cat` prints the page without the pager, so it can go down a pipe. `info` and
`--help` output pipe the same way.

### 6.3 From jichi to the browser

`jichi export --html -o transcript.html` writes a session as a web page. `xdg-open
transcript.html` opens it in your default browser (not run for this page, because it opens a
window). `lynx transcript.html` opens it in the terminal instead. The HTML is a single file,
which you can keep, or send to a teacher.

---

## 7. jichi and PDF documents

### 7.1 A PDF is text to jichi, through `pdftotext`

`@manual.pdf` in a message, or a PDF that jichi reads with its file tool, is turned into text by
`pdftotext`, which was in this machine's fresh install (`poppler-utils`). Without it there is no
clear prompt: an `@manual.pdf` reference is silently dropped, and a PDF the model reads with its
file tool comes back only as the tool error `reading PDFs needs 'pdftotext' (poppler-utils) on
PATH`, which goes to the model, not to you. `jichi doctor` warns when it is missing, so check
there before you rely on PDFs. The config key `pdfCommand` names another converter.

```sh
# in the project directory
jichi --readonly -q -p "According to @refs/shared-mime-info-spec.pdf, in which directories does the system look for MIME information, and in what order? Quote the specification's wording for the order." < /dev/null
```

The PDF in that command is the freedesktop.org MIME specification, which Ubuntu ships in
`/usr/share/doc/shared-mime-info/`. The two models' answers, and what was wrong with one of
them, are in §11.

### 7.2 A few pages, not the whole book

**An `@` reference is cut off at 32 KB.** The MIME specification is 19 pages, and its text is
37,099 bytes, so the model saw it only up to section 2.13. The stronger model said so in its
answer, and the other did not. A manual of several hundred pages is cut off much sooner, and most
of it is not about your question anyway. Find the pages in the PDF viewer first. In Atril,
`Ctrl-F` searches and the page number is in the toolbar. Then hand jichi only those pages:

```sh
# in the directory with the PDF
pdftotext -f 12 -l 14 manual.pdf - | jichi --readonly -p "Summarise these pages in five points."
```

`-f` and `-l` are the first and last page, and `-` writes the text to the pipe.

For a whole manual you return to often, jichi can index it as a documentation source and
retrieve only the relevant passages. That needs a model with the `embed` role
([DOCS.md](DOCS.md)).

### 7.3 What does not work

A **scanned** PDF is a picture of text. `pdftotext` finds no text in it, and jichi then has
nothing to read. Recognising the text first (OCR, for example with `tesseract`) is outside this
page. `pdffonts file.pdf` listing no fonts is a good sign that a PDF is scanned (manual).

---

## 8. jichi and the file manager

### 8.1 ranger, in the terminal

ranger shows three columns: the parent directory, the current one, and a preview of the selected
file. You move with the arrow keys, or with `h` `j` `k` `l` as in Vim. It is a good way to learn
a project's layout before you ask jichi about it. The keys worth knowing first, from the
`rc.conf` that ranger 1.9.3 ships and from `man ranger`:

| Key | It does |
|---|---|
| `h` `j` `k` `l` or arrows | left (up a directory), down, up, right (open) |
| `S` | open a shell in the current directory; `exit` returns to ranger |
| `zh` | show or hide hidden files |
| `/` | search for files in the current directory |
| `q` | quit |

`S` is the bridge to jichi. Browse to the project, press `S`, and run `jichi` in the shell ranger
opens. When you leave jichi and that shell, you are back where you were in ranger. ranger decides
which program opens a file from its `rifle.conf`.

### 8.2 Thunar, on the desktop

Thunar, Xfce's file manager, ships a right-click action **Open Terminal Here** (defined in
`/etc/xdg/Thunar/uca.xml`). It opens your terminal in that folder, which is exactly where jichi
should start.

### 8.3 Without a file manager

```sh
# in the project directory
tree -L 2          # the layout, two levels deep
jichi map          # the source files and their top-level symbols, no model call
```

`jichi map` printed `greet.c: main` for the one-file project used in §11. It is the same map
jichi gives the model, so it shows what the model knows about the layout.

---

## 9. Other tools that belong on the bench

| Tool | What it does | With jichi |
|---|---|---|
| `less` | page through long output; `/` searches | `jichi export \| less` to read a transcript |
| `man`, `info` | the installed manuals | pipe a page in (§6.2) |
| `grep`, `find` | find text and files | find the file, then `@` it |
| `diff`, `git diff` | show a change | review what jichi did (§5.4) |
| `make` | build and test | `jichi test` runs and parses the tests; `/verify` runs the verifier |
| `watch` | repeat a command every few seconds | `watch -n 5 git status` beside a session |
| `htop`, `journalctl` | processes, the system log | for §10's administrator cases |
| `notify-send` | a desktop notification | `--notify 'notify-send jichi done'` when a long run ends |
| `jq` | take a field out of JSON | `jichi --output json … \| jq -r .text` (§6.1) |
| `xdg-open` | open a file in its usual program | open an exported transcript (§6.3) |

`--notify` runs its command when a turn or an `--auto` run finishes, and `--bell` rings the
terminal bell instead ([NOTIFY.md](NOTIFY.md)). Neither was run for this page. A notification
appears on your desktop, which is the point, and also why a test had no business doing it.

---

## 10. Use cases, and the pack for each

`jichi init <pack>` writes a starting set of agent profiles, skills and commands into the
project's `.jichi/`, and an `AGENTS.md` at the project root (the `onboarding` pack writes no
`AGENTS.md`). `jichi init --list` shows all packs, and `--dry-run` shows what one
would write without writing it ([SCAFFOLDING.md](SCAFFOLDING.md)). Every case below names a
pack, a layout (§3) and the first commands.

```text
What do you need?
 |
 +-- an answer about text you have ---> pipe it in, read-only
 |
 +-- an answer about the project -----> /plan, or read-only
 |
 +-- a change ------------------------> chat mode
                                            |
              Can you afford to break the machine?
                 no  --> read every request; never auto
                 yes --> chat mode anyway, until section 12
```

In words: for an answer about text you already have, pipe it in with `--readonly`. For an answer
about the whole project, use a session in plan mode (`/plan`), or `--readonly`. For a change, use
a session in chat mode, where every change asks first. On a machine you cannot afford to break,
read every request and never use `--auto`. On one you can, stay in chat mode anyway until you
have read §12.

### 10.1 For the self-learner

| Case | Pack or command | Layout | Read |
|---|---|---|---|
| learn a language from its own documentation | `jichi init course`, then `/course` | work + reading | [LANGUAGE_COURSE.md](LANGUAGE_COURSE.md) |
| work through graded assignments | `jichi assignments`, `/assignment`, `/hint`, `/grade` | work | [ASSIGNMENTS.md](ASSIGNMENTS.md) |
| understand a project you did not write | `jichi init onboarding`, then `/onboard` | work + ranger | [READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md) |
| check that jichi works on your machine | `jichi init platform-test` | work | [PLATFORM_TESTING.md](PLATFORM_TESTING.md) |
| write notes or documentation | `jichi init docs`, `/write-docs`, `/proofread` | work + reading | [PROJECT_RECORDS.md](PROJECT_RECORDS.md) |

**Learning a language.** `jichi init course` writes `COURSE.md`, a `course-coach` skill and a
`/course` command. `COURSE.md` tells you to fetch the language's official documentation as a
versioned archive, and to add it as a docs source. Keep the documentation open on the reading
desktop in the browser. The coach asks you to predict before it reveals, and cites the file it
read for every claim. Check each citation in the browser. That habit is most of what the course
teaches.

**Assignments.** In jichi's own checkout, `jichi assignments` lists the graded tasks by stage,
from `00-hello.md` onwards. In a session, `/assignment docs/assignments/00-hello.md` turns the
model into a tutor that guides and does not solve, `/hint` reveals the next hint, and `/grade`
runs the task's own check. Edit in your editor, and use jichi to ask. The grade comes from a test,
not from the model.

**Understanding a project.** Before you ask anything, spend ten minutes in ranger or with `tree
-L 2`. Then `jichi map` shows the source files and their symbols, and `/onboard` (from the
`onboarding` pack) drafts an analysis and a tutorial without changing any code.

### 10.2 For the junior developer

| Case | Pack or command | Layout | Read |
|---|---|---|---|
| fix a bug in an existing project | `jichi init contributor`, `/triage` | work | [SDLC.md](SDLC.md) |
| review your change before you commit | `git diff \| jichi --readonly -p "…"` | work | §11 |
| refactor without changing behaviour | `jichi init refactor` | work | [TESTING_TUTORIAL.md](TESTING_TUTORIAL.md) |
| start a small program | `jichi init c-cli` (or `python-cli`, `zig-cli`, …) | work + reading | [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md) |
| read a library's manual as you go | `@url:`, `lynx -dump`, a docs source | work + reading | [DOCS.md](DOCS.md) |

**A bug fix, the careful way.** The `contributor` pack's order is: reproduce the bug, write a
test that fails, make the smallest change that fixes it, and describe it. Keep jichi in chat
mode. Run the test yourself in a shell beside the session, because a test you watched fail and
then pass is evidence, and a model saying "fixed" is not.

**Review before commit.** This ran for this page:

```sh
# in the project directory, with an uncommitted change
git diff | jichi --readonly -q -p "Review this change before I commit it. Is there a bug? Answer in at most five lines."
```

§11 shows what it found, and what it got wrong while finding it.

### 10.3 For the junior administrator

| Case | Pack or command | Layout | Read |
|---|---|---|---|
| make sense of a log | `jichi init log-analysis`; or pipe the log in | system + work | §11 |
| a routine health check | `jichi init sysadmin`, `/health-check` | system | [TOOL_DECISIONS.md](TOOL_DECISIONS.md) |
| understand a config file before changing it | `jichi --readonly -p "…@/etc/…"` | work | §13 |
| work on a server over ssh | jichi in tmux or screen on the server | one terminal | [SCREEN.md](SCREEN.md), [REMOTE_SSH.md](REMOTE_SSH.md) |
| write a runbook | `jichi init devops` | work + reading | [SCAFFOLDING.md](SCAFFOLDING.md) |

**A log, piped in.** You choose what the model sees, bounded by unit, priority and time:

```sh
# anywhere, on the machine whose log it is
journalctl -u nginx --since "1 hour ago" | jichi --readonly -p "Counts by severity, the distinct problems, and what to check first."
```

`journalctl` pages its output through `less` only when it writes to a terminal. Into a pipe it
writes plain text, as a test on the Xubuntu machine confirmed.

That exact command was not run for this page, because it would have sent a real system's log to
a model. The same question about a made-up log was run, and §11 shows both models' answers,
including the causes they guessed. The `log-analysis` pack's `/triage-log <file>` asks the same
of a log file in a session (not run for this page). Its `journalctl-syslog` skill carries
recipes for bounding a query by unit, priority and time.

**A health check.** The `sysadmin` pack's agent is told to "inspect first, change minimally", and
to show any command that changes something, with its effect and how to undo it, before it runs.
Its `/health-check` reports failed units, disk space and backup age, and its instructions end
"do not change anything without asking". That is an instruction to a model, not a fence. The
fences are these: chat mode asks before every shell command, and `sudo`, `doas`, `pkexec`, `su`
and `run0` are asked every time, whatever you approved before
([TOOL_DECISIONS.md](TOOL_DECISIONS.md)). Read each command before you press `y`.

**A server.** Run jichi on the server inside tmux or GNU screen, so that a dropped connection
does not end it. Use the lean profile if the server is small (`--lite`, which turns off snapshots (so `/undo`),
`@`-references, the repo map and markdown rendering; [LOW_MEMORY.md](LOW_MEMORY.md)).
[SCREEN.md](SCREEN.md) covers reaching one session from two machines.

---

## 11. Choosing a model, and checking what it tells you

### 11.1 Which model for which job

Choosing the model is a skill you learn, like choosing a tool. Start from the job, not the
model's name or size:
- **A mechanical change you can describe exactly** ("rename this function in these two files",
  "add a test for this case") suits a fast coding model.
- **Reasoning:** reviewing a change, planning, explaining why something fails, deciding between
  designs. That suits a stronger, slower model.
- **Prose** that nobody will compile needs more care than code, not less, because nothing catches
  its mistakes ([CHOOSING_A_MODEL.md](CHOOSING_A_MODEL.md) §3).

The free models on the university's gateway on 2026-09-29, and what is known about each:

| Model | Use it for | It needs | Source |
|---|---|---|---|
| `jlu/qwen3-coder-next` | small coding steps; fast | small tasks, strict guidance, supervision | the operator; §11.2; CHOOSING §3 |
| `jlu/qwen3.8-27b` | review, planning, explaining; slow | patience, and delegation made for it | the operator; §11.2; the weekend drive |
| `jlu/gemma-4-26b-it` | untested here | — | `doctor --live` only |

**`jlu/qwen3-coder-next` needs small tasks, strict guidance and supervision.** That is the
operator's experience across projects, and the records agree:
- Unfenced, it made a gate pass by editing the tests. Fenced to one file, the same model
  implemented the task correctly for a tenth of the tokens
  ([CHOOSING_A_MODEL.md](CHOOSING_A_MODEL.md) §3).
- As the lead of a team, it wrote code quickly and claimed success without evidence. In 182 runs
  as the lead it never delegated once ([DRIVING_A_TEAM.md](DRIVING_A_TEAM.md) §1).

So give it one small task at a time. Name the file and the function, and say what "done" means:
a test that must pass, or an output you can check. Keep it in chat mode and read each request. If
you let it run unattended, fence it with `--edit-scope` and a real `--verify`
([AUTONOMY.md](AUTONOMY.md)).

**`jlu/qwen3.8-27b` is slower, and tends not to delegate.** It answered §11.2's questions in 2 to
20 seconds, against the coding model's five or less. As a team's lead, its median model call took
7 seconds and its slowest 540
([the weekend drive, complete](analysis/2026-09-28-the-weekend-drive-complete.md) §2.2).
In the operator's experience it tends to do the work itself instead of handing it to a
faster model. The weekend drive shows the other side. When the prompt, the task cards and the
agent profiles made delegation the process, it did delegate, 89 times. So do not count on a
strong model to hand work down because a prompt asks it to. Make that structural:

- **Start fast, and let jichi escalate.** Routing starts every turn on the fast model and moves
  the turn to the strong one when the verifier fails, when the stream stalls, or when 75 % of the
  fast model's context is used (only if the strong model's declared `contextLength` is larger)
  ([ROUTING.md](ROUTING.md)):

  ```sh
  # in the project directory
  jichi --route-fast qwen3-coder-next --route-strong qwen3.8-27b
  ```

  **Both names must match models in your config.** With a config that listed both, `/route`
  showed both tiers and `routing: on`. With one that listed only the coding model, it said
  `(inert: set distinct fast+strong)`, and every turn stayed on one model. Nothing else warned.
  Check `/route` once at the start of a session.

- **Or pin the model in an agent profile**, so that delegated work runs on the fast model
  whatever the lead would have chosen, unless the lead names another model in the call. Put
  `model: qwen3-coder-next` in the profile's header
  ([SUBAGENTS.md](SUBAGENTS.md) § Named profiles), and name the profile in the task.
- **Let a second model check the first.** In the weekend drive, a double free the fast model had
  committed was found when a verification failed and jichi's routing (`escalateOnVerify`) moved
  the rest of the turn to `jlu/qwen3.8-27b`
  ([the day-one analysis](analysis/2026-09-26-the-weekend-drive-day-one.md)). A read-only
  reviewer on a different model from the writer is the same idea by hand
  ([DRIVING_A_TEAM.md](DRIVING_A_TEAM.md) §1).

Routing, as a diagram; the first point above says the same in words:

```text
every turn starts on the fast model       jlu/qwen3-coder-next
        |
        |  the verifier fails, the stream stalls,
        |  or 75 % of its context is used
        v
the rest of the turn runs on the strong model   jlu/qwen3.8-27b
```

In a session, `/model` lists the configured models and `/model 2` (or part of a name) switches.
With routing on, though, a `/model` switch lasts only the next turn -- routing re-routes to the
fast tier at the start of each turn -- so use `/route off` (or start the run with `--no-route`)
to make one model stick. `/route` shows the routing, and `--model` pins one model for a whole run
([MODELS.md](MODELS.md)).

**How to learn to choose.** Ask both models the same small question about your own project, as
§11.2 does. Note the time each takes, and check each answer the way §11.3 does. Ask each model
which version of your language it knows ([CHOOSING_A_MODEL.md](CHOOSING_A_MODEL.md) §4). After a
week you will know which of your jobs each model does well. That knowledge is about your jobs,
and a benchmark table cannot give it to you.

### 11.2 Eight questions, two models

The same eight questions were put to two free models for this page, each with `--readonly`:
`jlu/qwen3-coder-next`, a coding model, and `jlu/qwen3.8-27b`, a stronger reasoning model. The
coding model answered each in five seconds or less, and the reasoning model in 2 to 20 seconds.
Every answer was useful. The coding model's had four flaws that only checking against the source
revealed. The reasoning model avoided two of them, and kept a smaller version of the other two:

| Question | `qwen3-coder-next` | `qwen3.8-27b` |
|---|---|---|
| a diff with a loop `i <= argc` | the right bug; said `argv[argc]` is out of bounds | the right bug; "the NULL terminator (or out-of-bounds)" |
| a PDF: the MIME search order | exact quotes; its summary reversed them | exact quotes, a correct summary; said the text was cut off |
| a web page that returned 404 | answered from a site it chose, and did not say so | named the site it used instead |
| a log: counts, and what to check first | counts correct | counts correct; kept the slow page apart |
| the same log, continued with `-c` | a cause the timestamps rule out | "prime suspect": the disk, with no evidence in the log |
| a man page piped in | `-L level` and `-a`, correct | the same |
| a web page piped through `lynx` | `-t`, quoting the page | the same |
| a Makefile target, in jichi's checkout | `all test smoke`, quoting the line | the same, and why, from the Makefile's comment |

### 11.3 What each difference teaches

- **The diff.** C guarantees that `argv[argc]` is a null pointer, so reading it is not out of
  bounds. Passing that null to `printf("%s")` is the undefined behaviour. The coding model found
  the right line for a slightly wrong reason. The reasoning model gave the right reason, and then
  hedged with the wrong one in brackets. A beginner who learns the rule from either answer may
  learn it wrong. **Look the rule up**; here, the C standard's description of `main`.
- **The PDF.** The specification says the files load from `/usr/share/mime`, then
  `/usr/local/share/mime`, then `~/.local/share/mime`, and that "information from directories
  lower in the list takes precedence", so your home directory wins. Both models quoted both
  sentences exactly. The coding model then summarised them in the wrong order, and the reasoning
  model got the order right. **Read the quote, not the summary**, and find it in the PDF (`Ctrl-F`
  in Atril). The reasoning model also said "the reference was truncated at §2.13". That was
  true, and §7.2 explains why.
- **The web page.** The address was a 404. The coding model then fetched `linux.die.net` and
  answered from it, with nothing in the answer to say so. The reasoning model tried three more
  sites (another 404, then `man.archlinux.org`, which answered, and one that failed to connect),
  and said which one it quoted.
  **Look at where the answer came from.** The saved session records every fetch, and `/why`
  explains the last tool call.
- **The log.** The coding model said the database outage explained the slow responses. But the
  first slow response (08:02:13) came before the first database error (08:02:15), and the second
  (08:10:44) came eight minutes after the database reconnected (08:02:21). The reasoning model
  kept the slow page as a separate, ongoing problem, which fits the times. It then called the
  full disk the "prime suspect" for everything, although the disk warning is the log's last line
  (08:15:02) and nothing shows the disk's state earlier. It did name the check that would settle
  it, `journalctl -u postgresql`. **Put a causal claim next to its timestamps**, and treat a
  suspect as a question to check.

The coding model also tried, twice, to run a shell command in its headless `--readonly` run.
Both attempts were refused before anything ran (`Tool requires approval, unavailable in headless
mode`), and it went on with what it had. The reasoning model did not try. (These §11 runs were on
M839, where a `--readonly` session resumed with `-c` could still be offered mutating tools; M840
fixed that, so a current build holds `--readonly` across `-c`.)

A stronger model is worth using where the reasoning is the hard part. It is slower, and it still
needs checking. The quote, the page number, the timestamp and your own test make an answer
usable, whichever model gave it.

---

## 12. What jichi may do on your machine

| You want | Run | What it can do |
|---|---|---|
| an answer, nothing else | `jichi --readonly -p "…"` | read any file you can; fetch web pages; no shell, no edits |
| to look before any change | a session in `/plan` | read and propose; change nothing |
| to work together | a session in chat mode (the default) | read freely; every edit and shell command asks first |
| to let it run unattended | `--auto`, with its bounds | runs approved tools without asking; read [AUTONOMY.md](AUTONOMY.md) first |

- **`--readonly` removes the shell.** The shell tool counts as a tool that changes things, so it is
  neither offered nor run. In chat mode it is offered, and asks each time.
- **The shell is not fenced.** A command you allow runs as you, as `/fences` says. The path fence
  and the edit scope limit jichi's own file tools, not what a shell command does.
- **`sudo` is asked again every time**, even after you chose "always" for something else. In an
  unattended run it is refused.
- **`--auto` on a machine you care about is a decision, not a convenience.** Learn chat mode
  first. [TOOL_DECISIONS.md](TOOL_DECISIONS.md) sets out all nine mechanisms in order.
- **Do not run jichi as root.** Run it as yourself, and let `sudo` ask when a step needs it.

---

## 13. Your data: what leaves the machine

- **Everything the model reads goes to the model's server:** your message, an `@` file, a piped
  log, a PDF's text, a fetched page. With a model your institution hosts, it goes to your
  institution's server. With a local server such as LM Studio, it stays on your machine. With a
  commercial provider, it goes to that company.
- **The session is saved** under `~/.jichi.d/`, including what you piped in. The HTML export from
  §6.3 contained the whole log from §11. [STATE.md](STATE.md) lists what jichi keeps and how to
  remove it, and `--no-session` keeps a run from being saved.
- **Do not hand it secrets.** Keep keys, passwords, `/etc/shadow`, private keys and customer data
  out of `@` references and pipes. A config file can hold a password too, so read it before you
  attach it. Logs name users, addresses and hosts. Cut a log down to what the question needs.
  Remember, too, that the model can open such files itself: unless the path fence is on, its file
  tools reach any file you can read, whether or not you attached it.
- jichi records **metrics** about each run by default (tokens, tool calls, timings, and no
  content). `--log-level off` turns that off, and [TELEMETRY.md](TELEMETRY.md) explains it.

---

## 14. Something to do

Set up the bench once, on purpose, before you need it:

1. Make a scratch project: `mkdir -p ~/bench && cd ~/bench && git init`. Write a small C or
   Python file in your editor, then commit it: `git add . && git commit -m "first version"`.
2. Workspace 1: open two terminals side by side (§3.2). Start `jichi` in one, and your editor on
   the file in the other.
3. Workspace 2: open a PDF with Atril, and the language's documentation in Firefox.
4. Change the file yourself, introduce a bug on purpose, and run `git diff | jichi --readonly -p
   "Is there a bug?"`. Check its reason, not only its verdict (§11).
5. In the session, ask for the fix. Read the request, press `y`, and reload the file in your
   editor (§5.1). Then `/diff`, and `/undo`.
6. From workspace 2, pick three pages of the PDF and hand them over with `pdftotext -f … -l …`
   (§7.2). Find one sentence of the answer in the PDF yourself.
7. In the session, run `/export --html` (it writes `./jichi-<id>.html` in the current directory
   and prints the name), then open that file with `lynx`.

When all seven work without looking anything up, the bench is yours.

---

## 15. What this page cannot tell you

- **No shortcut was pressed.** Xfce 4.18's keys were read from its shipped settings, and one was
  queried with `xfconf-query`. GNOME 50.1's were read with `gsettings`. KDE Plasma, other
  desktops, macOS and Windows were not looked at.
- **"Out of the box" is one machine's installer list**, Xubuntu 22.04.2 in 2023. A newer Xubuntu,
  or another distribution, installs another set.
- **The editors' reload behaviour, ranger's keys and the terminal's copy keys are from their
  manuals** and the programs' own strings, not from driving them.
- **`xdg-open`, `notify-send` and `--notify` were not run**, because they act on the desktop.
- **Eight questions, two models, one day.** Another model, or the same model next month,
  answers differently. The flaws in §11 show what to check, not how often a model gets things
  wrong.
- **The GNOME second-monitor setting** was read, not tried with two monitors.

---

## Words used on this page

- **Checkpoint** — a copy of the project's files that jichi takes before its first change in a
  turn; `/undo` returns to it. It needs `git` ([SNAPSHOTS.md](SNAPSHOTS.md)).
- **Chat mode, plan mode, auto mode** — how much jichi may do without asking: ask before changes,
  change nothing, or run approved tools without asking ([AGENT_MODES.md](AGENT_MODES.md)).
- **Headless** — jichi run with `-p`: one question, one answer, no session window.
- **Pack** — a starting set of agents, skills and commands that `jichi init` writes into
  `.jichi/` ([SCAFFOLDING.md](SCAFFOLDING.md)).
- **Pipe** — `a | b`: the output of program `a` becomes the input of program `b`.
- **Tiling** — placing windows side by side so they do not overlap.
- **Workspace** — a virtual desktop: a full screen of windows you switch to at once.
- **X selection** — text selected with the mouse, pasted with the middle button; separate from
  the clipboard.

---

## Bibliography

Full entries, with what each is for, are in [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) §1, "The workbench".
Each was checked on 2026-09-29. "Read" means the page was fetched and read, "probed" that the
address answered, and "ISBN verified" that Open Library returned that title and publisher, and
the edition where its record names one.

**Free to read**
- William Shotts, *The Linux Command Line*, seventh internet edition, CC BY-NC-ND, free PDF —
  <https://linuxcommand.org/tlcl.php> (read; secondary).
- *The Missing Semester of Your CS Education*, MIT, 2026 edition, including a lecture on agentic
  coding — <https://missing.csail.mit.edu/> (read; secondary).
- *GNU Bash Reference Manual* — <https://www.gnu.org/software/bash/manual/> (probed; primary).
- *GNU Coreutils manual* — <https://www.gnu.org/software/coreutils/manual/> (probed; primary).
- *Vim user manual*, and `vimtutor` on your machine — <https://vimhelp.org/usr_toc.txt.html>
  (probed; primary).
- *GNU Emacs Manual*, and `C-h t` for the tutorial —
  <https://www.gnu.org/software/emacs/manual/html_node/emacs/> (probed; primary).
- *GNU nano documentation*: manual, FAQ, `nanorc(5)` — <https://www.nano-editor.org/docs.php>
  (read; primary).
- *ranger official user guide* — <https://github.com/ranger/ranger/wiki/Official-user-guide>
  (read; primary).
- Scott Chacon and Ben Straub, *Pro Git*, 2nd edition (2014), CC BY-NC-SA 3.0 —
  <https://git-scm.com/book/en/v2> (read; secondary).
- *XDG Base Directory Specification*, version 0.8 —
  <https://specifications.freedesktop.org/basedir-spec/latest/> (read; primary).
- Eric S. Raymond, *The Art of Unix Programming* — <http://www.catb.org/esr/writings/taoup/html/>
  (probed; secondary).
- *GNOME Help: Useful keyboard shortcuts* —
  <https://help.gnome.org/users/gnome-help/stable/shell-keyboard-shortcuts.html> (probed; primary).
- *Xfce documentation: xfwm4*, the window manager — <https://docs.xfce.org/xfce/xfwm4/start>
  (probed; primary).

**In print**
- Jerry Peek et al., *UNIX Power Tools*, 3rd edition, O'Reilly — ISBN 978-0-596-00330-2 (ISBN
  verified; secondary).
- Drew Neil, *Practical Vim*, 2nd edition, Pragmatic Bookshelf, 2015 — ISBN 978-1-68050-127-8
  (ISBN verified; secondary).
- Brian W. Kernighan and Rob Pike, *The UNIX Programming Environment*, Prentice-Hall, 1984 — ISBN
  978-0-13-937681-8 (ISBN verified; secondary).

**On your machine, and always the right version:** `man bash`, `man ranger`, `man pdftotext`,
`info coreutils`, `vimtutor`, and `nano`'s `^G` help.

## Read further

- [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md): the first session, if you have not had it.
- [TOOL_DECISIONS.md](TOOL_DECISIONS.md): what jichi may do to your machine, in the order it
  decides.
- [REFERENCES.md](REFERENCES.md): every `@` reference.
- [SCRIPTING.md](SCRIPTING.md): pipes, output formats and exit codes.
- [EDITORS.md](EDITORS.md), [EMACS.md](EMACS.md), [VIM.md](VIM.md): the editor integrations.
- [TMUX.md](TMUX.md), [SCREEN.md](SCREEN.md): one session that survives the connection.
- [SCAFFOLDING.md](SCAFFOLDING.md): every pack, and the domain benches in `examples/`.
- [STATE.md](STATE.md): what jichi keeps, and where.
