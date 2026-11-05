# Working across machines with GNU screen

*This page is for a self-learner or a junior developer who runs something long on one machine
— a jichi session, an `--auto` run, a build, a test suite — and wants to reach it from another:
a laptop and a workstation, a desk at home and a desk at work. It teaches GNU `screen`, and
especially the part short guides tend to skip: one program, reached from two machines, without
losing it.*

*After reading it you can:*
- *start a program so that it survives a dropped connection;*
- *reach it from a second machine, or from two at once;*
- *take it over from a terminal you forgot to leave;*
- *read back what it printed, and save that to a file;*
- *recognise the mistakes that end a session, including a crash in screen 4.9.1 that we found
  no report of (§8.1).*

*What you need first: two machines you can log in to with `ssh` (see
[REMOTE_SSH.md](REMOTE_SSH.md) if that is new), and permission to install a package on the one
where the program will run. You do not need tmux. If you already use it,
[TMUX.md](TMUX.md) does the same job with different keys, and §13 compares the two.*

*Where it fits: [TMUX.md](TMUX.md) is the tmux version of this page, with pane layouts for
supervising a run. [REMOTE_SSH.md](REMOTE_SSH.md) drives jichi headless over ssh, where there is
no terminal to keep. [DEPLOYMENT.md](DEPLOYMENT.md) covers servers and small boards.*

*How it was checked: the screen commands and keys below were run on 2026-09-29 against
**GNU screen 4.9.1**. That is Ubuntu 24.04's package `4.9.1-1ubuntu1`, unpacked into a scratch
directory and driven through pseudo-terminals by a script, plus Ubuntu 26.04's `4.9.1-3ubuntu2`
on a workstation for the ssh and process observations. What was not run is marked: "manual" for
what comes from `screen(1)`, "not run" for the package installs, "expected" for what was
reasoned. A second reviewer later built upstream 4.9.1 and 5.0.2 from the signed GNU tarballs;
5.0.2 has the §8.1 crash too. §14 says what the tests did and did not cover.*

---

## 0. The answer first

1. **screen runs on the machine where the program runs.** Install it there. The machine you type
   on needs only `ssh`.
2. **Start a named session there:** `screen -S agent`, then start jichi inside it (§3).
3. **Leave with `Ctrl-a d`.** The program keeps running. `Ctrl-c` does not leave: it goes to
   the program. `exit` ends the session (§8.2).
4. **Come back from anywhere:** `ssh -t workstation screen -r agent` (§4).
5. **Open it in two places at once:** `screen -x agent` in each (§5).
6. **Take it over from a terminal you left attached:** `screen -d -r agent` (§4.3).
7. **After switching terminals, press `Ctrl-a F`** to fit the picture to the terminal you are in
   (§5.2).
8. **Never leave a terminal in scrollback mode.** Copy mode belongs to the window: while it is
   on, the program stops once its output fills screen's buffer, for every attached terminal (§6.2).
9. **Do not open the displays list (`Ctrl-a *`) while two terminals are attached.** When a
   terminal left while the list was open, screen 4.9.1 crashed at the next attach in 38 of 44
   tests, and took the program with it (§8.1).

---

## 1. What screen is: four ideas

**The program runs inside screen, not inside your terminal.** When you start `screen`, it
starts a background process, the **screen server**, which gives each window a terminal of its
own and runs your shell in it. Your terminal only shows what the server draws. Close your
terminal, or lose the connection, and the server and everything in it carry on.

```text
laptop terminal  -- ssh -->  workstation
                             screen server "agent"
desk terminal  --------------> window 0: jichi
                               window 1: a shell
```

The laptop reaches the workstation over ssh. A terminal at the workstation's own desk attaches
directly. Both show the same session, with its two windows.

The four words this page uses:

- **Session** — one screen server and everything in it, known by a name such as `agent`.
  `screen -ls` lists them. This is not a *jichi* session, which is a saved conversation
  (`jichi ls`). A screen session can hold several jichi sessions, one after another.
- **Window** — one program with its own screen, inside a session. A new session starts with
  window 0. You can open more (`Ctrl-a c`).
- **Display** — one terminal attached to a session. A session can have none (**detached**), one,
  or several (**attached**, §5).
- **The prefix key, `Ctrl-a`.** Everything you type goes to the program, except `Ctrl-a`. That
  tells screen the next key is a command for it. Press `Ctrl-a`, release both keys, then press
  the command key. `Ctrl-a d` means: Ctrl and a together, let go, then d.

---

