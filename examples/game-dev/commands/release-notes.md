---
description: Write short, player-facing release notes for a version — what's new, what's fixed, in plain language players care about.
---
Write player-facing release notes for a version into `CHANGELOG.md` (or
`releases/vX.Y.md`). These are for *players*, not for you — plain language about
what changed in the game they play, not the code.

From the recent work (the git log, the bug notes, the features added), draft:

1. **A version and date**, and a one-line summary of the release's headline.
2. **New** — features and content a player will notice, in their words ("You can
   now double-jump", not "added `dash()` to the player controller").
3. **Changed / balanced** — tweaks to how the game plays (a slower enemy, a bigger
   level), especially anything that changes the feel.
4. **Fixed** — bugs players hit, described as the player experienced them ("Fixed
   falling through the floor near the second checkpoint").

Keep it short and human. Skip internal refactors players cannot see. If this is a
playtest build, note what you especially want feedback on. Honest, readable notes
build trust with the people playing your game — and they double as your own record
of what each version actually was.
