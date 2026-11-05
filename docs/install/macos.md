# Installing jichi on macOS

**Who this is for:** anyone with a Mac. Be warned up front: **there is no *verified*
macOS build.** Two people have told us they compiled jichi on a Mac (2026-09), but
neither sent the report Section 8 asks for, and we do not know whether they had to change
the source to do it — so nothing here is measured, and everything below is our best guess
from how close macOS is to the BSD systems jichi *is* proven on. That means two things
for you: it will probably work, and whatever happens, **you can be the first to send a
verified result** — please tell us (Section 8). No C knowledge assumed; copy the commands
and read what each one prints.

**What you will have at the end:** either jichi built and answering you, or a concrete
failure — and in both cases the first *verified* macOS result on record, if you send it
(please report either; Section 8).

**What you need first:**
- A Mac where you can install software.
- The jichi **source code** already on the Mac (the folder with `Makefile`, `src/`,
  `docs/`).
- A model to talk to (a local [LM Studio](https://lmstudio.ai) or other
  OpenAI-compatible server, or a hosted model's key). Step 5 covers this.

**Where this fits:** the generic build is [`INSTALL.md`](../INSTALL.md); the closest
proven relatives are the BSD builds in [`BUILD.md`](../BUILD.md).

## 1. Status

**Partly verified, and driven** (M817, 2026-09-26). One build on Apple Silicon -- a
MacBook Air M4, macOS 26.5.1, Apple clang 21 and the libcurl 8.7.1 that comes with the
Xcode SDK -- passed `make WERROR=1`, the unit suite and the smoke tier, and drove a model
through a tool-calling turn. Dima Mellnik did it first, and the fixes it needed are in
the tree ([the porting record](../platform-notes/macos.md)). *Partly* means two things:
`make ci` has never run on a Mac, and that build used the SDK's libcurl, not the
Homebrew curl that Step 1 below installs, so these exact steps have not been run.
An Intel Mac would be its own row ([`PLATFORMS.md`](../PLATFORMS.md#the-matrix)).

*Corrected 2026-10-04: until then this section said "Never compiled", which stopped
being true at M817.*

## 2. Who this is for, and what "installed" means

Built from source with `make` (which is GNU make on macOS, so no special command).
"Installed" means the program is built; there is no tested `make install` path on
macOS yet, so you will run it as `./jichi`.

## 3. Before you start: open Terminal and find the source folder

1. **Open Terminal.** Press ⌘-Space, type `Terminal`, press Return.
2. **Install Apple's command-line tools** (the compiler), if you have not already:
   ```sh
   xcode-select --install
   ```
   A dialog appears — click **Install** and wait. If it says they are already
   installed, good.
3. **Go to the jichi source folder:**
   ```sh
   cd ~/jichi        # replace with wherever your copy is
   ls
   ```
   You should see `Makefile`, `src`, `docs`. If not, `cd` to where you put jichi.

## 4. The steps

### Step 1 — install the network library

macOS ships an old copy; install a current one with [Homebrew](https://brew.sh) (if
you do not have `brew`, its home page has the one-line installer):

```sh
brew install curl pkg-config
export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig"
```
The `export` line tells the build to use Homebrew's `curl`. (It lasts for this
terminal session; run it again if you open a new window before building.)

### Step 2 — build jichi

```sh
make
```
- **You should see** the build run with no lines containing `error`, then your prompt.
- **Confirm the programs:** `ls jichi jichi-convert` shows both.
- **If it stops with an error**, this is the interesting case — copy the first few
  lines containing `error`; that is exactly what Section 8 asks for.

### Step 3 — confirm networking, then run it once

```sh
make info | grep HAVE_CURL     # -> HAVE_CURL = yes  (blank -> re-run the export in Step 1, then make)
./jichi --version              # -> a version line
./jichi doctor                 # -> a health report; model warnings are normal now
```

### Step 4 — point jichi at a model

```sh
./jichi setup
```
Answer the prompts (for a local LM Studio server, choose the OpenAI-compatible option
and give `http://localhost:1234/v1`). To write the config by hand, see
[debian-ubuntu.md](debian-ubuntu.md) Step 5. Then `./jichi doctor` — the model
warnings should clear.

### Step 5 — your first turn

```sh
./jichi -p "say hello in one line" < /dev/null
```
A one-line reply means it works on your Mac — **please report it** (Section 8), so the
matrix can record its first *verified* macOS result.

## 5. Measured difficulties

Nothing is measured here yet — no verified build has been reported. The **one** thing
known specifically about macOS:

### The one Mac-specific line in the source
jichi reads total memory with a Mac-specific call (`sysctl(HW_MEMSIZE)`). The page
once claimed there was *no* Mac-specific code at all — and was wrong: that line existed
and would not even compile under jichi's own rules until it was fixed. It is fine now,
but it is why you should trust *measured* claims over confident-sounding ones: the
"it's just POSIX" assumption was wrong once here already.

## 6. Possible difficulties (not measured — you may be first)

- **Small differences the BSD systems did not show.** macOS's userland is not quite a
  BSD; expect the possibility of something nobody has hit. When you hit it, it is a
  finding, not a failure — write down the exact command and the exact message.
- **A "cannot be opened / unidentified developer" prompt** the first time you run a
  program you built yourself. This is Gatekeeper; right-click the binary → Open, or
  allow it in System Settings → Privacy & Security.

## 7. Verifying the install

```sh
make check-target        # jichi's own tests -- a result, green OR red, is welcome
./jichi doctor --live    # one real round trip, once a model is configured
```
This is the one row in the whole matrix with no verified result. A model that answers a
tool-using turn here, sent with the details below, would be the **first verified macOS
result**.

## 8. Reporting back — please do, whatever happened

A red result here is as valuable as a green one; it is the first *verified* data this row
would have. Send:
- the output of `make info` and `sw_vers` (your macOS version),
- for a build failure, the first lines containing `error`,
- for a success, the `make check-target` summary.

The short row template is in [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3
("Writing the row"); sending a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`BUILD.md`](../BUILD.md) — the macOS notes and the BSD builds they lean on.
- Apple's `sysctl(3)` man page and the Homebrew documentation (primary sources).
- [`PLATFORMS.md`](../PLATFORMS.md) — the verdict, *Partly verified, driven*, and what each verdict means.