## 2. Install it, and try it once (five minutes)

**Install it on the machine where programs will run.** On Debian and Ubuntu the package is
called `screen`:

```sh
# on the workstation
sudo apt install screen    # not run for this page
screen --version
```

You should see `Screen version 4.09.01 (GNU) 20-Aug-23` or similar. On other systems the package
is usually called `screen` too. This page ran only the Ubuntu package.

**Try it before you need it.** Two terminal windows on one machine are enough to practise
everything on this page, including working from two machines at once:

```sh
# terminal window 1
screen -S try          # a new session named "try"; you are now inside it
sleep 600              # something that keeps running
#   press Ctrl-a d     # detach: the sleep keeps going
screen -ls             # the session is listed as Detached
screen -r try          # back inside; the sleep is still running
```

`screen -ls` prints something like this:

```text
There is a screen on:
	143016.try	(09/29/26 10:06:21)	(Detached)
1 Socket in /run/screen/S-you.
```

`143016` is the server's process id, and `try` is the name you gave it. The date is roughly
when the session started, printed in your locale's format. Finish the exercise with `Ctrl-c`
(stops the `sleep`), then `exit`, which ends the shell and with it the session.

---

## 3. Starting a session for real work

**Name every session.** `screen -S agent` gives you a name you can type and a list that reads
like a job list. Without `-S`, the name is made from the terminal and host names and is hard to
tell apart. Short names without spaces are easiest.

**Start it in the directory you will work in:**

```sh
# on the workstation
cd ~/project
screen -S agent
jichi                  # inside the session
```

jichi's repo map and session list are tied to the directory it starts in, and so is the path
fence's boundary when the fence is on (by default only under `--auto`); `jichi --help` describes
`ls` and `-c` per directory. One screen session per project keeps that clear.

**Start a command without attaching**, which is useful for a build or a headless run:

```sh
# on the workstation, in the project directory
mkdir -p ~/logs
screen -dmS tests sh -c 'make test > ~/logs/tests.log 2>&1'
```

`-dm` starts the session detached. **The session ends when its command ends, and whatever it
printed to the screen is gone with it.** A command that finished in a second left nothing to
reattach to in the tests. So redirect the output to a file, as above, or ask screen to log the
window with `-L -Logfile`:

```sh
# on the workstation
screen -dmS tests -L -Logfile ~/logs/tests.screenlog sh -c 'make test'
```

