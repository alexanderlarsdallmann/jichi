# Built to be debugged — design that lets a failure be found

**Who this is for.** Someone who has spent an evening looking for a fault
and wants the next one to take ten minutes; and anyone designing a program
that other people, or an agent, will have to diagnose. For the search
itself, read [DEBUGGING.md](DEBUGGING.md) first. **After reading** you can
look at a design and say how long a failure in it would take to find, and
change the design so that the answer is shorter. **You need first:**
[DEBUGGING.md](DEBUGGING.md), whose small example this page continues.
**Where it fits:** after [DEBUGGING.md](DEBUGGING.md); before
[HARDENING.md](HARDENING.md), which is about faults that someone causes on
purpose.

Every program fails. What a design decides is how far the failure travels
before anyone notices, and how much it carries with it when it arrives. A
program is *debuggable* when a failure is noticed close to its cause and
arrives with enough evidence to name that cause. Neither happens by itself.
Both are built in, mostly in small and unglamorous ways, and mostly before
the first failure.

## 見える化（みえるか） — making it visible

The Toyota workshops, where the 改善（かいぜん） of
[PHILOSOPHY.md](PHILOSOPHY.md) is daily practice, have a habit that comes before any
improvement: make the problem visible to everyone, *mieruka*
(見える化（みえるか）). A fault that can be seen gets repaired. A fault that
cannot be seen gets worked around, and the workaround becomes part of the
job.

Three more of their words name the three things this page asks of a design,
and each is something you can check a program for.

- **自働化（じどうか）, *jidōka*.** The character in the middle is written with
  the sign for a person: automation that knows when to stop. A machine that
  detects something abnormal stops itself, so that it does not go on making
  bad parts. In a program: **stop at the fault, not three functions later.**
- **アンドン, *andon*.** The board above the line that shows where a problem
  is. In a program: **say what happened, where, and to whom it matters.**
- **ポカヨケ, *poka-yoke*.** A fitting shaped so that the part cannot be put
  in the wrong way round. In a program: **make the mistake impossible, or
  caught by a machine before a person has to look.**

The first page's rule was 現地現物（げんちげんぶつ）: go and look. This page is about
building the thing so that there is something to see when you get there.

## The example, built again

[DEBUGGING.md](DEBUGGING.md) repaired its program with one condition, and
noted that the repaired program cuts a long word short without a word. Run
on `"an extraordinary day"` it prints `extraor`. That is no longer a crash.
It is worse in one respect: a wrong answer that looks like a right one, with
exit status 0.

The same program, built to be debugged:

```c
/* longest2.c -- the same program, built to be debugged */
#include <stdio.h>
#include <string.h>

#define WORD_MAX 7              /* letters; the buffers hold one more */

/* Returns 0 when every word fitted, or the number of words that were too
 * long. A word that is too long is never copied in part. */
static int longest(const char *text, char *out, size_t cap)
{
    char word[WORD_MAX + 1];
    size_t n = 0;
    size_t best = 0;
    int too_long = 0;
    int skipping = 0;
    const char *p;

    out[0] = '\0';
    for (p = text; ; p++) {
        if (*p == ' ' || *p == '\0') {
            word[n] = '\0';
            if (skipping) {
                too_long++;
            } else if (n > best && n + 1 <= cap) {
                best = n;
                memcpy(out, word, n + 1);
            }
            n = 0;
            skipping = 0;
            if (*p == '\0') {
                break;
            }
        } else if (n < WORD_MAX) {
            word[n++] = *p;
        } else {
            skipping = 1;
        }
    }
    return too_long;
}

int main(int argc, char **argv)
{
    char out[WORD_MAX + 1];
    int status = 0;
    int i;

    for (i = 1; i < argc; i++) {
        int too_long = longest(argv[i], out, sizeof(out));

        printf("%s\n", out);
        if (too_long > 0) {
            fprintf(stderr, "longest: %d word(s) longer than %d letters "
                    "were left out of argument %d\n", too_long, WORD_MAX, i);
            status = 1;
        }
    }
    return status;
}
```

