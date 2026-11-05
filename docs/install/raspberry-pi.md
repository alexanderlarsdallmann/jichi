# Installing jichi on a Raspberry Pi (and other small boards)

**Who this is for:** anyone building jichi on a Raspberry Pi or a similar small
board. A Pi runs Linux, so the steps are the Debian/Ubuntu ones — this page adds the
two things that are different on a small board: the model usually runs on **another**
computer, and the test suite needs a **timeout setting** because the board is slower.
No prior experience assumed.

**What you will have at the end:** `jichi` built on the Pi, pointed at a model
(usually on your laptop), and answering you.

**What you need first:**
- A Pi running Raspberry Pi OS (or another Debian-based image) where you can run `sudo`.
- The jichi **source code** on the Pi (the folder with `Makefile`, `src/`, `docs/`).
- A model to talk to. The Pi is usually too small to *host* one, so this page assumes
  the model runs on another machine on your network (e.g. LM Studio on your laptop) —
  Step 5 shows how to reach it.

**Where this fits:** the board is Linux, so the underlying build is
[debian-ubuntu.md](debian-ubuntu.md); this page adds the board-specific notes.

## 1. Status

**Verified — the full gate, and Driven** on both the Raspberry Pi Zero 2 W and the
Pi 400. See their rows in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix); RAM tiers and
the hardware list are in [`LOW_MEMORY.md`](../LOW_MEMORY.md).

## 2. Who this is for, and what "installed" means

Same as any Linux: `make` builds the program in the source folder. The board is just
slower and often has less memory, which changes two numbers, not the procedure.

## 3. Before you start: open a terminal and find the source folder

1. **Open a terminal** on the Pi (the Terminal icon, or connect over SSH from another
   machine).
2. **Go to the jichi source folder:**
   ```sh
   cd ~/jichi        # replace with wherever your copy is
   ls
   ```
   You should see `Makefile`, `src`, `docs`. If not, `cd` to where you put jichi.

## 4. The steps

### Step 1 — install the build dependency

```sh
sudo apt install build-essential libcurl4-openssl-dev pkg-config
```
Enter your password when asked, confirm with `Y`, and wait for it to finish.

### Step 2 — build jichi

```sh
make
```
- **You should see** the build run (longer than on a laptop — a few minutes on a
  small board is normal), no lines containing `error`, then your prompt.
- **Confirm the programs:** `ls jichi jichi-convert` shows both.

### Step 3 — confirm networking, and check 32- vs 64-bit

```sh
make info | grep -E 'HAVE_CURL|LONG_BIT'
```
- **You should see** `HAVE_CURL = yes` and a `LONG_BIT` of 64 (64-bit image) or 32.
- **If `HAVE_CURL` is blank,** redo Step 1.
- **On a 32-bit board, do not add `WERROR=1`** to your build — a 32-bit build with the
  network library cannot pass that stricter mode (see the armhf rows in
  [`PLATFORMS.md`](../PLATFORMS.md)). Plain `make` (Step 2) is what you want anyway.

### Step 4 — run it once, with no model yet

```sh
./jichi --version        # -> a version line
./jichi doctor           # -> a health report; model warnings are normal now
```

### Step 5 — point jichi at a model on another machine

A small board usually cannot run the model itself, so run the model on your laptop
and have the Pi talk to it over your home network. Three small steps:

1. **On the laptop,** start the model server (e.g. LM Studio) and set it to listen on
   the network, not only on `localhost`. In LM Studio's server settings this is the
   "Serve on Local Network" option; the port is usually `1234`.
2. **Find the laptop's address on the network.** On the laptop: on Linux/macOS run
   `hostname -I` (or `ipconfig getifaddr en0`); on Windows run `ipconfig`. You are
   looking for something like `192.168.0.20`.
3. **On the Pi,** run the wizard and give that address:
   ```sh
   ./jichi setup
   ```
   Choose the OpenAI-compatible option and enter `http://192.168.0.20:1234/v1`
   (with the laptop's real address). To write the config by hand instead, use the
   `~/.jichi` shape in [debian-ubuntu.md](debian-ubuntu.md) Step 5 with that `apiBase`.

Then check it:

```sh
./jichi doctor
```
- **You should see** the model warnings cleared and the server reported reachable.
- **If it says the server is unreachable,** re-check that LM Studio is serving on the
  network (step 1) and that the address is right (step 2); a home firewall on the
  laptop can also block the port.

### Step 6 — your first turn

```sh
./jichi -p "say hello in one line" < /dev/null
```
A one-line reply means it works. If it cannot reach the model, check that the laptop's
model server is listening on the network (not only on `localhost`) and that no
firewall blocks the port; `./jichi doctor` names what is unreachable.

### Step 7 (optional) — install onto the Pi's PATH

```sh
make -j4 && sudo make install
```

## 5. Measured difficulties

### The test suite times out with default settings — use a multiplier
The shell test tier's time limits are set for a fast desktop; on a Pi they will trip.
Run the on-machine gate with a multiplier:
```sh
JC_SMOKE_TIMEOUT_MULT=20 make check-target
```
`JC_SMOKE_TIMEOUT_MULT` is a *ratio* — how many times slower your board is than a
desktop — so 20 is a starting point for a Pi; raise it if tests still time out. (The
project's Pi rows run at 19–28.)

### 32-bit builds and the stricter `WERROR=1` mode
On a 32-bit board, `make WERROR=1` with the network library cannot build (a warning in
one file that strict mode then rejects). Use plain `make`. This does not affect using
jichi at all.

## 6. Possible difficulties (not measured on every board)

- A board with an unusual C library or a very old kernel: the build detects what is
  available and degrades gracefully; `make info` shows the result. The verified boards
  cover the common libraries, so an unusual one is a new data point worth reporting.

## 7. Verifying the install

```sh
JC_SMOKE_TIMEOUT_MULT=20 make check-target
./jichi doctor --live    # one real round trip, once the model is reachable
```
Both should finish without failures. The two Pi rows are already *Driven*; a **new**
board is not tested until a model has answered a tool-using turn on it.

## 8. Reporting back

A new board is a welcome result. The row template is
[`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3; a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`LOW_MEMORY.md`](../LOW_MEMORY.md) — RAM tiers, the hardware list, and what jichi does under memory pressure (primary here).
- [debian-ubuntu.md](debian-ubuntu.md) — the underlying Linux build, step by step.
- [`DEPLOYMENT.md`](../DEPLOYMENT.md) — static and cross-compiled builds for boards.
- [`PLATFORMS.md`](../PLATFORMS.md) — the board rows, the multipliers, and the numbers.
