# Installing jichi on Alpine Linux (musl)

**Who this is for:** anyone on Alpine, or another musl-libc Linux (including a
`musl` container image). Alpine uses a different C library (musl, not glibc) and a
different package manager (`apk`), but jichi builds the same way. No C knowledge
assumed.

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:** a machine (or container) where you can run `apk add`, the
jichi **source code** (the folder with `Makefile`, `src/`, `docs/`), and a model to
talk to (Step 4).

**Where this fits:** this page gives the Alpine specifics; the fuller version of every
shared step is in [debian-ubuntu.md](debian-ubuntu.md).

## 1. Status

**Verified — the full gate.** musl is a proven libc for jichi, including fully static
`x86_64-linux-musl` and `aarch64-linux-musl` builds. See
[`PLATFORMS.md`](../PLATFORMS.md#the-matrix) (the *musl* rows).

## 2. Who this is for, and what "installed" means

Built from source with `make`. On Alpine there is no `sudo` by default — run the
`apk add` as root (or `doas`), and the rest as your user.

## 3. Before you start

Open a terminal, `cd` to the jichi source folder (`ls` shows `Makefile`), and install
the dependency — **the one line that differs from Debian:**

```sh
apk add build-base curl-dev pkgconf      # as root, or: doas apk add ...
```

## 4. The steps

In the source folder:

```sh
make                            # build; no line should say 'error'
ls jichi jichi-convert          # both names should print
make info | grep HAVE_CURL      # -> HAVE_CURL = yes  (blank -> re-check the apk line)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings are normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, or a hand-written config, and for what each
command's output means, follow [debian-ubuntu.md](debian-ubuntu.md) Steps 4–6 —
identical here.

## 5. Measured difficulties

### A fully static build (no shared libraries at all)
Alpine/musl is the usual base for a static binary you can copy to another machine. The
static and cross-compile recipes are in [`DEPLOYMENT.md`](../DEPLOYMENT.md) §3e and
[`LOW_MEMORY.md`](../LOW_MEMORY.md); the plain `make` above is the ordinary build.

### `HAVE_CURL =` is blank
`curl-dev` or `pkgconf` is missing — re-run the `apk add` line. (Alpine's dev package
is `curl-dev`, not `libcurl-dev`.)

## 6. Possible difficulties (not measured here)

- A minimal container image may lack a shell or the `git` the *test tier* uses; the
  build itself needs only the three packages above.

## 7. Verifying the install

```sh
make check-target        # the unit tests + the shell test tier
./jichi doctor --live    # one real round trip, once a model is configured
```

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [debian-ubuntu.md](debian-ubuntu.md) — the full step-by-step version of the shared steps.
- [`DEPLOYMENT.md`](../DEPLOYMENT.md) — static and cross builds (musl is the usual base).
- [`PLATFORMS.md`](../PLATFORMS.md) — the musl rows and the numbers.
