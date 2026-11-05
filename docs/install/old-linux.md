# Installing jichi on an old Linux userland

**Who this is for:** anyone on an old Linux release — Debian 5–9, CentOS/RHEL 6 or 7,
or a similarly aged distribution — where the C library and tools are years behind
current ones. jichi is written to build on very old systems; this page says what to
expect. No C knowledge assumed.

**What you will have at the end:** `jichi` built, its unit tests passing, and — if the
system is new enough to have a working package for it — a model answering you.

**What you need first:** a machine where you can install a package, the jichi **source
code** (the folder with `Makefile`, `src/`, `docs/`), and a compiler.

**Where this fits:** this page notes the old-system specifics; the shared steps are in
[debian-ubuntu.md](debian-ubuntu.md) (Debian-family) or [fedora.md](fedora.md) (RHEL/CentOS).

## 1. Status

**Partly verified** — the build and the unit suite pass on glibc as old as 2.7
(Debian 5, CentOS 6), each run in a container on the development box's kernel. The
network build and the full shell test tier are what varies with age. See the
*old userlands* row in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

Built from source. On a very old system the safest result to aim for first is a clean
build and a green unit suite; networking depends on an available libcurl.

## 3. Before you start

Open a terminal and `cd` to the source folder (`ls` shows `Makefile`). Install the
build dependency the way your distro does it — `sudo apt install build-essential
libcurl4-openssl-dev pkg-config` on old Debian/Ubuntu, or `sudo yum install gcc make
libcurl-devel pkgconfig` on CentOS/RHEL 6–7 (`yum`, not `dnf`, on those).

## 4. The steps

```sh
make                            # build; watch for lines containing 'error'
ls jichi jichi-convert          # both names should print
make info                       # read the detected features (see below)
./jichi --version               # -> a version line
./jichi doctor                  # -> a health report
```

**Read `make info` on an old system** — it tells you what the compiler could and could
not do:
- `HAVE_CURL = yes` means networking is on. If it is blank, the libcurl `-dev`/`-devel`
  package is missing or too old; the build still produced a working offline jichi.
- `clock_gettime` may show *needs -lrt* (glibc 2.12–2.16) — the build adds `-lrt`
  automatically; nothing for you to do.

If networking is on, point it at a model with `./jichi setup` and take a first turn
(`./jichi -p "say hello in one line"`), exactly as in
[debian-ubuntu.md](debian-ubuntu.md) Steps 5–6.

## 5. Measured difficulties

### Very old glibc and `clock_gettime`
On glibc 2.12–2.16 (RHEL/Debian of that era) `clock_gettime` lives in a separate
library; the build probes for it and links `-lrt` when needed. This was an unstated
build floor once — now it is detected. Nothing for you to configure.

### The shell test tier expects reasonably current text tools
Old `grep`/`sed`/`awk` can differ; the **unit suite** (`make test`) is the reliable
gate on an old system, and it passes down to glibc 2.7.

## 6. Possible difficulties (not measured on every old release)

- A distro too old to have a usable libcurl: you get an offline jichi (build succeeds,
  networking off). A static build made elsewhere and copied in is the way around it
  ([`DEPLOYMENT.md`](../DEPLOYMENT.md)).

## 7. Verifying the install

```sh
make test                # the unit suite -- the reliable gate on an old system
make check-target        # adds the shell test tier (may show tool-version differences)
```

## 8. Reporting back

An old release is a useful data point. The row template is
[`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [debian-ubuntu.md](debian-ubuntu.md) / [fedora.md](fedora.md) — the shared steps.
- [`DEPLOYMENT.md`](../DEPLOYMENT.md) — static builds for systems too old to build networking.
- [`PLATFORMS.md`](../PLATFORMS.md) — the old-userland rows and exactly how old is proven.
