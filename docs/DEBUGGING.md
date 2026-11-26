# Debugging — finding out what is so

**Who this is for.** Anyone whose program does something other than what they
meant, and who wants a way of working rather than a lucky guess. If you want
the stance and the graded practice, the course has them in
[curriculum/04-debugging-as-science.md](curriculum/04-debugging-as-science.md);
if jichi itself misbehaves, start at [DOCTOR.md](DOCTOR.md). **After reading**
you can take a failure from "it is broken" to a cause you can show, with an
order of work and the instruments that fit each step. **You need first:** a C
compiler and a shell; `gdb`, `valgrind`, `strace` and `git` for the parts that
use them. **Where it fits:** after module 4, which gives the method its
shape; before [DEBUGGABLE_DESIGN.md](DEBUGGABLE_DESIGN.md), which is about
building things so that this page is needed less.

Every command and every piece of output below was run on one bench on
2026-11-25 (gcc 15.2.0, gdb 17.1, valgrind 3.26.0, strace 6.19, git 2.53.0,
x86-64 Linux with glibc). Addresses and process numbers will differ on your
machine. Line numbers will not, if you copy the program as it stands.

## 現地現物（げんちげんぶつ） — go and look

*Genchi genbutsu* (現地現物（げんちげんぶつ）) is a rule of the Toyota workshops: go to
the actual place and look at the actual thing. A report about the machine is
not the machine.

Debugging is that rule applied to a program. The cause of a failure is in the
running program, and it is hardly ever in your head, because your head is
where the program is already correct. So the measure of a debugging hour is
not how much you thought. It is **how many things you observed that you did
not know before**. If an hour has passed and you have a better theory but no
new observation, you have been guessing.

## The order of work

    1  WRITE THE SYMPTOM    what you ran, what you expected, what came
    2  REPRODUCE            make it fail whenever you ask
    3  REDUCE               the smallest input that still fails
    4  LOOK                 with an instrument, at the failing run
    5  EXPLAIN              one cause that predicts something new
    6  REPAIR               the cause, with the smallest change
    7  PROVE                a check that fails without the repair
    8  RECORD               symptom, dead ends, root cause, lesson

The order matters more than any step. Most lost days come from doing step 6
first.

**1. Write the symptom.** The exact command, the exact output, and the output
you expected. "It crashes" is not a symptom. "`./longest aaaaaaaaa` prints
`*** stack smashing detected ***` and exits with 134; I expected
`aaaaaaaaa`" is one. Writing it is often where you notice that what you
expected was never promised.

**2. Reproduce.** Until the failure comes when you call it, you cannot tell
a repair from a coincidence. A failure that comes one time in ten needs this
step most: find what the one time has that the nine have not.

**3. Reduce.** Cut the input in half and ask again. Keep the half that still
fails. A small case is quicker to run and to read, and the cutting is itself
a search: the moment the failure disappears, you have just removed something
that matters.

**4. Look.** Now use an instrument on the failing run: the next section
lists them. Look before you form a theory if you can. A theory formed first
decides what you look at.

**5. Explain.** State one cause. A real explanation predicts something you
have not yet seen — "then eight letters must already write past the end,
even though nothing is printed" — and you go and see. An explanation that
only accounts for what you already saw has passed no test.

**6. Repair.** The cause, not the symptom, and nothing else in the same
change.

**7. Prove.** A check that fails on the old program and passes on the new
one. Without the first half you have a check that passes, which is a
different thing ([APPROACH.md](APPROACH.md) §3).

**8. Record.** The four parts of
[curriculum/04-debugging-as-science.md](curriculum/04-debugging-as-science.md),
the wrong turns among them, on the same day.

After the repair comes one more thing, and it has a name in archery:
*zanshin* (残心（ざんしん）), the mind that remains after the arrow has left. Stay
five minutes and ask where else the same mistake lives.

## A worked example

A program that prints the longest word of each argument. It is C89 and
compiles without a warning under `-std=c89 -pedantic -Wall -Wextra`.

