# What a gateway's undeclared metadata costs a client — lessons kept, report withheld

> **The report this file used to contain is not published, and that is deliberate.**
> On 2026-08-09 jichi was measured against the OpenAI-compatible gateway this project
> develops on, and the findings were written up **as a report to that gateway's
> administrators** — an operator's configuration, their upstreams, their auth and their
> quotas. That document is correspondence with a third party. It is theirs to publish or
> not, the matters it raises are still being worked through with them (2026-08-19), and
> naming another organisation's operational posture in a public repository is not
> something this project gets to decide on their behalf.
>
> **What the measurement taught *jichi* is a different thing, and all of it is below.**
> Not a summary — the reasoning, the numbers, and what each one changed in the code. The
> operator-facing generalisation, written for *any* gateway rather than one, is
> [`../GATEWAY_ADMIN.md`](../GATEWAY_ADMIN.md); it owes its eight sections to this work.
>
> Colleagues with access to the development history can retrieve the original at
> `git show 9afd976:docs/analysis/2026-08-09-hrz-gateway-findings.md`. It is out of the
> index rather than out of existence, because the public snapshot's manifest **is** the
> index (M484) — so withholding a file and keeping it are the same operation here.

Measurement conditions, kept because they bound every number: one key, one day, one
client. Where this says "measured", it means exactly that.

---

## 1. A client cannot infer what a proxy does not declare — and guesses expensively

An OpenAI-compatible proxy may advertise a model with **no context window, no price and
no tool-calling flag**. Every one of those is a field jichi needs and cannot compute.

The cost is not theoretical and not small. jichi had `contextLength: 32000` configured
for a model whose real window was many times that, because nothing said otherwise. The
measured consequence in one project was **926 history compactions** — the agent
summarising and discarding context it could have simply kept, paying a summarisation
call each time, and losing work it then re-read.

**What changed in jichi.** Three things, and the ordering matters:

- **Detect the state instead of only suffering it (M459).** `last_prompt_tokens` is the
  server's own count *for a request it accepted*, so a served request larger than the
  declared limit **proves the declaration understates the model**. jichi now says so
  (`tests/smoke/context_underdeclared.sh`) instead of advising the user to shrink tool
  output — which was the previous advice and was pointed at the wrong lever entirely.
- **Self-tune the estimate (M77).** The byte/4 heuristic runs ~2× optimistic, so
  `jc_calib` folds each call's real `prompt_tokens` into a per-model ratio. A freshly
  configured model corrects itself within its first turn.
- **Warn at configuration time.** `doctor` flags an undeclared `contextLength`, because
  the number has to come from the operator and the client can only ask.

**The generalisable rule:** *absent* is not *zero*, and a client that treats it as zero
will be wrong in the expensive direction. See [`../GATEWAY_ADMIN.md`](../GATEWAY_ADMIN.md) §1.

## 2. A role can be dead while everything else works, and retrieval fails *quietly*

An embeddings endpoint can return an error — including an HTML error page wrapped in a
JSON envelope, when something upstream is answering as a web server — while chat
completions are perfectly healthy.

This is the worst failure shape jichi has a name for, and the reason is the audience:
**semantic code search then returns nothing, and "no results" reads as "your code does
not contain this"**, not as "the endpoint is down". The same shape as M461's
`search_code` defect and M483's silently-skipped index directory, which ANECDOTES
records as *the tool did not fail: it LIED, silently, on every call*.

It is also the endpoint least likely to be exercised by a human tester, because a person
evaluating a coding agent types a question into a chat box.

**What changed in jichi.** `doctor` reports embed/rerank role coverage rather than
assuming a configured role works; a retrieval failure is surfaced as a failure rather
than an empty result set; and M483 extended the same principle to the index walk, where
an unreadable directory now counts and reports a hole instead of vanishing.

## 3. Reachability and authorization are separate facts

A key that authenticates against a proxy need not authenticate against the host the
proxy fronts. A client holding one `apiKeyEnv` and two `apiBase` values can therefore
have exactly one of them work, with no way to tell from the config that this is possible.

