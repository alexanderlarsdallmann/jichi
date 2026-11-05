---
name: read-a-loss-curve
description: How to read the step lines charlm prints -- learning, a plateau, overfitting, and a rate that is too high.
---
# Reading a loss curve

`charlm` prints a line every `--every` steps:
`step 1500   train 2.583  validation 2.941`. Read the two columns together.

- **Both fall.** The model is learning something that carries over to unseen text.
  Keep going while validation still falls.
- **Both flat.** A plateau. Maybe more steps help, a larger model, or a larger
  learning rate. Or the model has learned what it can from this much text.
- **Train falls, validation rises.** Overfitting: the model is memorising the
  training part. The best model was at the step before validation turned. More
  text, a smaller model, or fewer steps are the usual answers.
- **Both jump about, or rise.** The learning rate is too high for this model. Halve
  it and run again.
- **Validation below train.** Possible on a small text, where the held-out tail can
  simply be easier. Not a sign of anything good. It is a reason to try another split.

Step 0 is before any training, and should sit near the uniform baseline, because an
untrained model knows nothing. If it does not, something is wrong with the
program, not the model.