```c
/* longest.c -- print the longest word of each argument */
#include <stdio.h>
#include <string.h>

static void longest(const char *text, char *out)
{
    char word[8];
    size_t n = 0;
    size_t best = 0;
    const char *p;

    out[0] = '\0';
    for (p = text; ; p++) {
        if (*p == ' ' || *p == '\0') {
            word[n] = '\0';
            if (n > best) {
                best = n;
                memcpy(out, word, n + 1);
            }
            n = 0;
            if (*p == '\0') {
                break;
            }
        } else {
            word[n++] = *p;
        }
    }
}

int main(int argc, char **argv)
{
    char out[8];
    int i;

    for (i = 1; i < argc; i++) {
        longest(argv[i], out);
        printf("%s\n", out);
    }
    return 0;
}
```

**The symptom.**

```sh
cc -std=c89 -pedantic -Wall -Wextra -o longest longest.c
./longest "a bb ccc"
./longest "an extraordinary day"
```

The first prints `ccc`. The second prints
`*** stack smashing detected ***: terminated` and exits with status 134.

**Reproduce and reduce.** It fails every time, so step 2 is done. Reducing:
the spaces are not needed, and neither is the meaning of the word. What is
left is a length.

| Argument | Result |
|---|---|
| `aaaaaaa` (7 letters) | `aaaaaaa`, status 0 |
| `aaaaaaaa` (8 letters) | `aaaaaaaa`, status 0 |
| `aaaaaaaaa` (9 letters) | `*** stack smashing detected ***`, status 134 |

It would be easy to stop here with "nine letters is too many". Hold that
thought; the table is about to be shown wrong.

**Look, with a sanitizer.** Build the same file with the compiler's checks
switched on, and give it the input that *worked*:

```sh
cc -std=c89 -g -fsanitize=address,undefined -o longest-san longest.c
./longest-san aaaaaaaa
```

```
longest.c:15:17: runtime error: index 8 out of bounds for type 'char [8]'
...
==3768865==ERROR: AddressSanitizer: stack-buffer-overflow on address ...
WRITE of size 1 at 0x6d196f8f0068 thread T0
    #0 0x5bb5c5b625ac in longest longest.c:15
    #1 0x5bb5c5b629a9 in main longest.c:36
...
  This frame has 1 object(s):
    [32, 40) 'word' (line 7) <== Memory access at offset 40 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow longest.c:15 in longest
```

Eight letters were already wrong. Line 15 is `word[n] = '\0';`: with eight
letters stored, the terminator goes into a ninth place that `word` does not
have. The plain program printed the right answer and exited with 0 while
doing it. **A run that ends well has not shown that nothing went wrong.**

**Look, with a debugger.** The same failure, seen where it surfaces:

```sh
cc -std=c89 -g -o longest-g longest.c
gdb -batch -ex run -ex bt -args ./longest-g aaaaaaaaa
```

```
*** stack smashing detected ***: terminated
Program received signal SIGABRT, Aborted.
...
#8  0x00007ffff7d45234 in __stack_chk_fail () at ./debug/stack_chk_fail.c:24
#9  0x0000555555555267 in longest (text=0x7fffffffdd9d "aaaaaaaaa", out=0x7fffffffd830 "aaaaaaaaa") at longest.c:28
#10 0x00005555555552ba in main (argc=2, argv=0x7fffffffd978) at longest.c:36
```

The debugger is truthful and points at line 28, which is the closing brace
of the function. That is where the damage was *discovered*, when the
function checked its stack on the way out. The damage was *done* at lines 15
and 25. **Where a program falls is seldom where it was pushed.** A backtrace
tells you the first; finding the second is your work.

**Look, with valgrind.** A third instrument, on the plain build:

```sh
valgrind -q ./longest-g aaaaaaaa
```

It prints `aaaaaaaa` and nothing else. No error. Valgrind's memory checker
watches the heap closely and a local array on the stack hardly at all, so
this fault lies in its blind spot. **An instrument that reports nothing has
not reported that all is well.** Know what each one can see before you take
its silence for an answer.

**Explain, repair, prove.** The cause: the loop stores letters without
asking whether there is room, and the terminator needs one place more. The
repair is one condition:

