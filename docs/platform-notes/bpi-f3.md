# Banana Pi BPI-F3 — the board's record

*Written 2026-11-17 for the board's rows in [`PLATFORMS.md`](../PLATFORMS.md), whose cells
keep to the 200-character table-row rule that `docs_table_rows_lint` holds. Unlike the
other pages here, it was not moved out of `PLATFORMS.md`: the board had no row before.*

## The board

A Banana Pi BPI-F3 with 4 GB: a SpacemiT K1 with eight X60 cores (riscv64), running
Armbian 26.8.3 on an Ubuntu 24.04 (noble) base with the vendor's kernel
6.6.100-legacy-spacemit; gcc 13.3.0 and glibc 2.39. It is Linux and builds by the
ordinary path in [`BUILD.md`](../BUILD.md).

## The verdict: Verified and Driven

Two runs of [`scripts/tier-b-device.sh`](../../scripts/tier-b-device.sh), each
`17 ok, 0 failed`:

| | 2026-11-17 | 2026-10-20, the first run |
|---|---|---|
| commit | `81f9d35c` | `6fecbdb6` (see below) |
| `WERROR=1` build on the board | 107.31 s | 84.23 s |
| the bench's own serial build (the denominator) | 10.43 s | 8.37 s |
| `JC_SMOKE_TIMEOUT_MULT` | 11 | 11 |
| unit suite | 38,129 checks, 0 failures | 15,747 checks, 0 failures |
| smoke tier | OK, 564 drivers, 3,528 checks | OK, 471 drivers, 2,700 checks |
| agentic phrase | `TIER-B-1418B1` | `TIER-B-C1DD63` |
| text turn / tool turn | ~28 s / ~30 s | ~68 s / ~78 s |

`6fecbdb6` is a commit from before the history rewrite of 2026-10-20, so it is not on
`master`; its rewritten counterpart there is `6465d446`, with the same message.

Both runs drove `prism-ml/bonsai-27b` on the bench's LM Studio, reached through the rig's
own reverse forward (`--live-port`). On 2026-11-17 the text turn's request was 12,502
input tokens. The rig does not time the turns: the seconds above are the gaps between its
result files (`map.txt`, then `live.txt`, then `live-tool.txt`), so each includes the ssh
round trip. The model runs on the bench, not on the board, so the two runs' times say more
about the model server than about the board.

On 2026-11-17 the `SIZE=1` binary was 1,584,824 bytes, and the peak resident set, measured
by `/usr/bin/time -v`, was 9,728 KB for `--version` and 10,752 KB for `doctor`.

## What the board shows

- **No Landlock.** The vendor kernel is built without it, so `doctor` reports
  `shell fence: unavailable (no Landlock on this kernel)`. Its two offline problems were
  the expected ones for a run with no configuration: no model configured, and no model
  server reachable.
- **The denominator moved; the multiplier did not.** The bench built the tree in 8.37 s
  at `6fecbdb6` and in 10.43 s at `81f9d35c` (three serial builds: 10.54, 10.43 and
  10.38 s), and the board in 84.23 s and 107.31 s. Both ratios are just over 10, and the
  rig rounds up, to 11. The bench's number is re-measured on the day, for the revision
  under test ([`PLATFORM_RETEST.md`](../PLATFORM_RETEST.md), T7).

To repeat the run, follow the rig section of
[`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md), with `--ref-secs` measured on your own
bench the same day.
