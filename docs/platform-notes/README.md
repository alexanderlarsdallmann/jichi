# Platform porting notes

The long record of how each non-trivial platform row was proven, and the defects it
found on the way. This is **developer and porting material**, moved here at M780 so
that [`PLATFORMS.md`](../PLATFORMS.md) can stay a short matrix and the
[install pages](../install/) can stay lean, step-by-step walkthroughs.

**Who this is for:** someone porting jichi, debugging a platform failure, or curious
how a verdict was earned — not someone just trying to install it (that is
[`install/`](../install/)). Nothing here is required reading to build or use jichi.

Each page is kept **verbatim** from where it used to live in `PLATFORMS.md`; `git log
-S` on any phrase names the commit that wrote it. They are war stories with a lesson,
in the same spirit as [`ANECDOTES.md`](../ANECDOTES.md).

| Notes | What it covers |
|---|---|
| [freebsd.md](freebsd.md) | the FreeBSD row and the seven defects it found |
| [openbsd.md](openbsd.md) | the OpenBSD rows: the M472 hardening break, what OpenBSD found beyond FreeBSD, and its 23-driver failure set |
| [illumos.md](illumos.md) | the illumos row, and the ten drivers one `grep -o` hid (with the correction that shrank the work thirty-fold) |
| [windows.md](windows.md) | the Cygwin and WSL2 rows and the defects they found |
| [architectures.md](architectures.md) | the architecture sweep, and what it refused to blame on jichi |
| [macos.md](macos.md) | the macOS row: the first verified Darwin build (M817) and its three test fixes |
| [haiku.md](haiku.md) | the Haiku row: one Makefile line, and what the smoke tier found (M830) |
| [freedos.md](freedos.md) | the FreeDOS/DJGPP row, written for a reader new to DOS: how to reproduce it, and what text mode, 8.3 names, `TZ`, sockets and one-program-at-a-time each broke (M833–M839) |

For the short, user-facing version of each platform's difficulties, see that
platform's [install page](../install/). For the verdicts and the numbers, see
[`PLATFORMS.md`](../PLATFORMS.md).