```c
        } else {
            if (n < sizeof(word) - 1) {
                word[n++] = *p;
            }
        }
```

And the proof is the sanitizer build on the input that used to pass in
silence: before the repair it exits with 1 and the report above, after it
with 0. (The repaired program cuts a long word short without saying so.
Whether that is acceptable is a question of design, and
[DEBUGGABLE_DESIGN.md](DEBUGGABLE_DESIGN.md) takes it up.)

**When the failure arrived with a change: bisect.** Suppose the program
worked last month. With the history in git, you do not need to read the
changes; you can let the failing check find the one. A script that says good
or bad:

```sh
#!/bin/sh
# check.sh -- exit 0 when this revision is good, 1 when it is bad
cc -std=c89 -g -fsanitize=address,undefined -o /tmp/longest-check longest.c || exit 125
/tmp/longest-check aaaaaaaa > /dev/null 2>&1
```

```sh
git bisect start
git bisect bad
git bisect good f861ed8
git bisect run sh ../check.sh
git bisect reset
```

On a history of eight commits made for this page, with the fault planted in
the fifth, it answered after three builds:

```
578c35875b1ff6b728732f2413155e3eedd9e1a8 is the first bad commit
    longest: simplify the copy loop
 longest.c | 4 +---
 1 file changed, 1 insertion(+), 3 deletions(-)
```

`f861ed8` is the first commit of that history; yours will have another
name. Exit status 125 tells git to skip a revision that does not build.
Bisecting needs step 2 to be solid: with a failure that comes and goes, it
will name an innocent commit with full confidence.

**When the program says too little: trace it.** A different small program,
whose only message is `cannot start`:

```c
/* readconf.c -- says only "cannot start" when its file is missing */
#include <stdio.h>

int main(void)
{
    FILE *f = fopen("conf/settings.conf", "r");

    if (f == NULL) {
        fprintf(stderr, "cannot start\n");
        return 1;
    }
    fclose(f);
    return 0;
}
```

```sh
cc -std=c89 -pedantic -Wall -Wextra -o readconf readconf.c
strace -e trace=openat ./readconf
```

```
openat(AT_FDCWD, "conf/settings.conf", O_RDONLY) = -1 ENOENT (No such file or directory)
cannot start
```

(The lines for the shared libraries, which every program opens first, are
left out here.) The kernel knows which file was asked for, even when the program does not
say. On FreeBSD and illumos the tool for this is `truss`, on the other BSDs
`ktrace` with `kdump`; those were not run for this page.

## The instruments, and what each cannot see

| Instrument | Shows you | Does not show you |
|---|---|---|
| The message itself, read to its last line | what the program or the tool already knows | anything it was not written to say |
| A print statement | the value at one place you chose | the places you did not choose; and it changes timing |
| An assertion | the first moment a belief of yours is false | beliefs you did not write down |
| A sanitizer build (`-fsanitize=address,undefined`) | the bad access itself, with the line, even when the run "works" | faults on paths the run did not take |
| A debugger (`gdb`) | the stack and the variables where the program stopped | where the damage was done, if that was earlier |
| `valgrind` | heap errors and leaks, without rebuilding | most errors in local arrays; leaks of memory still reachable at exit |
| A system-call trace (`strace`) | what the program asked of the kernel, and the answer | anything that happens without a system call |
| `git bisect` | which change brought the failure | why; and nothing, if the failure is not steady |
| A second compiler or a second system | assumptions you did not know you had | which of the two is right |
| Your record | that you have been here before | — |

Two rows deserve a sentence. On memory, [LOW_MEMORY.md](LOW_MEMORY.md) notes
that a leak checker reports zero for memory that is still reachable when the
program ends, so a lifetime mistake needs a different instrument. On the
second system, the project's rules file (`CLAUDE.md`) calls a non-Linux row "a
defect detector, not compatibility work"; [ANECDOTES.md](ANECDOTES.md) 97 is
one that it detected.

## When the fault is not where you are looking

Sometimes the program is right and something around it is wrong. These are
the cases that cost days, because every instrument is pointed at the wrong
thing.

