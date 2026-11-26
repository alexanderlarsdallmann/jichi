# Guiding yourself — the learner without a teacher

**Who this is for.** A self-learner working without an instructor, at any stage
of [JOURNEY.md](JOURNEY.md). If you have a teacher or a cohort, the seat this
page describes is theirs, and [TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md)
is the page to read. **After reading** you can check your own understanding,
set your own pace, and decide for yourself when to move on, with evidence.
**You need first:** a record of your mistakes, however short
([JOURNEY.md](JOURNEY.md), the section on Shu, says how to begin one).
**Where it fits:** beside [JOURNEY.md](JOURNEY.md), which is the map; before
[PATTERN_LANGUAGE.md](PATTERN_LANGUAGE.md), which begins where a lesson
repeats.

Most pages in this tree assume that something stands beside you: a tutorial, a
rubric, a gate, a model. This page is about the part none of them can do.

[JOURNEY.md](JOURNEY.md) gives each stage a line that begins *Ready to move on
when*. It does not say who decides. With a teacher, the teacher decides. Alone,
you decide — and you are the one person with a reason to decide early. The
same holds for *slow down*, for *that is enough for today*, and for *you do not
understand this yet*. A classroom says these things to a student. Nobody says
them to a self-learner.

That is the seam this page tries to mend. It is an essay, like
[PHILOSOPHY.md](PHILOSOPHY.md), and nothing in it is normative. It holds itself
to that page's test: advice counts only where an observer — or you, tomorrow,
reading your own notes — could catch you not following it. Where a paragraph
below is only a mood, cut it.

## 独学（どくがく） — two seats, one person

*Dokugaku* (独学（どくがく）) is study by oneself.

A classroom has two seats, and the work in them is different. The student's
seat *proposes*: it tries, guesses, builds. The teacher's seat *checks*: it
asks for the reason, sets the pace, and says *not yet*. Self-study does not
remove the second seat. It leaves it empty, unless you sit in it on purpose
and at set times.

Sitting in it is a handful of actions:

- **Say what will happen before you run it.** `/predict` in the TUI writes the
  expectation down and keeps the tally ([ARGUMENT.md](ARGUMENT.md) §3). A
  prediction that was not written down is remembered as having been right.
- **Explain it with the screen dark.** Close the editor and say what the code
  does, why it is built this way, and what you decided against. If the
  explanation needs the screen, the understanding is still in the screen.
- **Write the gate before the attempt.** "I am done when the parser rejects
  these three inputs", on paper, first. A gate chosen after the result opens
  for whatever result arrived.
- **Change seats by the clock, not by feeling.** The last ten minutes of a
  session belong to the teacher's seat, with one question: *what did I claim
  today that I did not check?*

Two old words belong to that seat. Practice in the Japanese arts is called
*keiko* (稽古（けいこ）), and its characters mean *to consider the old*: practice
there is not repetition, it is going back over what was handed down. Alone,
what is handed down to you each morning is your own note from the day
before, so begin a session by reading it. And the ten minutes at the end
have a name too: *hansei* (反省（はんせい）), looking back over what one did. In
the workshops that practise 改善（かいぜん） ([PHILOSOPHY.md](PHILOSOPHY.md)) it is
described as something done after a success as well as after a failure: a
success that
nobody examined cannot be repeated on purpose.

The model is not the teacher's seat. It is a third thing: a well-read
companion that answers at once, does not tire, is sometimes wrong in a
confident voice, and leans towards agreeing with you. It can hold up a mirror,
and [GROUNDED_DISCOURSE.md](GROUNDED_DISCOURSE.md) shows how to make it do that
well. It cannot say that you are ready, because all it knows of your
understanding is what you told it.

## 正直（しょうじき） — honesty begins at your own desk

*Shōjiki* (正直（しょうじき）) is honesty in the plain sense: saying what is so.

This project asks it of the tool — output that was cut says that it was cut —
and of its own pages. For a learner alone it takes a harder form, because you
are both the one who might deceive and the only one present to notice.

Four sentences deserve distrust. Each has a test.

1. **"I understand this."** It often means *I recognised it*. The test:
   tomorrow, from an empty file and without looking, build the core of it
   again. Recognition does not survive that. Understanding does.
2. **"It works."** It often means *it ran once, on the input I had in mind*.
   The test is the project's own ([APPROACH.md](APPROACH.md) §3): make your
   check fail on purpose. If you cannot turn it red, you do not know what it
   checks.
3. **"I am stuck."** It often means *I have not written the question down*.
   The test: two sentences on what you expected, two on what you saw, and a
   list of what you have ruled out. Sometimes the writing ends the matter.
   When it does not, you now hold a question that a person or a model can
   answer.
