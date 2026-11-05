# SLM lab conventions

*This file tells the agent how to behave in a small-language-model lab. jichi loads
it automatically. Keep it short; edit it to match your lab.*

## What runs where (read this first)

jichi does **not** train anything. It never has, and this lab does not change that
(`docs/ML_SUPPORT.md`). The training is done by **your program**, `charlm.c`: a C89
file that you compile yourself and can read line by line. jichi helps you build it,
runs it for you through the shell (a long run in the background, polled), reads its
numbers with you, and reviews your method. Every command it runs is shown to you
before it runs. Read it.

Everything is on your CPU. There is no GPU, no Python, and no download.

## The method, always

1. **Hold out before you train.** `charlm` keeps the last part of the text (10% by
   default) as validation data, and training never reads it. Never train on it,
   never tune against it more than you report, and never quote a *training* loss as
   the result.
2. **A loss means nothing alone.** Every number is read beside its baselines:
   *uniform* (knowing nothing), *unigram* (letter frequencies) and *bigram* (the
   previous letter, counted). A model that does not beat the bigram baseline has
   learned less than counting pairs of letters, and that is the finding.
3. **One change at a time, one seed stated.** A run is reproducible from its command
   line. Compare two runs only when one thing differs, and write down which.
4. **Say what was not evaluated.** A validation loss on one held-out slice of one
   text is one number. The report says so, and says what else would be needed
   (another text, several seeds, a different split).

## The workflow (commands)

- `/build` — compile `charlm` with strict warnings, and read what it will do.
- `/train` — a run in the background, with its command line and seed written down.
- `/evaluate` — read a run's numbers against the baselines, honestly.
- `/sample` — generate text, and say what it does and does not show.

Before you trust a result, run the read-only `leakage-reviewer` agent over your
runs and notes.
