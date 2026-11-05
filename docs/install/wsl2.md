# Installing jichi on Windows (through WSL2)

**Who this is for:** anyone on Windows. jichi does not run on Windows directly — it
needs a Linux environment, and Windows has an official one built in called **WSL2**
(Windows Subsystem for Linux). This page walks you from a plain Windows machine to
jichi answering you, no Linux experience assumed. Two other options (Cygwin, MSYS2)
are covered at the end, with one **security caveat you must read** before using MSYS2.

**What you will have at the end:** WSL2 installed, jichi built inside it, pointed at a
model, and answering you.

**What you need first:** a Windows 10/11 machine where you can open PowerShell **as
administrator** once, and a model to talk to (Step 6).

**Where this fits:** native Windows is not supported by design; WSL2 is the intended
path. Where Windows and POSIX differ is mapped in
[`PORTING_WINDOWS.md`](../PORTING_WINDOWS.md).

## 1. Status

- **WSL2 — Verified (the full test suite) and Driven.** The Linux build runs
  unchanged inside WSL2.
- **Cygwin / MSYS2 — Partly verified** (they build; see the end of this page).

See the rows in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix).

## 2. Who this is for, and what "installed" means

"Installed on Windows" means installed **inside WSL2** — a real Ubuntu Linux running
alongside Windows. You build jichi there and run it from the WSL terminal.

## 3. Before you start: install WSL2

1. **Open PowerShell as administrator.** Click Start, type `PowerShell`, right-click
   *Windows PowerShell*, choose **Run as administrator**, and accept the prompt.
2. **Install WSL with Ubuntu.** In that PowerShell window, type:
   ```powershell
   wsl --install
   ```
   - **You should see** it enable the feature and download Ubuntu. When it says to
     **restart**, restart Windows.
   - After the restart, an **Ubuntu** window opens by itself and asks you to create a
     username and password (for the Linux side — remember them; the password is what
     `sudo` will ask for).
3. **Open the Ubuntu terminal from now on.** Click Start → **Ubuntu**. Everything
   from here happens in this Ubuntu window, which is a normal Linux terminal.

## 4. The steps (inside the Ubuntu / WSL terminal)

### Step 1 — get the jichi source into WSL

Keep the source **inside the Linux side** (`~/…`), not under `/mnt/c/…`, or the build
will be slow. If you have `git`:

```sh
sudo apt update && sudo apt install -y git
git clone <the jichi repository URL> ~/jichi
cd ~/jichi
```

Replace `<the jichi repository URL>` with wherever you obtain jichi. If you already
have the source as a folder or archive on Windows, copy it into your WSL home
(`cp -r /mnt/c/Users/<you>/Downloads/jichi ~/jichi`) and `cd ~/jichi`.

**Check you are in the source folder:** `ls` should show `Makefile`, `src`, `docs`.

### Step 2 — install the build dependency

```sh
sudo apt install build-essential libcurl4-openssl-dev pkg-config
```
Enter your WSL password when asked and confirm with `Y`.

### Step 3 — build jichi

```sh
make
```
- **You should see** about a minute of output, no lines containing `error`, then your
  prompt. Confirm the programs: `ls jichi jichi-convert` shows both.

### Step 4 — confirm networking, then run it once

```sh
make info | grep HAVE_CURL     # -> HAVE_CURL = yes  (blank -> redo Step 2)
./jichi --version              # -> a version line
./jichi doctor                 # -> a health report (warnings about the model are normal now)
```

### Step 5 — point jichi at a model

```sh
./jichi setup
```
Answer the prompts. If you run **LM Studio on Windows**, it is reachable from WSL2 at
`http://localhost:1234/v1` (choose the OpenAI-compatible option and give that
address). To write the config by hand instead, see
[debian-ubuntu.md](debian-ubuntu.md) Step 5. Then `./jichi doctor` again — the model
warnings should clear.

### Step 6 — your first turn

```sh
./jichi -p "say hello in one line" < /dev/null
```
A one-line reply means jichi is working end to end. If it errors, run `./jichi doctor`
and read the first failing line.

### Step 7 (optional) — install onto your PATH

```sh
make -j4 && sudo make install
```
Then `jichi` works from any folder in the Ubuntu terminal.

## 5. Measured difficulties

### The whole test suite runs on WSL2 — but only WSL2
```sh
make ci                  # green on WSL2; on a loaded machine add JC_SMOKE_TIMEOUT_MULT=3
```
This is the only Windows route where the full suite passes. You do not need it to
*use* jichi.

### Keep the source inside WSL, not on the Windows drive
Building under `/mnt/c/…` is much slower. Clone or copy into `~/…` (Step 1).

## 6. The two other options (Cygwin and MSYS2), if you cannot use WSL2

Both build with plain `make` (GNU make is their `make`), but the test suite is slower
and neither runs the full `make ci`. Use `JC_SMOKE_TIMEOUT_MULT=10 make smoke` so the
timing tests do not fail on speed alone.

**MSYS2 — read this before you put an API key on it.** By default MSYS2 mounts its
filesystem so that `chmod` does nothing: jichi's attempt to make your **API key
file** private silently fails, leaving it readable by other accounts. This is a
one-line fix — add to `/etc/fstab`:
```
C:/msys64/tmp /tmp ntfs binary,acl 0 0
```
and restart the MSYS2 shell. Cygwin does not have this problem. **Do not run Cygwin
and MSYS2 at the same time** — they conflict and both start failing.

## 7. Verifying the install

```sh
make ci                  # WSL2: the whole suite
make check-target        # Cygwin/MSYS2: unit tests + the shell test tier
./jichi doctor --live    # one real round trip, once a model is set up
```

## 8. Reporting back

The short row template is [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full
result is [`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`PORTING_WINDOWS.md`](../PORTING_WINDOWS.md) — why native Windows is not supported (primary).
- [debian-ubuntu.md](debian-ubuntu.md) — the Linux build you are running inside WSL2.
- Microsoft's WSL documentation, and the Cygwin / MSYS2 project docs (primary).
- [`PLATFORMS.md`](../PLATFORMS.md) — the verdicts and the numbers.
- [`platform-notes/windows.md`](../platform-notes/windows.md) — the developer history: the Cygwin and WSL2 rows and the defects they found.