4. **"I wrote this."** With an agent beside you it often means *I approved
   this*. The test: could you have written it? Delete the heart of it once
   and write that part by hand. [JOURNEY.md](JOURNEY.md) says a solution
   peeked at is a debt; an edit approved without reading is the same debt.

None of this is a beginner's weakness. The project's own record has entries
whose titles are confessions of exactly this kind:
[ANECDOTES.md](ANECDOTES.md) 64, "The cap turned a hang into a result, and the
result agreed with me", and 68, "The 500 went away, so I stopped asking what
had answered". In both, careful work stopped at the moment the answer became
the wanted one. That gives the rule its general form: **check hardest when the
result pleases you.**

There is a Japanese word for the state of mind this needs: *sunao*
(素直（すなお）), taking a thing as it is, without first defending yourself
against it. As conduct it is small: when a check contradicts you, write down
what the check said before you write down why it must be mistaken.

Two smaller habits keep the larger one alive.

**Write the entry on the day, in that day's words.** Your record
(*symptom → dead ends → root cause → lesson*, as [JOURNEY.md](JOURNEY.md)
describes it) is honest only if it keeps what you believed while you were
wrong. An entry tidied a week later tells a story in which you were never
confused, and such a story teaches nothing. The entries whose root cause is
*me* are the valuable ones.

**Say what you did not check, even in notes nobody will read.** "Tested with
ASCII input; other input not tried." [APPROACH.md](APPROACH.md) §5 asks this of
every report in the project. The habit forms where nobody is watching, or it
does not form.

## 急がば回れ（いそがばまわれ） — patience is a method

*Isogaba maware* (急がば回れ（いそがばまわれ）): "if you are in a hurry, take the long way
round."

The proverb is not about staying calm. It is a claim about cost: the short cut
is the dearer road. Patience in this craft is therefore not a feeling you wait
to have. It is a small set of actions, and each can be observed:

- **Read the error to its last line before you touch anything.** The first
  line names the symptom. The cause is often further down.
- **Change one thing, then look.** After two changes and a success you do not
  know which change it was, and you will carry the useless one along.
- **Reproduce before you repair.** A repair for a failure you cannot produce
  at will is a hope.
- **When it works at last, stay five minutes longer** and ask why it failed
  before. Without that answer you will pay for the same failure again.
  Archery and swordsmanship call this *zanshin* (残心（ざんしん）), the mind that
  remains: the form is not over when the arrow has left the bow.
- **Stop at "I do not know why" and write that down.** It is a better place to
  stop than "it is fine now".

Impatience has a shape you can learn to recognise: *acting to find out what
reading would have told you*. The project paid for one instance and kept the
receipt: [ANECDOTES.md](ANECDOTES.md) 66, "I ran the destructive command to
find out whether it existed".

**The long clock.** 石の上にも三年（いしのうえにもさんねん）: sit on a stone for three
years and even the stone grows warm. The essay this project's author set his
course by says the same with a larger number: Peter
Norvig's *Teach Yourself Programming in Ten Years*
([BIBLIOGRAPHY.md](BIBLIOGRAPHY.md), section 1). Ten years is not a sentence
passed on you. It is a unit of measure, and it changes what the small numbers
mean. A bad week is noise. The fair comparison is between you and your own
record of a season ago, never between you and someone else's today.

**Plateaus.** There are stretches where nothing seems to improve. Often that
is the time in which a form moves from the head into the hands, and the
feeling of progress is a poor instrument for it. Your record is a better one.
Read an entry from three months back. If the mistake in it now looks obvious,
you have moved, whatever the week felt like.

**Two kinds of hurry.** One rushes ahead and opens its gates early. The other
hides in preparation: one more tutorial, one more evening arranging the
editor, before any real attempt. The second kind looks like diligence, which
makes it the harder one to see. Its test: *did I build something this week
that could have failed?*

**The interval.** In Japanese music and on the stage, *ma* (間（ま）) is the
pause between two sounds or two movements, and it is counted as part of the
piece. Rest is part of this work in the same way. Its observable form: end a
session by writing the next step down and then stopping, instead of taking
that step tired. What you could not see at night is often plain in the
morning, and the note is what lets you put it down.

**Patience with yourself is the same method.** [JOURNEY.md](JOURNEY.md) asks
you to forgive yourself "mechanically, until it is character". Mechanically
means: an error becomes an entry, not a verdict. Being kind to yourself does
not mean lowering the gate. It means keeping the gate where it is and
declining to punish yourself for not being through it yet.

## 自問自答（じもんじとう） — the questions a teacher would have asked

*Jimon-jitō* (自問自答（じもんじとう）) is to put a question to yourself and answer it.

