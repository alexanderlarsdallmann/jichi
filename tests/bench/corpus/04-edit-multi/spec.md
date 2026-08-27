---
title: Raise a constant consistently across files
audience: agent
verify: "grep -q 'MAX_ITEMS 32' queue.h && grep -q '32' README.md && ! grep -q 'MAX_ITEMS 16' queue.h && ! grep -q '(16)' README.md"
points: 2
---
The queue's capacity must be raised from 16 to 32. Update the `MAX_ITEMS`
definition in `queue.h` and the capacity mentioned in `README.md` so both say
32. Do not change `queue.c`.
