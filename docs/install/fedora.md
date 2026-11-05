# Installing jichi on Fedora / RHEL (and Arch)

**Who this is for:** anyone on Fedora, RHEL/CentOS/Rocky/Alma, or Arch Linux. These
are all Linux, so the build is the same as Debian/Ubuntu — only the one command that
installs the dependency differs. No C knowledge assumed.

**What you will have at the end:** `jichi` built, checked, pointed at a model, and
answering you.

**What you need first:** a machine where you can install a package (`sudo`), the jichi
**source code** (the folder with `Makefile`, `src/`, `docs/`), and a model to talk to
(Step 4).

**Where this fits:** this page gives the Fedora/Arch specifics; the fuller,
click-by-click version of every shared step is in [debian-ubuntu.md](debian-ubuntu.md).

## 1. Status

**Verified — the full gate, and Driven** as a Linux/glibc platform (the reference the
project's tests run on). See [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

No distribution package — you build from source with `make`, then optionally
`sudo make install`. Until then you run it as `./jichi` from the source folder.

## 3. Before you start

Open a terminal and `cd` to the jichi source folder (the one with `Makefile`; check
with `ls`). Then install the dependency — **the one line that differs from Debian:**

```sh
# Fedora / RHEL / CentOS / Rocky / Alma:
sudo dnf install gcc make libcurl-devel pkgconf-pkg-config

# Arch / Manjaro:
sudo pacman -S base-devel curl pkgconf
```

Everything after this is identical to Debian/Ubuntu.

## 4. The steps

In the source folder:

```sh
make                            # build (about a minute; no line should say 'error')
ls jichi jichi-convert          # both names should print
make info | grep HAVE_CURL      # -> HAVE_CURL = yes  (blank -> re-check the dnf/pacman line above)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report; model warnings are normal now
./jichi setup                   # the wizard: answer the prompts to point it at a model
./jichi doctor                  # model warnings should now be gone
./jichi -p "say hello in one line" < /dev/null   # -> a one-line reply: it works end to end
```

For the guided wizard, a free local model, or writing the config by hand, and for what
each command's output means, follow [debian-ubuntu.md](debian-ubuntu.md) Steps 4–6 —
they are identical here. To put it on your `PATH`: `make -j4 && sudo make install`.

## 5. Measured difficulties

### `HAVE_CURL =` is blank
The `-devel` headers or `pkgconf` are missing. Re-run the install line for your distro
(note Fedora's package is `libcurl-devel` and the pkg-config provider is
`pkgconf-pkg-config`). A blank here means a jichi that built but cannot reach a model.

The install/uninstall notes (the "dirty tree" refusal, the root-owned-object-file
trap) are the same as [debian-ubuntu.md](debian-ubuntu.md) §5.

## 6. Possible difficulties (not measured here)

- RHEL/CentOS may need EPEL for `pkgconf-pkg-config`; on a very old release, the old
  glibc `clock_gettime`/`-lrt` case applies (the build handles it — see the
  old-userland rows in [`PLATFORMS.md`](../PLATFORMS.md), and [old-linux.md](old-linux.md)).

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
- [`BUILD.md`](../BUILD.md) — the build system and its dependency table (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the verdict and the numbers.
