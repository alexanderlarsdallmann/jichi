# Installing jichi on OpenBSD

**Who this is for:** anyone building jichi on OpenBSD. It is very close to FreeBSD —
same `gmake` rule, same "no procfs" design — so this page gives the OpenBSD specifics
and points to [freebsd.md](freebsd.md) for the shared, fully-explained steps. No C
knowledge assumed.

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:** an OpenBSD machine where you can run `pkg_add` (as root), the
jichi **source code** (the folder with `Makefile`, `src/`, `docs/`), and a model to
talk to.

**The one thing that trips everyone first:** type **`gmake`**, not `make` (OpenBSD's
`make` cannot read jichi's build file).

## 1. Status

**Verified — the full gate, and Driven** (OpenBSD 7.9, clang 19.1.7). Build clean, the
whole test tier runs, a model driven on it — twice. See its row in
[`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source with `gmake`. Note OpenBSD's `/bin/sh` is **ksh** — that is fine, the
tests account for it.

## 3. Before you start

Open a terminal, `cd` to the source folder (`ls` shows `Makefile`), and install the
dependencies:

```sh
pkg_add gmake curl git
```

(Optional, for the two PDF test drivers: `pkg_add poppler-utils`.)

## 4. The steps

In the source folder:

```sh
gmake                           # NOT `make`; build should be quick, no 'error' lines
ls jichi jichi-convert          # both names should print
gmake info | grep HAVE_CURL     # -> HAVE_CURL = yes  (blank -> a pkg-config-style dep is missing)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, and what each output means, follow
[freebsd.md](freebsd.md) Steps 4–6 (identical). Install onto `PATH` with
`gmake -j4 && doas gmake install` (or as root).

## 5. Measured difficulties

### `make` gives a parse error partway through the build file
That is OpenBSD's `make`. Use **`gmake`** — the single most common first-build stop.

### A test driver fails and jichi looks wrong — suspect the driver
The OpenBSD row's history is one to know: its failures were almost all GNU-only options
in the *test tier's own* `grep`/`sed`/`awk` (a BRE `\(…\)\?` OpenBSD refuses, a `\|`
alternation it reads literally), plus two real bugs that turned out to affect **every**
platform — an inherited `SIGPIPE` that hung pipelines, and a hardening break. That is
the argument for the row: it is a defect detector. The full history is in
[`platform-notes/openbsd.md`](../platform-notes/openbsd.md).

## 6. Possible difficulties (not measured here)

- `pledge`/`unveil` sandboxing is not something jichi sets up itself; running under an
  external sandbox is untested.

## 7. Verifying the install

```sh
gmake check-target       # unit tests + the shell test tier
./jichi doctor --live    # one real round trip, once a model is configured
```
OpenBSD is *Driven*, so this confirms your build.

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [freebsd.md](freebsd.md) — the shared BSD steps, fully explained.
- [`BUILD.md`](../BUILD.md) — the three BSDs together.
- OpenBSD's `pkg_add`, `ksh` and FAQ documentation (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the OpenBSD verdict and the numbers.
- [`platform-notes/openbsd.md`](../platform-notes/openbsd.md) — the developer history: the hardening break, what it found beyond FreeBSD, and its failure set.
