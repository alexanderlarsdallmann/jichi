---
title: Name what's wrong — reference review
audience: student
---
# Reference review: smelly.c

Compare after you pass the floor. Your wording will differ; what should
match is the *kind* of finding and the presence of a real consequence. If
you found a genuine smell this review missed, that is a win.

## Smell 1: duplicated validation block

where: smelly.c:14–22 and smelly.c:29–37

The three guard checks (`NULL`, `id <= 0`, `idle < 0`) appear verbatim in
both `session_idle_expired` and `session_too_old`.

**Why it matters:** the next validation rule must be added in two places,
and someday it will be added in one. The copy has in fact *already* drifted
subtly: `session_too_old` checks `s->idle < 0` — an idle-time guard pasted
into a lifetime function, where `s->lifetime < 0` was presumably meant.
Duplication is not a size problem; it is a divergence generator. (Fix shape:
one `session_valid()` helper both predicates call — which is exactly what
the next assignment has you do to a sibling file.)

## Smell 2: magic numbers

where: smelly.c:23 (`1800`) and smelly.c:38 (`28800`)

The cutoffs live as bare literals inside comparison expressions.

**Why it matters:** the values *are* policy — 30 minutes idle, 8 hours
lifetime — but nothing says so, and nothing connects them. Whoever changes
the idle policy must know that 1800 here means seconds, and must find every
place policy hides. A named constant (`IDLE_CUTOFF_SECONDS`) makes the next
change a one-line edit and turns the diff into documentation.

## Smell 3: dead code kept "in case"

where: smelly.c:41–44 (`session_expired_v1`)

No caller anywhere; the comment admits it is being kept out of sentiment.

**Why it matters:** dead code is read by every future maintainer (and every
agent — it lands in the model's context and dilutes it), it silently rots —
its `900`/`14400` cutoffs now contradict the live policy, so it actively
*misinforms* anyone who reads it as documentation — and version control
already remembers it perfectly. Deleting it costs nothing; `git log` is the
"in case".

---

*Not flagged:* `session_report`'s repeated `printf` shape is borderline but
defensible as-is; the `s ? s->id : -1` in only the first branch is safe
(the NULL case cannot reach the later branches) though worth a comment. A
review that flagged either with a real consequence is fine — the discipline
is the argued consequence, not this exact list.