That log kept the output after the session had gone. The same applies to a detached
`jichi --auto` run: keep its record in a file, as
[TMUX.md](TMUX.md#long-autonomous-runs-detached) shows, and do not count on the screen to hold
it.

---

## 4. Leaving and coming back

### 4.1 Leaving

- **`Ctrl-a d` detaches.** Your terminal prints `[detached from 143016.agent]` and returns to the
  shell you started screen from. The session keeps running.
- **A dropped connection detaches for you.** When the terminal hung up without detaching, the
  way a lost ssh connection does, the session was listed as Detached a second later, and the
  program was still running. The manual calls this `autodetach`, and it is on by default.

### 4.2 Coming back

```sh
# on the workstation
screen -ls             # which sessions exist, and are they Attached or Detached?
screen -r agent        # attach to "agent"
screen -r ag           # a unique prefix of the name is enough
```

With several detached sessions, a bare `screen -r` refuses to guess:

```text
There are several suitable screens on:
	144303.second	(09/29/26 10:16:56)	(Detached)
	144234.probe	(09/29/26 10:16:41)	(Detached)
Type "screen [-d] -r [pid.]tty.host" to resume one of them.
```

**From another machine, run screen through ssh and give ssh `-t`:**

```sh
# on the laptop
ssh -t workstation screen -r agent
```

`-t` asks ssh for a terminal on the far side. Without it, screen stops with
`Must be connected to a terminal.`, which was checked over ssh. `ssh workstation screen -ls`
works without `-t`, because listing needs no terminal.

### 4.3 When it says the session is attached somewhere else

`screen -r` only takes a session that no terminal is showing. If you left it attached at the
other desk, you get:

```text
There is a screen on:
	143016.agent	(09/29/26 10:06:20)	(Attached)
There is no screen to be resumed matching agent.
```

You have two choices:

| You type | It does |
|---|---|
| `screen -d -r agent` | detaches the other terminal first, then attaches here |
| `screen -x agent` | attaches here too; the other terminal stays attached (§5) |

With `-d -r`, the terminal you left behind prints `[remote detached from 143016.agent]` and
returns to its shell. Nothing inside the session is touched.

---

## 5. Working from two machines at once

### 5.1 `screen -x` in both places

```sh
# on the workstation, at its own keyboard
screen -x agent
# on the laptop
ssh -t workstation screen -x agent
```

`-x` joins a session whether or not another terminal is attached. It worked on an attached
session and on a detached one, so the order does not matter. You do not need to detach anything
first. `screen -d -r` would do the opposite of what you want, because it removes the other
terminal.

Both terminals now show the same windows, and **both keyboards type into the same program.**
There is one prompt, not two, so type in one place at a time.

`Ctrl-a d` in one terminal detaches only that one. The other stays attached, and the program
keeps running either way.

### 5.2 The size of the picture: `Ctrl-a F`

A window has one size, and two terminals rarely match. After `screen -x`, the window kept the
size of the terminal that was there first. Pressing `Ctrl-a F` (*fit*) in the second terminal
resized it to that terminal, and pressing it in the first terminal resized it back:

| After | The window measured |
|---|---|
| `screen -x` from a 120×40 terminal, into an 80×24 session | 80×24 (unchanged) |
| `Ctrl-a F` in the 120×40 terminal | 120×40 |
| `Ctrl-a F` in the 80×24 terminal | 80×24 |

The manual says why: screen "doesn't adapt the window size automatically if the window is
displayed more than once". The terminal that does not match shows the picture cut off, or with
empty space around it. So press `Ctrl-a F` each time you move to the other machine. jichi has no
resize handler; it re-reads the width the next time it redraws its input line, so after
`Ctrl-a F` its prompt fits from the next keystroke ([TMUX.md](TMUX.md) § Gotchas).

### 5.3 Who else is attached?

`screen -ls` says Attached, but not how many terminals, or which ones. `ps` shows each of them,
because every attached terminal runs its own small screen client:

```sh
# on the workstation
ps -C screen -o pid,tty,args
```

Output from the test, with the session renamed:

```text
    PID TT       COMMAND
 145382 ?        SCREEN -dmS agent
 145385 pts/7    screen -r agent
 145389 pts/8    screen -x agent
```

The upper-case `SCREEN` line is the server. Each lower-case line is one attached terminal, with
the terminal it runs on. Two such lines mean two terminals.

screen also has a built-in list of displays, `Ctrl-a *`. **Do not use it while two terminals
are attached** (§8.1). If you open it anyway, close it at once with **Return**, Escape or
`Ctrl-g`. `q` does not close it; the list stays open. In the list, `d` detaches the selected
terminal and `D` power-detaches it (manual).

### 5.4 One terminal in copy mode stops it for everyone

A terminal left in scrollback mode at the other desk holds the program for every terminal on
that window, once its output fills screen's buffer (§6.2). Before you walk away from a terminal, press
Escape, or detach it.

---

## 6. Reading back what happened

### 6.1 Scrollback, and how much of it there is

screen keeps a history of lines that scrolled off the top of each window. The manual's default
is **100 lines**. Ubuntu's `/etc/screenrc` raises it to **1,024** (the same line on 24.04 and
26.04). Both were measured with `screen -Q info`, which prints the size after the `+`:

```sh
# on the workstation
screen -S agent -Q info    # (3,1)/(80,24)+1024 ... 0(sh)
```

One agent turn can print more than 1,024 lines. To keep more, put this in `~/.screenrc` on the
machine that runs screen:

```text
defscrollback 20000
```

screen reads `~/.screenrc` when a session starts, so it applies to sessions started afterwards,
not to the ones already running. For one session, `screen -h 20000 -S agent` does the same.

**What it costs.** screen reserves the whole history when the window opens, not as lines
arrive. The server's resident memory (`VmRSS`), measured with one window at 80 columns, was
already its full size before anything had been printed, and printing a full history added 8 kB:

| History lines | Server memory, empty | After filling it |
|---|---|---|
| 1,024 | 2,456 kB | 2,464 kB |
| 20,000 | 5,124 kB | 5,132 kB |
| 100,000 | 16,436 kB | 16,444 kB |

That is about 145 bytes per line, per window, at 80 columns; other widths were not measured.
100,000 lines is nothing on a laptop, and worth counting on a 512 MB board
([LOW_MEMORY.md](LOW_MEMORY.md)). Each history kept exactly its size: 100,000 lines of history
plus the 23 lines on screen.

### 6.2 Copy mode: reading the history — and why the program waits

`Ctrl-a [` (or `Ctrl-a Escape`) enters **copy mode**, where the keys move through the history
instead of going to the program. The status line says `Copy mode - Column 3 Line 1(+1024)`.

