---
name: train-a-char-model
description: How charlm trains a character-level model -- the model, the loss, the knobs, and what each one trades.
---
# Training a character-level model

`charlm` predicts the next byte of a text from the bytes before it.

**The model**, in the order a byte flows through it:
1. Each of the last `--ctx` bytes is looked up in a table of `--emb` numbers: its
   *embedding*. Similar bytes end up with similar numbers because that helps the
   prediction; nobody tells the model which bytes are similar.
2. The embeddings, side by side, feed `--hidden` tanh units.
3. A softmax turns the hidden units into one probability per byte of the
   vocabulary.

**The loss** is the negative log of the probability given to the byte that really
came next, averaged, in nats. `ln(vocabulary size)` is knowing nothing, and 0 is
certainty that was always right.

**Training** is minibatch stochastic gradient descent: pick `--batch` positions of
the training part, compute the loss's gradient by hand (the backward pass in
`charlm.c`, one line per rule of calculus), and step each parameter against it by
`--lr`. The rate drops to a tenth for the last quarter of the steps.

**The knobs, and what each trades:**
- `--ctx`: more context can see more (word endings, spaces), but costs parameters
  and needs more text to learn from.
- `--emb`, `--hidden`: capacity. More fits the training part better, and past a
  point fits it *too* well. Watch validation.
- `--steps`: more training, until validation stops improving.
- `--lr`: too high and the loss jumps about or rises; too low and it crawls.
- `--seed`: a different start. Differences between seeds are the run-to-run
  spread, the noise floor any comparison must clear.

Change one at a time, and write it down.
