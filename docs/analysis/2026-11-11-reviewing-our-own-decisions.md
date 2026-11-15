# Reviewing our own decisions: what one day's design review of jichi found, and how to do one yourself

*Analysis, 2026-11-11, written at the operator's request: "document all of this honestly, and thoroughly
for self-learners, too!" It was written by the supervisor of the harness-port drive, a Claude Code session,
which also did the review. Some of the mistakes below are that supervisor's own, and they are told as such.
The tree reviewed is jichi's master at commit 53a1ef4e. Every number comes from that tree or from a
measurement named beside it. The exercises in §10 were run on Linux exactly as printed, before this page
was written. Nothing described here was fixed at the time of writing; §9 says who fixes what.*

## 1. Who this is for, and how to read it

This page is for you if you maintain a program for longer than a few weeks. After a while a program rests
on decisions that nobody remembers making. This page shows one way to look at them again, and what
turned up when jichi did.

You need a C compiler and a shell. Each idea is explained when it first appears, and the glossary (§11)
repeats the short definitions.

- **For the story:** §2 and §3.
- **For the method, which you can copy:** §4.
- **For the ideas:** §5.
- **For the findings:** §6.
- **For the mistakes, and what went right:** §7 and §8.
- **To see the effects on your own machine:** §10 takes about fifteen minutes.

A companion page, [the worker that went in circles](2026-11-11-the-worker-that-went-in-circles.md),
tells what happened earlier the same day and why this review was asked for.

## 2. In one paragraph

jichi is written in C89, an old and small version of the C language, and it runs on many systems. The
operator asked for a review of the decisions behind that: the language, Linux, the other platforms, the
processor types and the ways people use jichi. Five read-only helper agents each read one area of the
source and the documentation. The supervisor then re-read about 55 of their claims by hand and measured
whatever was cheap to measure. The review found four defects that could be shown by a small experiment,
and about sixteen further findings. It also found that the project's most basic decisions were never
written down with their alternatives. The clearest result is about C89: it has no 64-bit integer, and
five of the findings come from that single gap.

## 3. Why a review, and why now

Earlier that day one of the team's worker agents had gone in circles for four hours (the companion page
tells that story). While reading the code for that case, the supervisor noticed something unrelated: the
code that watches the workers stores a clock value in a type that is too small on 32-bit machines. That
was found by accident. The operator then asked for a review on purpose: look at the design decisions,
their alternatives and what follows from them, "specifically regarding c89, linux, possibly the other
supported platforms, and systems (risc-v, arm64), and the various use-cases for jichi".

There was one rule: the review reads and measures. It does not change jichi, and it does not write the
fixes. The team's agents do that.

## 4. How the review was done

You can copy this for a project of any size. The steps are in the order they were taken.

1. **Pin one copy.** The supervisor made a fresh clone of the repository at one commit, 53a1ef4e, in a
   scratch directory. Nothing else works in that copy, so every line number means the same thing all day.
2. **Read what was already known.** An earlier analysis, of 2026-10-15, had covered portability. The
   review's first question was what had become of that analysis's findings.
3. **Split the work by area, not by file.** There were five areas: the C89 decision; the processor types;
   Linux and the other operating systems; the use cases; which decisions are recorded at all.
4. **Give each area to a read-only helper.** Each helper agent got the same rules:
   - read only inside the pinned copy;
   - build and run nothing;
   - cite every claim as a file and a line;
   - mark general knowledge as such;
   - say what you could not check.
5. **Re-check by hand.** A helper produces plausible text quickly, and that alone is not a record. The
   supervisor opened the cited lines for about 55 claims, the ones the review rests on.
   - All but three held as stated.
   - One was wrong: a helper said the README never mentions the third program, `jichi-harness`. It does,
     in its milestone banner.
   - Two counts came out different, and the review does not use them.
6. **Measure what is cheap.** A claim like "this breaks on a 32-bit machine" is one small program away
   from being a measurement. §6 and §10 show the experiments.
