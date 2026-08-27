# The lost first send, explained — and it is the harness, not jichi

*2026-08-17, on threadwork (Linux 7.0.0-29, x86-64), jichi at `730d8ee`. Written
because `docs/PLATFORMS.md` carried this as "open, and deliberately not guessed at"
and it is now measured. The conclusion is the opposite of the one I set out to
confirm, which is the reason to write it down.*

## What was open

The OpenBSD row (M461) reached the `accessible` driver and stopped there:

> the `accessible` driver's first send is lost on OpenBSD. The transcript is 193
> bytes — banner, the typed line echoed by the tty, then the prompt drawn *after*
> it — and it is **byte-identical** with a 1,200 ms and a 5,000 ms pre-send delay,
> so it is not a race. `jc_term` enters raw mode with `TCSAFLUSH`, which discards
> pending input, and that is a plausible mechanism but does not explain the delay
> result.

The hypothesis I started with was that `TCSAFLUSH` was a latent bug on every
platform, on the precedent of the two findings from the same row that were exactly
that (an inherited ignored SIGPIPE; an orphaned pipeline). **It is not.**

## The mechanism, measured

`enter_raw` is called from **two** places, and they are not the same call:

| Call site | Purpose | Flush |
|---|---|---|
| `jc_term_read_key` (`jc_term.c:177`) | one keypress — the `[y]/[n]/[a]/[e]/[v]` approval prompt | `TCSAFLUSH`, and the M254 safety property: stray type-ahead must not answer a y/n |
| `jc_term_readline` (`jc_term.c:916`) | the line editor, **once per prompt** | `TCSAFLUSH`, which discards anything typed before the prompt existed |

So input typed between process start and the first `jc_term_readline` is queued by
the tty in canonical mode, echoed by the tty, and then discarded when the editor
enters raw mode. **Reproduced on Linux** by shrinking the driver's pre-send delay:

| pre-send delay | requests the mock received | verdict |
|---:|---:|---|
| **1 ms** | **0** | the send is discarded |
| 100 ms | 1 | answered |
| 300 ms | 1 | answered |
| 600 ms | 1 | answered |
| 1,200 ms | 1 | answered |

The window on this bench is under 100 ms, which is why no Linux run ever showed it.
The transcript at 1 ms is the same shape OpenBSD produced:

```
hello there                                    <- the tty's own echo, canonical mode
jichi - interactive agent. Type /exit to quit...  <- then the banner
[auto:m:0%] >                                  <- then the prompt, drawn after it
```

**And that explains the byte-identical result at both delays**, which is what had
made the OpenBSD finding look like "not a race". It is a race — just not one either
delay won. On that guest jichi needs longer than 5 s to reach its first prompt, so
1,200 ms and 5,000 ms land on the same side of it and produce the same transcript.

## Why this is not a jichi defect

The discard is deliberate, and its reasoning is already written down — in the header
of `tests/smoke/typeahead.sh`, describing why M254's mid-turn type-ahead was made
opt-in at M257:

> keystrokes typed while the agent worked were echoed into the streamed output by
> the tty and then DISCARDED by the `TCSAFLUSH` at the next raw-mode entry. Since
> M257 the feature is **OPT-IN** (`--type-ahead`), because jichi cannot guarantee
> the typing is visible in every window and **input you cannot see is input you
> cannot correct.**

That principle covers the startup window too: a line typed before the prompt exists
is echoed by the tty, not by jichi's wrap-aware editor, and cannot be corrected
there. Accepting it silently would be the thing M257 decided against.

**So the OpenBSD stop is a result about the harness.** The `accessible` driver sends
after a fixed `delay 1200` with no `expect` on the prompt — deliberately, as its own
header says, because that arm runs with colour **on** and the prompt is therefore
hard to match as a plain substring. A fixed delay is a bet on jichi's startup time,
and it is a bet that loses on a slow guest.

## What is actually owed

One asymmetry, and it is the only part that looks like a defect:

**Mid-turn type-ahead has four notices; the startup discard has none.**
`JC_MSG_QUEUED`, `JC_MSG_QUEUE_FULL`, `JC_MSG_QUEUE_UNSENT` and
`JC_MSG_QUEUE_DROPPED` all exist so a user learns what happened to what they typed.
At the first prompt the line is echoed, discarded, and nothing is said —
`grep -ciE "queue|discard|ignored|unsent"` over the transcript returns **0**. The
user watched their input appear and then vanish.

Whether to close that with a notice is a design question rather than an obvious fix:
detecting "there was pending input" before flushing needs a non-blocking probe of the
tty that the current code does not do, and M257's own principle argues the input
should not be *accepted* — only, perhaps, acknowledged. Recorded in
`docs/DEFERRED.md` rather than guessed at.

## What this note does not claim

- It does **not** claim the OpenBSD stop is now reproduced *on OpenBSD*. The
  mechanism is reproduced on Linux and the transcript shapes match; confirming it
  there needs the row re-run, which is cheap (`scripts/tier-v-openbsd.sh --reuse`)
  and has not been done.
- It does **not** claim jichi takes >5 s to reach its first prompt on that guest —
  that is the inference the byte-identical result forces, not a measurement.
- It does not touch the FreeBSD `setup_keyfile` stop, which is a different open
  finding with a different candidate cause (that row ran with no multiplier at all
  until M464, i.e. at the tightest possible deadlines).

---

## CORRECTION (M467, same day): this explained a real mechanism, not this failure

**OpenBSD's `accessible` stop was not caused by what this page describes.** With one
harness fix it passes on the platform — all 8 checks — and so do `paste_special`,
`prefix_churn` and `advice`.

The actual cause was `ptydrive` treating a zero-length read on the pty master as EOF.
On OpenBSD, before the child has opened the slave, `select()` reports the master
**readable** and `read()` returns **0 with errno 0** — identical to a closed slave — so
any script opening with `expect` took the first zero read for the child's death and
stopped reading. Measured side by side against Linux, which reports not-readable for
the same window; reproduced with `cat` as the child, so it was never about jichi.
Full write-up: [`the-zero-read-that-meant-two-things`](2026-08-17-the-zero-read-that-meant-two-things.md).

**What survives from this page:** the flush mechanism is real. `jc_term_readline`
does flush once per prompt, and on *Linux* a 1 ms pre-send delay really does reach the
mock with 0 requests where 100 ms reaches it with 1. That measurement stands.

**What does not:** the inference that it explained the OpenBSD row. The tell was in the
evidence and I did not weigh it — a lost *send* cannot produce a transcript of **0 of 0
bytes**, because a flush affects input and the driver was waiting for output. Having
published a plausible mechanism made it harder to drop than a stranger's guess would
have been, and it cost the next sitting two wrong hypotheses before anyone pointed the
harness at a child that was not jichi.

**One consequence does carry over.** A send issued before the slave is open is still
lost on OpenBSD (0 ms → 0 bytes, 200 ms → 14), so the runbook's *human-scale delays
between sends* is a correctness requirement there rather than a courtesy.
