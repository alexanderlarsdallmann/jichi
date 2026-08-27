# jichi under JupyterHub — what was measured, what was not, and how to decide

*This page owns the verdict for "can I use jichi with JupyterHub?". Other pages
link here rather than stating their own answer, for the reason
[`PLATFORMS.md`](PLATFORMS.md) gives: five pages once carried three different
confidence levels for the same untested thing, and a newcomer read whichever
one they opened first.*

---

## Evidence status, stated first

This page uses [`PLATFORMS.md`](PLATFORMS.md)'s three words strictly.

| Part | Status | Evidence |
|---|---|---|
| jichi in a **JupyterLab terminal** (Shape A) | **Verified** | 11 checks, run 2026-08-18 against JupyterLab 4.6.3 / jupyter-server 2.20.0 / terminado 0.18.1, plus a negative control that proves the checks can fail. §13 |
| jichi from a **notebook cell** (Shape B) | **Verified** | a real `nbconvert --execute` run; the jsonl event sequence is in §13 |
| **What a notebook costs** | **Verified** | measured with jichi's own accounting, not estimated. §4 |
| The **multi-user hazard** (the mechanism) | **Verified by reading the source** | `jc_lease_path()` and `jc_snapshot`, §8 |
| The **multi-user hazard** (the consequence) | **Partly verified** | the *precondition* is staged in a two-user Debian 12 VM — two agents editing one tree, every guard silent. The *harm* (one run reverting another's work) is not staged. §8a |
| A hub with **PAM + `LocalProcessSpawner`**, and the §9 config | **Verified** | installed and started in that VM. §13 |
| **Building jichi for a hub image**, and removing the toolchain afterwards | **Verified** | measured from a pristine Debian 12: three packages, a 5 s build, and it still runs once they are purged. §7 |
| **How the key reaches jichi**: server environment inherited by a terminal, the terminal being a *login* shell, and the user's startup files overriding the spawner | **Verified** | four probes into a live JupyterLab terminal. §9 |
| **When a lease is taken at all** | **Verified** | measured per invocation against a stalling model. §8a |
| **Browser key bindings** (xterm.js) | **Never run** | a websocket probe cannot press Ctrl-R in Firefox. §6, §15 |
| Any **other hub configuration** — DockerSpawner, KubeSpawner, OAuth, a shared network home | **Never run** | one configuration was tested. §15 |

> **What "Verified" cost, and why the number is trustworthy.** The rig that
> produced §13 was wrong four times before it was right, and each wrong version
> was **green**. It waited for a prompt marker that does not exist under
> `LC_ALL=C`, so it timed out on a healthy run and blamed the locale. It matched
> the terminal's *echo of the typed command* instead of the command's output, so
> two checks passed without anything running. It searched the whole transcript,
> so the second measurement of a repeated marker was satisfied by the first.
> The fix that matters is the **negative control**: the same suite is run
> against a stub that emits nothing, and it must go red. Three checks stay green
> there — the ones that describe the *terminal* rather than jichi — and that is
> the expected result, written down in advance.

---

## 1. Which part of this page is yours

- **A learner** — you have been given a JupyterHub account and want to use jichi
  in it: **§2**, then **§6** when something surprises you.
- **A junior developer** — you are wiring jichi into a hub, or scripting it:
  **§3**, **§4**, **§5**.
- **A learning administrator** — you run the hub for a course: **§7** first
  (there is no package; you build jichi), then **§8** (the part that can lose
  someone's work), then **§9**–**§12**.
- **Deciding whether to bother at all**: **§14**. It says what a hub buys you,
  what it costs, and what would make the answer *no*.

---

## 2. For learners: jichi in a browser terminal

A JupyterHub gives every user their own server, and that server can open a
**Terminal** — a real terminal, in a browser tab. jichi runs in it. Nothing has
to be installed by you and nothing has to be configured; if your administrator
put jichi in the image, you open a terminal and type:

```
jichi
```

That is the whole integration. There is no jichi plugin, no notebook extension,
and no button. That is not a disappointment — §3 explains why it is the right
answer.

### What works, and was checked

Everything you would notice: the prompt, colours, the tool lines with their
`▸` and `✓` marks, streamed answers, pasting several lines at once, and typing
while jichi is still working. All eleven checks in §13 passed in a real
JupyterLab terminal.

### Where your work lives

In **your** home directory, which is yours alone:

| What | Where |
|---|---|
| your project files | wherever you started jichi |
| jichi's memory of the conversation | `~/.jichi.d/sessions/` |
| its undo checkpoints | `~/.jichi.d/checkpoints/` |
| your API key | an environment variable, or `~/.jichi.env` (mode 0600) |

Nobody else on the hub can read those. **This was checked**: the terminal's
`$HOME` is exactly the one the server was started with, which is what makes all
of the above true. (It is checked because it has gone wrong before: jichi once
put a key file in `/tmp` when `HOME` was unset, and said nothing about it.)

### Three things that will surprise you

1. **Your terminal may not start where you expect.** A JupyterLab terminal
   inherits the *server's* working directory, not the folder you were browsing.
   Type `pwd` first. This was observed, not guessed.
2. **Some keys belong to the browser, not to jichi.** `Ctrl-R` reloads the page
   in every browser; jichi wants it for history search. See §6 — this is the one
   part of the terminal nobody has measured yet.
3. **jichi cannot see inside your notebooks.** To jichi an `.ipynb` file is a
   big JSON file. §4 is the honest version of that sentence, with numbers.

### Stopping it, and undoing it

- `Ctrl-C` interrupts the current turn. Twice at an empty prompt quits.
- `/undo` restores your files to jichi's last checkpoint.
- `/exit` quits.

If you are new to jichi itself rather than to the hub, read
[`PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) first; this page assumes you have used
jichi somewhere.

---

## 3. The four shapes, and which to pick

### Shape A — a terminal in JupyterLab · **recommended** · Verified

Install jichi in the single-user image. Users type `jichi`.

- **Cost:** packaging, and nothing else. No code, no adapter, no new surface.
- **You get:** everything jichi does, with per-user `$HOME`, per-user keys,
  per-user audit log and per-user checkpoints — all of it for free, because the
  hub already gives each user their own account.
- **Limits:** no notebook integration (§4); browser keys unmeasured (§6); a user
  can still point jichi at a shared directory, which is what §8 is about.

### Shape B — a notebook cell driving headless jichi · Verified

```python
import subprocess, json
p = subprocess.run(
    ["jichi", "-p", "summarise the failures in results.csv",
     "--output", "jsonl", "--no-session"],
    capture_output=True, text=True)
events = [json.loads(l) for l in p.stdout.splitlines() if l.startswith("{")]
```

- **Use `--output jsonl` and parse it.** Those event objects are the **stable**
  tier of jichi's contract ([`EMBEDDING.md`](EMBEDDING.md) §4): every object
  carries `v` and `type`, existing fields keep their meaning, and unknown types
  must be ignored. Prose summaries are explicitly *provisional* — do not scrape
  them.
- The observed event sequence for one tool-using turn is in §13.
- **Do not** start the interactive TUI from a cell. There is no pty, and jichi
  will correctly fall back to non-interactive behaviour rather than doing what
  you meant.
- Add `--auto` only with an `--edit-scope`, and read §8 first.

### Shape C — a hub service, or a kernel/magic · **recommended against**

A hub-level service keeping a jichi daemon warm and routing cell requests to it.

Three reasons not to build it:

1. **It re-creates the multi-user problem inside jichi.** A shared daemon holds
   one process's `$HOME`, so the per-user isolation Shape A gets for free —
   keys, audit log, checkpoints, leases — would have to be re-invented, and
   jichi has no notion of a caller identity to key any of it on.
2. It would sit on the daemon surface, whose own open questions are still
   recorded in [`DEFERRED.md`](DEFERRED.md).
3. **The demand is hypothetical.** Nobody has reported Shape A is insufficient.
   §15 states what evidence would change this.

### Shape D — the web bridge behind `jupyter-server-proxy` · **Never run**

`examples/web-bridge/bridge.py` binds loopback and mints a boot token — it says
so itself: *"a boot token (the Jupyter model)"*. `jupyter-server-proxy` can put
it at `/user/<name>/proxy/8765/` with **the hub's authentication in front**,
which supplies the one thing the bridge deliberately lacks: real accounts.

This is a genuinely attractive fit and it is **untested**. Two specific
unknowns: whether Server-Sent Events survive the proxy without buffering, and
whether the bridge's own token adds anything once the hub authenticates. Treat
it as a design worth trying, not a recipe.

---

## 4. The notebook problem, with numbers

**jichi has no notebook support at all.** `grep -rc ipynb src/ include/` returns
nothing. To jichi, `analysis.ipynb` is a large JSON file with base64 blobs in it.

That is usually stated as a caveat. Here is what it actually costs, measured
with jichi's own accounting — the same byte heuristic its compaction acts on —
by having it `read_file` each of three byte-stable fixtures:

| file | bytes | tokens the tool result cost | truncated at the 256 KB read cap? |
|---|---:|---:|---|
| `small.ipynb` (5 cells, no outputs) | 1,373 | ~457 | no |
| `small.py` (the jupytext pair) | 683 | ~253 | no |
| `large.ipynb` (200 cells, no images) | 43,027 | ~13,520 | no |
| `large.py` | 18,340 | ~6,326 | no |
| **`outputs.ipynb`** (5 cells, **one figure**) | 315,718 | **~65,631** | **yes** |
| **`outputs.py`** | 689 | **~255** | no |

Read the last two rows twice. **One notebook with one plot in it cost ~65,600
tokens — and it was still truncated**, because it is larger than jichi's 256 KB
`read_file` cap. The same code as a paired `.py` cost ~255. That is **257×**.

Two consequences that are not obvious:

- **Even a notebook with no outputs costs about twice its code**, because JSON
  structure, cell ids and metadata are all real bytes.
- **The cost is dominated by outputs, not by code.** `outputs.ipynb` has *fewer*
  cells than `large.ipynb` and costs five times more.

### What to do about it

1. **Pair the notebook and point jichi at the `.py`.** This is the
   recommendation.
   ```
   jupytext --set-formats ipynb,py:percent analysis.ipynb
   ```
   Jupyter keeps the two in sync as you work. Honest cost: jichi cannot see your
   outputs, and the pairing is a habit you have to keep.
2. **Strip outputs before jichi reads a notebook**, with `nbstripout`. This turns
   the 65,631-token row into something like the 457-token one.
3. **If you must hand jichi a notebook, wrap the conversion in a user-defined
   tool** — config `tools[]`, no C change, see [`USER_TOOLS.md`](USER_TOOLS.md).
   A three-line shell wrapper around `jupytext --to py:percent` gives the model
   a way to read cells without you converting by hand.

### What jichi could do, and deliberately has not

Teaching `read_file` to render an `.ipynb` as cells would follow the pattern the
PDF path already uses (detect the extension, transform to text, never mark it
read-before-edit because it is not editable that way). It is modest and in
character. It is **not built**, because the use case is undecided — see §14. Full
cell-*editing* is recommended against outright: it is a new corruption class in
exchange for something `jupytext` already delivers.

---

## 5. For junior developers: what is a promise and what is not

Read [`EMBEDDING.md`](EMBEDDING.md) before you build anything on jichi. Its four
tiers apply here unchanged:

- **Stable** — exit codes, the jsonl event objects, `stop_reason` values, the
  core headless flags, and the JSON projections of `ls`/`export`/`status`.
  Build on these.
- **Provisional** — prose summaries, the tool set, telemetry JSONL, `.jichi/`
  assets.
- **Not an interface** — stderr text, the on-disk session store, and internal C
  symbols. jichi is a program, not a library: there is no `libjichi`.

Concretely, for a hub:

- Drive jichi with `-p … --output jsonl`. Parse events by `type`, **ignore types
  you do not know** — that rule is what lets the schema grow without breaking
  you.
- `--heartbeat <secs>` emits a `heartbeat` event while a model call is in
  flight, so a supervisor can tell "wedged" from "long model call".
- Exit codes are the supervisor's contract: 0 ok, 1 failed, 2 usage, 130
  interrupted, 143 graceful SIGTERM.
- Extend jichi through **user-defined tools** (`tools[]`) and MCP, not by
  patching it. The notebook wrapper in §4 is the worked example.

---

## 6. The surprises list

One place, for the person who just hit one.

| Surprise | Why | What to do |
|---|---|---|
| The terminal opened somewhere unexpected | terminado inherits the **server's** cwd, not the file browser's location or `--ServerApp.root_dir` | `pwd`, then `cd`. Administrators: start the server where users should land |
| `Ctrl-R` reloaded the page | the browser owns that key before xterm.js sees it | **Unmeasured** — see below. Use the arrow keys for history |
| `Ctrl-G` did something odd in Firefox | Firefox binds it to find-next; jichi wants it for ghost text | **Unmeasured** |
| Tool lines show `>` and `ok` instead of `▸` and `✓` | your image's locale is not UTF-8 | that is jichi behaving correctly; fix the image's locale if you want glyphs |
| No colour | `NO_COLOR` is set, or output is not a terminal | jichi honours `NO_COLOR`; verified |
| jichi says it has no API key, though you set one in `~/.bashrc` | the terminal is a **login** shell, which reads `~/.profile`, not `~/.bashrc` | put the line in `~/.profile`, or ask your administrator to set it for everyone. Measured, §9 |
| A pasted block behaved oddly | bracketed paste is on and works over the websocket (verified) — but the browser's own paste path was not tested | see below |

**The honest gap.** Everything above marked *unmeasured* is the **browser half**.
The rig that produced §13 speaks terminado's websocket directly, so it tests
jupyter-server and terminado completely and xterm.js not at all. A websocket
cannot press Ctrl-R in Firefox. The checklist to run by hand, in a browser,
ships with the rig and is reproduced in §13.

---

## 7. Getting jichi into your hub — there is no package

**jichi is released as source code.** There is no distribution package, no
wheel, no container image and no binary release. Whoever deploys it **builds
it**. That makes the build a deployment step, so here it is with numbers rather
than adjectives — measured from a **pristine Debian 12 cloud image**, which
turns out to have no compiler at all.

### What a stock image has, and what you must add

| | |
|---|---|
| already present | `curl` (the program) |
| **absent** | `cc`, `gcc`, `make`, `git`, `pkg-config`, and the curl **headers** |
| you must add | **`gcc make libcurl4-openssl-dev`** — three packages, nothing else |

jichi has exactly one external dependency, **libcurl**, and vendors no
third-party source. No Python, no Node, no package manager of its own.

### The numbers

| Step | Measured |
|---|---|
| `apt-get install gcc make libcurl4-openssl-dev` | **45 s** |
| `make jichi` | **5 s** on 2 CPUs |
| the binary | **1,754,680 bytes**; **1,643,800** stripped |
| resident footprint in use | ~9.5 MB, peak ~17 MB ([`PLATFORMS.md`](PLATFORMS.md)) |

A five-second build is worth knowing before you plan an image pipeline around
it.

### The build dependencies can be removed afterwards — verified

This is the question a container image turns on, so it was tested rather than
assumed. After building, all three build packages were **removed**:

- `gcc` gone, curl headers gone;
- only the **runtime** `libcurl4` remains (a dependency your image almost
  certainly already has, since anything speaking HTTPS pulls it);
- `jichi --version` still runs;
- `jichi doctor` still reports **`✓ libcurl available (networking enabled)`**.

So a two-stage container image is worth it, and a single-stage image can simply
`apt-get remove` the toolchain in the same layer.

### A single-user image (container spawners)

```dockerfile
FROM quay.io/jupyter/base-notebook:latest

USER root
COPY jichi-src /tmp/jichi-src
RUN apt-get update  && apt-get install -y --no-install-recommends gcc make libcurl4-openssl-dev  && make -C /tmp/jichi-src jichi  && install -m 0755 /tmp/jichi-src/jichi /usr/local/bin/jichi  && apt-get purge -y gcc make libcurl4-openssl-dev  && apt-get autoremove -y && rm -rf /tmp/jichi-src /var/lib/apt/lists/*
USER ${NB_UID}
```

Adjust the base image to whichever one your hub already spawns; the three lines
that matter are install, `make`, purge. **Verify inside the image** with
`jichi doctor` — it reads the environment and names what it cannot confirm.

### Without containers (TLJH, a VM, a plain hub node)

Build once, install to a path every spawned user can read:

```sh
make jichi
sudo install -m 0755 jichi /usr/local/bin/jichi
```

One binary, on a shared read-only path, is the whole deployment. Nothing is
per-user except the state in `$HOME` (§2).

### If you are a learner and nobody installed it

You can build it in your own home directory **if** the image has a compiler and
the curl headers — many Jupyter images have neither. Try:

```sh
cc --version && ls /usr/include/curl/curl.h
```

If both answer, `make jichi` gives you `./jichi` in about five seconds and you
need no administrator. If the headers are missing, jichi still *builds* — the
core compiles without libcurl — but it **cannot call a model**, which for an
agent means it cannot do anything useful. Ask your administrator rather than
working around it.

### The licence, stated plainly because you are planning a deployment

At the time of writing there is **no `LICENSE` file in the jichi tree, so no
licence is granted** — that is the copyright default, not a policy. The licence
is expected to be **Apache-2.0** and is waiting on a rights question put to the
university.

For you as an administrator this means: **check the `LICENSE` file in the source
you actually received** before planning a deployment for students. If it is
absent, the answer to "may we run this for our cohort?" is a question for your
institution's legal or IT-governance people, not one this page can answer.
[`README.md`](../README.md) states the current position and what it is waiting
on.

---

## 8. For administrators: the part that can lose work

This is the section worth your time. It is not a bug report about JupyterHub,
and it is not really about JupyterHub at all — it is about a configuration that
hubs make normal and that jichi has never been designed for.

**jichi's threat model is one operator, one machine, and a model that may
misbehave.** It is not "twenty students on one node".
[`DEPLOYMENT.md`](DEPLOYMENT.md) has sections on SSH, embedded devices,
air-gapped hosts and unattended runs, and — before this page — nothing on
multi-user at all.

### 8a. The workspace lease is per-user, so it does not protect a shared tree

[`AUTONOMY.md`](AUTONOMY.md) is blunt: jichi has **no lock of any kind**, and the
autonomy envelope assumes **one actor per tree**. The mitigation is an advisory
lease — and, verified by reading `jc_lease_path()`, it is written to:

```
<home>/.jichi.d/leases/<hash-of-the-work-tree>.json
```

`<home>` comes from `jc_home_dir()`. In a hub every user has their own `$HOME`.
So two users running jichi in the **same shared directory** compute the **same
key** and write it into **different homes**. Each takes a lease, neither sees
the other's, and both proceed as if alone — *including under `--lease fail`*.

> **This was staged, in a two-user VM, and it behaves exactly as the source
> says.** Debian 12, two real Unix accounts (`stud1`, `stud2`), one
> group-writable `/srv/shared`, jichi built in the guest:
>
> - `stud1` starts a bounded run that stalls inside the model call, so it sits
>   there **holding** its lease:
>   `/home/stud1/.jichi.d/leases/13790145076329198101.json`.
> - `stud2` then starts a run on the **same tree** with `--lease fail`, the
>   strictest setting there is.
> - **`stud2` ran to completion.** It was not warned, not delayed and not
>   refused; it wrote into the shared tree while `stud1`'s run was live.
> - Both users' checkpoint directories are keyed `13790145076329198101` — the
>   **same** tree — one per home.
>
> What that measurement does *not* show is the harm: nothing here staged one
> run's `revertOutOfScope` sweep reverting the other's work. What it shows is
> the **precondition** — two agents editing one tree with every guard that
> exists reporting nothing at all.

The lease is not broken. It was designed for one user's concurrent runs and it
does that correctly. But its protection evaporates in exactly the configuration
a shared course directory creates, and the failure mode is not a conflict
message. With `revertOutOfScope` armed, jichi's end-of-turn sweep diffs the
whole tree against a run-start baseline and **cannot tell a sibling's edits from
an out-of-scope write by the model it polices**.

> **There is a second half to this, and it surprised the person writing the
> page.** A lease is taken **only when the autonomy envelope is armed** — the
> acquisition sits behind `if (app.env != NULL)`. Measured by running each
> variant against a stalling model and listing the lease directory mid-run:
>
> | invocation | lease taken? |
> |---|---|
> | `--auto` alone | **no** |
> | `--auto --edit-scope '…'` | yes |
> | `--auto --budget-tokens N` | yes |
> | `--auto --journal <path>` | yes |
>
> So a course policy of "students may use `--auto`" gets **no lease at all**,
> and the one that is taken is per-`$HOME` anyway. If you tell learners to use
> `--auto`, tell them to bound it — which you want for other reasons (§9).

> **The rule: in a hub, `--auto` with `revertOutOfScope` must not be used on a
> shared writable directory.** Per-user workspaces make the whole problem
> disappear.

### 8b. Checkpoints are per-user too, so `/undo` is per-user

The shadow git repo lives at `<home>/.jichi.d/checkpoints/<same-key>` and its
work tree is the shared directory. Two users therefore hold **two independent
checkpoint histories over one tree**. `/undo` restores *your* view of a tree
someone else has since changed. Nothing warns about this. Same mitigation.

### 8c. Capacity: the fork pool sizes itself to the node, not to your share of it

`maxParallelAgents` defaults to `0`, meaning auto = `min(cpu, 8)`. On a 32-core
hub node, ten users each running `spawn_parallel` can ask for eighty concurrent
agent forks. jichi is behaving exactly as documented; it simply has no idea it
is sharing. **Mitigation: set `maxParallelAgents` and `memBudgetMb` hub-wide** —
ordinary config keys, no new feature (§9).

### 8d. Secrets are per-user, and that part is already right

`apiKeyEnv` plus a per-user `~/.jichi.env` at mode 0600 is the documented
pattern, and per-user `$HOME` makes it correct in a hub with no changes. Two
cautions:

- A hub-wide config must **never** carry a literal `apiKey`. `doctor` warns
  about literals; do not ship past that warning.
- A **fixed `logging.path`** in a shared config merges every user's telemetry
  into one file. Leave telemetry at its per-`$HOME` default.

---

## 9. A hub-wide configuration to start from

Ship this as a read-only file (say `/srv/jichi/config.json`) and point users at
it with `$JC_CONFIG`. Every key here exists; none of them is new.

```jsonc
{
  // Capacity: a hub node is shared. See §8c.
  "maxParallelAgents": 1,        // never min(cpu,8) on a shared node
  "memBudgetMb": 512,
  "lowResource": true,           // smaller caps, fewer resident subsystems
  "toolProfile": "auto",

  // Containment.
  "pathFence": 1,                // reads and writes stay in the workspace

  // Secrets: an env var NAME, never a key. Each user exports their own --
  // and note the login-shell caveat below before choosing where they do it.
  "models": [
    { "name": "course-chat",
      "provider": "openai",
      "model": "<the id your gateway serves>",
      "apiBase": "https://<your-gateway>/v1",
      "apiKeyEnv": "JICHI_API_KEY",
      "contextLength": 32000,    // declare it: see GATEWAY_ADMIN.md
      "roles": ["chat", "edit", "apply"] }
  ]

  // Deliberately ABSENT: "logging": {"path": ...}. A fixed path merges every
  // user's telemetry into one file. Leave it per-$HOME.
}
```

### If your institution runs an LLM gateway — and what it does to §4's numbers

Many universities put one OpenAI-compatible endpoint in front of the models they
host, with a personal key per member. If yours does, that is the whole model
configuration: [`curriculum/INSTITUTIONAL.md`](curriculum/INSTITUTIONAL.md) is
the pattern and carries a worked instance for the **JLU Gießen HRZ** gateway
(base URL, model ids, and the ready-made secret-free config).

Two details a hub operator needs:

- **The key's environment-variable name is yours to choose.** `apiKeyEnv` names
  it; the current default in the setup wizard and example configs is
  **`JICHI_API_KEY`**, while `JLU_API_KEY` still works wherever a config or a
  user's `~/.jichi.env` names it — which matters because the HRZ onboarding
  hands people that name. Pick one for your cohort and put it in
  `c.Spawner.environment` so nobody has to be told twice.
- **If your gateway does not cache prompts, §4's notebook numbers get worse —
  and this is measured, not feared.** The HRZ gateway reports **zero**
  prompt-cache hits across 967 real calls, so the repeated prefix of an agent
  conversation is re-processed at full cost on every call. A tool result is
  **re-sent with every later request in the same turn**. So a 65,631-token
  notebook read is not a one-off charge of 65,631 tokens: it is 65,631 tokens
  **again on every subsequent request of that turn**. On a caching backend you
  would barely notice; on a non-caching one it is the dominant line in your
  bill.

  Practical consequences for a course: prefer the paired `.py` (§4) rather than
  merely recommending it, `/compact` earlier, prefer a fresh session per task
  over one long one, and check your own numbers with `jichi telemetry`. jichi
  also injects a short **cost-model** section into its system prompt
  automatically when prompt caching is off, so the model itself is told that
  reads are expensive — you do not have to configure that.

On the JupyterHub side, give each user their key through the spawner rather than
a file you have to distribute:

```python
# jupyterhub_config.py
c.Spawner.environment = {"JC_CONFIG": "/srv/jichi/config.json"}
c.Spawner.default_url = "/lab"
```

### What `c.Spawner.environment` actually does, and what it is wrong for

`Spawner.environment` is a JupyterHub config trait: a dict of environment
variables placed in the **single-user server's process environment** when the
hub spawns it. Every spawner class inherits it, so the same line works for
`LocalProcessSpawner`, `DockerSpawner` or `KubeSpawner`.

**A terminal inherits it — measured.** A variable set only in the server's
environment was visible in a JupyterLab terminal. That is what makes it the
right vehicle for `JC_CONFIG`: one path, same for everyone, not a secret.

**It is the wrong vehicle for an API key**, for two reasons:

1. The dict is *the same for every user*. A key is per-user. (JupyterHub does
   accept a callable — `c.Spawner.environment = lambda spawner: {...}`, with
   `spawner.user.name` available — which is how you would inject per-user
   secrets from a real secret store. **Not tested here**; it is JupyterHub's
   documented mechanism, not a measurement of ours.)
2. **Setting it to an empty string does nothing at all.** jichi's `resolve_key`
   uses `getenv(apiKeyEnv)` *only when the value is non-empty*, and otherwise
   falls through to the provider convention (`OPENAI_API_KEY`). An earlier draft
   of this page had `"JICHI_API_KEY": ""` in the dict; it was removed, because a
   line that looks like it reserves a variable and in fact does nothing is worse
   than no line — and in the fall-through case it could quietly let a stray
   `OPENAI_API_KEY` in the image be used instead.

**The user's shell wins over the spawner, and that is a two-edged fact.** Also
measured: with a stock `~/.profile` in place, a `~/.bashrc` export **overrode**
the value the server had been given. So `Spawner.environment` sets a *default*
that a user can replace — usually what you want for a key, and worth knowing
before you assume it enforces anything.

> **The gotcha that will actually cost someone an afternoon.** A JupyterLab
> terminal runs bash as a **login shell** (measured: `shopt -q login_shell` says
> yes). A login shell reads `/etc/profile` and `~/.profile` / `~/.bash_profile`
> — **not** `~/.bashrc`. jichi's own setup wizard tells users to add
> `[ -f ~/.jichi.env ] && . ~/.jichi.env` to `~/.bashrc`, and that advice is
> written for an ordinary desktop terminal, which is a *non-login* shell.
>
> In a hub it works only because a stock Debian/Ubuntu `~/.profile` sources
> `~/.bashrc`. Both halves were measured: with no `~/.profile`, the `.bashrc`
> export was **not** applied; with the stock stanza restored, it was.
>
> **So:** if your image ships a minimal home skeleton, or you create home
> directories yourself, a user's key setup will silently not fire and jichi will
> report no API key. Put the line in `~/.profile` instead, or ship a skeleton
> that sources `.bashrc`, or set the variable in `Spawner.environment`.

Then have each user put their own key in `~/.jichi.env` (0600) — noting the
login-shell caveat above — or read it from whatever per-user secret store your
institution already has. If your institution
runs an LLM gateway, [`curriculum/INSTITUTIONAL.md`](curriculum/INSTITUTIONAL.md)
is the config pattern and [`GATEWAY_ADMIN.md`](GATEWAY_ADMIN.md) is what the
gateway itself must get right for an agent (an agent is not a chat UI: it sends
30–50 requests per task, each carrying the whole conversation).

Validate with `jichi doctor`, which reads the environment and names what it
cannot verify.

---

## 10. Capacity: what a seat costs

jichi's own footprint is small and measured — around **9.5 MB** resident, with a
peak around **17 MB**, on real hardware ([`PLATFORMS.md`](PLATFORMS.md)). It runs
its full gate inside a **256 MB** VM. Compared with the Python stack a hub is
already running per user, jichi is not what will size your node.

What *can* size your node is the fork pool (§8c) and the model context, so:

- set `maxParallelAgents` to 1 or 2 hub-wide;
- set `memBudgetMb`;
- set `contextLength` honestly for the model — an undeclared window makes jichi
  guess, and its guess is optimistic.

**Not measured here:** N concurrent users on one node. §8c is arithmetic from a
documented default, not a measurement, and this page will not dress it up as one.

---

## 11. Cohort operations

A hub gives you something no other deployment does: **one filesystem holding
every learner's work**. That makes a cohort view a shell loop rather than a
product.

```sh
for h in /home/*; do
  [ -f "$h/course/.jichi/progress.jsonl" ] || continue
  printf '%s: ' "$(basename "$h")"
  jichi --config /srv/jichi/config.json assignments --output json 2>/dev/null |
    jq -r '[.[] | select(.status=="passed")] | length'
done
```

Be honest with yourself about what this is: **there is no cohort view in jichi,
deliberately.** It is recorded as an open deferral, and the reason is that a
gradebook needs identity, storage and a policy about grades, none of which belong
in a coding agent. jichi provides the machine surface; a sidecar owns the
aggregation. If you build that sidecar, build it on `--output json`.

For the teaching side of this — the curriculum, the graded assignments, the
instructor's guide — see [`CURRICULUM.md`](CURRICULUM.md) and
[`curriculum/INSTRUCTOR.md`](curriculum/INSTRUCTOR.md).

---

## 12. Security in a multi-tenant hub

**Where a hub is better than a single box.** jichi's oldest safety deferral is
that `run_terminal_command` has no OS-level sandbox, and its stated answer has
always been *"the real fix is deployment: run as a non-root user, in a
container or VM"*. **A hub already does that** — every user's server is a
separate account, and with a container spawner a separate container. Running
jichi under a hub is a stronger posture than running it on a shared login box,
not a weaker one.

**What still needs your attention:**

| Concern | Where it stands |
|---|---|
| Keys | per-user `$HOME`, `apiKeyEnv`, `~/.jichi.env` at 0600. Correct by construction; §8d |
| Privileged commands | a model-issued `sudo`/`doas`/`pkexec` is detected and gated *below* the approval verdict, and every attempt is audited to `~/.jichi.d/audit/`. Per-user, so per-user auditable |
| The shell | not sandboxed inside jichi. The hub's per-user account **is** the boundary. Do not give that account privilege |
| Untrusted content | pages, feeds and MCP resources a *model* chose to fetch are fenced as data, not instructions — a mitigation, not a fix. The defences that do not need the model's cooperation are the path fence, approvals, edit scope and budgets |
| Shared writable directories | **the real risk**, and it is §8. Per-user workspaces |
| Telemetry | leave `logging.path` unset so each user's stays in their own home |

Read [`HARDENING.md`](HARDENING.md) for the full posture.

---

## 13. What was actually run

**Tier J1 — jichi in a JupyterLab terminal.** JupyterLab 4.6.3, jupyter-server
2.20.0, terminado 0.18.1, Python 3.14.4, jichi 0.9.0, 2026-08-18. The rig drives
terminado's websocket directly; the same turn is driven first on a bare pty as a
control.

| # | Check | Result |
|---|---|---|
| 1 | a pty on stdin **and** stdout (jichi's `isatty` gate) | ok |
| 2 | the terminal's `$HOME` is exactly the server's | ok |
| 3 | terminado propagates `TIOCGWINSZ` — 100×40 **then** 132×24, both observed | ok |
| 4 | jichi starts its interactive TUI | ok |
| 5 | jichi enables bracketed paste (`ESC[?2004h` reaches the client) | ok |
| 6 | a full turn: tool line, success glyph, streamed answer | ok |
| 7 | a pasted 3-line block does **not** submit on its embedded newlines | ok |
| 8 | the pasted block submits as **one** logical line on Enter | ok |
| 9 | `LC_ALL=C`: ASCII fallbacks, no UTF-8 glyphs | ok |
| 10 | `NO_COLOR=1`: no SGR escapes in jichi's output | ok |
| 11 | text typed during a turn survives **and** the turn completes | ok |
| — | **Shape B**: a notebook cell drives headless jichi and gets parseable jsonl | ok |

The observed jsonl sequence for one tool-using turn:

```
message_start  usage  tool_call  tool_result  message_start  text  usage  done
```

**Two sizes, not one, in check 3** on purpose: a single measurement is satisfied
by a hardcoded default.

**The negative control.** The identical suite run against a stub that emits
nothing must go red, and does. Exactly three checks stay green — 1, 2 and 3 —
because they describe the *terminal*, not jichi, and are true whatever binary
runs. Check 7 also stays green and says so in the rig: it is a negative
assertion, and a stub satisfies it by doing nothing; its teeth are in check 8.

**Tier J2 — two users, one tree.** A Debian 12 VM on KVM, jichi built in the
guest, two real Unix accounts sharing a group-writable directory. 2026-08-18.

| # | Check | Result |
|---|---|---|
| 1 | jichi builds in the guest and installs | ok |
| 2 | the guest reaches the host's mock model (ssh reverse tunnel) | ok |
| 3 | while running, `stud1` holds a lease **in its own home** | ok |
| 4 | **the finding**: `stud2` ran to completion on the same tree while `stud1` held a lease under `--lease fail` | ok |
| 5 | two independent checkpoint histories over one tree, same key, one per home | ok |
| 6 | the cp311 guest wheelhouse installs JupyterHub 5.5.1 **offline** in the guest | ok |
| 7 | `configurable-http-proxy` from Debian's `npm` | ok |
| 8 | the hub starts with PAM auth + `LocalProcessSpawner` | ok |
| 9 | the hub-wide jichi config is readable by a spawned user and names an env var, never a literal key | ok |

Rows 6–9 are what makes §9 a recipe rather than a sketch: that configuration was
installed and started, not written from memory. Row 6 also settled a question
about the *preparation*: a wheelhouse built for another interpreter is **not**
verified by a successful download. `pip download --python-version 3.11` silently
omitted `overrides>=5.0; python_version < "3.12"` — a dependency the cp314 host
never needs — and the gap appeared only when the wheelhouse was installed on the
target.

### Reproducing all of it

The rigs are in `scripts/`, and they are an **operator tier** — deliberately
outside `make ci`, like `scripts/tier-v-terminals.sh`, because they want a
Python virtualenv with JupyterLab in it and (for J2) qemu.

```sh
scripts/jhub-prepare.sh --dry-run     # prints every step, touches nothing
scripts/jhub-prepare.sh               # ~150 MB: two wheelhouses, sdists, fixtures

scripts/jhub-measure-notebooks.sh     # the §4 table
scripts/jhub-verify.sh                # Tier J1
scripts/jhub-verify.sh --negative-control   # PROVE the checks can fail
scripts/jhub-tier-j2.sh               # Tier J2 (a VM; needs qemu + /dev/kvm)
scripts/jhub-build-in-image.sh        # the §7 build numbers
```

Artifacts and results go to `$JHUB_DIR` (default `~/.cache/jichi-jupyterhub`),
**never** the checkout — `jhub-prepare.sh` refuses a `$JHUB_DIR` inside it, for
the reason [`SESSION_RUNBOOK.md`](SESSION_RUNBOOK.md) gives.

**Run `--negative-control` before believing a green J1 run.** Four versions of
that probe were green and wrong; the control is what caught them, and §13's
opening note says what each mistake was.

### The manual checklist this cannot replace

Run these in a browser, against a JupyterLab terminal. Each is a key jichi binds
that a browser also binds; the question is who wins.

```
[ ] Ctrl-R   jichi: reverse history search.  Browser: reload the page.
[ ] Ctrl-G   jichi: ghost-text suggestion.   Firefox: find-next.
[ ] Ctrl-W   jichi/readline: delete word.    Some browsers: close the tab.
[ ] Ctrl-C   jichi: interrupt the turn (twice at an empty prompt = quit).
[ ] Ctrl-D   jichi: end input / quit at an empty prompt.
[ ] Ctrl-L   clear.
[ ] paste    Ctrl-Shift-V or right-click: does a 3-line paste stay one line?
[ ] resize   drag the window mid-turn: does the redraw follow?
[ ] glyphs   are the tool lines drawn as UTF-8? (a font question, not a jichi one)
```

---

## 14. Deciding whether a hub is worth it

You may be reading this to decide whether to run jichi in a hub at all. Here is
what the evidence supports.

### What a hub actually buys you

| You get | Because |
|---|---|
| **No bench setup per learner** | the image has jichi; a student opens a terminal and works. Module 0 of the curriculum becomes "open a terminal" |
| **Per-user isolation for free** | keys, sessions, checkpoints, audit log — all `$HOME`-keyed, and the hub gives every user a `$HOME` |
| **A better security posture than a shared login box** | §12 |
| **One filesystem for cohort operations** | §11 |
| **A browser is the only client** | which matters for a tablet, a Chromebook, or a locked-down lab machine |

### What it costs you

| Cost | Size |
|---|---|
| Running a hub | this is the real cost, and it is not jichi's — a hub is a service with users, storage, upgrades and an authenticator |
| Notebooks work poorly with jichi | §4. Real, measured, and only partly mitigable |
| Browser keys are unmeasured | §6. Ten minutes with a browser closes this |
| Shared directories are a trap | §8. Free to avoid, expensive to hit |

### What would make the answer *no*

- **Your learners live entirely in notebooks and will not keep a paired `.py`.**
  Then jichi sees 257× the tokens for the same code and cannot see the outputs
  anyway. Fix the workflow first, or do not use jichi for that course.
- **You need one shared writable project directory** that several people edit at
  once with `--auto`. Do not do this (§8). If it is genuinely required, the
  honest answer is OS-level isolation per user, not a lock inside jichi.
- **You wanted a notebook-native AI assistant.** jichi is not one and this page
  will not pretend otherwise. It is a terminal coding agent that happens to run
  well in a terminal a browser is displaying.

### If you are answering a loose "can we use this?" question

The short version, defensible from what is on this page:

> **Yes, and the easy path is genuinely easy** — a terminal in JupyterLab, with
> no jichi-side integration to build. That was tested: 11 checks in a real
> JupyterLab terminal, plus a notebook cell driving it headless.
>
> **Three things to decide before committing:**
> 1. **Notebooks.** jichi has no notebook support. A notebook with one figure
>    costs 257× the same code as a paired `.py`, and on a gateway without prompt
>    caching that cost repeats every call. If your cohort lives in `.ipynb` and
>    will not keep a paired `.py`, fix the workflow first.
> 2. **Shared directories.** Never point an `--auto` run with
>    `revertOutOfScope` at a directory two people can write. jichi's lease and
>    checkpoints are per-`$HOME`, which was staged and confirmed in a two-user
>    VM. Per-user workspaces make this disappear entirely.
> 3. **Licensing.** jichi ships as source with no package, and at the time of
>    writing carries no `LICENSE` file (§7). Check the source you actually
>    receive before planning a deployment.
>
> **What nobody has checked yet:** the browser half — whether `Ctrl-R` reaches
> jichi or reloads the page. That is ten minutes with a browser and the
> checklist in §13.

### What would change these recommendations

- **Notebooks turn out to be the point** → the `.ipynb`-reading change in §4
  becomes a real milestone, and cell editing gets re-argued with a corruption
  test plan.
- **Someone reports Shape A is insufficient**, with the specific thing they could
  not do → revisit Shape B's ergonomics first, Shape C last.
- **Shape D is tried** and SSE survives the proxy → it becomes the recommended
  answer for learners who want a browser UI rather than a terminal.

---

## 15. Honest limits, in one place

1. **Two configurations were tested, and they are both plain ones**: JupyterLab
   from a virtualenv on Linux (Python 3.14, Tier J1), and a Debian 12 VM running
   JupyterHub 5.5.1 with **PAM auth + `LocalProcessSpawner`** (Python 3.11, Tier
   J2). Not DockerSpawner, not KubeSpawner, not OAuth/Shibboleth, not a network
   or NFS home directory, not The Littlest JupyterHub, not a container image.
   A **network home shared between nodes** is the one worth calling out: every
   per-user guarantee on this page rests on `$HOME`, and nothing here tested a
   `$HOME` that two machines mount at once.
2. **The browser was never involved.** Everything in §13 came through
   terminado's websocket. xterm.js, browser key bindings, browser paste and
   browser resize are **unmeasured**.
3. **No resource figure for N concurrent users.** §8c is arithmetic from a
   documented default.
4. **Shape D is a design, not a recipe.** Nothing has been run.
5. **The notebook numbers are jichi's own byte heuristic**, which is what jichi
   acts on — but it is not a real tokenizer, and a model's own count will differ.
   The 257× ratio is robust to that; the absolute figures are not exact.
6. **`jichi doctor` does not know about hubs.** It will not warn you about a
   shared workspace, because jichi cannot see that another user exists.
7. **No learner has used this.** Every measurement here is a rig's. The rig can
   check that a paste survives; it cannot tell you whether a first-year student
   understands what jichi just did to their files.