```sh
cc -std=c89 -pedantic -Wall -Wextra -o longest2 longest2.c
./longest2 "an extraordinary day"
```

It prints `day` on standard output, and on standard error
`longest: 1 word(s) longer than 7 letters were left out of argument 1`,
and exits with status 1. Four things changed, and none of them is clever.

1. **The limit has a name.** `WORD_MAX` appears once; the two buffers are
   sized from it. In the first version the number 8 stood in two places
   that had to agree and were not told so.
2. **The function knows the size of what it writes into.** `cap` is passed
   in. The first version wrote into `out` on trust.
3. **A word that does not fit is never half used.** A truncated word would
   be a plausible wrong answer. A missing word with a message is an obvious
   one.
4. **The failure leaves the function as a value, and the program as a
   status.** The caller decides what to print. A script that runs the
   program can tell from the status alone that something was left out.

This is the tree's own habit in small. [PHILOSOPHY.md](PHILOSOPHY.md) puts
it as "Every buffer is bounded, every cap is named, and when output is
truncated the truncation says so".

## Ten things to build in

Each has the principle, where this tree does it, and what it costs. The
costs are real; a design that paid none of them would also tell you nothing.

**1. Stop at the fault.** Check a bound where the write happens, a status
where the call returns, an assumption where it is made. Every function a
bad value passes through before it is noticed is a function you will have
to read later. *In this tree:* the rules file asks for `jc_snprintf` and
never `sprintf`, so that an overflow cannot start. *Cost:* a check at
every boundary, and the question of what to do when it fires.

**2. Carry the failure as a value, and keep two kinds apart.** "The
machinery broke" and "the thing you asked for did not happen" are different
facts and want different handling. *In this tree:* `jc_status` for the
first, a result with `is_error` for the second
([reading/tsuiseki-04-the-call-that-was-wrong.md](reading/tsuiseki-04-the-call-that-was-wrong.md)).
*Cost:* every caller looks at both.

**3. Never swallow an error.** The most expensive characters in a shell
script are `2>/dev/null` on a command whose failure matters. The same goes
for a status that is not looked at and a `default:` that does nothing. If
an error truly does not matter, say why in a comment, so that the next
reader can check the claim. *Cost:* some noise, which you then have to make
meaningful.

**4. Make every limit say so.** A cap that cuts silently turns a failure
into a wrong answer. The mark must be in the output itself, where a person
or a model will read it, not only in a log. *Cost:* the mark takes room and
needs its own test.

**5. Give each fact one home.** If the size of a buffer, the name of a
setting or the spelling of a format lives in one place, a fault in it has
one address. If it lives in three, you will repair two. *In this tree:*
the diff preview and the edit go through the same code
([EDITING.md](EDITING.md)), so that what is approved is what is written.
*Cost:* the shared part must be written to be shared.

**6. Leave a trail that says who wrote it.** A log line is evidence only if
you can tell which build produced it. *In this tree:* all four sinks record
the build that wrote them ([OBSERVABILITY.md](OBSERVABILITY.md)), and
`--dump-requests` writes the exact bytes sent to the model
([DOCTOR.md](DOCTOR.md)), because a log of the *content* once led to a
wrong conclusion about the *request*. *Cost:* disk, and the care not to
write secrets into the trail.

**7. Keep the trail out of reach of the thing it watches.** A recorder
inside the blast radius records its own destruction at best.
[ANECDOTES.md](ANECDOTES.md) 1 is this project's first war story for a
reason: a log kept inside the workspace was rolled back together with the
work it described, and for a while the output looked truncated. *Cost:* a
second place to manage.

**8. Make it repeatable.** A failure you can run again is half found.
That means no hidden inputs: the time, the random seed, the network, the
order of a directory listing. Where the real peer cannot be made to
repeat itself, give the tests a scripted one. *In this tree:* the smoke
tier talks to `mockmodel`, a scripted model server that captures every
request ([TEST_TIERS.md](TEST_TIERS.md)); the real model is driven
separately, because a scripted peer proves only what was scripted.
*Cost:* the fixture has to behave like the real thing, and keeping it so
is work ([TESTING_RUNBOOK.md](TESTING_RUNBOOK.md) §3c).

