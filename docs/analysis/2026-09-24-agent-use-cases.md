# What agents are built for, and what almost nobody builds (2026-09-24)

*An analysis, written for the operator's question of 2026-09-24: "What is the most
sought-after use-case implementation for AI agents? What is the most overlooked?" It is
written for three readers: a self-learner choosing what to build, a junior developer
choosing what to learn, and an agent deciding which feature of jichi to work on next.*

**Read the two halves differently.** The first half is *reported*: three surveys and
usage studies, each with its date, its sample and what it cannot tell you. The second
half is *my judgement*, argued from gaps in that evidence. Nobody measures what is
overlooked -- an overlooked use case is by definition one nobody counted -- so that half
is an argument, not a finding, and says so.

---

## 1. The evidence, and what each source can and cannot say

### 1.1 LangChain, *State of AI Agents* (survey 18 Nov - 2 Dec 2025)

1,340 responses, mostly from the technology industry (63%), then financial services
(10%) and healthcare (6%). Asked for their **primary** agent use case:

| use case | share |
|---|---|
| customer service | 26.5% |
| research and data analysis | 24.4% |
| internal workflow automation | 18% |

The largest barrier to production was **quality** (32%), then latency (20%). 57% had
agents in production. On checking what agents do: 89% had "some form of" observability,
62% detailed tracing of individual steps, **52.4% ran offline evaluations and 37.3%
online evaluations**; 59.8% relied on human review and 53.3% on LLM-as-judge. Over 75%
used more than one model, and 57% did not fine-tune. For the respondents' *own* daily
work, the report says coding agents dominate.

*What it cannot say:* it is a framework vendor surveying its own audience -- people who
already build agents -- so it measures what builders build, not what anyone needs.

### 1.2 Anthropic, *Economic Index* (15 Sep 2025, and 26 Jun 2026)

Usage of one vendor's models, classified from sampled conversations.

- **September 2025** ("Uneven geographic and enterprise AI adoption"): computer and
  mathematical tasks were **36%** of Claude.ai usage; educational instruction and library
  tasks rose from 9% to **12%** across the index's versions. In the first-party API --
  businesses deploying the model in their own systems -- "little less than half" of the
  traffic mapped to computer and mathematical tasks, and **77%** of business uses showed
  *automation* patterns, against about 50% on Claude.ai. Directive conversations (the user
  hands the task over) rose from 27% in late 2024 to 39%.
- **June 2026** ("Cadences", data 10 Apr - 10 Jun 2026): by output type, explanations
  were 17% of chat conversations, documents and reports 15%, guidance 11%. Computer and
  mathematical occupations were about 30% of survey respondents against 4% of US
  employment. Agentic coding sessions ran with measurably more autonomy than chat.

*What it cannot say:* it is one product family's users. It shows what people ask one
assistant for, not the market, and it cannot see a use nobody attempted.

### 1.3 Stack Overflow, *Developer Survey 2025*

About 49,000 respondents. 84% used or planned to use AI tools; 51% of professional
developers used them daily. **31% used AI agents** (14.1% daily, 9% weekly, 7.8% less
often), 17.4% planned to, 37.9% did not plan to. The tasks already done *mostly with AI*
were searching for answers (54.1%), generating content or synthetic data (35.8%) and
**learning concepts (33.1%)**. The tasks developers did **not** plan to hand to AI:
deployment and monitoring (75.8%), project planning (69.2%), predictive analytics
(65.6%), code review (58.7%). Trust was low: 3.1% highly trusted the accuracy of AI
output, 45.7% distrusted it.

*What it cannot say:* it is developers only, and it asks about intentions as well as
use.

---

## 2. The most sought-after use case: coding, then customer service and research

The three sources agree once you ask *who is asking*:

- **Developers, for their own work:** coding. It is the largest single category of use in
  both Anthropic reports and nearly half of business API traffic, and LangChain's
  respondents name coding agents as what they use every day.
- **Organisations, for their products:** customer service, and research and data
  analysis -- together more than half of the primary deployments LangChain counted.
- **Individuals, for themselves:** explanations, guidance and documents -- and learning.

So the honest answer is **coding agents**, and the most sought-after *kind* of
implementation is the one jichi already is: an agent that reads a repository, changes
it, and runs its tests. That is also why the space is crowded, and why "another coding
agent" is not by itself a reason for jichi to exist.

---

## 3. The most overlooked use cases -- my judgement, argued from the gaps

Four candidates, strongest first. Each names the evidence it rests on and what would
refute it.

### 3.1 An agent whose deliverable includes the evidence for its claims

**The gap.** Quality is the first barrier to production (32%), developers distrust AI
accuracy more than they trust it (45.7% against 33%), and yet only about half of the
organisations that build agents run offline evaluations, and 37% online ones. The effort
goes into agents that *do more*; very little goes into agents that *show* what they did
and whether it worked.

