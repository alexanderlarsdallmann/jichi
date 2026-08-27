---
title: Make the failing test pass — reference solution
audience: student
---
# Reference solution: make the failing test pass

Solve the task before reading this. The value of a reference solution is the
*comparison* — where your path differed, and whether the difference matters.

## The walk

**1. Run the test and read the failure, not just the exit code.**

```
$ sh docs/assignments/06-make-the-test-pass/test.sh
1..4
ok 1 - max of positives
not ok 2 - max of negatives: got 0, want -2
ok 3 - max of one
ok 4 - sum
```

The failing line already carries three facts: the broken function is
`stats_max`, the breaking input is *all-negative* values, and the wrong answer
is `0` — a value that is not even in the input array. A zero that appears from
nowhere is a strong hint that somebody *initialized* something to zero.

**2. Form the hypothesis before touching anything.**

In `stats.c`:

```c
int stats_max(const int *v, int n)
{
    int best = 0;      /* <- assumes the answer is never below zero */
    int i;
    for (i = 0; i < n; i++) {
        if (v[i] > best) {
            best = v[i];
        }
    }
    return best;
}
```

With `{-5, -2, -9}` no element is ever greater than the initial `0`, so `best`
never moves. The test with positive numbers passes by luck — the bug was
always there, invisible until a test asked the right question. (That is the
module's lesson in one sentence.)

**3. The smallest fix: initialize from the data.**

```c
int stats_max(const int *v, int n)
{
    int best = v[0];
    int i;
    for (i = 1; i < n; i++) {
        if (v[i] > best) {
            best = v[i];
        }
    }
    return best;
}
```

Reading `v[0]` is safe because the header's contract says `n >= 1`. A guard
for `n == 0` would be *defensive* but would also need a defined answer for an
impossible case — contracts exist so you don't have to invent one.

**4. Re-run. All four pass. Grade records it:**

```
$ jichi grade docs/assignments/06-make-the-test-pass.md --record
Make the failing test pass: PASS
  tests: 4 run, 0 failed  (100%)
```

## Compare your solution

- Did you run the test **before** reading the source? If you read the code
  first and spotted the bug, fine — but the test told you *which* code to
  read in one command.
- Did your fix touch only `stats_max`? A diff that reformats the file or
  "improves" `stats_sum` on the way through is a bigger change than the task
  needed — smallest change that works.
- Did you consider `INT_MIN` as the initializer? It also passes. `v[0]` is
  preferred here because it cannot be below the true answer *by construction*,
  needs no header, and reads as intent rather than trivia.
- If you asked the agent to fix it: did you read its diff before approving,
  and did it leave `test_stats.c` untouched?
