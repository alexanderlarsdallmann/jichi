# Installing jichi on Guix System

**Who this is for:** anyone on Guix System (or the Guix package manager on another
distro). Guix is Linux with glibc, so jichi builds normally — the one difference is
that Guix has **no global `/usr/lib`** (it is "non-FHS"), so you bring the compiler and
libraries in with a `guix shell` rather than a system install. No C knowledge assumed.

**What you will have at the end:** `jichi` built inside a Guix environment, checked,
and pointed at a model.

**What you need first:** Guix installed, the jichi **source code** (the folder with
`Makefile`, `src/`, `docs/`), and a model to talk to (Step 3).

**Where this fits:** this page gives the Guix specifics; the shared steps are in
[debian-ubuntu.md](debian-ubuntu.md).

## 1. Status

**Partly verified, and Driven.** It builds and passes the unit suite and the
parallel/signal test drivers, and a model has been driven on it. Guix's non-FHS layout
is exactly what makes it a useful row — it catches anything that assumed a global
library path. See the *Guix* row in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source inside a `guix shell` that provides the toolchain. "Installed" means
built; you run it as `./jichi` from that shell.

## 3. Before you start

Open a terminal and `cd` to the source folder (`ls` shows `Makefile`). Instead of a
system package install, enter a shell that has the compiler and libraries:

```sh
guix shell gcc-toolchain make pkg-config curl
```

Your prompt is now inside that environment; the build commands below run there. (Add
`--pure` if you want to be sure nothing leaks in from your profile.)

## 4. The steps

Inside the `guix shell`:

```sh
make                            # build; no line should say 'error'
ls jichi jichi-convert          # both names should print
make info | grep HAVE_CURL      # -> HAVE_CURL = yes  (blank -> add curl/pkg-config to the guix shell)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings are normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, and what each output means, follow
[debian-ubuntu.md](debian-ubuntu.md) Steps 4–6.

## 5. Measured difficulties

### The toolchain and libraries come from the shell, not the system
If `make` cannot find the compiler or `HAVE_CURL` is blank, you are probably outside the
`guix shell` (or left a package out of it). Re-enter with all four packages in Step 3.

### `pkg-config` is what finds libcurl in the store
Because there is no `/usr/lib` to fall back on, `pkg-config` is doing the real work of
locating libcurl in the Guix store. Keep it in the shell.

## 6. Possible difficulties (not measured here)

- Running the built binary **outside** the `guix shell`: it links against store paths,
  so it works within the profile that has them. For a portable copy, a static musl
  build ([alpine.md](alpine.md), [`DEPLOYMENT.md`](../DEPLOYMENT.md)) is simpler than a
  Guix binary.

## 7. Verifying the install

```sh
make check-target        # unit tests + the shell test tier
./jichi doctor --live    # one real round trip, once a model is configured
```

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [debian-ubuntu.md](debian-ubuntu.md) — the full step-by-step version of the shared steps.
- Guix manual, `guix shell` section (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the Guix row, what non-FHS caught, and the numbers.
