# The philosophy of jichi

*Translations: [Deutsch](i18n/de/PHILOSOPHY.md) ·
[Español](i18n/es/PHILOSOPHY.md) · [日本語](i18n/ja/PHILOSOPHY.md) ·
[中文](i18n/zh/PHILOSOPHY.md) — this English text is canonical.*

Most of this repository's documentation says *how*. This page says *why* — the
principles behind a from-scratch C89 reimplementation of an AI coding agent, and
the debts it owes. It is an essay, not a reference; nothing here is normative
the way [CONTRIBUTING.md](../CONTRIBUTING.md) is. But if a design decision ever
seems arbitrary, this is the frame it was made in.

## 温故知新（おんこちしん） — study the old to know the new

*Onkochishin* (温故知新（おんこちしん）), from the Analects: "review the old, and you will know
the new."

The obvious question about this project is: why C89, in an era of Rust and
TypeScript? The answer is not nostalgia. C89 is the most widely implemented
programming-language standard in existence. A program that holds to it — no
`//` comments, declarations at block top, no `long long`, string literals split
under 509 characters — compiles on machines the npm ecosystem has never heard
of: a decade-old lab computer, a university's shared login host, a single-board
machine in a classroom with no budget. The constraint is the feature. An
agentic tool *for learning* is worth little if the learner's machine can't run
it.

There is a second, quieter reason. The newest thing in computing — a large
language model reasoning about code — turns out to be well served by the oldest
disciplines we have: bounded buffers, explicit ownership, status codes instead
of exceptions, small tools composed over pipes and processes. The agent loop
forks, pipes, and `select`s the way UNIX programs did in 1985. Studying the old
was how the new got built.

## 職人気質（しょくにんかたぎ） — the craftsman's temperament

*Shokunin katagi* (職人気質（しょくにんかたぎ）) names an attitude: mastery pursued as an
obligation to the work itself, not to an audience. The shokunin sweeps the
workshop floor whether or not a customer is coming. (気質 is read both かたぎ
and きしつ; this page uses かたぎ, the usual reading in this compound. The body of
this section romanized it *kishitsu* against its own heading until M299 — a small
inconsistency, corrected rather than left, which is itself the point of the
section.)

In this codebase the swept floor looks like: zero warnings under
`-std=c89 -pedantic -Wall -Wextra`, always, with `WERROR=1` in CI; more than
10,000 unit checks that run offline, so a contributor on a train can trust the
suite; provider streaming tested by feeding synthetic SSE events rather than by
calling a network; every `cJSON_Parse` matched by a `cJSON_Delete`; a fuzzing
tier for the parsers that face untrusted bytes. None of this is visible in a
demo. All of it is visible in year three.

Craft also means honesty about what the tool did. Tool errors are returned as
values, never control flow. A subagent stopped at its iteration cap says so —
"[stopped at its iteration limit]" — instead of passing off a partial answer as
a finished one. The Japanese and Chinese translations under `docs/i18n/` are
labeled *draft* until a native speaker reviews them, because a confident wrong
translation is worse than a marked-honest one.

**And since M299 the temperament is asked of the agent, not only kept by the
codebase.** The system prompt now carries a *How to work* section: understand
before changing, measure rather than assume, ask when two readings would lead to
different work, design and write down the decisions *including the rejected
alternatives*, then implement → test → correct → refactor, prove a test can fail
before trusting it, say plainly what is unverified or skipped, and work without
hurry.

Every line of it is phrased as behaviour an observer could check. That constraint
is deliberate and it is the hard part: **Japanese vocabulary laid over unchanged
conduct is the exact opposite of the value it names.** A section that said "act
with the spirit of the craftsman" would be decoration. "Report a failure with its
output rather than a summary of it" is a thing you can catch someone not doing.
Each term on this page earns its place only while something in the code or the
practice actually differs because of it — and where one does not, it should be cut
rather than admired.

## 縁（えん） — the connection you did not plan

*En* is the thread between things that meet without being introduced: coincidence
that turns out to matter. Serendipity, but with the sense that the meeting was
always available and you happened to be open to it.

Most of what this project knows arrived that way. The 12 GB memory report came
from someone else's machine. The empty-assistant-turn bug that silenced tool
calls was found because a small local model was too literal to paper over a
malformed request. Two test fixtures in one session passed while checking the
wrong thing, and finding out *why* taught more than either test did. None of that
was on a plan.

The practical form of 縁 is a working posture, not a hope: keep the diary while
the project is alive (see `docs/ANECDOTES.md`), record the thing that surprised
you before you have explained it away, and stay willing to follow the better idea
that arrives uninvited — even when it arrives in the middle of something else.
You cannot schedule the encounter. You can be the sort of project that notices.

## 改善（かいぜん） and 守破離（しゅはり） — small steps, and the shape of learning

*Kaizen* (改善（かいぜん）) is improvement by accumulation. This project has never had a
rewrite; it has had milestones — M0's skeleton to M134's hardening pass — each
small enough to review, test, and if necessary revert. The
[ROADMAP](ROADMAP.md) is a lab notebook of that accumulation, and
[ANECDOTES.md](ANECDOTES.md) records the failures worth remembering, because a
mistake studied is a lesson and a mistake forgotten is a rehearsal.