7. **Mark every sentence.** In the review each statement is marked as measured, read by the reviewer,
   read only by a helper, or general knowledge. A reader can then tell how far to trust each one.
8. **Keep the raw material.** The helpers' reports, the list of what was re-checked, the small programs
   and the build logs are kept together, outside the repository.

**What it cost:** an evening. The helpers ran for 15 to 23 minutes each, at the same time. The re-check
and the measurements took longer than the helpers did.

## 5. The ideas you need

### 5.1 Versions of C

C has been standardised several times. **C89** (also called ANSI C or C90) is the first standard. **C99**
added many things people now think of as plain C: `//` comments, declaring a variable where you first
use it, a guaranteed 64-bit integer type (`long long`), and a safe `snprintf`. Later versions (C11, C17,
C23) added more.

A compiler can be told which version to accept. With `gcc -std=c89 -pedantic`, anything newer is refused
or warned about. jichi builds this way, and treats warnings as errors.

### 5.2 How wide is a `long`?

C does not fix the size of its integer types. A `long` holds at least 32 bits, and how many it really
holds depends on the machine:

- On 64-bit Linux, a `long` has 64 bits.
- On 32-bit Linux, a `long` has 32 bits. Its largest value is 2,147,483,647.

C89 has no integer type that is 64 bits everywhere. A program that needs big numbers on every machine
has three choices in C89, and jichi uses all three:

- **Use `long`** and accept that it is small on 32-bit machines.
- **Use `double`,** a floating-point type. It holds whole numbers exactly up to 2^53, which is about
  9,000,000,000,000,000.
- **Build the number from bytes** by hand.

Each choice is safe only as long as everyone remembers its limit.

### 5.3 Byte order

