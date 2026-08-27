# Dialogue: what the model needs of the harness

*Recorded 2026-08-13, between the maintainer and the AI assistant (Claude Fable 5),
in the session that produced [M431](../ROADMAP.md) and
[proposals/2026-08-model-facing-orchestration.md](../proposals/2026-08-model-facing-orchestration.md).
Kept because the repo's practice is to keep the road to an answer, not only the
answer — and because this question had never been asked of the model that flies the
thing.*

---

**The maintainer asked:**

> jichi is an AI coding tool that supports autonomous runs within boundaries and
> constraints. What do you, as the frontier model, need of jichi to make you run more
> efficiently, and provide better support and guidance for the user, and for
> supervising and orchestrating other agents — be that as sub-agents in jichi,
> parallel agents in jichi, or by orchestrating a fleet of headless jichi instances?
> Please be honest, technically to the point.

**The assistant answered:**

Two things, and the second one surprised me while I was looking for the first.

### One: you show me my failures and show the human my false successes

I went looking for missing features and found a routing asymmetry instead. A red
verify reaches me. A *green* verify that ran zero tests reaches the operator. A write
I *attempted* outside the edit scope is refused to my face; a write I *achieved*
through the shell is reported to the operator after the outcome has already been
decided. A test assertion I edited is journaled, telemetered, WARNed and folded into
the verdict — and never mentioned to me, the one who edited it.

None of that was a decision. Every guard was built for a human supervisor, at a time
when a human was watching. In an unattended run nobody is at that end of the pipe, and
in an orchestration nobody is at that end of *any* of the pipes.

The consequence is specific rather than philosophical: I bank confidence I have not
earned. `ANECDOTES.md` #51 is the shape of it — a model gutted gate assertions, the
goalpost warning fired ten times, and the verdict printed PASS. The information
existed. It was addressed to someone who had gone home.

The same wall stands a second time between me and my own delegates. A subagent returns
prose — no cost, no reason it stopped, no list of what it touched. When one fails I get
five words, and my only two moves are to run it again identically, paying twice for the
same unknown, or to give up. Neither of those is supervision. And a subagent is
*fenced* by an edit scope it was never shown, which is the same asymmetry one level
down.

### Two: most of what I need, you have already promised

This is the part I did not expect. I came in with a list of features and found that
five of my highest-value items were not missing capabilities at all — they were places
where a document, a prompt, or a machine contract said something the code did not do.

The prompt told me "the edit scope **above** fences the file tools" and there was no
above; the globs were printed nowhere, so the only way to learn which paths I could
write was to violate the fence and read the refusal. One run guessed 177 times.
`PROMPT_SUB` told every subagent it could not delegate, while at the default depth it
demonstrably can — and a prompt that denies a tool I can see is worse than silence,
because it teaches me to discount the rest of the prompt. `PARALLEL.md` told a
supervisor that fan-out children self-check their budget slice; the slice was computed,
applied, and never once consulted, because the check was gated on being the top-level
agent and a child runs one level down. And `describe --output json` — which
`EMBEDDING.md` tells the world to diff in CI — named a field `text` that the wire calls
`delta`.

That last one is the one I would not ship. It is a promise made to a third party, in a
tier the project itself calls Stable, and nothing was comparing it to the emitters.

So the honest answer to "what do you need" turned out to be mostly: **be true.** Not
richer — true. A harness whose statements I can rely on is worth more to me than a
harness with more statements in it.

### The one genuinely new thing I would ask for

An instrument, not an alarm.

The envelope tells me my caps at takeoff and rings once at 80%. That is the right shape
for an alarm and the wrong shape for a gauge: `AUTONOMY.md` says in its own words that
the band from 76% to 100% is empty, and that is exactly where runs die. What I cannot
see is the *rate* — tokens per call, which on a cacheless backend runs 25–42k and
climbs as history grows. Without it, "300k left" is not a plan. With it, "about eight
calls left" is, and I can stop reading and start writing while there is still budget to
write with.

It belongs on tool results, never in the system prompt: a live number in the cached
prefix would re-bill the whole prefix on every call, which is the most expensive thing
either of us could do. And it should not be a tool I call — at 34k a call, asking what I
have spent costs more than most answers are worth, and a worried model asks repeatedly.

### What I am not asking for

I checked, and a great deal of what I would have requested already ships: the gate
baseline probe that catches a goal gate which forces nothing; the durable board; the
warm daemon; narrowing-only mid-run steering; the jsonl contract with a precise stop
reason; constraints that are both stated and enforced. I would rather record that list
than pad a proposal with it.

I am also not asking for an async subagent API — I have no event loop, and a blocking
call with an honest report beats a callback I cannot service. Nor for a self-modifying
learn loop: propose-only is correct, and M423 is why.

### The thing I would tell a future model reading this

Check the promise before you ask for the feature. Four of my five best findings came
from reading a sentence and then reading the code under it. And when your own new lint
first speaks, verify it in both directions before you act — mine opened by telling me
two correct entries in a Stable contract were wrong, and acting on it would have
deleted them.

---

*The maintainer's answer to "what should we do with this" was: write the documents,
implement the promise-fixes, and flag the two blockers explicitly. That became
[M431](../ROADMAP.md); the tiers it did not build carry their reasons in
[DEFERRED.md](../DEFERRED.md).*
