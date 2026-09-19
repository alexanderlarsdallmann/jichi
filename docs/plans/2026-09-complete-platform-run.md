# A complete platform run — what "complete" has to mean now

*Plan, 2026-09-18. Written after the operator asked for a complete test run across
the platforms, and after a correction that changes what the run is for: the matrix
records whether jichi **compiles and passes its tests** on a platform, and does not
record whether a **model has ever been driven** on it. Both Raspberry Pis were
connected and running while this was written.*

## 0. The correction this plan is built on

A row's gates are **all offline** — build, unit suite, smoke tier, and the four
surfaces (`--version`, `doctor`, `describe`, `context`). A platform can be
*Verified* having never called a model.

It does **not** follow that no platform has. The operator remembered otherwise and
was right: the **M459 fleet push** (2026-08-15/16) drove a Raspberry Pi, an Android
tablet and a proot guest with a model over ssh, executing tools and writing files.
`scripts/tier-b-device.sh` has had a `--live URL` step since it was written. The
gap is that **the matrix does not carry any of it**, so a reader cannot tell which
rows ran the agent loop — and neither could I, which is how a wrong count got
published. See `PLATFORMS.md` §"Driven".

**And `--live` is weaker than it sounds.** Step 5 sends `reply with OK` and checks
for a `"text"` field. That is a *text turn*: it proves the wire, the provider and
the SSE framing. It proves nothing about the **agent loop** — no tool is chosen, no
tool is executed, no second turn consumes a result. Every documented failure mode
in this area lives past that point: a model that *describes* tool calls instead of
invoking them terminates perfectly cleanly with an empty workspace
(`AUTONOMOUS_LOOPS.md`, "done is not a success verdict").

## 1. What a complete row is, from now on

Four parts, in this order. A row that stops early says where it stopped.

1. **Builds** — `WERROR=1`, zero diagnostics, with the seconds recorded and the
   multiplier computed from *this bench's* denominator, never a published row's.
2. **Passes** — unit suite and the smoke tier, with **both numerators and
   denominators** (`N of M drivers`), because a count without its denominator is
   not a measurement and is what left three rows with uncomputable debt.
3. **Is driven** — a **text turn** *and* a **tool-calling turn whose result the
   model must use**. The second is the one that exercises the product.
4. **Is recorded on the row** — task, model, seconds. Evidence in an analysis page
   that the matrix does not carry is evidence nobody will find.

## 2. The change the rigs need first

**`tier-b-device.sh` step 5 gains a tool-calling turn**, and `tier-v-*.sh` gain the
step at all. The shape is proven — it is what was run by hand on FreeBSD today:

    write a file holding a known phrase in the scratch workspace
    ask the model to read it WITH THE TOOL and report the phrase
    assert the phrase in the output, and that the tool actually ran
             (`tool_calls_executed` on the journal's `end`, not just `done`)

Two things it must not do. It must not assert on **exit codes** — a dead ssh and a
refused task both exit non-zero, which is why this repo's fleet verdicts come from
positive markers in the stream. And it must not trust a **quoted string**: quotes
drift (measured 2026-09-18 — a model quoted three passages, two verbatim, one
short an article), so assert the phrase, which is a token, not a sentence.

**The reverse tunnel belongs in the rigs.** Both driven VM rows and both Pi runs
today used `ssh -R 1234:127.0.0.1:1234` so LM Studio stays loopback-bound while the
target reaches it. It works, it is in two analysis pages and in no script, so the
next row does it from memory or not at all.

## 3. The inventory, and what each row costs

| Rig | Rows | Offline gate | Driven | Cost per run |
|---|---|---|---|---|
| `tier-b-device.sh` | Pi 400, Pi Zero 2 W, Android/Termux, proot guest, Guix | yes | `--live`, **text turn only** | minutes to ~2 h (multiplier 5 to 13) |
| `tier-v-bsd.sh` | FreeBSD | yes | **driven by hand today** (text + tool call) | ~40 min |
| `tier-v-openbsd.sh` | OpenBSD | yes | no step | ~40 min |
| `tier-v-netbsd.sh` | NetBSD | yes | no step | ~40 min |
| `tier-v-illumos.sh` | illumos | yes | text turn by hand (M663) | ~45 min |
| `tier-v-vm.sh` | the whole-VM rows (v2e…v2k) | yes | no step | varies |
| `tier-v-tiny.sh` | the 256 MB / below-a-distro rows | yes | no step | varies |
| `tier-v-arch.sh` | **14 architectures** under `qemu-user` | yes | **impossible as built** | ~1 h for the sweep |
| `tier-v-console.sh`, `tier-v-terminals.sh` | the terminal-reality cells | yes | n/a | minutes |
| `fleet-run.sh` | N devices at once | no | **yes, and it is the only one that really drives** | minutes |

**The architecture sweep cannot be driven and should stop being counted as if it
could.** Those 14 rows link `HAVE_CURL=` — there is no HTTP in the binary at all.
Driving them needs a curl-enabled cross-build and a qemu-user network path, which
is a different piece of work, not a longer run. Say so on the rows.

## 4. The order to run them

Ordered by what each answers, not by convenience.

1. **The two Pis** — in flight as this is written. The Pi 400 is the only row in
   the matrix with **no driver count at all**, so its debt is not a large number,
   it is not a number. Both get `--live`.
2. **The tool-calling step** — build it, prove it red against a describe-only model
   (`qwen2.5-coder-14b` on this bench reports `text` and is the perfect negative
   fixture), then re-run the two Pis with it.
3. **FreeBSD, illumos** — already driven by hand; re-run through the rig so the
   result is reproducible rather than a transcript.
4. **OpenBSD, NetBSD** — the two BSD rows that have never been driven. OpenBSD is
   the sharpest of them (its `/bin/sh` is ksh, so ~200 smoke drivers run under an
   implementation nothing else exercises).
5. **The tablet rows** (Termux, proot) — they were driven by the M459 fleet, so
   this is recording and re-confirming, not discovering.
6. **The architecture sweep** — offline only, with the reason stated on the row.

## 5. What has to be true before a run starts

- **The model is probed first.** `jichi doctor --live` classifies tool calling as
  native / text / none in one request, and `scripts/probe-models.sh` sweeps a
  server. A `text` caller burns the budget narrating and looks exactly like a
  platform failure. This bench has measured both outcomes on the same server, and
  has measured the classifier failing to distinguish *cannot call tools* from
  *cannot be loaded* (M517) — when it says *failed*, ask the server directly.
- **The denominator is this bench's**, re-measured, not copied.
- **Rig output goes outside the repository tree.** `--out` to a scratch directory;
  a rig that dirties the tree it tests has been three separate mistakes.
- **Devices stay thin clients.** The model stays on the workstation; a 4 GB board
  hosting a model measures the model, not jichi (`ROBOTICS_BRINGLIST.md`).

## 6. What this run cannot answer

It measures that the loop *runs* on a platform. It does not measure whether the
work is any **good** there — that is the craft A/B's question, it needs a human
grader, and it is not platform-dependent in any way this matrix could detect.
Keeping those separate is what stops "the tool call executed on aarch64" from
being reported as "jichi works well on aarch64".
