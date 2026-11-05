# Installing jichi on GNU/Hurd

**Who this is for:** anyone running Debian GNU/Hurd (the GNU project's microkernel
system) who wants to build jichi. It has glibc and GNU make, so the build is Debian's;
a few things behave differently and this page names them. No C knowledge assumed.

**What you will have at the end:** `jichi` built and checked; with a current build, the
interactive terminal works too.

**What you need first:** a Debian GNU/Hurd system where you can run `apt-get`, the
jichi **source code** (the folder with `Makefile`, `src/`, `docs/`), and a model to
talk to (Step 4).

**Where this fits:** this page gives the Hurd specifics; the shared steps are the same
as [debian-ubuntu.md](debian-ubuntu.md).

## 1. Status

**Partly verified, and Driven** (first built at M767, on Debian GNU/Hurd hurd-amd64
under QEMU). It builds and runs the unit suite and the shell test tier, and a model has
been driven on it. One unit check is a known exception (below). See the *GNU/Hurd* row
in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source with `make` (GNU make is the Hurd's `make`). "Installed" means built;
run it as `./jichi`.

## 3. Before you start

Open a terminal and `cd` to the source folder (`ls` shows `Makefile`). Install the
build dependency **and git** — on the Hurd, `git` is not optional if you want the shell
test tier (eight of its drivers need it and fail rather than skip without it):

```sh
apt-get install build-essential libcurl4-openssl-dev pkg-config git
```

## 4. The steps

```sh
make                            # build; no line should say 'error'
ls jichi jichi-convert          # both names should print
make info | grep HAVE_CURL      # -> HAVE_CURL = yes  (blank -> re-check the apt-get line)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings are normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, and what each output means, follow
[debian-ubuntu.md](debian-ubuntu.md) Steps 4–6.

## 5. Measured difficulties

### Use a current build for the interactive terminal (M768 or later)
Before M768, the interactive TUI **froze before every model call** on the Hurd (its
key poll used a mode the Hurd's terminal server blocks on). A current build fixes this;
if you are on an older checkout and the TUI hangs at startup, that is why — update.

### One unit-suite check is a known exception
`make test` reports one known failure on the Hurd: a check that reads a
just-killed process as still alive for a while, because the Hurd keeps a killed
process visible longer than Linux does (deferred, with the analysis linked from
[`PLATFORMS.md`](../PLATFORMS.md)). Everything else passes.

### `git` is required for the shell test tier
Without it, eight drivers fail rather than skip. Install it (Step 3).

## 6. Possible difficulties (not measured here)

- The Hurd is under active development; behaviour outside the measured build/test path
  may differ. A new finding here is welcome.

## 7. Verifying the install

```sh
make check-target        # unit tests (one known Hurd exception) + the shell test tier
./jichi doctor --live    # one real round trip, once a model is configured
```

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [debian-ubuntu.md](debian-ubuntu.md) — the full step-by-step version of the shared steps.
- The Debian GNU/Hurd documentation (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the GNU/Hurd row, the two quirks it found, and the numbers.