**What it would be.** An agent that does not report "done" but reports *what it checked*:
the command, its exit code, the test that went red before the fix and green after it, what
it did not test. This is not an evaluation platform bolted on afterwards; it is the agent's
own habit. jichi's practice is an instance -- the gate, perturbation per check, journals of
what was run, "report the effect, never the attempt", `--verify` on edit runs -- and the
overnight drives measure it on a real project.

**What would refute it:** a survey showing evaluation adoption near 100% among agent
builders, or users who do not care whether the agent's claims are checked. Neither is in
the evidence above.

### 3.2 An agent that teaches instead of doing

**The gap.** Learning concepts is already one of the three tasks developers do mostly with
AI (33.1%); explanations and guidance are over a quarter of chat outputs; education tasks
are growing (9% to 12%). But an *agent* -- a program that acts -- is built for delegation,
and a learner who delegates the exercise learns nothing. The tools that act are optimised
to finish the work; the tools that explain cannot see or check the learner's work.

**What it would be.** An agent that sets a task, lets the learner do it, runs an
executable grader against the result, explains the failure, and refuses to write the
solution unless asked -- and says when it was asked. jichi already has the pieces: a
course of graded assignments ([`../assignments/INDEX.md`](../assignments/INDEX.md)), graders that are
themselves tested, and fences that can make the agent read-only. The in-session pieces
exist too -- `/assignment` turns the model into a tutor that "guides, never solves",
and `/grade` runs the brief's own grader ([`../CURRICULUM.md`](../CURRICULUM.md)). What
is missing is evidence: no learner has been observed using them.

**What would refute it:** evidence that learners who use a doing-agent learn as well as
those who use a coaching one. The surveys above do not measure learning at all -- which is
itself the gap.

### 3.3 Operations help that stays read-only

**The gap.** Deployment and monitoring is the task developers most refuse to hand to AI
(75.8%), code review the fourth (58.7%). Trust is lowest where mistakes cost most. The
usual response is to build more autonomous operations agents; the overlooked one is the
agent that *never acts* in production: it reads the alert, the log and the config, says
what it thinks is wrong and why, and drafts the command for a human to run.

**What it would be.** jichi's read-only agents and path fences already express "may read,
may not change"; a runbook assistant is mostly a skill and a fence profile, not new code.

**What would refute it:** operators who find such an assistant useless because the reading
was never the hard part.

### 3.4 Agents on modest hardware, with free models

**The gap.** Most agent products assume a frontier model in someone else's cloud and a
fast machine. LangChain's respondents mostly use several models (over 75%) and mostly do
not fine-tune (57%), which says the open and local side matters to builders -- but the
long tail of *users* is barely served: a school lab, a 512 MB board, an air-gapped
machine, a university on its own gateway.

**What it would be.** Exactly what jichi's platform matrix measures: an agent that runs on a
Raspberry Pi Zero, a 160 MB virtual machine and an Atari emulator, and is driven by free
local models. The honest limit: small local models call tools less reliably, which is why
every driven row records the model.

---

## 4. What this means for jichi -- recommendations

1. **Keep coding as the core.** It is where use is; it is also where jichi can be measured
   against its own corpus drives.
2. **Make the evidence the product (3.1).** Every run already journals what it did. The next
   step is a final answer that *cites* that journal -- which checks ran, which failed, what
   was not tested -- by default, not only in corpus drives.
3. **Build the coach mode (3.2)** on the existing assignments and graders, for the
   self-learners this project is written for.
4. **Ship a read-only operations profile (3.3)** as a fence profile plus a skill -- cheap, and
   a direct answer to the lowest-trust tasks.
5. **Keep the modest-hardware work (3.4)** and keep recording the model on every row.

## 5. What this page cannot tell you

- No source here measures self-learners or junior developers specifically.
- Usage is not value: a use case can be common because it is easy to try.
- The surveys are from builders and one vendor's users; nothing here is a market study.
- "Overlooked" is not measured anywhere. Section 3 is an argument; if you find a
  measurement that contradicts it, the measurement wins.

## Sources

- LangChain, *State of AI Agents* (survey 18 Nov - 2 Dec 2025): https://www.langchain.com/state-of-agent-engineering
- Anthropic, *Economic Index: Uneven geographic and enterprise AI adoption* (15 Sep 2025): https://www.anthropic.com/research/anthropic-economic-index-september-2025-report
- Anthropic, *Economic Index report: Cadences* (26 Jun 2026): https://www.anthropic.com/research/economic-index-june-2026-report
- Stack Overflow, *2025 Developer Survey*, AI section: https://survey.stackoverflow.co/2025/ai

All four were read on 2026-09-24; the figures are quoted as the pages state them.
