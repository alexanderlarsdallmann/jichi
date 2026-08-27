---
description: Turn a finished analysis into a short, honest report a reader can trust and reproduce.
---
Write a short report of the finished analysis into `REPORT.md`. It must let a
reader both **trust** the result and **reproduce** it.

Structure it as:

1. **Question** — the testable question, in one sentence.
2. **Data** — the source, the sample size, and the key cleaning decisions (link
   `notes/cleaning.md`).
3. **What we found** — the answer, with the sample size and the caveats *in the
   sentence*, not hidden. If a chart is included, its axes are labeled with units
   and its y-axis is not truncated to exaggerate.
4. **What we cannot say** — the limits: what this analysis does NOT show, and what
   would be needed to say more.
5. **How to reproduce** — the exact commands to re-run from the raw data.

Do not overstate. A modest, correct finding honestly reported is worth more than
a bold one that does not survive scrutiny. Offer to have the `methods-reviewer`
read it first.
