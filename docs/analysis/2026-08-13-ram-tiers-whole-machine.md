# RAM tiers on whole machines, 2026-08-13 — the recipe built, and what the floor is actually made of

Run on **threadwork** (AMD Threadripper PRO 7955WX, 32 threads, 252 GB, kernel
7.0.0-29) to close the two [`DEFERRED.md`](../DEFERRED.md) rows that named this
machine as their unblocker: *"Run jichi on a machine with ≤64 MB of physical RAM"*
and *"Build a minimal single-TLS-backend libcurl and link it into a static musl
jichi"*. M403 graded the RAM tiers and could measure only the top two. This note
measures the bottom two, and finds that the number worth knowing is not jichi's.

Every figure was produced by a script in the tree, and every verdict is read from a
**positive marker in the output, never an exit code** — the rule
[`TEST_INTEGRITY.md`](../TEST_INTEGRITY.md) states and that M403 broke twice in one
hour writing the harness this extends. Where a figure disagrees with a published
one, both are kept (the M259 lesson).

## 1. The ≤64 MB tier's own recipe now exists

M403 measured the two *ends* of the tier's recommendation and wrote the honest
bound: *"A real minimal-libcurl build lands between 0.5 and 8.6 MB, and until
somebody builds one that range is the whole of what is known."* It is **804 KB**.

[`scripts/minimal-curl.sh`](../../scripts/minimal-curl.sh) builds it. Measured with
`SIZE=1`, **curl 8.18.0 on every column** so the configuration is the only variable,
and an **isolated `HOME`** so the operator's own config cannot move the numbers:

| | system libcurl | minimal libcurl | **minimal + static musl (mbedTLS)** | M403: musl, no libcurl |
|---|---:|---:|---:|---:|
| binary on disk | 1,159 KB | 1,159 KB | **2,698 KB** | 1,051 KB |
| shared libraries (`ldd`) | 34 | 9 | **0** | 0 |
| `--version` peak RSS | 10,012 KB | 5,988 KB | **804 KB** | ~500 KB |
| `map` peak RSS | 10,884 KB | 6,800 KB | **2,208 KB** | 2,048 KB |
| `doctor` peak RSS | 17,240 KB | 12,388 KB | **4,864 KB** | 4,505 KB |
| can make a model call? | yes | yes | **yes** | **no** |

Two things worth reading twice. The complete recipe lands **within ~300 KB of the
curl-free build while keeping networking** — the thing M403 could only bound. And
**on-disk size and resident footprint move in opposite directions**: the static
binary is 2.3× larger on disk because curl and mbedTLS are *inside* it, and its
resident set is 12× smaller because there is no shared TLS chain to map. Anyone
optimising for one of those two numbers should know they are choosing.

The model call is measured, not inferred: a real turn against a mock model returned
its answer marker with one request received.

**Honest limits.** The glibc rung uses OpenSSL, because that is what
[`LOW_MEMORY.md`](../LOW_MEMORY.md) recipe literally names; the musl rung uses
**mbedTLS**, because OpenSSL's perl-driven cross configuration is an afternoon per
target while mbedTLS is a `CC` override — and for a ≤64 MB target mbedTLS is the
better-matched library anyway. Those are **two different claims** and must not be
quoted as one number. The verified turn is **HTTP**, so no TLS handshake is in these
figures.

## 2. Whole machines: a stock distro stops long before jichi does

[`scripts/tier-v-vm.sh`](../../scripts/tier-v-vm.sh) gained rows V2g–V2k: the same
image, seed, runbook and gate as the published 256 MB V2e row, with only the ceiling
changed — the identity is the point, because numbers only compare if the procedure
did.

| ceiling | verdict |
|---:|---|
| 256 MB | V2e, published (M272/M273) |
| 192 MB | gate **ok** *(measured on the pre-M429 tree; not re-run)* |
| **160 MB** | gate **ok** — `make check-target` green: 11,342 checks + smoke OK (174 drivers, 900 checks); footprint **8,632 / 14,864 KB**; guest saw 121 MB |
| 128 MB | **kernel panic** — `System is deadlocked on memory`, 0.91s in, unpacking its own initrd |
| 64 MB | **kernel never started** — sits in the GRUB menu; cannot load an 8 MB kernel + 32 MB initrd |
| 32 MB | same |

