# Installing jichi on Debian / Ubuntu

**Who this is for:** anyone on Debian, Ubuntu, or a close relative (Mint, Pop!_OS,
Raspberry Pi OS on a 64-bit board — boards have a few extra notes in
[raspberry-pi.md](raspberry-pi.md)). **You do not need to know C or how compilers
work.** You will copy a handful of commands, and after each one this page tells you
what you should see and what to do if you see something else.

**What you will have at the end:** the `jichi` program built, checked, pointed at a
model, and answering you.

**What you need first:**
- A Debian/Ubuntu machine where you can run `sudo` (to install one package).
- The jichi **source code** already on the machine — the folder that contains a
  `Makefile`, a `src/` directory and the `docs/` directory you are reading this in.
  If you cloned it with `git` or unpacked a release archive, you have it.
- A model for jichi to talk to. The easiest free option is **[LM Studio](https://lmstudio.ai)**
  running on the same machine; any OpenAI-compatible server, or an API key for a
  hosted model, also works. Step 6 covers this.

**Where this fits:** this is the friendliest, most-tested path. The generic build is
in [`INSTALL.md`](../INSTALL.md); the build-system reference is in [`BUILD.md`](../BUILD.md).

## 1. Status

**Verified — the full gate, and Driven.** Linux is the platform the project's
automated tests run on for every change, so it is the most reliable path here. See
the *Linux, glibc* row in [`PLATFORMS.md`](../PLATFORMS.md#the-matrix). "Installed"
means built from source — there is no `apt install jichi`; you build it once with the
steps below.

## 2. Who this is for, and what "installed" means

There is no distribution package. You run `make` once, which turns the source code
into a program called `jichi` (and a helper, `jichi-convert`). Until you run
`sudo make install`, the program lives in the source folder and you run it as
`./jichi`. After `sudo make install`, you can type just `jichi` from anywhere.

## 3. Before you start: open a terminal and find the source folder

1. **Open a terminal.** On most desktops: the Activities/Applications menu → search
   "Terminal". You will type the commands in this window.

2. **Go to the jichi source folder** — the one with the `Makefile` in it. If you
   cloned it, that is the folder `git` created (often `jichi`). For example:

   ```sh
   cd ~/jichi
   ```

   (Replace `~/jichi` with wherever your copy is.) **Check you are in the right
   place** by listing the folder:

   ```sh
   ls
   ```

   You should see `Makefile`, `src`, `docs` and more among the names. If you do
   **not** see `Makefile`, you are in the wrong folder — `cd` to the folder you
   unpacked or cloned jichi into and run `ls` again.

## 4. The steps

### Step 1 — install the one thing jichi needs to build

In the terminal (any folder is fine for this one), type:

```sh
sudo apt install build-essential libcurl4-openssl-dev pkg-config
```

It will ask for your password (typing shows nothing — that is normal) and then to
confirm with `Y`. It downloads and installs a compiler and the network library.

- **You should see**, at the end, lines about packages being "set up" or "newly
  installed", and your prompt returning.
- **If you see `sudo: command not found`**, you are on an unusual setup — ask whoever
  administers the machine, or run the command without `sudo` as the root user.

### Step 2 — build jichi

Make sure you are in the source folder (`cd ~/jichi`, or wherever it is — Section 3).
Then type:

```sh
make
```

- **You should see** about a minute of output as each file compiles, and then your
  prompt returns. **None of the lines should contain the word `error`.** (Lines
  containing `warning` are fine.)
- **Check the two programs were built:**
  ```sh
  ls jichi jichi-convert
  ```
  You should see both names printed. If instead you get `No such file or directory`,
  the build did not finish — scroll up to the first line containing `error` and read
  it; the most common cause is skipping Step 1.
- **If `make` itself says `command not found`,** Step 1 did not complete — run it again.

### Step 3 — confirm the build can reach the network

jichi needs the network library to talk to a model. Check it was found:

```sh
make info | grep HAVE_CURL
```

- **You should see:** `HAVE_CURL = yes`.
- **If it is blank** (`HAVE_CURL =` with nothing after), the network library was not
  found. Re-run Step 1 (you need `libcurl4-openssl-dev` **and** `pkg-config`), then
  run `make` again. A blank here means jichi built but cannot reach any model — worth
  catching now.

### Step 4 — run it once, with no model yet

```sh
./jichi --version
```

- **You should see** a line like `jichi 0.13.0 ...`. That is the program running.

Now the built-in health check:

```sh
./jichi doctor
```

- **You should see** a list of checks. Some **warnings are normal at this stage** —
  you have not configured a model yet, so it will say so. You are looking for the
  program to run and print a report, not for everything to be green.

### Step 5 — point jichi at a model (the guided way)

The simplest way is the built-in wizard. Type:

```sh
./jichi setup
```

- It **asks you questions** — which provider, which model, and where your API key is
  — and **writes the config file for you**, then validates it. Answer the prompts. If
  you are using **LM Studio**, choose the OpenAI-compatible option and give the
  address LM Studio shows (usually `http://localhost:1234/v1`).
- **You should see**, at the end, a validation summary with no hard failures.
- **If you would rather write the file by hand:** create a file called `.jichi` in
  your home folder (`~/.jichi`) containing, for a local LM Studio server:
  ```json
  {
    "model": {
      "provider": "openai",
      "model": "local-model",
      "apiBase": "http://localhost:1234/v1",
      "roles": ["chat"]
    }
  }
  ```
  Replace `"local-model"` with the model name LM Studio is serving. For a hosted
  model, use `"provider": "anthropic"` (or `"openai"`) and set `"apiKeyEnv"` to the
  name of the environment variable holding your key (e.g. `"ANTHROPIC_API_KEY"`), so
  the key stays out of the file. Full details: [`INSTALL.md`](../INSTALL.md)'s
  *First-run setup* and [`CONFIG_TUTORIAL.md`](../CONFIG_TUTORIAL.md).

Then re-run the health check:

```sh
./jichi doctor
```

- **You should see** the model-related warnings gone. If `doctor` says the API key is
  missing, set the environment variable it names — for a hosted model,
  `export ANTHROPIC_API_KEY=...` (a local LM Studio server needs no key).

### Step 6 — your first turn

```sh
./jichi -p "say hello in one line" < /dev/null
```

- **You should see** a one-line reply from the model. That is jichi working end to end.
- **If it hangs or errors,** run `./jichi doctor` again and read the first failing
  line — it usually names exactly what is missing (a key, an unreachable server).

### Step 7 (optional) — install it so you can type `jichi` anywhere

```sh
make -j4              # build again as yourself (install does not build)
sudo make install     # copies jichi onto your PATH, with its man page
```

- **You should see** the files being copied. Afterwards, `jichi --version` works from
  any folder (not just the source folder, and without the `./`).
- **To remove it later:** `sudo make uninstall`.

## 5. Measured difficulties

### `install` refuses a binary built from a "dirty" tree
You see `refusing to install a binary built from a dirty tree ... stamped '<rev>-dirty'`.
This happens if you built with uncommitted changes. Fix: run `make -j4` again as
yourself, then `sudo make install` (M593). To install one on purpose:
`sudo make install ALLOW_DIRTY=1`.

### After a `sudo` build, a plain `make` says "Operation not permitted"
An older `sudo make install` that also built left root-owned files behind. Fix
without `sudo`: `make clean`, then build again. (jichi's own `install` avoids this by
not building — M586.)

### `make ci` (the full test suite) times out on a small or busy machine
`make ci` is for contributors; you do not need it to *use* jichi. If you run it,
add `JC_SMOKE_TIMEOUT_MULT=3` on a slow or loaded box, and judge the result by the
final line's exit status, not by scrolling (M368).

## 6. Possible difficulties (not seen here, but possible)

- A very old compiler that rejects a flag: the build works around it where it can,
  and `make info` shows what it detected. Very old glibc (< 2.17) needs `-lrt`, which
  the build adds automatically ([`PLATFORMS.md`](../PLATFORMS.md), the old-userland rows).

## 7. Verifying the install

You already did the important checks (Steps 3–6). To run jichi's own on-machine test
suite:

```sh
make check-target        # the unit tests + the shell test tier
```

You should see it finish with a summary and no failures. To prove a model really
works end to end: `./jichi doctor --live` (adds one real round trip). A platform is
only fully "tested" once a model has answered a tool-using turn on it
([`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §1) — on Debian/Ubuntu that is
already the reference, so you are confirming, not breaking new ground.

## 8. Reporting back

If something here did not match what you saw, that is worth telling the project. The
short template for a result is in [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3
("Writing the row"); sending a full result is
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).

## 9. Read further

- [`INSTALL.md`](../INSTALL.md) — requirements, first-run setup in more depth, and the files jichi creates.
- [`CONFIG_TUTORIAL.md`](../CONFIG_TUTORIAL.md) — configuring models, roles and tools once you are past hello.
- [`BUILD.md`](../BUILD.md) — the build system and its options (primary reference).
- [`PLATFORMS.md`](../PLATFORMS.md) — the verdict and the numbers behind it.
