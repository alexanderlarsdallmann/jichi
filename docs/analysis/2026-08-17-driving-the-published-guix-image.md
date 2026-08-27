# The published Guix image can be driven — up to the bootloader (2026-08-17)

*threadwork. `guix-system-vm-image-1.5.0.x86_64-linux.qcow2`, downloaded by the
operator. Written because M450 recorded a conclusion that is **wrong at the
bootloader level**, and because the four attempts below narrow the remaining
obstacle to something mechanical rather than something unknown.*

## What M450 said, and what is actually true

> "The published guix-system-vm-image is a live graphical desktop with no serial
> console and no sshd (M450), so it cannot be driven."
> — `docs/plans/2026-08-guix-bench-system.scm`, header

**GRUB in this image both writes to *and reads from* the serial line.** Measured:
booting with `-nographic -serial mon:stdio -display none` renders the GRUB menu over
serial (after `error: no video mode activated`), and **pressing `e` opens the entry
editor** — so the keystroke arrived. That is a two-way channel, which is the thing
M450's sentence denies.

What is true is the narrower claim: **the kernel** is not told to use serial. The entry
carries `quiet` and no `console=`, so once GRUB hands over, the line goes silent. That
is a one-argument problem, not an undrivable image.

## The boot entry, extracted (this is the data a rig needs)

Captured from the editor screen, GRUB's `\` line-wrap markers removed:

```
setparams 'GNU Guix 1.5.0'
search --fs-uuid --set 38af4c98-f6b1-2062-a75a-14c038af4c98
linux  /gnu/store/pbpfl2ll5qz36bqfnl3g3vvag764714h-linux-libre-6.17.12/bzImage \
       root=38af4c98-f6b1-2062-a75a-14c038af4c98 \
       gnu.system=/gnu/store/3clkkwp48fi3i6iybh4sgylqf7yhkg77-system \
       gnu.load=/gnu/store/3clkkwp48fi3i6iybh4sgylqf7yhkg77-system/boot \
       modprobe.blacklist=usbmouse,usbkbd quiet
initrd /gnu/store/py7wvw1qf0091hj7vlf6n9kxc4vrc7pq-raw-initrd/initrd.cpio.gz
```

Store paths are version-specific; a rig must *discover* them the way this did (press
`e`, read the screen) rather than hardcode them — the same rule as everywhere else in
this campaign.

## Four attempts, and what each proved

| # | Approach | Result | What it establishes |
|---|---|---|---|
| 1 | boot with `-serial mon:stdio`, no input | GRUB menu renders, then silence after the 5 s countdown | serial **output** works; the kernel is the silent half |
| 2 | `e`, one `Ctrl-N`, `Ctrl-E`, append `console=ttyS0` | `error: no such device: console=ttyS0,` | serial **input** works — and the editor's first line is `setparams`, so one `Ctrl-N` lands on `search`, not `linux` |
| 3 | as 2 with **two** `Ctrl-N` | *same* error, text again on the `search` line | line navigation is not advancing as `Ctrl-N` implies; do not assume emacs semantics over a serial GRUB |
| 4 | GRUB command line (`c`) with the exact paths, chunked sends and 400–800 ms delays | `error: unspecified search type`, then `error: file`, then `error: you need to load the kernel first` | GRUB received `search` **without** its `--fs-uuid` token: bytes are being dropped on input even when chunked |

Attempt 4's error is the informative one. `unspecified search type` means GRUB parsed a
bare `search`, so the first argument never arrived — dropped input, not a wrong command.

## What to try next, in order of expected cost

1. **QEMU monitor `sendkey` instead of serial typing.** One keypress per monitor
   command, acknowledged by the monitor, with no serial timing to lose a race with.
   Deterministic where attempts 2–4 were probabilistic.
2. **Bypass the bootloader.** The kernel and initrd paths are known now, so QEMU's
   `-kernel` / `-initrd` / `-append` boots directly with `console=ttyS0` and no GRUB at
   all — which is exactly how `scripts/tier-v-tiny.sh` already works. Extracting the two
   files needs read access to the image's ext4: `qemu-nbd` wants root, but attaching the
   qcow2 as a **second disk to the existing Debian rig** (`scripts/tier-v-vm.sh`) needs
   none, and that rig already exists.
3. **Build the headless image properly** from
   [`../plans/2026-08-guix-bench-system.scm`](../plans/2026-08-guix-bench-system.scm),
   which no longer hardcodes one host's ssh key (M466). This is the originally intended
   path and it needs a working `guix`.

**The one step that needs a human** is five minutes at this image's *graphical* console:
enable `sshd` and drop in a key, or run the `.scm` build from inside. After that
everything is automatable. That is a fair ask precisely because it is bounded — and it
is the operator's machine.

## Why this is no longer urgent