At 128 MB the kernel reserved **59,592K of the 130,516K** it saw, leaving 70,720K,
and died inside `do_populate_rootfs`. So the `~128 MB` tier **cannot be graded with
a stock cloud image at all**: what fails is the image, before any userspace exists,
which says nothing about jichi. The lowest ceiling a stock Debian 12 survives is
**160 MB**, and there it passes the entire gate.

The 128/64/32 MB rungs are properties of the **image**, not of jichi's tree, so they
were not re-measured after the rebase onto M429; the 160 MB row was.

Footprint compares directly with the physical boards: 8.6/14.9 MB here against the
Pi Zero 2 W's 8.9/16.3 MB (aarch64) and 6.9/13.1 MB (armhf).

## 3. Remove the distro, and the floor is the kernel

[`scripts/tier-v-tiny.sh`](../../scripts/tier-v-tiny.sh) boots a kernel plus a
busybox initramfs carrying a static jichi — no distro, no disk, no root on the host.

| ceiling | offline surfaces | + a verified model turn |
|---:|---|---|
| 128 MB | COMPLETE | COMPLETE |
| 96 MB | COMPLETE | COMPLETE |
| **80 MB** | COMPLETE | **COMPLETE** |
| **64 MB** | **COMPLETE** | panic |
| 48 MB | no kernel | — |

jichi's own gauge reports **1,160 KB** peak offline and **1,396 KB** with a turn —
under 2% of its own floor. That the rest is the kernel is not an inference; swapping
kernels moves the floor while jichi is byte-identical:

| kernel | `init_size` | offline floor |
|---|---:|---:|
| Debian 6.1.0-51 generic | 63 MB | 96 MB |
| Alpine 6.12.81 `virt` | 37.8 MB | **64 MB** |

**This extends M403 by one level.** M403: *the footprint floor is libcurl and its TLS
stack, not jichi.* Take that away (§1) and on a whole machine the floor is **the
kernel**.

**A caveat in jichi's favour, stated because it is real:** an initramfs is resident
by construction, and a real board with a rootfs on flash would not pay for it, so
this floor is pessimistic. **Against:** the host page cache still holds the images,
and the turn is HTTP. A VM is not a board.

## 4. Four cgroup floors, and two of them are filesystem measurements

[`tests/measure/ram_floor.sh`](../../tests/measure/ram_floor.sh) measured one
workload; the plan that asked for it
([`2026-07-hardware-testing.md`](../plans/2026-07-hardware-testing.md) V1) asked for
three. It now takes `--workload turn|doctor|units|smoke`. All re-measured on M429:

| workload | floor | previously published |
|---|---:|---|
| one headless turn | **3 MB** | 3 MB (M403) — reproduced exactly |
| `doctor` | **3 MB** | 5 MB (M265) — *not comparable, see below* |
| the unit suite | **72 MB** | 14 MB (M403) — *filesystem, see below* |
| the whole smoke tier | **56 MB** | ≤32 MB, "not bisected lower" |

**The unit suite's 72 MB is not a regression.** `tests/test_bounds.c:117` writes a
session fixture of `JC_READ_FILE_MAX + 4096` — 64 MiB + 4 KB — to prove
`jc_read_file` refuses an over-cap file, and it hardcodes `/tmp`. threadwork's
`/tmp` is **tmpfs**, and tmpfs pages are charged to the cgroup. Proven with jichi
entirely absent: a bare 64 MiB `dd` survives a 72 MB ceiling on tmpfs, is
OOM-killed at 64 MB, and survives 64 MB written to a disk-backed path, because disk
page cache is reclaimable and tmpfs is not.

