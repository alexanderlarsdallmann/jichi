# Installing jichi on FreeBSD

**Who this is for:** anyone building jichi on FreeBSD. **You do not need C or
compiler knowledge** — you copy the commands, and after each one this page says what
you should see. NetBSD and OpenBSD are very close; until they have their own pages,
[`BUILD.md`](../BUILD.md) covers all three, and the differences are small (the
package command, and one or two facts noted below).

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:**
- A FreeBSD machine where you can run `pkg install` (as root, or with `sudo`).
- The jichi **source code** already on the machine — the folder with a `Makefile`,
  `src/` and this `docs/` in it.
- A model to talk to (a local [LM Studio](https://lmstudio.ai) or other
  OpenAI-compatible server, or a hosted model's key). Step 5 covers this.

**The one thing that trips everyone up first:** on FreeBSD you must type **`gmake`**,
not `make`. FreeBSD's own `make` cannot read jichi's build file and gives a confusing
error. Every build command below uses `gmake`.

**Where this fits:** the generic build is [`INSTALL.md`](../INSTALL.md); the build
reference is [`BUILD.md`](../BUILD.md).

## 1. Status

**Verified — the full gate, and Driven** (FreeBSD 15.1-RELEASE, clang 19.1.7). The
build is clean, the whole test suite runs, and a model has been driven on it. See its
row in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source with `gmake`. "Installed" means the program is built (and, if you
choose in Step 6, copied onto your `PATH`).

## 3. Before you start: open a terminal and find the source folder

1. **Open a terminal** and become able to install packages (`su` to root, or use
   `sudo` if it is installed).
2. **Go to the jichi source folder** — the one with the `Makefile`:
   ```sh
   cd ~/jichi        # replace with wherever your copy is
   ls
   ```
   You should see `Makefile`, `src`, `docs` among the names. If not, `cd` to the
   folder you unpacked or cloned jichi into.

## 4. The steps

### Step 1 — install what jichi needs

```sh
pkg install gmake pkgconf curl git
```

- **You should see** the packages resolve and install, then your prompt return.
- **`pkgconf` is not optional.** Without it, jichi builds but **cannot reach the
  network**, with no error to warn you. Step 3 catches this if it happens.

### Step 2 — build jichi (with `gmake`, not `make`)

In the source folder:

```sh
gmake
```

- **You should see** a short build (about 7 seconds on a normal machine), no lines
  containing `error`, then your prompt.
- **Check the programs were built:** `ls jichi jichi-convert` — both names should print.
- **If you typed `make` by mistake** and got a parse error partway through a file,
  that was FreeBSD's `make`. Use **`gmake`**.

### Step 3 — confirm the build can reach the network

```sh
gmake info | grep HAVE_CURL
```

- **You should see:** `HAVE_CURL = yes`.
- **If it is blank,** `pkgconf` was not installed, so the network library's headers
  (under `/usr/local`) were not found. Install `pkgconf` (Step 1) and run `gmake`
  again.

### Step 4 — run it once, with no model yet

```sh
./jichi --version        # -> a version line
./jichi doctor           # -> a health report; some warnings are normal before Step 5
```

You are confirming the program runs and prints a report, not that everything is green.

### Step 5 — point jichi at a model

The guided way:

```sh
./jichi setup
```

Answer the prompts (for a local LM Studio server, pick the OpenAI-compatible option
and give the address LM Studio shows, usually `http://localhost:1234/v1`). It writes
and validates the config for you.

- **To write it by hand instead:** create `~/.jichi` as shown in
  [debian-ubuntu.md](debian-ubuntu.md) Step 5 (identical on FreeBSD).

Then: `./jichi doctor` — the model warnings should be gone.

### Step 6 — your first turn

```sh
./jichi -p "say hello in one line" < /dev/null
```

- **You should see** a one-line reply. That is jichi working end to end.
- **If it errors,** run `./jichi doctor` and read the first failing line.

### Step 7 (optional) — install onto your PATH

```sh
gmake -j4 && sudo gmake install     # (or as root, without sudo)
```

Afterwards `jichi` works from any folder. Remove with `sudo gmake uninstall`.

## 5. Measured difficulties

### `make` gives a parse error in the middle of the build file
That is FreeBSD's `make` (bmake) trying to read a GNU build file. Use **`gmake`** —
this is the single most common first-build stop on the BSDs.

### `HAVE_CURL =` is blank
`pkgconf` is missing, so the compiler never found the network library's headers.
Install `pkgconf` and rebuild. A blank here is the difference between a working jichi
and a silent networkless one.

### A test driver fails and jichi looks wrong
Suspect the **test script**, not jichi: on the BSDs, almost every failure so far has
been a GNU-specific option in the test tier's own `grep`/`sed`/`awk` that a BSD
handles differently — not a defect in the program. The longer history (the seven
defects the FreeBSD row found) is in
[`platform-notes/freebsd.md`](../platform-notes/freebsd.md).

### The one FreeBSD-specific line in the source
There is exactly one `#if defined(__FreeBSD__)` in the whole tree
(`src/util/jc_proc.c`): it lets `<sys/procctl.h>` compile under jichi's strict build
flags, for the feature that hides your API key from other programs (M771 — FreeBSD's
`procctl` is the equivalent of Linux's `PR_SET_DUMPABLE`). `doctor` reports whether
that protection is on. You do not need to do anything about it.

## 6. Possible difficulties (not measured here)

- NetBSD and OpenBSD differ in small ways ([`BUILD.md`](../BUILD.md)): NetBSD also
  needs a `pkg-config`-style package and ships GNU grep; OpenBSD's `/bin/sh` is ksh.
  Their verdicts and numbers are in [`PLATFORMS.md`](../PLATFORMS.md).

## 7. Verifying the install

```sh
gmake check-target       # the unit tests + the shell test tier
./jichi doctor --live    # adds one real round trip, once a model is configured
```
Both should finish without failures. FreeBSD is already *Driven*, so this confirms
your build rather than breaking new ground.

## 8. Reporting back

The short row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full
result is [`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`BUILD.md`](../BUILD.md) — the three BSDs together, dependencies, and the `gmake` rule.
- FreeBSD Handbook, and the `pkg` and `procctl(2)` man pages (primary sources).
- [`PLATFORMS.md`](../PLATFORMS.md) — the verdict and the numbers.
- [`platform-notes/freebsd.md`](../platform-notes/freebsd.md) — the developer history: the seven defects this row found.
- [`HARDENING.md`](../HARDENING.md) §2 — the API-key `/proc` protection, Linux and FreeBSD.
