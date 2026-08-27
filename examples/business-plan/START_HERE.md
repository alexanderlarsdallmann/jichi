# Start here — pressure-test your idea cheaply (before you build it)

*A numbered, first-session walkthrough. Do these steps once on a real idea you are
considering. You will end with a one-page model, honest numbers, and a cheap plan to
find out whether it's real — which is worth far more than a long, confident plan.*

## Read this first

This bench helps you **plan and test** a small venture; it is **not** business,
financial, or legal advice. It helps you think clearly and cheaply — for real
decisions (registering a business, taxes, contracts, raising money), see a qualified
professional. And the whole point: **do not build the thing yet.** An idea is a
stack of assumptions; the smart, cheap move is to test the riskiest one *first*.
Finding out an idea does not work, for a week and no money, is a **win**.

## Before you begin

You need **jichi** (you have it) and **a model** (copy `config.example.json`'s
`models` block into your config; a local one is free and private —
`docs/LOCAL_MODELS.md`; then `jichi doctor`). Bring one real idea you are actually
considering — a product, a service, a side project.

## Your idea on one page (and then a reality check), step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`).

2. **Put the whole idea on one page.** Type:

   ```
   /lean-canvas
   ```

   jichi walks you through the nine boxes of a **lean canvas** — problem, customer,
   solution, value, channels, revenue, costs, metrics, advantage. As you go, mark
   what you *know* versus what you *guess* (spoiler: most is a guess — that is normal
   and useful). It writes `canvas.md`. At the end, find the **one box that, if wrong,
   kills the idea.** That is the riskiest assumption.

3. **Get specific about the customer.** The box most ideas die on:

   ```
   /market
   ```

   Push past "a big market" to a *specific* person with a *specific*, painful
   problem. Vague customer, dead business. Note who you could actually talk to.

4. **Check whether one sale makes money.** The arithmetic beginners skip:

   ```
   /pricing
   ```

   jichi helps you work out the price, the cost to deliver one unit, and roughly what
   it costs to get a customer — and whether a *single* sale actually comes out
   positive. If one transaction loses money, more customers make it worse. Better to
   know now.

5. **Have your assumptions challenged.**

   ```
   Spawn the assumptions-reviewer agent to review my plan.
   ```

   It reads (never edits) and finds the guesses you stated as facts and the riskiest
   assumption you took for granted — the skeptical friend every founder needs.

6. **Design the cheapest test.** Now turn the riskiest assumption into an experiment:

   ```
   /milestones
   ```

   jichi helps you design the *cheapest thing that could prove you wrong* — talking
   to 10-20 potential customers, a landing page, a pre-order, a by-hand "concierge"
   version — with a clear go / pivot / stop threshold decided *before* you run it.
   A real signal (someone pays or commits) beats a vanity metric (page views, polite
   "sounds cool") every time.

That is the loop: **canvas → customer → pricing → challenge → cheap test.** Then go
run the test in the real world. Come back and update the canvas with what you
learned. Build only what survives contact with real customers — that is how you
avoid spending six months on a hunch.

## If you get stuck

- Your "customer" is "everyone" → that is nobody; narrow it hard (`/market`,
  `skills/lean-canvas`). Specific is testable.
- The numbers feel fine but you did the math loosely → do the unit economics
  honestly, counting your own time (`skills/unit-economics`, `/pricing`). One sale
  must make money.
- You want to just build it → that is the expensive instinct; test the riskiest
  assumption first (`skills/validation`, `/milestones`). Cheap now, or costly later.
- The idea does not survive the test → that is a *success* — you learned it for
  almost nothing. Pivot or move on with clear eyes.