**9. Make it reversible.** If trying something can be undone, you will try
more things, and debugging is trying things. *In this tree:* snapshots,
`/undo` ([SNAPSHOTS.md](SNAPSHOTS.md)) and `/rewind` ([REWIND.md](REWIND.md)).
[JOURNEY.md](JOURNEY.md) says fearlessness is downstream of reversibility.
So is a short search. *Cost:* storage, and a clear statement of what is
*not* covered by the undo.

**10. Keep the instruments one flag away, and test the instruments.** A
build with the sanitizers on should be a single option, not a project.
*In this tree:* `make SAN=1`, and `make smoke-faults`, which builds with
`FAULT=1` so that tests can reach the error paths on purpose; both are
in [BUILD.md](BUILD.md). And an instrument must be able to say
"I could not measure": the smoke runner, when it kills a test at its
deadline, prints that the output below "is NOT EVIDENCE about jichi". A
tool that knows the limits of its own report is rare, and worth copying.
*Cost:* a second and third way to build, each of which must stay working.

## Questions to put to a design

Ask them before the first failure. Each has an answer you can point at, or
it has none yet.

- If this value is wrong, **where is it noticed**? How many calls after it
  was made?
- When this fails, **what does the user see**? What does the log hold? Can
  the two be matched to each other?
- Can I **run the failure again** tomorrow, from what was recorded?
- Which errors here are **dropped**? For each: is the reason written down?
- Is there a number, a name or a format here that **lives in two places**?
- If the program is killed half way, **what is left**, and does it say that
  it is half?
- Can a newcomer **turn the checks on** with one command?
- Which mistake in using this is **easy to make**? Can the shape of the
  interface rule it out?

## What it costs, and when to stop

Checks cost time to write and a little to run. Messages cost thought. A
trail costs disk and has to be kept free of secrets. Two builds cost twice
the upkeep. None of this is free, and a program can be over-instrumented
until its own diagnostics are the thing that fails.

A fair rule: pay for a check where a wrong value would otherwise travel,
for a message where a person will otherwise have to guess, and for a trail
where a failure cannot be made to happen again. Elsewhere, wait for the
first failure to show you where the dark places are, and then light that
one. [ANECDOTES.md](ANECDOTES.md) is, read this way, a list of the places
this project lit only after falling over in them.

## The honest limits of this page

- **Ten is this page's count**, and the ten are habits of one tree written
  in C by a small team. A system with many services, or one that must
  never stop, needs things this page does not mention.
- **The Japanese words are used as [PHILOSOPHY.md](PHILOSOPHY.md) demands**,
  each next to something a program can be checked for. Their meanings were
  checked on 2026-11-25 against Toyota's own description of its production
  system and against secondary accounts, not against the original writings.
- **The example is sixty lines.** It shows the direction. It does not show
  what the same care costs in sixty thousand.
- **It was drafted with the help of a model**; the program in it was
  compiled and run before it was written down.

## Read further

- [DEBUGGING.md](DEBUGGING.md): the search this page tries to shorten.
- [OBSERVABILITY.md](OBSERVABILITY.md): what jichi leaves behind, sink by
  sink.
- [HARDENING.md](HARDENING.md): the same bounds and fences, seen as
  defence.
- [TESTING_RUNBOOK.md](TESTING_RUNBOOK.md) §2 and §3c: proving an
  instrument can fail, and making a fixture behave like the real peer.
- [ANECDOTES.md](ANECDOTES.md) 1: the recorder inside the blast radius
  (primary).
- *Hints for Computer System Design*, Butler Lampson (primary), and *The
  Practice of Programming*, Kernighan and Pike (secondary). Both entries
  are in [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md), section 1.
