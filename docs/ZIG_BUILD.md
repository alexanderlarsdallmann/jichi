# Compiling jichi with the Zig compiler — findings, honestly

Written from the real run (2026-07-28, zig 0.16.0, 32-core Linux host),
the sibling of [CPP_BUILD.md](CPP_BUILD.md) and
[PORTING_WINDOWS.md](PORTING_WINDOWS.md): a third boundary survey, this
time of the *toolchain* rather than the language or the OS.

## The claim, tested

Zig ships a C compiler (`zig cc`) that claims C projects build out of the
box. **For jichi, the claim holds outright:**

```sh
make CC="zig cc"        # builds and links; the binary runs
make CC="zig cc" test   # full suite, 0 failures
```

Zero source changes, zero Makefile changes — the feature probes, `-std=c89
-pedantic`, and the dependency tracking all pass straight through, because
`zig cc` is (this is the honest headline) **a clang driver** with bundled
headers, bundled libcs, and a build cache. jichi already builds under
clang in `make ci`, so "the third compiler" is secretly the second — what
zig adds is not a new front-end but a *hermetic, cross-capable toolchain*
around it.

## Measured (this host; environment snapshot, not a benchmark)

| | cc (gcc) | `zig cc` default | `zig cc -O2` |
|---|---|---|---|
| cold build, `-j8` wall | **0.7 s** | 14.3 s | ~14 s |
| warm rebuild (one file touched) | — | 1.3 s (cache) | — |
| binary size | **1.4 MB** | 9.7 MB (4.6 MB stripped) | 3.6 MB |
| test suite | green | green | green |

Read the size/speed columns fairly: `zig cc`'s *default* mode compiles
with debug info and **UBSan in trap mode** — a safety posture, not bloat;
`-O2` drops most of it. And on a 32-core host with hot system headers,
plain gcc is simply very fast; zig's compile cache is aimed at cold CI
containers and incremental cross builds, neither of which this table
exercises. Speed is not the reason to use `zig cc` here.

## Cross-compilation: the compiler crosses, the dependency doesn't

The genuinely novel zig capability is `-target`:

```sh
make CC="zig cc -target x86_64-linux-musl"    # static musl jichi?
```

**First result (2026-07-28, morning): link failure — which found a real
bug.** The documented "core + tests build without libcurl" had drifted:
the Makefile excluded all of `src/net/` when curl was absent, though only
`jc_http.c` actually touches curl (and it carries runtime stubs for
exactly this case). The drift also hid a second defect: the SSRF
IP-literal classifier was guarded on `JC_HAVE_CURL` although `inet_pton`
is POSIX. **Both repaired at M190** (src/net compiles unconditionally;
the classifier works in every build), and now:

```sh
make CC="zig cc -target x86_64-linux-musl"
file jichi   # ELF 64-bit ... statically linked
```

**A fully static, zero-dependency jichi builds and runs** — every offline
feature works (map, doctor, init/setup, grade, sessions…); networking
reports its documented "built without libcurl" error. For a static jichi
*with* networking, libcurl must still be cross-built for the target first
([LOW_MEMORY.md](LOW_MEMORY.md)'s recipe): zig bundles *libcs*, not your
dependency tree — that half of the lesson stands.

## Verdict

- **Use it when** you want a hermetic toolchain (pinned compiler + libc
  as one artifact, no system toolchain drift), or you are cross-building
  and are prepared to cross-build libcurl too, or you want UBSan-trap
  defaults on a debug binary for free.
- **Don't expect** speed on a fast host, a smaller binary (gcc wins
  here), or dependency magic — but do expect a working **fully static
  offline jichi** from one command, post-M190.
- jichi's shipped, CI-gated build remains gcc/clang `-std=c89 -pedantic`;
  like the C++ property, zig-buildability is a **property of the
  codebase**, kept cheap by the same discipline (strict C89, one
  dependency) — not a target.

## The extracurricular task

The experiment ships as a graded curriculum extra:
[`docs/assignments/19-the-third-compiler.md`](assignments/19-the-third-compiler.md)
— run the build yourself, measure, attempt one cross target, and write
the findings report (including the one question that unmasks the
toolchain: *what is `zig cc` actually running?*). Zig is a single
download, so unlike the Windows survey this extra has no hardware
prerequisite.