**What changed in jichi.** `jc_net_reachable` probes rather than assumes, the model
`fallback` chain walks to the first *reachable* server, and `doctor` reports
per-server reachability. The lesson is narrow and worth stating anyway: **a config file
records intent, not capability**, and the only honest way to know is to ask the server.

## 4. A vendor's published constant can be wrong for the current generation

Prompt caching was measured working — and the **minimum cacheable block was 4096 tokens
where the published documentation said 2048 and 1024** for the models concerned. A
prefix under the real minimum is not an error: it is silently not cached, bills as
ordinary input, and looks exactly like a client that does not support caching.

**What changed in jichi.** The full measurement, including the two hours spent believing
the bug was ours, is in
[`2026-08-09-hrz-prompt-caching.md`](2026-08-09-hrz-prompt-caching.md) §7, and the
constant is documented at `include/jc_promptcache.h` rather than folded silently into a
`#define`. **Trust the wire over the documentation** — the wire is the thing that bills.

## 5. "Caching is requested" and "caching happened" are different claims

Across **967 logged calls** to two locally-hosted models, cached-token counts were
**zero throughout**, with roughly **28k tokens of fixed prefix re-sent on every call**.
That is very likely an upstream capability gap rather than a defect — prefix caching, if
present, is simply not reported through the OpenAI usage fields — but from the client's
side the two are indistinguishable, and the cost is identical.

**What changed in jichi.** This is why the cache hit-rate is *auditable* rather than
assumed: the per-model hit-rate line in the `telemetry` summarizer, `cached=N` on the TUI
token line, and `doctor`'s warning when caching is on for a priced model with no cache
pricing. It is also why the M440 cost-model prompt section is gated on the **configured**
cache setting and never on the observed hit-rate — a running statistic in the system
prompt would change the cached prefix every turn and destroy the caching it describes.

`include/jc_toolout.h` names this measurement directly, because the right per-tool output
policy is *opposite* on a caching and a non-caching backend, and a project that cannot
tell which it is talking to cannot choose.

## 6. List what you can reach, not what you were told exists

A published capability list and the set of models a given key can actually call are
different sets, and assuming the first cost this project **weeks of planning around a
constraint that did not apply**.

**What changed in jichi.** The `models` subcommand lists configured models *with live
reachability*, and `doctor --live` (M167c) makes one real request and classifies the
answer native/text/none rather than trusting a `supports_function_calling` flag. Ask the
endpoint; it is one HTTP request and it cannot be wrong about itself.

## 7. A quota is invisible until it ends a session mid-flight

An 18-run measurement session died with **16 of 18 runs failing in under a second each**,
after the first two consumed a per-key budget the client could not see: the key-info
route returned nulls for the cap, the spend and the reset cadence. An agentic session is
30–50 requests over several minutes, so it will *always* be mid-flight when a cap lands.

Two client-side lessons, and the second is the one worth keeping:

- **Check the budget before a campaign, not after.** The DEFERRED entry for the frontier
  craft A/B had recorded "reachability checked" and had never checked the *quota* — the
  M326b shape (a reason resting on an unchecked factual claim) occurring inside the very
  register written to prevent it.
- **Surface the server's own words.** The gateway's refusal named the key, the spend and
  the cap; that is far more actionable than any message jichi could synthesise. A client
  that swallows an upstream error and reports "request failed" is destroying the most
  useful thing it received. jichi's `--output json` terminal object therefore carries a
  structured `error{code,type,message}` for **every** terminal state.

## 8. What this does not claim

No configuration was audited, no upstream was visible, and every figure came from one key
on one day. The generalisations above are drawn from a single operator's gateway and are
offered as *shapes to expect*, not as a survey.

And the item worth ending on, because it is the reason the original report was written in
the tone it was: **the caching finding began as a wrong conclusion about jichi's own
software.** Two hours went into a client-side bug that did not exist before the wire was
read carefully enough to show the real minimum. A measurement that makes another party
look wrong deserves more scepticism than one that makes you look wrong, not less.
