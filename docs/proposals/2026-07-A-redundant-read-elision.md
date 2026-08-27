# Proposal A — redundant-read elision — ASSESSED: already covered

**Verdict: withdraw.** The problem is real and grounded, but jichi already addresses
it (M93/M94/M95). This doc records the assessment so the next harden pass doesn't
re-propose it (the ANECDOTES #15 lesson: verify before proposing).

## The observation (grounded, 2026-07-10 drives)
On the cacheless HRZ backend, single `--auto` drives re-read the same large files
many times: **`vm.zig` ×14**, **`codegen.zig` ×17** in one session each. Each read
re-injects a ~1000-line file, and with 0% prompt cache the whole prefix re-bills
every call. The initial instinct: add a tool-side guard that returns a compact
"unchanged since your last read" marker instead of re-injecting identical bytes.

## Why it's already handled
`jc_compact_trim_superseded_reads` (`src/chat/jc_compact.c:698`, **M93**) elides
duplicate `read_file` results from history, keeping only the latest read of each
file. **M94** runs it **eagerly every round** (not just under budget pressure), so
at most ~one copy of any file is ever in context — the 14 reads of `vm.zig` do NOT
accumulate 14 copies in the prompt. ANECDOTES #8 ("84% of the reads were the same
files, over and over") is this exact finding, already closed. M95 added the
learn-analyze outcome line + a mentor "propose only net-new" prompt around it.

## Residual gap (and why it's not worth closing now)
M93/M94 dedup the **history** (context cost). They do NOT stop the model from
*issuing* the re-read, so each re-read still costs one tool round-trip and one
content injection in the round it arrives (dropped by the next round's elision). A
tool-side "unchanged" marker would additionally (a) avoid billing even that single
arriving copy and (b) hint the model it already has the file. But:
- The additive saving over M94 is small — context stays flat at ~1 copy either way.
- It is **behavior-changing** on the read path (the tool returns different content),
  with a real correctness risk if the "unchanged" judgment is ever wrong (external
  edits, partial reads with `offset`/`limit`, delegate-backed reads via ACP).
- The dominant remaining cost is the *prefix* re-bill (system+tools+history), which
  the M-band prefix levers (toolProfile, repoMap, M73 fitting) and, structurally, a
  prompt-cache-capable backend address far more than per-read elision would.

**Recommendation:** no code change. If a future drive shows the model *choosing* to
re-read the same unchanged file many times despite M94 (a target-selection waste,
not a context-cost one), revisit a lightweight advisory hint — but measure that
behavior first.
