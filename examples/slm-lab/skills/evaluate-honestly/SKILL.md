---
name: evaluate-honestly
description: How to read a model's loss so the conclusion is no bigger than the evidence -- held-out data, baselines, spread, and what was not measured.
---
# Evaluating honestly

A training run produces confident-looking numbers. Most of them mean nothing
without four things.

1. **Held-out data.** The number that counts is on text the model never trained on:
   `charlm`'s validation part, the last tenth of the file. A training loss says how
   well the model memorised, not how well it predicts.
2. **Baselines.** A loss is read against what needs no training. *Uniform* is
   knowing nothing. *Unigram* knows letter frequencies. *Bigram* knows which letter
   follows which. A model that does not beat the bigram baseline has learned less
   than counting pairs, whatever its sample looks like.
3. **Spread.** Run the same thing with two or three seeds. The differences between
   them are the noise. A change that improves validation loss by less than that
   spread has not been shown to help.
4. **What was not measured.** One text, one held-out slice, one seed, one size.
   Write the list next to the result. It is part of the result.

Words to use: "on this text", "on the held-out tenth", "with one seed", "beats the
bigram baseline by 0.3 nats". Words to avoid: "it understands", "it learned the
language", "better" without a number and a baseline beside it.