*Shu-ha-ri* (守破離（しゅはり）) describes how a student of any craft advances: **shu**
(守), keep the form exactly; **ha** (破), break the form deliberately; **ri**
(離), leave the form behind. The learning features are built to this shape. The
assignments band gives a beginner the form — a brief, a rubric, a hint ladder —
and the learner tiers (`learner-junior|student|senior`) loosen scaffolding as
the student earns it. Even the agent itself is a student here: the
learning/mentor loop reads its own telemetry, drafts lessons, and — since M78 —
*corrects* stale ones rather than piling new advice on old. Teaching that
cannot retract is dogma; the loop was not finished until it could take a lesson
back.

## 不易流行（ふえきりゅうこう） — the unchanging and the flowing

Bashō taught his students *fueki-ryūkō* (不易流行（ふえきりゅうこう）): art needs both the
permanent and the fleeting, and they are at root the same pursuit.

This project's architecture is that lesson applied. The **unchanging**: C89,
POSIX, arenas, the `jc_status` contract, a provider vtable the agent never
looks behind. The **flowing**: models, endpoints, and protocols that turn over
every season. The boundary between them is drawn so the flow never erodes the
core — the agent does not branch on provider; a new backend is a new vtable, not
a new agent. That is why the same binary that talks to a frontier API also runs
against a llama.cpp server on the desk beside you, and why it will still build
when both have been replaced.

## 侘寂（わびさび） — the beauty of honest limits

*Wabi-sabi* (侘び寂び) finds worth in the imperfect, impermanent, and
incomplete. Software culture mostly pretends otherwise; this codebase tries not
to. The token estimate is a byte heuristic and *known* to run optimistic — so
M77 calibrates it against reality per model instead of claiming precision it
lacks. Budgets end runs mid-thought, so M80 learned to keep the partial work
rather than burn it for failing to be whole. Every buffer is bounded, every cap
is named, and when output is truncated the truncation says so. A tool that
admits its limits can be trusted at them; a tool that hides them cannot be
trusted anywhere.

## Giants, and heaps of dwarves

We stand on the shoulders of giants — and, just as truly, on heaps of dwarves:
the countless small contributions with no famous name attached. Both debts are
real, and this project pays respect to each.

The giants are easy to name: C and the committee that froze it in 1989 into
something the whole world could build on; UNIX and POSIX; libcurl, which has
moved bytes for more of humanity than any of us will meet; the cJSON project's
API design, which `src/json/` reimplements rather than copies; git, whose plumbing quietly powers the snapshot and worktree
machinery; the Language Server Protocol, the Model Context Protocol, and the
Agent Client Protocol, each an act of generosity that traded a private
advantage for a public standard; [Continue](https://continue.dev), whose CLI
this reimplements and whose design proved the shape worth building; and the
model providers whose APIs give the loop something to talk to.

The dwarves are harder to name, which is the point: the man-page authors, the
Stack Overflow answerer of one obscure `tcsetattr` question, the person who
reported the locale decimal-comma bug in some other project years ago so that
this one knew to guard `LC_NUMERIC`, the reviewers of RFCs, the translators,
the testers of release candidates on strange machines. No single one of them is
a giant. Together they are the ground. A project that only credits giants has
misunderstood where the ground comes from — and a project that hopes to matter
should aspire, mostly, to join the heap: to be someone else's unremarkable,
reliable foundation.

## Mountains, and the sea

井の中の蛙大海を知らず、されど空の深さを知る — "the frog in the well knows
nothing of the great ocean; yet it knows the depth of the sky."

The proverb cuts both ways, and both edges matter here. Depth without breadth
is the well: this project climbs its mountain — one language standard, one
platform family, mastered thoroughly. Breadth without depth is a tourist's
ocean. The discipline is to do both deliberately: dogfooding runs against
foreign codebases (the zigodot sessions that produced a dozen milestones),
localization that forces English-centred assumptions into the open (M127's
wide-character work exists because Japanese text broke the line editor's idea
of a column), accessibility work that asks what the interface is like when you
cannot see it.

We must explore to change our point of view. Every genuinely good milestone in
the roadmap traces back to a change of vantage — running the tool instead of
writing it, reading the telemetry instead of trusting the design, handing it to
a learner instead of an expert. The mountain teaches craft; the sea teaches
humility; the well, tended honestly, teaches the sky. A tool for learning
should be built by people still willing to learn — and, where it can manage it,
should be one of them.

---

*Companion pages: [JOURNEY.md](JOURNEY.md) (the road from first step to
mastery), [ANECDOTES.md](ANECDOTES.md) (lessons paid for),
[ROADMAP.md](ROADMAP.md) (the kaizen ledger), [LEARNING.md](LEARNING.md) (the
mentor loop), [TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) (pedagogy),
[i18n/README.md](i18n/README.md) (localization policy),
[ACCESSIBILITY.md](ACCESSIBILITY.md).*
