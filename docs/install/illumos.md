# Installing jichi on illumos / Solaris (OmniOS)

**Who this is for:** anyone building jichi on an illumos distribution (OmniOS,
OpenIndiana, SmartOS) or Solaris. It is a non-Linux, non-BSD system, and **two parts of
the build command are load-bearing** — get them right and it builds clean. No C
knowledge assumed; the commands below are exact. Package names are shown for **OmniOS
CE**; other illumos distributions spell them differently.

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:** an illumos system where you can run `pkg install` (as root or
with the right profile), the jichi **source code** (the folder with `Makefile`, `src/`,
`docs/`), and a model to talk to.

## 1. Status

**Partly verified, fully driven, and green** (OmniOS CE r151058): the first non-Linux,
non-BSD kernel jichi was built on. It compiles clean and runs the unit suite and most
of the shell test tier, and a model has been driven on it. See its row in
[`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source with **`gmake CC=gcc`**. Both parts matter (below). "Installed" means
built; run it as `./jichi`.

## 3. Before you start

Open a terminal, `cd` to the source folder (`ls` shows `Makefile`), and install the
toolchain:

```sh
# OmniOS CE -- other illumos distributions spell these differently:
pkg install developer/gcc14 developer/build/gnu-make developer/versioning/git
```

You do **not** install a curl package: OmniOS ships libcurl and its headers in the base
image, so `HAVE_CURL = yes` with only the compiler and make installed.

## 4. The steps

Build with **both** `gmake` and `CC=gcc` — this is the one thing to get right:

```sh
gmake CC=gcc                    # NOT `make`, and CC=gcc is required (see difficulties)
ls jichi jichi-convert          # both names should print
gmake CC=gcc info | grep HAVE_CURL   # -> HAVE_CURL = yes
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings normal now
./jichi setup                   # the wizard: point it at a model
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply
```

For the wizard, a free local model, and what each output means, follow
[debian-ubuntu.md](debian-ubuntu.md) Steps 4–6.

## 5. Measured difficulties

### `gmake`, because illumos's own `make` is Sun make
The command `make` **exists and runs** on illumos — it is just not the one this tree
needs, and it will not parse a GNU build file. Always use `gmake`.

### `CC=gcc`, because there is no `cc` or `c99`
The build defaults to a compiler named `cc`; when that is absent, the result is not a
clear "no compiler" error — instead **every feature probe answers "no"** and you build
a silently degraded binary (no vsnprintf, no curl, no `malloc_trim`), with no error
anywhere. Passing `CC=gcc` makes the probes tell the truth. If `gmake info` shows
features you know the system has as absent, you forgot `CC=gcc`.

### Two build facts you do not set, but will see in `gmake info`
- `-D__EXTENSIONS__` — Solaris hides non-POSIX interfaces (including `TIOCGWINSZ` for
  the terminal) under `_POSIX_C_SOURCE`; the build adds this macro when it probes that
  it is needed.
- `-lsocket -lnsl` — `accept`/`listen`/`bind` are not in the base libc; the build links
  them where the probe says they are required.
Both are probed, not something you configure.

### Some test drivers fail — the cause is known
illumos ships the legacy Solaris text tools, and its `grep -o` prints only the **first**
match per line where GNU prints every one; a few lints extract many items that way.
[`PLATFORMS.md`](../PLATFORMS.md) carries the current count and per-driver detail. The
build and the model both work; this is a test-tier tooling difference, not a jichi
defect.

## 6. Possible difficulties (not measured here)

- A non-OmniOS illumos distribution: the package names differ, and a curl package may be
  needed if the base image does not ship libcurl. `gmake CC=gcc info` tells you what was
  found.

## 7. Verifying the install

```sh
gmake CC=gcc check-target    # unit tests + the shell test tier (some known driver gaps)
./jichi doctor --live        # one real round trip, once a model is configured
```

## 8. Reporting back

The row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`BUILD.md`](../BUILD.md) — the illumos section, and why both `gmake` and `CC=gcc` matter.
- [debian-ubuntu.md](debian-ubuntu.md) — the model-configuration steps, identical here.
- The OmniOS / illumos documentation and `pkg` man pages (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the illumos verdict and the numbers.
- [`platform-notes/illumos.md`](../platform-notes/illumos.md) — the developer history: the ten drivers one `grep -o` hid, and the correction to it.
