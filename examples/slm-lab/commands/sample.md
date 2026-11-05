---
description: Generate text from a trained run, and say what a sample does and does not show about the model.
---
Look at what the model produces, without mistaking it for a measurement.

1. Take the `sample` a run printed, or run again with the same command line and a
   larger `--sample`. Same seed, same text: the sample is reproducible.
2. Read it with the learner. Which patterns has it picked up (word lengths,
   spaces after full stops, common words), and which has it not?
3. Say plainly that a sample is an **illustration, not evidence**. Two samples of
   the same model look different, and a good-looking sample can come from a model
   with a worse validation loss. The number from `/evaluate` is the result.
4. If the text has bytes that print as `?` or boxes, that is a UTF-8 character split
   into two bytes: `charlm` models bytes, not letters. It is noted in its header as
   one of the things it does not do.
