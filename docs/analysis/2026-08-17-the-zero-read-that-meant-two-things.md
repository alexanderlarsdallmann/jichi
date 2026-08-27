# The zero-length read that meant two different things (2026-08-17)

*threadwork; OpenBSD 7.9 amd64 under KVM. M467. Written because this one harness
defect accounted for **21 of the 23** failing drivers in OpenBSD's first
run-to-completion, and because I reached for two wrong explanations before measuring
the right one.*

## The one fact

Before a child has opened the pty slave, the master looks different on the two
platforms. Same program, run on each:

| | OpenBSD 7.9 | Linux 7.0 |
|---|---|---|
| `select()` on the master, child alive but not yet open | **readable** | not readable |
| `read()` in that window | **returns 0, errno 0** | (not reached) |
| when the child writes at t≈1 s | *never seen* — the parent had already concluded EOF | `read()==18`, the data |

Raw output of the probe, identical source both sides:

```
=== OpenBSD ===                       === Linux ===
master fd=4 slave=/dev/ttyp0          master fd=3 slave=/dev/pts/5
  t=   0ms read=0 errno=0 <-- EOF       t=   0ms select=0 not-readable
  t= 300ms read=0 errno=0 <-- EOF       t= 300ms select=0 not-readable
  t= 600ms read=0 errno=0 <-- EOF       t= 600ms select=0 not-readable
  t= 900ms read=0 errno=0 <-- EOF       t= 900ms read=18 errno=0 data
  ...                                        [HELLO-FROM-CHILD]
```

So **a zero-length read means "the slave is not open yet" on one platform and "the
child is gone" on the other**, and nothing in the return value distinguishes them.

## What it broke

`tests/tools/ptydrive.c` had:

```c
n = (long)read(master, chunk, sizeof(chunk));
if (n <= 0)
    return -1;              /* EOF, or EIO after child exit (Linux) */
```

The parenthesis is the whole defect: the comment names the platform its assumption
holds on. Every smoke driver whose script **opens with `expect`** — most of them —
therefore raced the child's `open()` of the slave on OpenBSD, took the first zero read
for death, stopped reading, and reported:

```
ptydrive: line 1: expect "] " timed out (30s, child exited)
ptydrive: transcript tail (0 of 0 bytes):
```

**21 drivers**, all reporting some variant of that, and all of them reproducing with
`cat` as the child — so it was never about the program under test.

## Two wrong explanations, in order

Recorded because the failure modes transfer better than the fix.

1. **"It is jichi's `TCSAFLUSH`."** The pre-existing story (M464) was that
   `jc_term_readline` flushes pending input once per prompt, so a send before the first
   prompt is discarded. That is a real mechanism, it is reproduced on Linux, and it is
   **not this**: it cannot explain a zero-byte transcript, because a flush affects input
   and the drivers were waiting for *output*.
2. **"It is EIO."** BSD does return `EIO` on a master with no slave open, so I wrote the
   guard for `errno == EIO`, built it, shipped it to the guest — and nothing changed.
   The measurement then showed `read()` returning **0**, not `-1`, in that window. The
   guard was correct code for a branch that never executed.

What broke the loop was writing a twenty-line probe and running it on both platforms.
Two hypotheses cost more than the measurement would have.

## The fix, and why each half is load-bearing

Tolerate the ambiguity **only before the first byte**, and only a **bounded** number of
times:

```c
if (!g_saw_output && g_zero_reads < PT_SLAVE_WAIT) { nap(20ms); return 0; }
return -1;
```

- **Only before the first byte:** after any output the slave has certainly been opened,
  so a later zero read or EIO really is death and must stay fast.
- **Bounded (100 × 20 ms ≈ 2 s):** a child that exits having written nothing is a
  *genuine* EOF, and Linux reports it through this same path. Unbounded tolerance trades
  one platform's bug for every platform's diagnostics — measured: a silently-dying child
  took the full **30 s** expect without the bound and **2 s** with it.
- **The nap:** OpenBSD keeps reporting the master readable, so returning straight to the
  caller spins hot until its deadline.

Verified two-sided on Linux (`cat` with a zero-delay send: ok; silent death: rc=3 in 2 s;
a child whose first write is delayed 1 s: ok) and on OpenBSD, where `tui_basic` went from
three failed checks to `ok 1`/`ok 2`/`ok 3`.

## What this does NOT fix, and it matters

