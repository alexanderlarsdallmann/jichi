# Installing jichi on NetBSD

**Who this is for:** anyone building jichi on NetBSD. Very close to FreeBSD — same
`gmake` rule — so this page gives the NetBSD specifics and points to
[freebsd.md](freebsd.md) for the shared, fully-explained steps. No C knowledge assumed.

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:** a NetBSD machine where you can run `pkg_add` (as root), the
jichi **source code** (the folder with `Makefile`, `src/`, `docs/`), and a model to
talk to.

**The one thing that trips everyone first:** type **`gmake`**, not `make`.

## 1. Status

**Verified — the full gate, and Driven** (NetBSD 10.1, gcc; it was the cleanest
non-Linux row in the test tier). See its row in
[`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source with `gmake`. NetBSD ships **GNU** grep (unlike its BSD siblings),
which is why the test tier is especially clean here.

## 3. Before you start

Open a terminal, `cd` to the source folder (`ls` shows `Makefile`), and install the
dependencies:

```sh
pkg_add gmake pkg-config curl git-base
```

**`pkg-config` is required, not a nicety.** pkgsrc installs into `/usr/pkg`, which the
base compiler does not search, so without `pkg-config` the libcurl probe answers *no*,
`gmake info` shows a blank `HAVE_CURL =`, and you get a **networkless jichi that built
with no error**. (Optional, for the PDF test drivers: `pkg_add poppler-utils`.)

## 4. The steps

In the source folder:

```sh
gmake                           # NOT `make`; no 'error' lines
ls jichi jichi-convert          # both names should print
gmake info | grep HAVE_CURL     # -> HAVE_CURL = yes  (blank -> install pkg-config, above)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, and what each output means, follow
[freebsd.md](freebsd.md) Steps 4–6 (identical).

## 5. Measured difficulties

### `make` gives a parse error partway through the build file
That is NetBSD's `make` (bmake). Use **`gmake`**.

### `HAVE_CURL =` is blank
`pkg-config` is missing, so the compiler never searched `/usr/pkg` for libcurl. Install
`pkg-config` and rebuild — the single NetBSD-specific gotcha worth knowing.

## 6. Possible difficulties (not measured here)

- pkgsrc on a non-default prefix: `pkg-config` still finds libcurl as long as it is on
  `PATH`; a hand-rolled prefix may need `PKG_CONFIG_PATH` set.

## 7. Verifying the install

```sh
gmake check-target       # unit tests + the shell test tier
./jichi doctor --live    # one real round trip, once a model is configured
```
NetBSD is *Driven*, so this confirms your build.

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [freebsd.md](freebsd.md) — the shared BSD steps, fully explained.
- [`BUILD.md`](../BUILD.md) — the three BSDs together, and the pkgsrc/`pkg-config` note.
- The NetBSD Guide and `pkgsrc` documentation (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the NetBSD row and the numbers.
