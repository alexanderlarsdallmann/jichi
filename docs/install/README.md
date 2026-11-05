# Installing jichi, per platform

One page per platform or platform family. Start at the top-level
[`INSTALL.md`](../INSTALL.md) for the generic build; come here for the page that
matches the system in front of you.

**Who this directory is for.** A self-learner or junior developer building jichi from
source, working alone, with nobody to ask. Each page is a **step-by-step walkthrough**,
not a reference: it says which terminal to open, which directory to be in, exactly
what to type, **what you should see after each command**, and what to do if you see
something else. No C or compiler knowledge is assumed. It also gives the part a
generic guide cannot — *what has actually gone wrong on this platform and what to do
about it* — every difficulty measured, with the milestone that found it.

Start with [debian-ubuntu.md](debian-ubuntu.md) if you are new; it is the most
detailed, and the other pages follow the same shape.

**Where the verdicts live.** [`PLATFORMS.md`](../PLATFORMS.md) is the source of
truth for each platform's verdict (*Verified*, *Partly verified*, *Never
compiled*, *Driven*) and the numbers behind it. Each page's **Status** links to its
row there. This directory says how to install; `PLATFORMS.md` says what was proven.

## The pages

| Platform | Verdict | Page |
|---|---|---|
| Debian / Ubuntu (and the Linux reference build) | Verified · Driven | [debian-ubuntu.md](debian-ubuntu.md) |
| Fedora / RHEL / Arch | Verified · Driven | [fedora.md](fedora.md) |
| Alpine Linux (musl) | Verified | [alpine.md](alpine.md) |
| Old Linux userlands (Debian 5–9, CentOS 6–7) | Partly verified | [old-linux.md](old-linux.md) |
| Raspberry Pi and other boards | Verified · Driven | [raspberry-pi.md](raspberry-pi.md) |
| Guix System | Partly verified · Driven | [guix.md](guix.md) |
| GNU/Hurd | Partly verified · Driven | [gnu-hurd.md](gnu-hurd.md) |
| FreeBSD | Verified · Driven | [freebsd.md](freebsd.md) |
| OpenBSD | Verified · Driven | [openbsd.md](openbsd.md) |
| NetBSD | Verified · Driven | [netbsd.md](netbsd.md) |
| illumos / Solaris (OmniOS) | Partly verified · Driven | [illumos.md](illumos.md) |
| Windows (WSL2, Cygwin, MSYS2) | WSL2 Verified · Driven | [wsl2.md](wsl2.md) |
| macOS | Partly verified, driven | [macos.md](macos.md) |

**FreeMiNT (Atari) / ARAnyM** is a *cross-compile-and-emulate* target, not a system a
user installs jichi on, so it has no walkthrough here; its build lives in
[`PLATFORMS.md`](../PLATFORMS.md) and the rig script. For any platform whose page is not
yet written, [`PLATFORMS.md`](../PLATFORMS.md) and [`BUILD.md`](../BUILD.md) carry it.

## Every page has the same nine sections

So two platforms can be compared line for line:

1. **Status** — the verdict word, the date, the build, whether it is *Driven*, and
   a link to its `PLATFORMS.md` row.
2. **Who this is for** — and what "installed" means here.
3. **Before you start** — packages, versions, disk and memory, as measured.
4. **Steps** — copy-pasteable, each with the output that means it worked.
5. **Measured difficulties** — every problem this platform has actually shown, one
   heading each: what you see, why, what to do, and the milestone.
6. **Possible difficulties** — not seen yet, but following from how the platform
   works; marked *not measured*.
7. **Verifying the install** — `make check-target`, `doctor`, and one live turn.
8. **Reporting back** — the row template in
   [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md) §3.
9. **Read further** — the platform's own documentation, primary sources first.