**A send issued before the slave is open is still lost.** After the fix, `cat` with a
0 ms pre-send delay still fails — but the diagnostic changed from
`timed out (8s, child exited)` to `timed out (8s)`, i.e. the false EOF is gone and what
remains is a genuinely lost write. Measured on OpenBSD: a 0 ms pre-send delay reads 0
bytes, 200 ms reads 14.

That is the same shape as the M464 lost-first-send, and it means the guidance in
`docs/SESSION_RUNBOOK.md` — *give PTY scripts human-scale delays between sends* — is a
correctness requirement on OpenBSD rather than a courtesy. It is also why `accessible`
is expected to stay red: its complaint is about content, not about a zero-byte
transcript.

## The transferable lesson

This is the fourth finding in three days where **a Linux-only behaviour was written down
as a portable one**, next to `_SC_NPROCESSORS_ONLN`, `INADDR_LOOPBACK`, `SIGWINCH`, GNU
`\b`, and an assignment prefixed to a shell function. The pattern is now specific enough
to state as a rule: **when a comment names the platform an assumption was verified on,
that is not documentation, it is an unfiled bug report.**

---

## Addendum: the second harness bug, and the "deadlock" that was never jichi's

Fixing the zero read took OpenBSD from **178 of 201 drivers** to **196**. Five remained,
and two of them fell to one more shell difference — measured the same way, on both
shells, with the same script:

```sh
( cd /tmp && sleep 20 ) &  p=$!
```

| shell | what `$!` names |
|---|---|
| dash (Linux `/bin/sh`) | **`sleep`** — implicit exec |
| bash | **`sleep`** — implicit exec |
| **OpenBSD ksh** (`/bin/sh`) | **`sh`** — the subshell; the command is a grandchild |

With an explicit `exec`, every shell names the command.

So on OpenBSD every `kill`, `kill -INT` and `wait` on that pid hit **a shell, not the
process under test**. Two drivers, two shapes:

- **`parallel_abort`** SIGINT-ed what it believed was jichi and reported *"parent did not
  exit within 15s of SIGINT — abort/reaping deadlocked"*. jichi never received the
  signal. **The driver was accusing the agent of a defect the harness caused.**
- **`stop_reason_capped`** killed the subshell and left the `jichi daemon` grandchild
  running, so `timeout(1)` — which waits for the whole process group on the BSDs — failed
  a driver whose every check had passed (`ok 1`…`ok 5`, then FAILED).

Both pass with `exec`. **198 of 201.**

**`signals.sh` had used `exec` since it was written** — its entire subject is signal
delivery, so it could not have worked otherwise — and the idiom was never generalised to
the five other backgrounded sites. That is the same shape as the `\b` finding earlier the
same day: the rule existed, in the file whose purpose forced it, one row short of the
family. `smoke_lint` check 15 now requires it, proven two-sided.

### What this does and does not say about Guix

`parallel_abort` on **Guix System** reports the identical sentence, and M458 filed it as a
jichi defect in the `spawn_parallel` reaping path. Earlier today I wrote — and committed —
that the two platforms therefore *share a cause*. **That was over-claimed.** Guix's
`/bin/sh` is bash, and bash performs the implicit exec (measured above), so this
explanation does **not** transfer to it.

The honest state: OpenBSD's instance is **closed, as a harness defect**. Guix's is
**still open and still unexplained** — but it is now cheap to settle, because the `exec`
fix is shipped and a single re-measure (`sh tests/smoke/run.sh parallel_abort` in a Guix
guest) distinguishes "the same harness bug after all" from "a real reaping defect". Until
that runs, neither claim is earned.

### The three that remain on OpenBSD

| driver | complaint | reading |
|---|---|---|
| `sessions_footprint` | `could not read the /context arena gauge (before='' after='')` | real; the driver now *runs* and fails on content rather than on a zero-byte transcript |
| `turn_scratch` | `could not read the /context turn-scratch gauge` | same family as above |
| `setup_keyfile` | `3 line(s) over 76 columns: [J  test command … [31C[?2004h` | the check counts **bytes**, and the quoted line is mostly escape sequences (`[31C`, `[?2004h`). A column assertion measuring bytes, exposed by a different renderer — a check defect, not a wrap defect |

None is a zero-byte transcript, and none blames a signal. That is the difference between a
platform with 23 failures and a platform with three specific, readable ones.
