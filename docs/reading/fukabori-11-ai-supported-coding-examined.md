# Fukabori 11 — AI-supported coding, examined

*[深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md) · chapter 11 of 12*

## The decision to be honest about the method

This codebase was built with agent support and documents that history
rather than hiding it. This chapter is the expert examination of the
method itself: what the reasoning trace does and does not warrant, why
tool-call schemas are an *interface-design* problem, how a project turns
its own telemetry into durable lessons, and — the part most writing on
this omits — the failure modes, named, because the anecdotes ship with
this repository precisely so this chapter could be written from evidence
instead of vibes.

## What the reasoning trace warrants (and does not)

The Annai (chapter 4) defined reasoning deflationarily: intermediate text
the model generates for itself because writing the steps makes the next
token likelier to be right. The expert corollary is a warning: **the
trace is not a transcript of a process and not a warrant for its
conclusion.** Fluent reasoning about a file the model never read is still
fiction, and the fluency makes it *more* dangerous, not less, because it
reads like diligence.

This codebase's response is structural, not exhortative. It does not ask
the operator to "be careful"; it makes the model's claims *checkable*:

- the reasoning is **streamed** so you can audit it, never hidden;
- every claim that touches reality goes through a tool whose result is
  **ground truth in the conversation** (chapter 4) — a claim about a
  file's contents is followed by the file's contents, and the divergence
  is visible;
- the autonomy envelope (chapter 6) **verifies** rather than trusting a
  self-report of success.

The transferable stance: treat the trace as a hypothesis-generator whose
every load-bearing claim must be independently grounded, and build the
grounding into the system so the human is not the only check.

## Tool schemas are interface design, and small models are the test

A tool's schema (chapter 5 of the Annai) is an API the model programs
against under uncertainty, and here is the expert insight the whole
`tests/bench/` tier exists to enforce: **a tolerant frontier model hides
your interface bugs; a small local model exposes them.** A malformed
request array, an ambiguous argument name, a menu with sixteen tools
crowding a small context — a large model absorbs these silently, and you
ship the defect. The bench measures against a real small model *because
its intolerance is the diagnostic*.

Read the consequences in the code: `doctor --live`'s probe
(`src/util/jc_toolprobe.c:jc_toolprobe_classify`) classifies whether the
configured model calls tools natively, and its design carries the scar —
it mirrors the loop's exact request shape, placeholder included, because
a probe that built its own tidy request would pass while the real build
was broken (chapter 2). The self-healing catalog — transparent aliases,
argument repair, the prose-call nudge — is the accumulated record of the
ways real models misuse a schema, each guard with a measured motivation
in its comment.

## The learning loop: telemetry → insight → lesson

The most examined-coding-specific subsystem is the propose-only learning
loop (`docs/LEARNING.md`), and it is worth reading as the method turned on
itself:

1. **`learn analyze`** — pure `src/util/jc_insights.c` ranks recurring
   problems from the telemetry summary (tools below an ok-rate, model
   stalls, retry/route/compaction pressure) plus a redo-loop detector.
   Offline, deterministic — the same telemetry that drove M218's memory
   fixes (chapter 3) and M219's alias additions (chapter 5), read as
   *lessons* rather than *bugs*.
2. **`/learn`** — a scaffolded mentor agent drafts reviewable lessons into
   a file; propose-only, never auto-applied.
3. **`learn apply`** — pure `src/util/jc_learn.c:jc_learn_parse_draft`
   commits the human-edited draft to memory notes and skills, and — the
   part that makes it a *loop* and not a ratchet — it can **correct**:
   `src/chat/jc_memory.c:jc_memory_apply_correction` supersedes a
   now-false remembered note instead of appending a reworded duplicate
   beside the stale one, driven by a staleness review
   (`src/util/jc_insights.c:jc_insights_stale_review`) that flags notes
   citing a specific line that may have moved.

The design decision worth naming: **every step is propose-only, and the
human is in the commit path.** A system that learns from its own logs
*and* applies the lessons without review is a system that amplifies its
own misreadings — the exact reasoning-trace failure mode above, at the
level of durable memory. The loop is powerful *because* it stops short of
autonomy.

## The failure modes, named

The anecdotes are the point of this chapter, not an appendix to it. Read
them as the evidence base for everything above:

- **#17 — a green gate over wrong code.** Confident output plus a hollow
  check. The origin of the whole test-integrity discipline (chapter 10)
  and of curriculum assignment 14.
- **#19 / #20 — the confident wire bug.** A model that "couldn't call
  tools" whose real problem was a malformed request, diagnosed only by
  replaying request bodies against a real server — and its sibling, a
  probe that lied by building a cleaner request than the loop. The reason
  the bench tier exists.
- **The dogfood record** (`docs/dialogues/`, `docs/SELF_IMPROVEMENT.md`):
  runs where the agent's confidence outran the code, kept as evidence of
  which supervision styles held. Read at least one; the honest record of
  a method's failures is rarer than the method's advocacy, and it is the
  more useful document.

## Prove it to yourself

Run the loop's front half offline and read what it found:

```sh
# in the jichi checkout (where you ran `make`)
# the log goes to /tmp on purpose: it is not yours to commit
jichi --readonly --log /tmp/t.jsonl --log-level metrics \
      -p "read src/util/jc_str.c and summarize it"
jichi telemetry /tmp/t.jsonl          # the summary jc_insights ranks
jichi learn analyze /tmp/t.jsonl      # the ranked problems, propose-only
```

Then read `src/util/jc_insights.c:jc_insights_stale_review` and ask what
it protects against: a remembered lesson that was true when written and
false now — the reasoning-trace failure mode, persisted. The guard is
that even the *memory* is treated as a hypothesis subject to correction.

## Where this bit us

Every claim in this chapter has an anecdote number, which is the chapter's
method in one sentence: **this project earns the right to describe
AI-supported coding by keeping the receipts for when it went wrong.**
`docs/ANECDOTES.md`, `docs/dialogues/`, `docs/TEST_INTEGRITY.md`, and
`docs/SELF_IMPROVEMENT.md` are the primary sources; this chapter is their
index. The transferable claim: the model proposes, the system and the
human dispose — build the disposing (grounding, verification, two-sided
checks, propose-only learning) into the architecture, and keep an honest
ledger of the times you did not, because that ledger is the only
literature on this subject that will still be true next year.

*Next: [chapter 12 — the migration road](fukabori-12-the-migration-road.md).*