So both figures are right and the number needs its filesystem stated: a disk-backed
`/tmp` reports jichi's working set (~14 MB); tmpfs reports `max(that, the fixture)`.
**Consequence for the tiers:** a board with a tmpfs `/tmp` needs **≥72 MB to run
`make check-target` at all**, whatever jichi costs. Check with
`findmnt -no FSTYPE /tmp` before quoting either. The smoke tier's isolated `HOME`
also lives in `/tmp`, so its 56 MB is very likely the same effect.

**The `doctor` floor is config-dependent, and the two figures are not comparable.**
`doctor` probes every configured server. Measured here, its peak RSS is **12,388 KB**
with an isolated `HOME` and **49,060–53,700 KB** against a real four-model
`~/.jichi` — roughly 37 MB of reachability probing. The 3 MB floor used a
deterministic one-model config; M265's 5 MB used something else. So M265's striking
observation — *a turn needs less RAM than the diagnostic that inspects it* — is
**neither confirmed nor refuted here**, and any published `doctor` footprint has to
name its config.

**One published claim needs its scope narrowed.** M265 recorded that *"constraint
costs almost nothing here: the same turn takes 0.13 s at a 3 MB ceiling versus
0.11 s unconstrained, with 0 major faults."* True of **one turn**. The whole smoke
tier at its floor takes roughly **twice** its unconstrained wall clock, so that
finding belongs to the workload it was measured on and does not generalise.

## 5. Instrument defects found, every one by an instrument disagreeing

Recorded because each was live at some point and each would have produced a
confident wrong number.

1. **A boot failure reported as a timeout.** `vm_wait_ssh` waited the full 600s and
   said "no ssh" — which reads as *the guest was slow* when the truth is *the image
   cannot boot that small*. Now two detectors (a kernel panic; a kernel that never
   started), each recording a `FINDING:` with the kernel's own memory figures and
   exiting **3** — distinct from a portability failure, because nothing about jichi
   was measured. 75s and rc=3 where it was 600s and rc=1.
2. **An early marker mistaken for completion.** The obvious `doctor` marker,
   `libcurl available`, prints on line **2**; a run OOM-killed two thirds through
   still prints it and would have scored COMPLETE. Now `" problems"`, from the final
   summary line, verified to be the only line containing the word.
3. **`grep -c … || echo 0` emits two lines**, so `[ "0 0" -eq 0 ]` died with
   *Illegal number* and the script took the wrong branch — reporting NO INIT for a
   guest whose console log was completely empty. A wrong verdict, not a wrong
   message.
4. **A turn that never happened scoring green.** Reaching the end marker sufficed
   even when the model call had failed; with `--turn` the answer marker is required.
5. **`/proc/self/status` read after the child exits** is the *shell's* peak, not
   jichi's. Replaced by jichi's own M180 `/context` gauge — the instrument the M272
   Pi rows already fell back to.
6. **A results file truncated per run** destroyed the offline row when the turn
   sweep followed it; the two modes now write separate files.
7. **qemu's stderr was discarded**, so "could not load kernel" would have been
   invisible.

Two were procedural errors of mine, and they are the same lesson twice. **`make`
does not track flag changes**, so a "restore the normal build" did nothing and a
guard was tested against the wrong binary — switching `CC` needs `make clean`.
And a concurrent `make clean` deleted `mockmodel` **mid-sweep**, producing a driver
failure that looked like a memory floor; a control run is what caught it, because
the dynamic build failed identically and thereby exonerated the static one. Sibling
of the M286 lesson that `make` does not build the test binary.

## 6. What this note does not license

- It does not say jichi runs on a **physical** 64 MB board. These are whole VMs on a
  large host; the page cache and the absence of competing pressure both flatter the
  result. [`PLATFORMS.md`](../PLATFORMS.md) keeps that distinction.
- It does not measure a **TLS** turn at any ceiling. HTTP only.
- It says nothing about **uClibc** or **a phone**, which remain never-compiled and
  never-run.
- The `units` and `smoke` floors are **filesystem-dependent** on this box, and are
  quoted with the filesystem named or not at all.