Much of what a good teacher gives is a question at the right moment. Alone,
you carry the list yourself. Keep it short enough that you will really ask.

**Before:** What do I expect to happen? What would show me that I am wrong?
What is the smallest version of this that I could finish today?

**While it resists:** What did I change last? What have I assumed and not
looked at? Am I still working on the question I began with?

**After:** What surprised me? What do I now believe that I did not check? What
would I tell someone who starts on this tomorrow?

**Once a week:** Which entry in my record came back in a new form? What am I
avoiding?

A mistake that comes back three times in different clothes is no longer an
accident. It is a form, and it wants a name;
[PATTERN_LANGUAGE.md](PATTERN_LANGUAGE.md) is about that step.

Write the answers down. A question left open in writing is honest. A question
skipped is not.

## Moving on, and going back

Take the *Ready to move on when* lines of [JOURNEY.md](JOURNEY.md) and treat
them as checks that need evidence. For each one, point at something: an entry
in your record, a file, a tally of predictions. "I feel ready" is not
evidence. "I do not feel ready" is not evidence either, and for some learners
it is the more frequent error.

The Nō actor and teacher Zeami (世阿弥（ぜあみ）) left a sentence that is usually
quoted as a call to remember one's early enthusiasm: 初心忘るべからず, do not
forget the beginner's mind (初心（しょしん）). In his treatise 花鏡（かきょう） he
means something more exact, and names three. Keep the memory of your
unskilled work, because it is the measure of everything after. Know that at
each new stage you are a beginner again, at that stage. And expect to be one
once more in old age. For a learner alone this settles two worries. The
record with its embarrassing first entries is not something to outgrow and
delete; it is the measure. And feeling like a beginner after moving on is
not a sign that you moved too early. It is what a new stage is.

If you cannot decide, do not decide. Run a small trial of the next stage and
see what your record says after two weeks. Going back a stage is a move on
the same road, not a defeat.

Some things cannot be learned alone, and it is better to know which. Another
mind reading your code, and your working inside code that someone else wrote,
are the two that matter most. This tree offers stand-ins:
[READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md) for the second, and the five
readings of [CODE_REVIEW.md](CODE_REVIEW.md), turned on your own work a week
after you wrote it, for the first. They are stand-ins. When a person is within
reach, one prepared question to that person is worth an evening of asking a
model.

## One week, as a shape

This is not a plan. It shows how little the practice costs.

- **Every session:** one written prediction before the first run; the last
  ten minutes in the teacher's seat.
- **Every failure that cost more than you expected:** an entry, the same day.
- **Once a week:** read the week's entries; answer the weekly questions;
  rebuild one small thing from an empty file.
- **Once a season:** read the record from its beginning.

## The honest limits of this page

- **It has not been tried on learners.** The protocol that would test pages
  like this one exists and has not been run
  ([LEARNER_STUDY.md](LEARNER_STUDY.md)). What stands here comes from this
  project's own record of its mistakes and from what crafts have long asked
  of apprentices. Take it as a guess about you, and keep the parts your own
  record confirms.
- **It is not written for hardship.** If study is making you ill, isolated or
  afraid, what you need is a person, not a page.
- **Reading it changes nothing.** [APPROACH.md](APPROACH.md) says of its own
  practices that a habit is not acquired by agreement. The whole of this page
  is habits.
- **The Japanese words are held to this project's own rule for them**
  ([PHILOSOPHY.md](PHILOSOPHY.md)): each stands next to something you can be
  caught not doing, and where one does not, it should go. What is said of
  Zeami's three beginner's minds was checked on 2026-11-25 against modern
  commentaries, not against the text of 花鏡（かきょう）.
- **It was drafted with the help of a model.** That is one more reason to
  hold it to its own tests and not to its tone.

---

## Read further

- [JOURNEY.md](JOURNEY.md): the road this page walks beside, and the record it
  relies on.
- [APPROACH.md](APPROACH.md): the same honesty, asked of the work
  (sections 3 and 5).
- [ARGUMENT.md](ARGUMENT.md): `/predict`, and the names of the moves.
- [GROUNDED_DISCOURSE.md](GROUNDED_DISCOURSE.md): talking with a model without
  being flattered by it.
- [ANECDOTES.md](ANECDOTES.md) 64, 66 and 68: the project's own record of the
  failures this page describes (primary).
- *Teach Yourself Programming in Ten Years*, Peter Norvig (secondary): the
  long clock. The entry is in [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md), section 1.
- [PHILOSOPHY.md](PHILOSOPHY.md): why the project is built as it is.
- [PATTERN_LANGUAGE.md](PATTERN_LANGUAGE.md): what to do when a lesson repeats.