A number that needs several bytes can be stored with its lowest byte first (**little-endian**; x86, ARM64
and RISC-V do this) or its highest byte first (**big-endian**; IBM's s390x does this). Inside one program
this never matters. It matters when the bytes leave the program: into a file, over a network, or into the
operating system's kernel.

### 5.4 Feature-test macros

A C library's header files declare more functions than any one standard allows. A program says which set
it wants by defining a **feature-test macro** before it includes a header. For example,
`-D_XOPEN_SOURCE=600` asks for the functions of one version of the X/Open standard and
`-D_XOPEN_SOURCE=700` for those of the next. A function that arrived in the later version is hidden under
the earlier setting. A strict build treats a call to a hidden function as an error.

### 5.5 Signed and unsigned `char`

C also leaves open whether a plain `char` can be negative. On x86 it can. On ARM64 and RISC-V Linux it
cannot. A test like `c < 0` on a plain `char` therefore behaves differently on those machines.

### 5.6 A default, and the stand-in that replaces it in tests

Programs often start other programs. A test usually replaces the other program with a small **stand-in**
that records how it was called. That makes the test fast and safe. It also means that the real program is
never asked whether the call makes sense.

## 6. What the review found

### 6.1 The first decisions were never written down

jichi keeps a decision register: a page where each decision is listed with the alternatives that were
rejected. The register says of itself that it starts at milestone M293. The roadmap, which tells the
project's history, has its first entry at M7.

So the decisions made at the very start have no record of the alternatives: C89, POSIX systems only,
libcurl as the only library, no threads, a plain Makefile. Their reasons were written later, in the
reading guides. One of those guides says so openly: "the tiers justify the standard, not the reverse".

**C99 was never weighed.** The guides weigh C++, Zig, Ada and Rust against C89. They describe C99 and do
not compare it. The stated main reason for C89 is reach: old and small machines have compilers that only
understand C89.

The review tested that reason against the tree itself:

- **The compilers in the platform table** are all versions of gcc, clang or zig. Each of those families
  also accepts C99.
- **The tree is already valid C99 and C11.** In a scratch copy, the supervisor changed the build's
  `-std=c89` to `-std=c99`, built everything with warnings as errors, and ran the unit tests. Then the
  same for `-std=c11`. Both builds had no warnings, and both passed all 37,546 unit checks, the same
  count as the normal build.

What was *not* checked: whether each old compiler in the table really accepts this tree as C99. That
takes one build per machine, and nobody has done it.

**What this means.** "Reach" does not tell C89 and C99 apart here. The reasons that remain are the other
two the guides give: a small language is easier to hold to its rules, and easier to learn from. Those
are real reasons. They are not the ones the pages lead with.

### 6.2 One missing type, five findings

This is the review's clearest result. Five findings come from the same cause: C89 has no 64-bit integer
(§5.2).

**The clock (measured).** jichi's clock function returns milliseconds as a `double`. Almost all of jichi
keeps that value as a `double`. The code that runs the worker agents converts it to a `long` in six
places.

- On a 64-bit machine that is harmless.
- On a 32-bit machine a `long` cannot hold more than 2,147,483,647 milliseconds. That is 24.86 days. The
  clock counts from when the machine started.
- So on a 32-bit machine that has been running for 25 days, the conversion gives a nonsense value. The C
  standard calls this undefined behaviour: the program may do anything.
- In the experiment (§10, exercise A), both the start time and the current time became -2147483648. The
  difference was 0. A rule "stop a worker after 30 minutes" then never fires.

**The kernel field (measured).** jichi confines a worker's shell with a Linux feature called Landlock. To
turn it on, jichi hands the kernel a 64-bit number that lists the forbidden operations. C89 has no 64-bit
type, so the code builds the eight bytes by hand, lowest byte first.

- On a little-endian machine that is exactly what the kernel expects.
- On a big-endian machine the kernel reads its own byte order, so it sees a completely different number
  (§10, exercise C).
- jichi then refuses to start the worker. That is the safe failure: nothing runs unconfined. But on such
  a machine no parallel write task could run at all.

The change that introduced the hand-packing ended its commit message with the words "BE is unpinned --
follow-ups". BE means big-endian. No follow-up was recorded anywhere else, and so none happened.

**Three more, read in the code and not run:**

- **File sizes** are returned as a `long`. On a 32-bit machine a file over 2 GB reports a wrapped size.
  One check that holds back very large new directories can be passed that way.
- **A timeout in seconds is multiplied by 1,000** to get milliseconds, after the value was only checked
  to fit before the multiplication.
- **Token counts sent by a model server** are converted from `double` to `long` with no check. A server
  that sends an absurd number reaches the same undefined conversion as the clock.

**The lesson.** None of these is hard to fix. What they share is that the limit lived only in people's
heads. A rule the compiler cannot check needs a lint, a test or a type.

### 6.3 A default that nobody ever ran (measured)

The team is porting a set of shell scripts into a C program, the harness. One script watches for a
finished batch of work and starts the test gate for it, as a background service, through the system
tool `systemd-run`.

The C port keeps the tool's name and options as its default. Then it adds three more words: the service's
name, a description, and the command. The original script passes the name and the description as
*options* (`--unit=...` and `-p Description=...`). The port passes them as plain words.

`systemd-run` treats the first plain word as the command to run. So it tries to run a program called
like the service, and answers: "Failed to find executable". No service starts (§10, exercise E).

**Why nothing caught it:**

- The port's tests replace `systemd-run` with a stand-in (§5.6). The stand-in records the words and is
  content.
- The supervisor ran the old script and the new program side by side for two days, 300 times, and they
  agreed every time. But that comparison ran the new program in a dry mode, which decides what to do and
  does not do it.

The defect sits exactly in the step that neither check performs.

**Added later the same evening: a second fault behind the first.**

The operator asked what this finding means for running the harness on other systems. To answer, the
supervisor read the code again and ran the real tool in more shapes. That turned up a second fault, and a
third.

- **The command is passed as one word.** The port joins the program, the commit and the batch number into a
  single word with spaces in it. The real tool then looks for a program whose name is that whole phrase, and
  answers "Failed to find executable /bin/true x y" (§10, exercise E, step 3). So a fix of the name and the
  description alone would still fail.
- **A test holds that fault in place.** The test's stand-in writes every word it receives on a line of its
  own. Two checks then look for the whole command on one line. That only passes if the command arrives as
  one word, so the test demands the broken form. The product's code comment names that test as the reason
  for the form.

The review had run the default shape, seen it fail at its first word, and stopped there. **A measurement
that stops at the first error cannot see the second one.** Once a fix gets past the first error, measure
again.

### 6.4 A flag that hides a function (measured)

A recent change made jichi's platform file call `mkdtemp`, a function that creates a private temporary
directory. The function needs the newer feature setting (§5.4). The file asks for it, but only if nothing
was asked for before.

On some older systems the build itself asks for the older setting, for another reason. There the older
setting wins, `mkdtemp` is hidden, and the strict build stops with an error (§10, exercise D).

The file's own comment says the older setting "is NOT enough for mkdtemp". The knowledge was there. The
two places that set the level did not know about each other.

### 6.5 Pages that drifted away from the code

A page is correct on the day it is written. Some examples the review found, each checked against the
code:

| A page says | The code does |
|---|---|
| the daemon keeps the conversation between requests | it starts a fresh process for each request |
| telemetry is opt-in | it has been on by default since M599 |
| sub-agents may nest 1 level deep by default | the default is 2 |
| there are two memory arenas | there are three |
| the shell is not confined | it is, by default, where Linux offers Landlock |
| `make` builds "both binaries" | it builds three |
| macOS "should build; unverified" | another table on the same page says "Partly verified, and Driven" |

The platform records had drifted too. Three rows cite commit numbers that no longer exist in the main
history, because the history was rewritten after the rows were recorded. One row names the wrong board.
Two pages count the verified platforms differently, 19 and 22.

### 6.6 What turned out fine

A review also reports what it looked for and did not find.

- **Signed and unsigned `char` (measured).** The whole unit suite, built so that `char` is unsigned as on
  ARM64 and RISC-V, passes all 37,546 checks with no new warning. A helper also searched the code for the
  usual traps and found none.
- **Byte order elsewhere.** Apart from the kernel field, nothing writes whole numbers out in the machine's
  own byte order, except one cache file that carries a tag saying which order it used.
- **Page size and alignment.** The code asks the system for the page size and aligns its memory blocks
  conservatively.
- **The older analysis's security findings.** Most of them had been fixed in the weeks between.

## 7. What went wrong, by role

### 7.1 The supervisor (the author)

- **I counted five where there were six.** I searched for the clock function's name next to a conversion
  and found five places. A helper found a sixth, where the value is first stored in a variable and
  converted one line later. I had already written "five" into a document for the team, and had to add a
  dated correction. *A search finds the pattern you typed, and nothing else.*
- **I took 300 agreements for coverage.** My side-by-side comparison of the old script and the new program
  had agreed on every one of 300 runs. I reported that number each hour as if it covered the program. It
  covered the deciding and never the doing (§6.3). *Before trusting a count, ask what the instrument
  actually reached.*
- **I proposed a change that an earlier decision had already rejected.** In a design note for the team I
  suggested a new name for one of the reasons a worker stops. The decision register already records why
  new names of that kind are refused: older versions read an unknown name as "finished". I found that
  only on a second reading of the code, and withdrew the proposal in writing. *Read the register before
  proposing, not after.*
- **I nearly repeated a helper's wrong claim.** One helper wrote that the README never names the third
  program. It reads well, and it fits the story. It is false. Only the re-check caught it.
- **I stopped at the first error** (added later the same evening).
  - I ran the service-start default once, saw it fail at its first word, and wrote the finding.
  - I also told the team's lead that a configured override could carry a correct command until the fix. It
    can only if the override is a small wrapper script; no list of the tool's options will do. I had not
    tried that either.
  - Both came to light when the operator asked a follow-up question and I measured again (§6.3).

  *A failure you have measured tells you nothing about what comes after it.*

### 7.2 The helpers

- They were right about almost everything that was re-checked: about 55 claims, one wrong, two counts
  off.
- Two helpers found the two most serious defects independently of each other and of the supervisor.
  Agreement between independent readers is evidence. It is not proof, and each finding was still
  measured.
- A helper cannot say how sure it is in a way you can use. That is what the marks are for.

### 7.3 The project

- **A follow-up that lived only in a commit message** was never followed up (§6.2).
- **A decision register that starts late** leaves the largest decisions without a record (§6.1).
- **Rules stated more strongly than they are kept.** One page says "Never call `sprintf` anywhere". Four
  calls are allowed, on purpose, in one file, and a lint lists them. The rule and its exception should
  be stated together.

## 8. What went right

- **jichi asks what a system can do, not what it is called.** The build probes for each feature. That is
  why it runs on so many systems at all.
- **The confinement fails safe.** On the big-endian machine the worker is refused, not run unconfined.
- **The register is honest about its limits.** It says where it starts. That sentence is what made the
  gap visible.
- **The lints caught the reviewer too.** One of the project's documentation checks refused one of the
  review's pages, because it quoted another program's option. The line was reworded.
- **The earlier analysis was acted on.** Most of its security and correctness findings were closed in the
  four weeks between the two reviews.

## 9. What changes

The operator decided on the same evening, in these words: "Please, follow your recommendations". The
recommendations were:

1. **The service-start default (§6.3) is fixed before the watcher is switched from the old script to the
   new program.** The team's lead has this as a condition.
2. **The clock, the kernel field and the hidden function (§6.2, §6.4) are small fixes** for the team's
   agents, each with a test that fails first.
3. **The drifted pages (§6.5) are corrected in one batch.**
4. **The first decisions get their rows in the register (§6.1),** written now and labelled as written
   after the fact.

At the time of writing, items 1 and 2 were with the team and not yet built.

## 10. Try it yourself

Each exercise is a few lines. Work in an empty directory:

```sh
mkdir review-exercises
cd review-exercises
```

If anything goes wrong, close the terminal and delete the directory. Nothing here needs `sudo`, and
nothing changes your system.

### Exercise A: a clock that no longer fits

1. Save this as `ms.c`:

```c
#include <stdio.h>
#include <limits.h>

int main(void)
{
    double day30 = 30.0 * 86400.0 * 1000.0;     /* milliseconds in 30 days */
    volatile double start = day30;              /* a task starts after 30 days of uptime */
    volatile double later = day30 + 7200000.0;  /* two hours after that */
    long a = (long)start;
    long b = (long)later;

    printf("long is %lu bytes, LONG_MAX is %ld\n", (unsigned long)sizeof(long), LONG_MAX);
    printf("start=%ld later=%ld elapsed=%ld ms\n", a, b, b - a);
    printf("a 30-minute timeout %s\n", (b - a > 1800000L) ? "fires" : "NEVER fires");
    return 0;
}
```

2. Build and run it for your machine:

```sh
gcc -std=c89 -pedantic -Wall -Wextra ms.c -o ms64 && ./ms64
```

   On a 64-bit system you should see:

```
long is 8 bytes, LONG_MAX is 9223372036854775807
start=2592000000 later=2599200000 elapsed=7200000 ms
a 30-minute timeout fires
```

3. Build and run it as a 32-bit program:

```sh
gcc -m32 -std=c89 -pedantic -Wall -Wextra ms.c -o ms32 && ./ms32
```

   You should see:

```
long is 4 bytes, LONG_MAX is 2147483647
start=-2147483648 later=-2147483648 elapsed=0 ms
a 30-minute timeout NEVER fires
```

   If the compiler complains about a missing header instead, your system lacks the 32-bit libraries (on
   Debian and Ubuntu the package is `gcc-multilib`). Skip the step and read the output above.

**What to notice:** the compiler gave no warning in either build. Two hours passed, and the program
measured none.

### Exercise B: is `char` signed?

1. Save this as `uc.c`:

```c
#include <stdio.h>

int main(void)
{
    char c = (char)0xE9;    /* the byte for an accented e in Latin-1 */

    if (c < 0) {
        printf("char is signed here: c is %d\n", (int)c);
    } else {
        printf("char is unsigned here: c is %d\n", (int)c);
    }
    return 0;
}
```

2. Build it twice, once as your machine does it and once with `char` forced to unsigned:

```sh
gcc -std=c89 -pedantic -Wall -Wextra uc.c -o uc && ./uc
gcc -std=c89 -pedantic -Wall -Wextra -funsigned-char uc.c -o uc-unsigned && ./uc-unsigned
```

   On x86 the first prints `char is signed here: c is -23`. The second prints a warning, "comparison is
   always false due to limited range of data type", and then `char is unsigned here: c is 233`.

**What to notice:** with the option, the compiler tells you about the trap. That is why building a whole
program once with `-funsigned-char` is a cheap test for ARM64 and RISC-V.

### Exercise C: byte order

1. Save this as `be.c`:

```c
#include <stdio.h>
#include <string.h>

/* Pack a value into 8 bytes, low byte first, the way jichi's fence code does. */
static void pack_low_first(unsigned char *dst8, unsigned long v)
{
    int i;
    memset(dst8, 0, 8);
    for (i = 0; i < (int)sizeof v; i++) {
        dst8[i] = (unsigned char)((v >> (8 * i)) & 0xff);
    }
}

int main(void)
{
    unsigned long v = 0x7fffUL;
    unsigned char packed[8], native[8];
    int i;

    if (sizeof v != 8) {
        printf("this exercise needs a 64-bit long\n");
        return 2;
    }
    pack_low_first(packed, v);
    memcpy(native, &v, 8);      /* the bytes the machine itself uses for v */
    printf("packed:");
    for (i = 0; i < 8; i++) printf(" %02x", packed[i]);
    printf("\nnative:");
    for (i = 0; i < 8; i++) printf(" %02x", native[i]);
    printf("\n%s\n", memcmp(packed, native, 8) == 0 ? "the same" : "DIFFERENT");
    return 0;
}
```

2. Build and run it on your machine:

```sh
gcc -std=c89 -pedantic -Wall -Wextra be.c -o be && ./be
```

   On x86, ARM64 or RISC-V you should see both lines as `ff 7f 00 00 00 00 00 00`, and `the same`.

3. This step is optional and needs two tools, `zig` and `qemu-user-static`. Build the same file for a
   big-endian machine and run it in the emulator:

```sh
zig cc -target s390x-linux-musl -std=c89 be.c -o be-s390x && qemu-s390x-static ./be-s390x
```

   You should see:

```
packed: ff 7f 00 00 00 00 00 00
native: 00 00 00 00 00 00 7f ff
DIFFERENT
```

**What to notice:** the same source, the same value, and a different answer. A test that compares the
packed bytes against a fixed list would pass on both machines, and would be wrong on one.

### Exercise D: a function hidden by a flag

1. Save this as `fm.c`:

```c
#include <stdlib.h>

int main(void)
{
    char name[] = "/tmp/exampleXXXXXX";
    return mkdtemp(name) == 0;
}
```

2. Compile it (only compile; `-c` means it is not linked or run) with the older setting:

```sh
gcc -std=c89 -Wall -Werror -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600 -c fm.c
```

   On Linux with glibc you should see an error: "implicit declaration of function 'mkdtemp'".

3. Compile it with the newer setting:

```sh
gcc -std=c89 -Wall -Werror -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=700 -c fm.c
```

   Now it compiles, and prints nothing.

**What to notice:** the source did not change. Only the flag did, and the flag can come from a build
file far away from the code.

### Exercise E: a default that the real tool refuses

This one needs a Linux system with systemd. The names are made up and harmless.

1. Run the tool the way the port's default does, with the name, a description and the command as plain
   words:

```sh
systemd-run --user --collect --nice=19 zz-design-review-probe "a probe description" "true a b"
```

   You should see: `Failed to find executable zz-design-review-probe: No such file or directory`.

2. Run it the way the original script does, with the name and the description as options:

```sh
systemd-run --user --collect --unit="zz-design-review-probe2" --nice=19 -p "Description=design review probe" /bin/true
```

   You should see a line that starts `Running as unit: zz-design-review-probe2.service`. The service runs
   `/bin/true`, ends at once, and is removed again.

3. Now keep the options, but pass the command and its arguments as **one** quoted word, the way the port
   passes them (added later the same evening, §6.3):

```sh
systemd-run --user --collect --unit="zz-design-review-probe3" --nice=19 -p "Description=design review probe" "/bin/true a b"
```

   You should see: `Failed to find executable /bin/true a b: No such file or directory`.

**What to notice:** a stand-in in a test would have accepted all three. Only the real tool tells them
apart. And step 3 only shows its error once step 1's error is gone.

### A checklist for reviewing your own project

1. Pin one commit, and work in a copy that nothing else touches.
2. Write down the decisions everything rests on. For each, find where its reason is written. If you
   cannot find it, that is your first finding.
3. For each stated reason, ask what would show it to be false. Then try exactly that. "The old compilers
   need this" became one changed flag and one build.
4. Look for quantities that can outgrow their type: times, sizes, counts. Search for conversions between
   types. Then search again a second way.
5. For every program yours starts, run the real one once, with harmless arguments.
6. For every check that says "all agree", write down what it does not exercise.
7. Re-check a sample of everything a helper tells you, whether the helper is a person, a tool or a
   model. Mark what you did not re-check.
8. Report what you looked for and did not find.

## 11. Glossary

| Word | Meaning |
|---|---|
| C89, C99, C11 | Versions of the C language standard, from 1989, 1999 and 2011. |
| `-pedantic` | A compiler option that warns about anything outside the chosen standard. |
| `long`, `double` | An integer type of at least 32 bits; a floating-point type that is exact for whole numbers up to 2^53. |
| 32-bit, 64-bit system | Here: whether a `long` and a pointer have 32 or 64 bits. |
| undefined behaviour | Something the C standard does not define at all; the program may do anything. |
| little-endian, big-endian | Lowest byte first; highest byte first. |
| feature-test macro | A definition such as `_XOPEN_SOURCE` that tells the C library which functions to declare. |
| Landlock | A Linux feature that lets a program restrict which files it and its children may change. |
| stand-in | A small replacement for a real program in a test. |
| dry run | A run that decides what to do and does not do it. |
| lint | An automatic check of the source or the documentation for one kind of mistake. |
| decision register | A page that lists decisions with the alternatives that were rejected. |
| pinned copy | A copy of the source at one fixed commit. |

## 12. Sources

- **The tree:** jichi master at 53a1ef4e.
- **The review itself:** `docs/internal/2026-11-11-design-review.md`, with its findings F1 to F20 and its
  measurement programs. The register row is `docs/DEFERRED.md` def-378. Its section 12 (added later the
  same evening) has the second and third faults of §6.3, measured with `systemd-run-shapes.sh`.
- **The earlier analysis:** `docs/internal/2026-10-15-portability-analysis.md`.
- **The code named in §6:**
  - the clock conversions: `src/tools/jc_tool_parallel.c` and `src/chat/jc_app.c`;
  - the kernel field: `src/util/jc_landlock.c`;
  - the service-start default: `src/harness/jc_hp_watchgate.c`;
  - the hidden function: `src/platform/jc_platform_posix.c` and the `Makefile`.
- **The written case for C89:** `docs/reading/fukabori-01-why-c89.md` and `docs/C_STANDARDS.md`.
- **The decision register:** `docs/DECISIONS.md`.
- **The supervisor's notes to the team's lead:** SV:1909, SV:1926 and SV:1939.