- **The test is wrong.** A test is a program too, and it builds its own
  instruments: a file, a server, a count. Check that the instrument exists
  before you believe what the test says about the product. A command that
  prepares a test and sends its own errors to `/dev/null` hides the one
  message you needed.
- **The tool stopped it.** When a runner reports that it killed a test at a
  deadline, believe nothing printed after that line. The lines below it are
  what a killed program leaves, and they often look like a finding. Ask
  first: was it slow, or stuck?
- **The same message is not the same cause.** If you reproduce a failure
  from another machine at home, you have shown that your change removes
  *your* reproduction. Whether it is that machine's failure, only that
  machine can say.
- **A mechanism that fits the number is not yet the cause.** An explanation
  that accounts for 64 where 32 was expected has passed one test. Make it
  predict a second number, and measure that.
- **One reading is not a measurement.** Repeat it. If it moves, the spread
  is your first result.
- **A limit fired.** [ANECDOTES.md](ANECDOTES.md) 64 and 120 are both about
  a time limit that changed the answer: once by ending a run, once by merely
  being there.
- **Your instrument was in the way.** [ANECDOTES.md](ANECDOTES.md) 1: output
  that seemed cut off had been written and then undone, because the log
  lived inside the directory the program was allowed to roll back.

## With an agent beside you

Module 4 says it in one line: use the agent as an instrument, not as an
oracle. In practice:

- Give it the **observation**, word for word, and ask what experiment would
  separate two explanations. Do not ask it for the bug.
- Do not let it repair before step 2 is done. A model handed a symptom will
  produce a plausible change at once, and a plausible change that makes the
  symptom go away is how a cause stays in the program.
- Ask it to argue the other side: "what would make this explanation false?"
  [GROUNDED_DISCOURSE.md](GROUNDED_DISCOURSE.md) has the moves.
- When jichi itself is the thing being debugged, look at what it left
  behind before asking anyone: [OBSERVABILITY.md](OBSERVABILITY.md) for the
  logs and journals, and `--dump-requests`, described in
  [DOCTOR.md](DOCTOR.md), for the exact bytes that were sent to the model.

## When you are stuck

- **Write the question down.** What you expected, what you saw, what you
  have ruled out. This ends more searches than any tool.
- **Halve something.** The input, the program, the history, the
  configuration. There is nearly always one more thing that can be cut in
  two.
- **Change where you stand.** Another compiler, another machine, another
  input size, the newest version and the oldest. A fault that will not show
  itself from here may be plain from there.
- **Distrust the last thing you were sure of.** List what you have assumed
  and not looked at. The cause is usually on that list, near the top.
- **Stop.** The pause is part of the work, as *ma* (間（ま）) is part of the
  music. Write the next step down, and take it in the morning.

## The honest limits of this page

- **One small example on one machine.** It shows the order of work and four
  instruments. It does not show a race between threads, a fault that
  appears only under load, or a failure in a program you cannot rebuild.
- **The tools are named for Linux.** The BSD and illumos equivalents are
  mentioned and were not run.
- **The section on faults outside the program is drawn from this project's
  own recent mistakes.** It is a list of what went wrong here, not a survey.
- **It was drafted with the help of a model**, and every command in it was
  run before it was written down.

## Read further

- [curriculum/04-debugging-as-science.md](curriculum/04-debugging-as-science.md):
  the stance, the record, and a graded task whose first suspect is wrong.
- [DEBUGGABLE_DESIGN.md](DEBUGGABLE_DESIGN.md): building so that the cause is
  easy to find.
- [ANECDOTES.md](ANECDOTES.md): more than a hundred real searches, with
  their wrong turns (primary).
- [TESTING_RUNBOOK.md](TESTING_RUNBOOK.md) §9: do not report a defect you
  have not reproduced.
- [OBSERVABILITY.md](OBSERVABILITY.md) and [DOCTOR.md](DOCTOR.md): reading
  what jichi leaves behind.
- *The Practice of Programming*, Kernighan and Pike (secondary): its chapter
  on debugging is the short classic, in C. The entry is in
  [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md), section 1.
- The manuals of the tools themselves (primary): `man gdb`, `man valgrind`,
  `man strace`, `git help bisect`.
