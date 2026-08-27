---
description: Answer one stated question with the simplest sound method — summaries before models, seed set, sample size stated.
---
Answer **one** clearly-stated question about the cleaned data. If the question is
not yet stated as a testable claim, pin it down first.

1. Start with the boring, checkable number: a mean, a count, a group-by, a
   cross-tab. Sanity-check it against common sense before anything fancier.
2. Only reach for a model or a test if the summary genuinely needs it — and if
   you do, set a random seed and state the assumptions it makes.
3. State the **sample size** next to every statistic, and the caveat next to
   every claim. Note anything that would change the answer (a confounder, a
   small group, a selection effect).
4. Keep the pipeline linear and re-runnable from a clean slate.

End with the honest answer to the question — including "the data cannot tell us"
if that is the truth. Then suggest running the read-only `methods-reviewer` agent
over the analysis before you trust it.
