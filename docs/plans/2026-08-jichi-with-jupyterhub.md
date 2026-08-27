# jichi as a CLI tool under JupyterHub — what works, what does not, and what I have not measured

*2026-08-17. Written in answer to a direct question: **can jichi be used as a CLI
tool with JupyterHub?** Short answer: **yes, and the easy path is genuinely easy —
but JupyterHub is a multi-user system and jichi has only ever been designed,
documented and tested single-user.** Two of its safety mechanisms are keyed per-user
and silently stop protecting anything the moment two users share a directory. That is
the part of this document worth reading.*

> **Evidence status, stated first.** **Nothing here has been run.** There is no
> JupyterHub, no `jupyter`, and no `jupyter-server` on this bench
> (`command -v` says so for all three). Every claim below is either (a) a fact about
> jichi verified by reading its source or its verified rows, or (b) a claim about
> JupyterHub's behaviour, which is **design reasoning and unverified**. Claims of
> type (b) are marked **[unverified]**. By this project's own vocabulary
> ([`PLATFORMS.md`](../PLATFORMS.md)) a JupyterHub integration is **never run** —
> not "probably fine".
>
> ---
>
> **SUPERSEDED IN PART, 2026-08-18 (M478). This plan was executed.** §6 items 1
> and 3 are done and item 4 is recorded as a deferral. The reader-facing page is
> [`JUPYTERHUB.md`](../JUPYTERHUB.md), which **owns the verdict** and carries the
> measurements; this page is kept as the design record — the reasoning, and the
> alternatives it rejected — not as the current statement of what is true.
>
> What changed against the predictions below, in one line each:
>
> - **§1's terminal table is no longer [unverified].** All four requirements were
>   measured in a real JupyterLab terminal (11 checks, plus a negative control
>   proving they can fail). Every one held.
> - **§2a's collision was staged and reproduced.** With one user's run holding
>   its lease, a second user's run completed on the same tree under `--lease
>   fail`. The *harm* (a revert) was not staged; the precondition was.
> - **§3's notebook claim gained numbers, and they are worse than the prose
>   suggested:** a notebook with one figure cost **~65,631 tokens** — over the
>   256 KB read cap, so truncated as well — against **~255** for the same code as
>   a jupytext-paired `.py`. **257×.**
> - **One thing the plan did not consider at all:** jichi ships as source, so
>   "install jichi in the image" is a *build*. Measured from a pristine Debian 12:
>   three packages, a 5 s build, and the toolchain can be purged afterwards.

---

## 1. The short answer, and why it is short

JupyterHub gives each user a *single-user server*, and that server can offer a real
terminal (`jupyter-server-terminals`, the "Terminal" tile in JupyterLab). A terminal
is a pty. jichi is a POSIX CLI that wants a pty.

**So the minimum viable integration is: install jichi in the image, and tell users to
open a terminal and type `jichi`.** No code, no adapter, no new surface. That is not
a disappointing answer — it is the correct one, and §5 argues it should stay the
recommendation for most deployments.

What jichi actually requires of a terminal is narrow, and none of it is exotic
(verified by reading `src/tui/jc_term.c` and `src/tui/jc_tui.c`):

| Requirement | How jichi decides | JupyterLab terminal |
|---|---|---|
| a pty on stdin **and** stdout | `isatty(in) && isatty(out)` | yes **[unverified]** |
| a column count | `TIOCGWINSZ`, then `$COLUMNS`, then 80 | xterm.js sets the winsize **[unverified]** |
| glyphs | `▸ ✓ ✗` only if the **locale** advertises UTF-8, else ASCII | depends on the image's locale, not on Jupyter |
| colour | `NO_COLOR` / `--color`, plus `isatty` | honoured either way |

Note what is *absent*: jichi never reads `$TERM`. It does not need terminfo, a
specific terminal type, or an alternate screen. It resizes from `TIOCGWINSZ` rather
than trusting the signal (which is why the FreeBSD row could guard `SIGWINCH` out
entirely at M460 at no cost).

**Headless is the better default in a hub anyway.** `jichi -p "…" --output jsonl` needs
no pty at all, and it is the surface with a documented stability contract
([`EMBEDDING.md`](../EMBEDDING.md) §4). Anything scripted from a notebook cell should
use it — see §4.

---

## 2. What JupyterHub adds that jichi has never been documented for

This is the finding. `docs/DEPLOYMENT.md` has sections on SSH, embedded devices,
air-gapped hosts and automated runs. It has **nothing** on multi-user, and a grep for
"multi-user", "shared machine" or "other users" across `DEPLOYMENT.md` and
`HARDENING.md` returns **zero hits**. jichi's threat model is "one operator, one
machine, and a model that may misbehave" — not "twenty students on one node".

Four consequences, in descending order of how much they would hurt.

### 2a. The workspace lease is per-user, so it does not protect a shared workspace

`docs/AUTONOMY.md` §3b is unusually blunt: *"jichi has **no lock of any kind** — and
the envelope assumes **one actor per tree**."* `revertOutOfScope` makes that
load-bearing, because the end-of-turn sweep *"diffs the whole tree against a
run-start baseline and cannot tell a sibling run's edits, or your mid-run merge, from
an out-of-scope write by the model it polices."*

The mitigation is the M431e advisory lease — and it is written to
**`~/.jichi.d/leases/<workspace-key>.json`**. In JupyterHub every user has their own
`$HOME`. So:

> Two users running jichi in the **same shared directory** each take a lease in their
> **own** `~/.jichi.d/`, never see each other's, and both proceed as if alone —
> including under `--lease fail`.

The lease is not broken; it was designed for one user's concurrent runs and it does
that correctly. But **its protection evaporates in exactly the configuration
JupyterHub makes normal** (a shared course or project mount), and the failure mode is
not a conflict message — it is one user's run reverting another's work, because
`docs/DEFERRED.md` still carries the open row *"the envelope attributes every mid-run
change to the run"*.

**Mitigation, and it is a policy not a patch:** in a hub, **`--auto` with
`revertOutOfScope` must not be used on a shared writable directory.** Per-user
workspaces make the whole problem disappear. See §3 for how to enforce that.

### 2b. Snapshots and checkpoints are also `$HOME`-keyed, so undo is per-user

The shadow git repo lives under `~/.jichi.d/checkpoints/<workspace-key>/` and its work
tree is the shared directory. Two users therefore hold **two independent checkpoint
histories over one tree**. `/undo` restores *your* view of a tree that someone else
has since changed. Nothing warns about this. Same mitigation: per-user workspaces.

### 2c. Capacity: the fork pool sizes itself to the node, not to your share of it

`max_parallel_agents` defaults to `0`, which means **auto = `min(cpu, 8)`**
(`jc_config.c`; `--lite` sets 1). On a 32-core hub node, ten users each running
`spawn_parallel` can ask for eighty concurrent agent forks. jichi is behaving exactly
as documented; it simply has no idea it is sharing.

**Mitigation:** ship a hub-wide config with `maxParallelAgents: 1` (or 2), and set
`memBudgetMb`. Both are ordinary config keys — no new feature.

### 2d. Secrets are per-user, and that part is already right

`apiKeyEnv` plus `~/.jichi.env` at mode 0600 is the documented pattern, and per-user
`$HOME` makes it correct in a hub without changes. Two cautions: a **hub-wide** config
must never carry a literal `apiKey` (`doctor` already warns about literals), and a
fixed `logging.path` in a shared config would **merge every user's telemetry into one
file** — the same class of mistake M459 found when a project's fixed `logging.path`
hid a 30×-larger corpus from every reader. Leave telemetry at its per-`$HOME` default.

---

## 3. The `.ipynb` problem, stated plainly

**jichi has no notebook support whatsoever.** `grep -rc ipynb src/ include/` returns
nothing: zero occurrences. To jichi, `analysis.ipynb` is a large JSON file with
base64 image blobs in it.

That matters more than it sounds, because it breaks the tools users would reach for
first:

- `read_file` returns notebook JSON, not code — mostly `"outputs"`, `"metadata"` and
  base64 PNGs, against a `readMaxBytes` cap of 256 KB.
- `edit_file`/`apply_patch` do exact string matching. Editing a code cell means
  editing a **JSON string with escaped newlines**, which is possible and is a very
  effective way to corrupt a notebook.
- `search_code` matches inside JSON, so hits are real but their context is unusable.

There is a `notebook-helper` agent in the `data` scaffold pack, but it is a **prompt**,
not a capability — it cannot make the file tools understand cells.

**Three options, and I recommend the first:**

1. **Document `jupytext` and work on the paired `.py`.** No jichi change. Users pair
   the notebook (`jupytext --set-formats ipynb,py:percent`), point jichi at the `.py`,
   and Jupyter keeps them in sync. Honest cost: outputs are not visible to jichi, and
   the pairing is a habit the user must keep.
2. **A `read_file` that renders `.ipynb` to cells** — the same shape as the M42 PDF
   path (detect the extension, transform to text, never mark it read-before-edit
   because it is not editable that way). Modest, in-character, and it fixes *reading*
   without pretending to fix editing.
3. **Full notebook editing (a cell-aware editor tool).** I recommend **against** it
   for now: it is a new tool, a new format contract, and a new class of corruption
   bug, in exchange for something option 1 already gives.

Option 2 is the one worth a milestone if notebooks turn out to be the actual use case.
Option 1 is what to write in the docs this week.

---

## 4. Three integration shapes, and which to pick

Mirroring [`EMBEDDING.md`](../EMBEDDING.md) §2's framing, because a hub is just
another consumer.

### Shape A — a terminal in JupyterLab (recommended)

Install jichi in the single-user image; users type `jichi`. Interactive TUI, or
`jichi -p` for one-shots.

- **Cost:** none beyond packaging.
- **Gets you:** everything jichi does, with per-user `$HOME`, per-user keys, per-user
  audit log.
- **Honest limits:** no notebook integration (§3); the TUI in xterm.js is
  **[unverified]**; and users can still point it at a shared mount, so §2's policy
  matters.

### Shape B — a notebook cell shelling out to headless jichi

```python
!jichi -p "summarise the failures in results.csv" --output jsonl --no-session
```

- **Use `--output jsonl`**, whose event objects are the **stable** tier of the
  contract (`v` + `type`, unknown types must be ignored). Parse it; do not scrape
  prose.
- Add `--auto` only with an `--edit-scope`, and read §2a first.
- **Do not** use the interactive TUI from a cell: there is no pty, and jichi will
  correctly fall back to non-interactive behaviour rather than doing what the user
  expected.

### Shape C — a JupyterHub *service* or a kernel wrapper

A hub-level service (or an IPython magic) that keeps a jichi daemon warm and routes
cell requests to it.

**I recommend against building this now**, on three grounds:

1. It re-creates the multi-user problem *inside* jichi. A shared daemon would hold one
   process's `$HOME`, so the per-user isolation that Shape A gets for free — keys,
   audit log, checkpoints, leases — would have to be re-invented, and jichi has no
   notion of a caller identity to key any of it on.
2. `docs/DEFERRED.md` still carries the daemon's own open questions (`fresh`, exit
   codes) as *"a planned EXPERIMENT with a discard gate"* on a branch that is **not in
   this repository**. Building a hub service on top of that is building on a
   deliberately unsettled surface.
3. The demand is hypothetical. Nobody has reported that Shape A is insufficient. §7
   states what evidence would change this.

---

## 5. Design decisions, each with what it rejects

| Decision | Rejected alternative, and why |
|---|---|
| **A terminal, not an integration.** Recommend Shape A and write documentation, not code. | A kernel/magic/service (Shape C): it would re-implement per-user isolation that `$HOME` already provides, and jichi has no caller identity to key it on. |
| **Per-user workspaces are the mitigation, not a new lock.** | Making the lease hub-aware (e.g. a lease directory under the shared tree): that means jichi learning about other users' identities and permissions, which is a new threat model, not a bug fix. The M159 rule applies — a mechanism that could loosen or arbitrate between users is a privilege surface wearing a convenience hat. |
| **Notebook *reading* before notebook *editing*** (§3 option 2 over 3). | Cell-aware editing: a new corruption class for a benefit `jupytext` already delivers. |
| **`--output jsonl` for anything programmatic**, never prose scraping. | Parsing the human transcript: `EMBEDDING.md` puts prose summaries in the *provisional* tier explicitly. |
| **Document the hazard even though it is not jichi's bug.** | Staying silent because "jichi is single-user by design": the user asked whether it *can* be used with JupyterHub, and the answer is incomplete without the one configuration that loses work. |
| **Label the whole document unverified.** | Writing it as if tested: there is no JupyterHub on this bench, and this project's word for that is *never run*. |

---

## 6. What to actually do — ranked, with costs

1. **Write `docs/DEPLOYMENT.md` §5: "Shared and multi-user hosts"** *(hours, no code)*.
   The four consequences in §2, the per-user-workspace rule, a hub-wide config
   template (`maxParallelAgents`, `memBudgetMb`, `apiKeyEnv`, no fixed `logging.path`),
   and the `.ipynb` guidance from §3 option 1. **This is the highest-value item and it
   is pure documentation** — it closes a gap that exists today for anyone on a shared
   box, JupyterHub or not.
2. **Add the multi-user limit where the mechanism is described** *(minutes)*.
   `AUTONOMY.md` §3b should say the lease is `$HOME`-keyed and therefore per-user;
   the `SNAPSHOTS` page should say the same about checkpoints. Both are one sentence
   each and both are currently a surprise waiting to happen.
3. **Verify Shape A once, on a real hub** *(an afternoon, needs a hub)*. Install
   JupyterHub in a VM here — `qemu` and `/dev/kvm` are available — run the TUI in a
   JupyterLab terminal, and record a row: does `TIOCGWINSZ` arrive, does resize work,
   does bracketed paste work, is the locale UTF-8. Until this exists, §1's table stays
   **[unverified]**.
4. **`.ipynb` reading (§3 option 2)** *(a milestone)*, **only if** notebooks are the
   real use case. Ask first.
5. **Nothing else.** Shape C stays unbuilt.

---

## 7. What would change these recommendations

- **Someone reports Shape A is insufficient**, with the specific thing they could not
  do → revisit Shape B's ergonomics first, Shape C last.
- **Notebooks turn out to be the point** (users want jichi to edit cells, not files)
  → §3 option 2 becomes a milestone, and option 3 gets re-argued with a corruption
  test plan.
- **A hub deployment with a shared writable workspace is actually wanted** → then the
  per-user-workspace rule is not enough, and the honest next step is *OS-level*
  isolation per user (the same answer `GATE_INTEGRITY.md` §9.3-B gives for the shell
  sandbox: the real fix lives outside jichi), not a lock inside jichi.
- **The daemon experiment concludes** → Shape C becomes arguable rather than
  premature.

## 8. What I have not verified, in one place

*Struck through where M478 measured it; see [`JUPYTERHUB.md`](../JUPYTERHUB.md)
§13. What remains is the honest residue.*

- ~~That the JupyterLab terminal satisfies jichi's four terminal requirements.~~
  **Measured — all four hold.** What is still unmeasured is the *browser* half:
  xterm.js key bindings, where jichi wants Ctrl-R and Ctrl-G and a browser
  claims both. A websocket probe cannot press a key in Firefox.
- ~~That `jichi -p` from a notebook cell behaves as described.~~ **Measured** via
  a real `nbconvert --execute` run.
- Any resource figure for N concurrent users. The capacity concern in §2c is arithmetic
  from a documented default (`min(cpu, 8)`), not a measurement.
- That two users on a shared mount actually collide as §2a predicts. The **mechanism**
  is verified by reading (`~/.jichi.d/leases/` is `$HOME`-relative, and `AUTONOMY.md`
  states the one-actor assumption); the **collision** has not been staged. It would
  take two accounts and ten minutes, and it is the first thing to run if anyone
  proposes a shared workspace.
