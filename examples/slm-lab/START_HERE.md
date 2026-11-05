# Start here — your first language model, trained on your CPU

*A numbered, first-session walkthrough. If you have never trained a model, do
exactly these steps once. At the end you will have trained one, and you will know
whether it learned anything.*

## Before you begin (one-time)

You need two things:

1. **jichi**, set up with a model to think with. A local one is free and private.
   Copy `config.example.json`'s `models` block into your config and run `jichi
   doctor` to check it. If jichi is not set up yet, do that first
   (`docs/CONFIG_TUTORIAL.md`) and come back.
2. **A C compiler**: check with `cc --version`. On Debian or Ubuntu,
   `sudo apt install build-essential` installs one.

No GPU, no Python, no download. Everything below runs on your CPU in seconds.

## Your first model, step by step

1. **Make a workspace** and copy the lab into it, as the examples README shows
   (`agents/`, `commands/` and `skills/` into `.jichi/`, and `AGENTS.md`,
   `charlm.c` beside them). Open jichi there: `jichi`.

2. **Build the program.** Type `/build`. jichi compiles `charlm` with strict
   warnings and reads its header with you. If the compile fails, read the error
   together before going on. A compiler message is the most exact help there is.

3. **Choose a text.** Plain text, at least a few kilobytes, that you may use: your
   own notes, a public-domain book, or this project's documentation (for example
   `docs/PHILOSOPHY.md`). Plain ASCII trains best, because `charlm` counts bytes and a
   letter such as ü is two of them. Save it as `corpus.txt`.

4. **Train once, with the defaults.** Type `/train`. The command line and seed go
   into `notes/runs.md` first, then the run starts. It prints the split, three
   baselines, and a line every 500 steps.

5. **Read the numbers.** Type `/evaluate`. The question is not "is the loss low?"
   but "**does the validation loss beat the bigram baseline**, and by how much?"
   Write that sentence down. A model that does not beat it has learned less than
   counting pairs of letters, and that is a real, respectable result.

6. **Look at a sample**, knowing what it is. Type `/sample`. Enjoy it, and do not
   treat it as evidence: that is the number from step 5.

7. **Change one thing and run again.** A longer `--ctx`, a bigger `--hidden`, more
   `--steps`. One at a time, each with its question. Then run the same line with
   another `--seed`: the difference between seeds is how much any other difference
   has to beat.

8. **Before you share a result**, ask the `leakage-reviewer` agent to read your
   notes and runs. It changes nothing. It looks for a training loss quoted as the
   result, a baseline missing, and runs that were tried and not reported.

**If you get stuck at any step:** say so in plain words ("the loss does not go
down", "I don't understand the baseline"). The lab guide agent will go back to the
step with you. And stopping after step 5 is fine: you trained a model and read it
honestly, which is the whole lesson.