The Guix row existed on the critical path for one reason: `parallel_abort`'s
abort/reaping deadlock (M458) was believed isolated to Guix. On the same day this was
written, the OpenBSD row — run to completion for the first time with
`JC_SMOKE_KEEP_GOING=1` — reported **the identical failure**, *"parent did not exit
within 15s of SIGINT — abort/reaping deadlocked"*, and OpenBSD has an unattended rig
that rebuilds from an 11 MB ISO in one command. So the deadlock can be iterated against
without Guix, and a Guix row is now worth having for its own sake (a fifth libc
environment, non-FHS) rather than to unblock a bug.

**Hygiene note:** every boot here ran against a `qemu-img` **overlay**, never the
operator's download. The original `.qcow2` is byte-unchanged.

---

## RESOLUTION, same day: it booted, and the deadlock does not reproduce

Everything above about "what resists" was **my own error**, and the answer to the
question the row existed for is *no defect*.

### The hour was an arithmetic mistake, not a broken image

`qemu-img info` prints:

```
virtual size: 20 GiB (21517828096 bytes)
```

21,517,828,096 bytes is **20.04** GiB — qemu rounds the display. I read the rounded
word and passed `20G` to `qemu-img create`, which is 21,474,836,480 bytes, so **the
overlay was 41 MB smaller than the image it backed**. That clipped the tail off
partition 2, the ext4 superblock then claimed more blocks than its device had, and
Guix's boot-time fsck correctly refused:

```
Guix_image: The filesystem size (according to the superblock) is 5242880 blocks
The physical size of the device is 5232384 blocks
Either the superblock or the partition table is likely to be corrupt!
File system check on /dev/vda2 failed
Spawning Bourne-like REPL.
```

5,242,880 − 5,232,384 = 10,496 blocks × 4096 = **42,991,616 bytes — exactly the
truncation.** Recreating the overlay with **no size argument at all**, so it inherits
the backing file's exact byte count, booted straight to the Xfce desktop.

Read out of the guest's VGA text buffer at physical `0xb8000` via the QEMU monitor
(`xp /4000xb 0xb8000`), which is worth remembering as a technique: it gives the exact
on-screen text of a guest with no serial console and no ssh.

**So M450's conclusion is partly retracted.** True: the image ships no sshd, and GRUB
must be told about serial (it renders to VGA whenever a display exists, and only falls
back to serial under `-nographic`). False: that it therefore *cannot be driven*. It
boots, and a 9p share plus one script is enough to run the gates in it.

### The measurement, at HEAD f025185

Built and run inside the booted image via `guix shell`, tree copied off 9p into /tmp:

| | |
|---|---|
| Guix | 1.5.0, `guix describe` commit `d58da8a` |
| kernel | Linux **6.17.12-gnu** x86_64 |
| `/bin/sh` | **bash 5.2.37** (not ksh — which is why M467's `exec` fix cannot apply here) |
| `cc` / `c99` | **neither exists** — M458's lesson, now measured rather than recalled, and the reason every `make` here passes `CC=gcc` |
| toolchain | `gcc-toolchain`, curl **8.6.0** from the store, 148.3 MB of substitutes |
| build | `-std=c89 -pedantic -Wall -Wextra -Werror` — **clean** |
| unit suite | **11,594 checks, 0 failures** |

And the driver the row existed for:

```
--- smoke: parallel_abort
1..2
ok 1 - SIGINT-ed parent exited (reaped both stalled children)
ok 2 - exit was prompt (1s) -- the abort path, not the 120s watchdog
```

Neighbours, for context: `parallel_hang` 2/2, `signals` 4/4, `stop_reason_capped` 5/5
(it fails on OpenBSD, which confirms *that* failure as ksh-specific). `parallel_merge`
skipped for want of `git` — my package list omitted it.

### I predicted the opposite, in writing

Before the run I wrote: *"`parallel_abort` I expect to still fail … which means the M458
finding is probably a **real** defect."* That was wrong, and the reasoning shows how:
I had correctly named three suspects at M466 — `448616d` (children inheriting jichi's
ignored SIGPIPE, so a pipeline producer spins on `EPIPE` instead of dying), `0d5b030`
(a timed-out capture orphaning the rest of its pipeline) and `ac166d5` (the harness
reaping only the last mock) — and then at M467, having just diagnosed two
platform-specific harness bugs, I narrowed onto *those* explanations and forgot my own
list. `448616d` is the best structural fit for a parent that never leaves `waitpid`.

**Honest caveat on strength.** This is not a byte-identical re-run of the M458 row: that
was a Guix System built from the recorded config; this is `guix shell` inside the
published desktop image with 6 vCPUs and 8 GB. A pass in a *different* environment is
weaker evidence than a pass in the same one. Combined with three named fixes landing in
between, "does not reproduce at HEAD" is the defensible claim — not "was never real".