From the manual; entering, `?` and Escape were tested.

| Keys in copy mode | Do |
|---|---|
| arrows, or `k` / `j` | one line up / down |
| `Ctrl-b` / `Ctrl-f` | one screen up / down |
| `g` / `G` | top of the history / bottom |
| `?text` then Return | search backward; the one you want most |
| `/text` then Return | search forward |
| `n` / `N` | next / previous match |
| Escape | leave copy mode (`Copy mode aborted`) |

**Copy mode belongs to the window, not to your terminal.** Entering it in any one attached
terminal puts every terminal on that window into it, and holds the program for all of them once
about 19 KB of its output is waiting. This was measured, and it is the opposite of what
[TMUX.md](TMUX.md) says about tmux's copy mode. A job printing 200,000 lines finished in 0.2 s
normally. With a terminal in copy mode it had not finished after 20 s, and it finished 5.3 s
after Escape. With two terminals attached and copy mode entered in one of them, it stopped for
both, and finished 8.6 s after Escape in either (a re-run on upstream 4.9.1 took 5.0 to 5.4 s).
The stop appeared in all five runs. In one run, set up differently, the job had still not
finished 20 s after Escape; why is not known. (A key that keeps copy mode, such as a first Space,
would look exactly like that; the run was not re-checked.)

**Which keys end it.** Movement and search keys, `Ctrl-g` (which only shows the position) and a
first Space or Return (which sets a mark) keep copy mode, and the program stays held. Escape, `q`,
or any key that is not a copy-mode command ends it, in any of the attached terminals, and so does
a second Space or Return, which copies. Most of the 5 s before the program resumes is screen's own
"Copy mode aborted" message: screen does not read the window while a message shows, and a
message stays 5 s unless you press another key, which clears it at once. The manual does not
mention any of this.

So copy mode is not only a view. Measured with jichi too, streaming a reply from a mock model: the
terminal shows nothing new at once, but jichi keeps streaming until about 20 KB of its output is
waiting (at 400 bytes a second, about 50 s), then waits, and finishes normally after you leave. A
shorter reply finishes during copy mode and appears when you leave. That is harmless for a
minute at your desk. It is not harmless if you walk away from a terminal still in copy mode and
then work from the other machine for an hour. In that case your agent has been waiting the whole
time.

You can check it yourself, with a second terminal to look from:

```sh
# inside a screen session
rm -f ~/done.txt
sleep 3; yes | head -n 200000; date > ~/done.txt
#   within 3 seconds press Ctrl-a [ and wait
#   in another terminal, `ls ~/done.txt` says it does not exist
#   press Escape in the screen session: the file appears a few seconds later
```

### 6.3 Saving the screen to a file, without attaching

```sh
# on the workstation, from any shell
screen -S agent -p 0 -X hardcopy -h $HOME/agent-history.txt
```

`-X` sends one command to the session, `-p 0` picks window 0, and `hardcopy -h` writes the
history plus the visible screen. Without `-h` you get only the visible screen. Give an absolute
path; the shell turns `$HOME` into one before screen sees it. This is also the safe way to see
what an agent is doing from another machine: it types nothing, and it attaches nothing.

### 6.4 Logging a window as it runs

Start with `screen -L -Logfile ~/logs/agent.screenlog -S agent`, or start logging a session that
is already running, from outside it:

```sh
# on the workstation
screen -S agent -p 0 -X logfile $HOME/logs/agent.screenlog
screen -S agent -p 0 -X logfile flush 1
screen -S agent -p 0 -X log on
```

