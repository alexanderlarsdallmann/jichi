# slm-lab — train a small language model from scratch, on a CPU

A bench for learning how language models are trained **and how their results are
read**. The learner trains a character-level model with `charlm.c`, a single C89
file of a few hundred lines with the gradients written out by hand. jichi is the
lab partner: it builds, runs, reads and reviews, and it trains nothing itself
([`../../docs/ML_SUPPORT.md`](../../docs/ML_SUPPORT.md)).

This is level (b) of the four in section 3 of the twelve-questions plan
([`../../docs/plans/2026-09-24-twelve-questions.md`](../../docs/plans/2026-09-24-twelve-questions.md)):
*train a character-level model from scratch*, the level the operator chose. It
needs no GPU and raises no dataset-licence question, and it teaches the
evaluation habit before the expensive levels.

**The habit it teaches:** a loss is read against a held-out part of the text and
against baselines that need no training. Every run of `charlm` prints both. Its
first measured run, on two of this project's documents (15 KB, 3,000 steps, about
4 seconds), printed:

```
baselines on validation (nats per character; lower is better):
  uniform  5.011  (every character equally likely: ln 150)
  unigram  3.483  (character frequencies of the training part)
  bigram   3.141  (the previous character, counted on the training part)
...
step 3000   train 2.348  validation 2.820
result: validation 2.820 -- beats the bigram baseline by 0.321
```

**Contents**, the beginner-support contract every bench keeps:
- [`START_HERE.md`](START_HERE.md): the first session, numbered;
- [`AGENTS.md`](AGENTS.md): what runs where, and the method;
- `agents/`: `lab-guide` and the read-only `leakage-reviewer`;
- `commands/`: `/build`, `/train`, `/evaluate`, `/sample`;
- `skills/`: `train-a-char-model`, `evaluate-honestly`, `read-a-loss-curve`;
- [`charlm.c`](charlm.c): the program;
- `config.example.json`: secret-free.

**Kept green by** `tests/smoke/example_slm_lab.sh`. It checks the assets as every
bench driver does. It also compiles `charlm.c` under strict C89 warnings, trains it
briefly, and checks that the baselines are ordered, that the model beats the
uniform baseline, and that a seed reproduces a run byte for byte.

**Next levels**, not built here: (a) reading a small model's inference code; (c)
fine-tuning a 0.5–1 B model with LoRA, which needs a GPU; (d) quantising and
serving the result, then driving it with jichi. Section 3 of the plan says what
each needs.