`logfile flush 1` writes to the file every second instead of the default 10. Inside the session,
`Ctrl-a H` switches logging on and off. A log of jichi's TUI, a line editor that still draws
with colour and cursor-movement sequences, holds those sequences as well as its text. jichi's own records are cleaner:
[TMUX.md](TMUX.md#save-a-run-to-a-file-which-is-better-than-scrolling) lists them.

---

## 7. The keys worth knowing, and the ones that hurt

"Tested" means the behaviour was driven and observed. "Manual" means it is from `screen(1)` and
was not run for this page.

| Keys | What happens | Checked |
|---|---|---|
| `Ctrl-a d` | detach this terminal; the session keeps running | tested |
| `Ctrl-a F` | fit the window to this terminal | tested |
| `Ctrl-a [` | copy mode: scroll and search the history (§6.2) | tested |
| `Ctrl-a a` | send one real `Ctrl-a` to the program | tested |
| `Ctrl-a c` | a new window with a shell | manual |
| `Ctrl-a n` / `Ctrl-a p` | next / previous window | manual |
| `Ctrl-a "` | a list of windows to choose from | manual |
| `Ctrl-a A` | name the current window | manual |
| `Ctrl-a ?` | show every key binding | manual |
| `Ctrl-a k` | kill this window; asks `Really kill this window [y/n]` | tested |
| `Ctrl-a \` | kill every window and end the session; asks first | tested |
| `Ctrl-a s` | freeze the window's output; `Ctrl-a q` releases it | tested |
| `Ctrl-a x` | lock the terminal behind your login password | tested (the prompt) |
| `Ctrl-a *` | list attached terminals; dangerous with two (§8.1) | tested |

Four of them catch people:

- **`Ctrl-a` is also a key your shell uses.** In bash it moves to the start of the line, and in
  Emacs and in jichi's own input line it does the same. Inside screen, type `Ctrl-a a` to send it.
- **`Ctrl-a s` looks like a hang.** Output stops, keys seem to do nothing, and nothing says why.
  `Ctrl-a q` releases it. If a *shell* window seems frozen, or jichi is mid-turn, try `Ctrl-a q`
  first. But **at jichi's own input prompt these keys do something else**: jichi clears the
  terminal's flow control and binds `Ctrl-q` to "prompt advice", so `Ctrl-a q` there asks the
  active model a question — a request that costs money on a priced model — and `Ctrl-a s` does
  nothing. The freeze only applies while output is streaming.
- **`Ctrl-a x` is one key away from `Ctrl-a c`.** It shows `Screen used by <you> on <host>.` and
  `Password:`. The session is fine: Ubuntu's build checks your login password (unlocking was not
  tested). If it will not unlock, `screen -d -r name` from another terminal takes the session
  over.
- **`Ctrl-a \` ends everything.** Ubuntu's `/etc/screenrc` binds it to *quit*. It asks
  `Really quit and kill all your windows [y/n]` first; answer `n`, and the session is kept.

---

## 8. Ways to lose a session, and how not to

### 8.1 The displays-list crash (screen 4.9.1)

**What happens.** A session has two terminals attached, and the displays list (`Ctrl-a *`) is
open. Then one terminal leaves: it detaches, or its connection drops, which is the one you will
not see coming. If the list was open in the terminal that left, **the next attach crashes the
screen server.** If it was open in the terminal that stayed, the next attach crashed it whenever
that next terminal had a different size. The terminal still attached prints

```text
[screen caught signal 11. (core dumped)]
```

and `screen -ls` then lists the session as `(Dead ???)`. **Every program in its windows is gone
too.** In the test, the shell in window 0 no longer existed afterwards.

Across roughly sixty runs the crash was reliable whenever the list was open in the terminal that
left, and reliable when the list was open in the terminal that stayed and the next terminal had a
different size. It did not appear on those runs where the list was closed first, never opened, or
there was only one terminal — but do not read those as safe: under a memory checker even the
single-terminal case is the same use-after-free, so a run that did not crash had a freed pointer
it happened not to dereference. Treat any open list at the moment a terminal leaves as unsafe.

The server hit signal 11, a segmentation fault. screen catches it, prints the message and
aborts: the process ended on `SIGABRT`.

**This is a live upstream bug.** No fix exists as of 2026-09-30, released or not. It reproduces
on the 4.9.1 that Ubuntu 24.04 ships (26.04's package carries the same displays-list code, and
was not run), and on the latest release, **5.0.2**, where a memory checker shows a heap
use-after-free while the server walks the displays list on the next attach. No commit since
5.0.2 touches that code. GNU screen bug #68657 (a use-after-free on quit with two displays
attached, open against 5.0.2, with an unmerged patch to a different function) is the same family,
and two other open reports, #68648 and #67444, involve several displays too; none describes this
trigger. The investigation is ANECDOTES #119.

**How not to meet it.** Use `ps -C screen -o pid,tty,args` to see who is attached (§5.3). Close
the displays list — with Return, Escape or `Ctrl-g`, since `q` does **not** close it — before
anything else happens, whether one terminal is attached or two. If a terminal left while the list
was open in yours, press Return before anyone attaches again.

### 8.2 Stopping the program when you meant to leave

`Ctrl-a d` leaves. `Ctrl-c` goes to the program, as it should: jichi and most other programs
stop or cancel on it. `exit` in the last shell of a session ends the session. So does `Ctrl-d`,
which you may reach for when a prefix key does not register: at an empty jichi prompt it exits
jichi, and in a shell it closes the window (and the session, if it was the last one). When in
doubt, `Ctrl-a d`.

### 8.3 A session that was only ever its command

As §3 showed, `screen -dmS name command` ends when the command ends, and takes the screen's
contents with it. A job that failed in a second leaves nothing to read. Redirect its output, or
use `-L -Logfile`.

### 8.4 The machine goes down, or your login is closed

screen keeps a session alive across a dropped *connection*, not across a reboot. After a
reboot the sessions and their programs are gone. If `screen -ls` still lists one as
`Dead ???`, `screen -wipe` removes it.

Logging out is decided by the system. `logind.conf(5)` says of `KillUserProcesses=yes` that it
"will break tools like screen(1) and tmux(1)". systemd's own default is `yes`, but Debian and
Ubuntu build it as `no`, so on those it does not kill your sessions. To see what a machine does:

```sh
# on the workstation: the effective value, drop-ins and all
busctl get-property org.freedesktop.login1 /org/freedesktop/login1 \
  org.freedesktop.login1.Manager KillUserProcesses
```

`b false` means it will not kill your sessions. Reading the config files by hand is less reliable:
a `grep '^KillUserProcesses' /etc/systemd/logind.conf` misses the drop-ins under
`/usr/lib/systemd/logind.conf.d/` that a distribution uses to set it, and prints nothing when the
value is left at the built-in default. Whether screen survives a *desktop*
logout was not tested for this page. If a session matters, lock the desktop instead of logging
out.

### 8.5 A dead session

If the server is killed, its socket stays behind. `screen -ls` lists it as `(Dead ???)` and says
`Remove dead screens with 'screen -wipe'`. `screen -wipe` does that; nothing can be recovered
from it.

---

## 9. Moving a program that is already running into screen

You started an agent in a desktop terminal window, and now you want to reach it from elsewhere.
**`reptyr`** moves a running program onto the terminal you run it from. Run it inside a screen
window, and the program lands in screen. This is a rescue, not a way of working; §9.3 says what
it leaves behind.

### 9.1 How

```sh
# on the workstation
sudo apt install reptyr   # not run for this page
screen -S agent
sudo reptyr -T 12345      # inside the session; 12345 is the program's process id
#   press Ctrl-a d; the program now lives in the screen session
```

- **Why `sudo`.** reptyr uses `ptrace`: on the program itself, or with `-T` on the terminal
  emulator whose terminal it takes over. Ubuntu sets `kernel.yama.ptrace_scope` to 1, which lets a
  process trace only its own descendants, and neither the program nor the emulator is one. The
  reptyr README describes the setting.
- **Why `-T`.** On 2026-09-25, plain `reptyr 12345` refused on the workstation, because a helper
  process the program had started shared its process group. `-T` "steal[s] the entire terminal
  session of the target", marked experimental, "and will attach all processes running on the
  terminal".
- **What was seen.** On 2026-09-25 an agent was moved this way with reptyr 0.9.0, Ubuntu 26.04's
  package (the binary reports itself as 0.8.0).
  Four days later it was still running inside the screen session, and still answering.

### 9.2 Finding the process id

```sh
# on the workstation
ps -eo pid,tty,lstart,args | grep '[j]ichi'
```

The brackets keep `grep` from listing itself. Pick the line whose terminal (`TT`) is the window
you started it in.

### 9.3 What reptyr leaves behind

This was measured four days after the move, from the process table:

- The program's **session leader is still the shell of the original terminal tab.** The program
  is that terminal's foreground process group.
- The program still belongs to the **terminal application's per-tab control group** (a systemd
  scope the terminal created for that tab).
- The terminal application **no longer holds the terminal's master side**. reptyr does, running
  as root inside the screen window.

What follows from that is **expected, not tested**:

- **Closing the original tab, or quitting the terminal application, probably ends the program.**
  The tab's shell exits. POSIX says that when a controlling process exits, "the SIGHUP signal
  shall be sent to each process in the foreground process group of the controlling terminal".
  That group is the program. Leave the tab open, and minimise it.
- **Killing the screen window (`Ctrl-a k`) probably ends it too**, because that kills reptyr,
  which holds the program's terminal.
- **reptyr only moves one way.** You cannot give the program back to the original tab, and you
  do not need to.

To get a clean arrangement, **restart the program inside screen at a natural pause.** For jichi,
quit it and run `jichi -c` in the screen window, from the same directory. `-c` resumes the most
recent jichi session for that directory. The program is then a child of screen, with no link to
the old tab. Better still, start long work inside screen in the first place.

---

## 10. The environment inside a session is frozen when it starts

A session keeps the environment variables it was started with. Reattaching from another machine
does not update them. In the test, a session started with `FOO=first` still said `FOO is first`
after a reattach from a shell where `FOO=second`.

What that means in practice (**expected, not tested here**):

- **`SSH_AUTH_SOCK`.** If you started the session over ssh with agent forwarding, a `git push`
  inside it works only while that first connection lasts. Reattaching from a new connection
  does not repair it.
- **`DISPLAY` and `WAYLAND_DISPLAY`** point to the desktop the session was started from, not to
  the one you are at now.

The fix is to start the program again in a shell that has the environment you need. A new
window (`Ctrl-a c`) is expected to inherit the session's old environment too, so it does not help
by itself.

---

## 11. screen inside screen, and ssh from inside screen

**A plain `screen` typed inside a screen window opens a new window in the same session**, like
`Ctrl-a c`. This is measured: the window list went from `0*$ sh` to `0-$ sh  1*$ sh`, and no
new session appeared. **`screen -S inner` typed inside a window starts a separate session,
nested in that window.** `screen -ls` listed both.

**Nesting also happens when you ssh from inside screen to a machine that runs screen too.** Then
`Ctrl-a` goes to the outer session. `Ctrl-a a` sends it to the inner one, so `Ctrl-a a d`
detaches the inner session and leaves the outer one attached. That was tested.

Nesting is easy to get lost in. **Run screen on one side only:** normally on the machine where
the work runs, and not on the laptop you ssh from. If you do nest, the `-e` option gives a
session a different prefix key (manual, not tested).

**Which machine am I on?** A caption line at the bottom of every window answers it. Put this in
`~/.screenrc` on each machine that runs screen:

```text
caption always "%H  %S  %n %t"
```

`%H` is the host name, `%S` the session name, `%n` the window number and `%t` its title. The
test showed `xubuntu  agent  0 sh`.

---

## 12. Talking to a session from a script, or from an agent

| Command | What it does |
|---|---|
| `screen -ls` | list sessions and their state |
| `screen -S agent -Q info` | window size and history size |
| `screen -S agent -Q windows` | the list of windows |
| `screen -S agent -p 0 -X hardcopy -h FILE` | save history and screen (§6.3) |
| `screen -S agent -X quit` | end the session and everything in it |
| `screen -S agent -p 0 -X stuff 'text^M'` | type `text` and Return into window 0 |

- **Always pass `-S`.** With two sessions and no `-S`, `-X` refused: `There are several suitable
  screens on:` … `Use -S to specify a session.` With only one session running, it goes to that
  one without asking.
- **`stuff` types, exactly as if you were at the keyboard.** `^M` is Return. Into an agent's
  prompt, that is an instruction to the agent. For jichi, if you started it with `--control`, use
  the [control channel](CONTROL.md) instead: it is built for steering a run from outside. It is
  off by default.
- **`stuff` expands `$VARIABLES` before typing.** `stuff 'echo $HOME'` typed the expanded path,
  not `$HOME`. Write `\$HOME` to send a literal dollar sign.
- **Leave `SCREENDIR` alone.** If it is set, it must name a directory whose socket paths fit a
  Unix socket's 108-byte limit. With a longer path, `screen -dmS` exited 0 and no session
  existed, with no message at all.

---

## 13. screen or tmux?

Both keep a program alive across a dropped connection, and both let two terminals share it. This
project documents tmux in [TMUX.md](TMUX.md). Choose by what you already know, and by what the
machine has installed. The differences you meet first:

| | screen 4.9.1 | tmux |
|---|---|---|
| Prefix key | `Ctrl-a` | `Ctrl-b` |
| Scrollback by default | 100 lines; 1,024 on Ubuntu | 2,000 lines |
| Share with a second terminal | `screen -x name` | `tmux attach -t name` |
| Take over from a terminal | `screen -d -r name` | `tmux attach -d -t name` |
| Copy mode stops the program | yes, once the output fills (§6.2) | no, per [TMUX.md](TMUX.md) |
| Splitting the screen | regions (`Ctrl-a S`, `Ctrl-a \|`) | panes |

The tmux column is taken from [TMUX.md](TMUX.md) and tmux's manual. It was not measured for this
page.

---

## 14. What this page cannot tell you

- **screen 4.9.1 and upstream 5.0.2 were run** (5.0.2 by a second reviewer, from the signed GNU
  tarball, with and without a memory checker): both have the §8.1 crash and the §6.2 copy-mode
  stop. macOS's screen and the BSDs' were not run.
- **The crash was reproduced** on Ubuntu 24.04's `4.9.1-1ubuntu1` and on upstream 4.9.1 and 5.0.2
  builds, all on Linux x86-64. Ubuntu 26.04's `4.9.1-3ubuntu2` was not run; its package carries
  the same displays-list code.
- **§9.3's consequences and §10's examples are reasoned, not observed.** Nobody closed the
  terminal tab to see what happened, and no one tested agent forwarding.
- **The memory figures are for 80 columns** and one window.
- **screen's multi-user mode** (`multiuser`, `acladd`), where two *different* accounts share a
  session, is not covered.
- **The tests used a script, not a person.** A person at a keyboard is slower, and a delay
  might change a timing. None of the results depended on speed, but that was not checked by
  hand either.

How the tests were run: an unpacked copy of Ubuntu's package, its own socket directory and
`HOME`, so no real session could be touched. A Python script opened pseudo-terminals, started
screen in them, pressed keys and read the answers, then checked the result with `screen -ls`,
`screen -Q` and `/proc`. Each surprising result was isolated and run again: the crash in seven
arrangements, the copy-mode stop against a control run. The observations of a moved program (§9)
were read from a real session's process table, without attaching to it.

---

## 15. Something to do

With two terminal windows on one machine, which is enough to act out two machines:

1. `screen -S try` in window 1. Run `top`.
2. `screen -x try` in window 2. Type in one window and watch the other.
3. Make window 2 larger, and press `Ctrl-a F` in it. Then press `Ctrl-a F` in window 1.
4. In window 2 press `Ctrl-a d`. Check with `ps -C screen -o pid,tty,args` that one client is
   left.
5. Close window 1 entirely, without detaching. In window 2, `screen -ls` says Detached.
   `screen -r try` brings `top` back.
6. `Ctrl-a [`, look around, press Escape. Then quit `top` with `q`, and `exit`.

If every step did what this page said, you know enough to leave an agent running at one desk and
pick it up at another.

---

## Words used on this page

- **Attached / detached** — whether any terminal is showing a session right now.
- **Copy mode** — screen's scrollback mode, where keys move through history instead of reaching
  the program (§6.2).
- **Display** — one terminal attached to a session.
- **Prefix key** — `Ctrl-a`, the key that makes the next key a command for screen.
- **Pseudo-terminal (pty)** — the kernel's imitation of a terminal, which programs like screen,
  ssh and terminal windows create; each has a master side (held by whoever draws it) and a slave
  side (where the program runs).
- **Screen server** — the background process that owns a session.
- **Session** — here, a screen session: one server and its windows. A *jichi* session is a saved
  conversation, a different thing.
- **Session leader, foreground process group** — how the kernel groups the processes on one
  terminal; they decide who receives `SIGHUP` when a terminal goes away (§9.3).
- **Window** — one program and its screen, inside a session.

---

## Read further

- *GNU Screen User's Manual*, <https://www.gnu.org/software/screen/manual/screen.html>, and
  `screen(1)` (primary). Every option and key on this page is there, apart from the two
  behaviours in §6.2 and §8.1. Checked 2026-09-29.
- reptyr's README, <https://github.com/nelhage/reptyr> (primary): what reptyr does, and the
  `ptrace_scope` setting. Checked 2026-09-29.
- POSIX.1-2024, `_exit()`,
  <https://pubs.opengroup.org/onlinepubs/9799919799/functions/_exit.html> (primary): the
  `SIGHUP` rule behind §9.3. Checked 2026-09-29.
- `logind.conf(5)`, <https://man7.org/linux/man-pages/man5/logind.conf.5.html> (primary):
  `KillUserProcesses`, and its warning about screen and tmux. Checked 2026-09-29.
- GNU screen bug #68657, <https://savannah.gnu.org/bugs/?68657> (primary): the related
  two-display use-after-free in 5.0.2 (§8.1). Open on 2026-09-29.
- [TMUX.md](TMUX.md): the same job with tmux, supervision layouts, and where to keep a run's
  record.
- [REMOTE_SSH.md](REMOTE_SSH.md): driving jichi over ssh without a terminal to keep.
- [LOW_MEMORY.md](LOW_MEMORY.md): what memory costs on a small board (§6.1).
- [CONTROL.md](CONTROL.md): steering a running jichi from outside, instead of typing into it
  (§12).
