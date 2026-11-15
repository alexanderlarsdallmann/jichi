# Tazuna 7 -- `notify` and `watch`: the verdict's messenger and the gate's clock

*[手綱（たづな）*Tazuna* — the harness](TAZUNA.md) · chapter 7 of 10*

## Why this exists

A drive's lead says a batch of work is finished by writing one line in the
drive's LOG, `BATCH 7 READY at 9ada7cd7`. From that line on, two things must
happen with nobody at the keyboard. The commit must pass the **gate**
(chapter 8) before it may reach `master`. Then the verdict must reach its two
readers: the lead, who has to act on it, and the operator, who has to know.

Before these commands existed, both jobs belonged to whichever session
happened to be awake. `docs/HARNESS.md` names the cost. On 2026-11-04 a green
gate waited 16.5 hours because no session ran. On another day a withdrawal and
a RED went to a digest file nobody read, and a GREEN was read from a log the
watcher had already closed. That day's four false verdicts are known by the
minute: 07:55, 11:32, 13:32 and 13:47 (`docs/HARNESS.md`, the `watch` section).
This chapter meets all four.

**`notify`** delivers one verdict to its readers at once; it decides nothing.
**`watch gate`** is one *tick* of the gate watcher: it reads the last READY line
and starts a gate, waits, or stays silent. **`watch units`** prints the timer
that runs a tick every ten minutes.

Both commands are ports of host shell scripts (`gate-note.sh`,
`gate-watch.sh`), and the ports have not replaced the scripts yet. The end of
the chapter explains why that matters: until M1178 (2026-11-13) the watcher
carried a defect that would have started a second gate beside a running one.

## The shape

```mermaid
flowchart LR
    T["timer, every 10 min"] --> W["watch gate: one tick"]
    W -->|"wait / silent: exit 0"| T
    W -->|"start jhp-gate-SHA8"| G["gate run (chapter 8)"]
    G -->|"each verdict"| N["notify: note, inject, digest, hook"]
```

One rule governs every arrow back to the timer: **a tick that waits is a
success.** It exits 0, and the next tick is the retry. Nothing sleeps inside a
tick: the timer's period *is* the polling loop.

## `notify`: four channels, no opinions

Open `src/harness/jc_harness_notify.c:jc_harness_notify`. It takes a config,
an optional scratch inbox, a SHA8, a TEXT and an optional ATTACH file. The
caller (chapter 8's `gate run`) has already chosen the verdict word, so a new
verdict word costs nothing here. The body first refuses what it cannot deliver
whole. Then it delivers on four channels.

- **Refusal comes before any write.** A missing SHA8 or TEXT is refused with
  exit 2. A TEXT or ATTACH too large for the note buffer is refused with exit 1,
  never truncated. In the code's words, "a silently truncated verdict is a wrong
  verdict". These refusals leave nothing on disk.
- **The note, for the lead:** `<inbox>/<stamp>-gate-<SHA8>.md`. It is written by
  `src/harness/jc_harness_notify.c:notify_write_atomic` as `<path>.tmp` and then
  `rename()`d into place. A POSIX rename replaces the target in one step, so a
  run assembling its prompt never reads half a note. Chapter 4's `run` moves
  the note into the next run's records, but only once that run's first model
  call has succeeded.
- **The digest line, for the operator:** one line, `<now> gate <SHA8>: <TEXT>`,
  appended to `<gateHome>/digest.md`.
- **The inject, for a run that is live right now.** The note waits for the
  *next* run. A run that is already going should hear the verdict at its next
  model call, so `notify` also injects the note into it. "Live" is three tests,
  chained with `&&`:

```c src/harness/jc_harness_notify.c:jc_harness_notify
    if (newest >= 0 && !scratch) {
...
            stat(sock_path, &st) == 0 && S_ISSOCK(st.st_mode) &&
...
            !notify_meta_ended(meta_path)) {
            live = 1;
        }
    }
```

  The newest four-digit run directory, found as a *numeric* maximum by
  `src/harness/jc_harness_notify.c:notify_newest_run`, must have a
  `ctl/lead.sock` that really is a socket (`S_ISSOCK`; a plain file of that
  name is not). Its `meta.txt` must have no `end=` line
  (`src/harness/jc_harness_notify.c:notify_meta_ended`). And the delivery must
  not be a rehearsal. When all three hold, `notify` runs the drive's *pinned*
  `bin/jichi control <sock> inject <body>`, with stdin at `/dev/null`.
- **The operator hook.** `host.notifyCommand` names a command, which receives
  **the digest path** as its last argument. The reference script hard-coded a
  desktop notification here. With the hook, mail or chat is a one-line script,
  and the harness holds no extra secret.

The inject's rc and the hook's rc never change `notify`'s rc. Once the note and
the digest line are written, the verdict has been delivered, and `notify`
returns 0. `--inbox <dir>` is the rehearsal form. Its note body starts with
`REHEARSAL (scratch inbox, nothing is delivered to the lead) -- `, and the inject
is skipped, so a rehearsal cannot pass for a verdict a live run must act on.

## `watch gate`: one tick, in the reference's order

`src/harness/jc_hp_watchgate.c:jc_hp_watchgate_run` is one long function, in
the reference script's order; read it alongside the eight steps below.

Besides `git` and the operator's hook, a tick calls four host programs:
`systemctl --user is-active`, `systemctl --user list-units`, `systemd-run` and
`pgrep`. Each of the four is a *shim*. Its command
comes from a flag first, then from the config (`pgrep` has no config field),
and only then from the reference's argv. That chain lets the smoke driver and
this chapter's exercises tick without systemd. Each command is copied into the
arena before it is split, because `strtok_r` writes NULs into the buffer it
splits. `src/harness/jc_hp_watchgate.c:wgate_split_cmd` then copies each token
into the arena too, so the four argument lists survive until the end of the
tick, where they are used.

**1. The last READY line.** `src/harness/jc_hp_watchgate.c:wgate_find_last_ready`
keeps the *last* line that `src/harness/jc_hp_watchgate.c:wgate_parse_ready_line`
accepts. That is optional leading `*`s (markdown bold), then `BATCH `, one
token, ` READY at `, and 7 to 40 lowercase hex. A LOG without such a line is
the normal quiet tick: exit 0, no output. With no `--log`, the LOG is
`<dir>/LOG.md`. Before M1156 the generated unit, which passes no `--log`,
refused every tick.

**2. Wait for the remote.** The commit must resolve in the host's `push.git`
and be an ancestor of `refs/remotes/origin/<branchPrefix>/main`. If it is not,
the push timer (chapter 6) has not carried it through the key scan yet.

**3. The state word.** The state file is append-only. Each event adds one
`<sha8> <word> <time>` line, from `src/harness/jc_hp_watchgate.c:wgate_verdict`
here or from chapter 8's gate, and nothing ever truncates the file.
`src/harness/jc_hp_watchgate.c:wgate_state_word` returns the **last** word for
this SHA8:

| Last word | The tick |
|---|---|
| none | goes on |
| `started`, unit active | exits 0 silently: a gate is running |
| `started`, unit not active | says "died without a verdict", re-gates |
| `regate` | says "gating it again", re-gates |
| anything else (`landed`, `red-ci`, `on-master`, ...) | exits 0 **silently**: the verdict stands |

`regate` is the operator's hand on the reins. Every verdict on file is final
unless a person appends `<sha8> regate <time>`. That was the 07:55 incident's
settled rule, and the withdrawal arm below writes this exact hint into the
digest.

**4. The withdrawal rule.** A lead sometimes takes a READY back, in prose, and
the program has to read that prose. The 11:32 and 13:32 false verdicts were two
wrong readings. At 11:32 one line named the still-READY commit *and* said that
an earlier commit "is WITHDRAWN". At 13:32 one sentence said that two earlier
commits, one marked WITHDRAWN, would not be gated, and named the READY commit
as the one to gate. The rule settled at 13:47 is a pure function,
`src/harness/jc_hp_watchgate.c:jc_gate_withdrawn_sentence`. A **sentence**
withdraws the READY commit only if all three of these hold:

- it contains `WITHDRAWN`, matched case-sensitively;
- it names the READY commit (a token whose first seven hex digits match);
- it names **no other** commit. A *commit token* is a run of 7 to 40 lowercase
  hex, bounded by non-alphanumerics, containing at least one letter `a`-`f`, so
  the date `20261105` is not one.

`src/harness/jc_hp_watchgate.c:jc_gate_withdrawn_n` cuts a line into sentences
on the reference awk's `[.!?]+[ \t]+`. The rule closes with its own argument:

```c src/harness/jc_hp_watchgate.c:jc_gate_withdrawn_sentence
    /* An ambiguous sentence does not withdraw: the cost of a missed
     * withdrawal is one gate run the lead can still replace; the cost of a
     * false one is a drive that waits, silently, for a verdict that never
     * comes. */
    return (self && !other) ? 1 : 0;
```

This is the chapter's most transferable idea. When a classifier has to guess,
first decide **which error you can afford**, then make the ambiguous case land
on that side. `tests/test_watchgate.c` pins sixteen cases at the boundary,
among them the 11:32 and 13:32 shapes; the sixteenth (M1178) checks that a line
is read only to its own end. A genuine withdrawal writes the state
word `withdrawn`, a digest line ending in the regate hint, and a call to the
hook.

**5. The master checks.** A commit already on `origin/master` gets `on-master`.
A commit that is not a fast-forward of master gets `not-ff`, and the hook
receives the reference's RED text: rebase, then write a new READY line. Look at
who hears that RED. The port gives the text to `host.notifyCommand` and to
nothing else. A `not-ff` tick run here with a recording hook wrote no inbox note
and no digest line. The same hook receives a *text* from this arm and a *path* from
`notify`. A `push.git` without `origin/master` makes the tick exit 1: a service
that cannot name master is broken, not waiting.

**6. Never two gates at once.** The gate's `make ci` stage takes about 48
minutes (the `jc_hp_gate.c` header), and two gates running side by side would
compete for one machine. So the tick lists the active gate units of both
drives' prefixes, `jhd-` and `jhp-` (decision DD5), and waits while one runs.
`src/harness/jc_hp_watchgate.c:wgate_is_busy_unit` decides which names count,
with the pattern `^j(hd|hp)-(gate|ci)-[0-9a-f]{7,40}\.service$`. The hex run
keeps out the watcher's *own* `jhp-gate-watch.service`. Counting that unit once
made every READY wait forever (the 01:58 incident). A comment records a second
scar: the first cut compared the kind over a fixed four characters, so `ci`
never matched. **Hold on to this step.** The worked defect (fixed in M1178) was
not in this function. It was in the line this function received.

**7. The team smoke.** A `pgrep -f tests/smoke/` process whose `/proc/<pid>/cwd`
is the team directory, or a directory under it, is a smoke run in the team's
tree. The tick waits for it to finish.

**8. The start.** The tick writes `started` **before** it runs the unit-run
command with the unit name `jhp-gate-<sha8>`, a description and
`<host.gateRun> <sha8> <batch>`, so a start that dies still leaves `started`
for step 3 to re-gate. With no `host.gateRun` the start is refused by name,
exit 1. This step has a recorded defect: `docs/DEFERRED.md` def-381 measured
that the default command's argument shape is not `systemd-run` syntax (real
`systemd-run` answers "Failed to find executable"; the driver's stand-in cannot
show it). def-381 is open at this tree.

### What `--dry` holds back, and what it does not

`--dry` makes a tick that *decides* but does not *act*: it skips the
withdrawal's state word, digest line and hook, the `not-ff` word and RED, and
the start, whose line becomes `DRY: would start ...`. It does **not** write
nothing. The `on-master` word is written even when dry, the gate home is
created, and say lines still reach `--out`. Read a dry mode's
effects from the code, not from its name.

## `watch units`: the timer, generated

`src/harness/jc_hp_watchunits.c:jc_hp_watchunits_run` prints two units and
installs neither: a oneshot service whose `ExecStart` is the harness's own path
(from `/proc/self/exe`) plus `watch gate --config <cfg>`, and the timer from
`src/harness/jc_hp_watchunits.c:build_watch_timer`. `OnCalendar=` comes from
`host.gateOnCalendar`; without it the command refuses, exit 2, rather than pick a
default that could make two drives' watchers share a minute. `Persistent=true`
runs a tick missed while the machine was off at the next boot.

## Try it

Everything runs in a throwaway directory; `true` or a one-line script stands in
for each host command, so neither systemd nor a network is needed. Fixed dates
make your commit ids match these. Your timestamps will not.

**1. A fixture: master at commit A, the team branch at commit B.**

```sh
# in the jichi checkout, after `make jichi-harness`
sh                                  # a throwaway shell: `exit` drops the exports
J=$PWD/jichi-harness
cd "$(mktemp -d)"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_AUTHOR_DATE=2026-11-13T00:00:00Z GIT_COMMITTER_DATE=2026-11-13T00:00:00Z
git init -q -b main w
git -C w -c user.name=t -c user.email=t@example.com commit -q --allow-empty -m A
git -C w -c user.name=t -c user.email=t@example.com commit -q --allow-empty -m B
git clone -q --bare w push.git
git -C push.git update-ref refs/remotes/origin/master "$(git -C w rev-parse HEAD~1)"
git -C push.git update-ref refs/remotes/origin/drive/x/main "$(git -C w rev-parse HEAD)"
B=$(git -C w rev-parse --short=8 HEAD); echo "$B"
```

```
30f4f6e4
```

**2. A config, a READY line, one dry tick.** `--unit-list true` and
`--pgrep true` print nothing: no gate runs, no smoke runs.

```sh
# in the throwaway directory from step 1, the same shell
cat > cfg.json <<EOF
{"dir":"$PWD/dir","drive":{"branchPrefix":"drive/x"},
 "host":{"pushRepo":"$PWD/push.git","gateHome":"$PWD/gate","gateRun":"/bin/true",
         "gateOnCalendar":"*-*-* *:07/10:00"}}
EOF
printf 'BATCH 1 READY at %s\n' "$B" > LOG.md
W="$J watch gate --config cfg.json --log LOG.md --state state --dry --pgrep true"
$W --unit-list true < /dev/null; echo "rc=$?"
```

```
DRY: would start jhp-gate-30f4f6e4 (gate-run.sh 30f4f6e4) for BATCH 1 (LOG.md:1)
rc=0
```

The `DRY:` line names `gate-run.sh`, the reference's script, whatever
`host.gateRun` says: the format string carries that name literally.

**3. The worked defect, in two lines.** Two stand-ins for `systemctl
list-units` print the *same* busy unit. The first prints it flush-left, as the
smoke driver's fixture did until M1178. The second prints it after two spaces,
as systemd does (step 6 shows the real bytes):

```sh
# in the throwaway directory from step 1, the same shell
printf '#!/bin/sh\necho "jhp-gate-0123abcd.service loaded active running x"\n' > flush.sh
printf '#!/bin/sh\necho "  jhp-gate-0123abcd.service loaded active running x"\n' > indent.sh
chmod +x flush.sh indent.sh
$W --unit-list ./flush.sh < /dev/null
$W --unit-list ./indent.sh < /dev/null
```

```
BATCH 1 READY at 30f4f6e4: waiting, a gate runs (jhp-gate-0123abcd.service )
BATCH 1 READY at 30f4f6e4: waiting, a gate runs (jhp-gate-0123abcd.service )
```

Both ticks wait. Before M1178 the second one printed
`DRY: would start jhp-gate-...` instead: the tick saw the flush-left gate and
missed the indented one, and without `--dry` it would have started a gate
beside the running one. "What went wrong here" below tells the story. Note the
space before `)`: every unit name is followed by one space, the last one too,
because that is what the reference script printed, and the shadow compares the
two byte for byte.

**4. The withdrawal rule, then the state word.**

```sh
# in the throwaway directory from step 1, the same shell
printf 'The earlier 1234abc is WITHDRAWN; %s stays READY.\n' "$B" >> LOG.md
$W --unit-list true < /dev/null
printf '%s is WITHDRAWN per the note 20261113-0900.\n' "$B" >> LOG.md
$W --unit-list true < /dev/null
printf 'BATCH 2 READY at %s\n' "$B" >> LOG.md
printf '%s landed 2026-11-13 09:00:00 +0000\n' "$B" > state
$W --unit-list true < /dev/null; echo "rc=$? (silence)"
printf '%s regate 2026-11-13 09:05:00 +0000\n' "$B" >> state
$W --unit-list true < /dev/null
```

```
DRY: would start jhp-gate-30f4f6e4 (gate-run.sh 30f4f6e4) for BATCH 1 (LOG.md:1)
BATCH 1 READY at 30f4f6e4 (LOG.md:1) is WITHDRAWN by a later line: not gated
rc=0 (silence)
BATCH 2 READY at 30f4f6e4: gating it again (gate/state says regate: the earlier verdict was withdrawn)
DRY: would start jhp-gate-30f4f6e4 (gate-run.sh 30f4f6e4) for BATCH 2 (LOG.md:4)
```

`1234abc` is a second commit token, so the first sentence withdraws nothing;
`20261113` has no letter, so the second one withdraws. After a new READY line,
`landed` keeps the tick silent and `regate` re-opens the commit.

**5. `notify` in its rehearsal form, and the generated units.**

```sh
# in the throwaway directory from step 1, the same shell
$J notify --config cfg.json --inbox scratch "$B" "GREEN (a test verdict)" < /dev/null
cat scratch/*-gate-"$B".md
cat gate/digest.md
$J watch units --config "$PWD/cfg.json" < /dev/null | grep -E '^(ExecStart|OnCalendar|Persistent)='
exit
```

```
2026-11-13 13:37:42 +0100 gate 30f4f6e4: note 20261113-1337-gate-30f4f6e4.md; no live run
# Host gate: 30f4f6e4

REHEARSAL (scratch inbox, nothing is delivered to the lead) -- Host gate (automatic, 2026-11-13 13:37:42 +0100): GREEN (a test verdict)

Records on the host: /tmp/tmp.Jr9pJ4xfYB/gate/30f4f6e4/ (the supervisor reads them; you need not).
2026-11-13 13:37:42 +0100 gate 30f4f6e4: REHEARSAL: GREEN (a test verdict)
ExecStart=.../jichi-harness watch gate --config /tmp/tmp.Jr9pJ4xfYB/cfg.json
OnCalendar=*-*-* *:07/10:00
Persistent=true
```

`ExecStart` copies `--config` as given, and systemd does not start the unit in
your directory: hence the absolute path.

**6. (Optional, a systemd machine, read-only.)** See the two spaces:

```sh
# anywhere, on a systemd machine (read-only)
systemctl --user list-units --no-legend --state=active 'dbus.service' < /dev/null | od -c | sed -n 1,2p
```

```
0000000           d   b   u   s   .   s   e   r   v   i   c   e       l
0000020   o   a   d   e   d       a   c   t   i   v   e       r   u   n
```

Measured here on systemd 255, a healthy unit's line starts with two blanks,
an empty status column. A failed unit's line starts with `●` (the bytes
`342 227 217`) and one blank instead; that was measured with a transient unit
that ran `/bin/false`, then cleared with `systemctl --user reset-failed`.

## What went wrong here, and what it teaches

### The worked defect: a filter blind to two leading spaces

This defect is fixed in M1178. The story is told as it happened, because the
way it hid is the lesson.

Step 6 of the tick reads the unit list line by line. Before M1178 it took each
line's first token and passed it to `wgate_is_busy_unit`. The loop read, before
the fix (this block is history, not today's code):

```c
            while (*p != '\0') {
                char unit[WGATE_PATH_CAP];
                size_t tl = 0;
                while (*p != '\0' && *p != ' ' && *p != '\t' &&
                       *p != '\n' && tl < sizeof unit - 1) {
                    unit[tl++] = *p;
                    p++;
                }
                unit[tl] = '\0';
                if (wgate_is_busy_unit(unit)) {
```

The inner loop copies characters *up to* the first blank. It never skips the
blanks *before* a token. The default list command is `systemctl --user
list-units --state=active,activating --no-legend ...`, and every line it prints
starts with two blanks (Try it, step 6). So on every line the inner loop stopped
at the first byte, `tl` stayed 0, and `unit` was the empty string, which is
never busy. A watcher running this loop **saw no running gate, ever**. A READY that
arrived during a 48-minute `make ci` would have got a second gate beside the
first, which is exactly what the busy filter and DD5 exist to prevent.

The reference script did not have this bug. It piped the list through
`awk '{print $1}'`, and awk skips leading blanks before it counts fields. The
port rewrote `$1` by hand and lost the one behaviour that awk had supplied for
free.

Why did the tests stay green? Look at what the smoke driver feeds its unit-list
stand-in, one name per line:

```sh tests/smoke/harness_gate_watch.sh
    printf '%s\n' "$2" > "$tmp/units"
...
_busy_check a "jhp-gate-watch.service
jhp-gate-$m5_8.service" 1 "jhp-gate-$m5_8.service" || _15_err="$_15_err a"
```

Before M1178 the stand-in printed those names as they were: flush-left. The
busy-filter cell's five cases, a to e, were red first and green afterwards, and
not one of them showed the parser a line shaped the way systemd shapes it. The fixture behaved like its author's idea of
the peer, not like the peer. `CLAUDE.md` has a rule for this, "make a fixture
behave like the real peer", and here it was broken in the one place it
mattered. The defect was found on 2026-11-13 by replaying a state through the
watcher's shadow, a method chapter 10 tells.

**The fix (M1178), in three parts.** First the fixture: the stand-in now
prints each name the way systemd does, two blanks, the unit, then the LOAD,
ACTIVE, SUB and DESCRIPTION columns, so cell 15 sees the real shape and went
red on the unfixed loop:

```sh tests/smoke/harness_gate_watch.sh
printf '#!/bin/sh\nsed -e "s/^/  /" -e "s/\\$/ loaded active running stub unit/" "%s/units" 2>/dev/null\n' \
    "$tmp" > "$UHOME/unit-list.sh"
```

Then the loop skips the blanks before it reads the unit, as awk's `$1` does,
and also a `●` with its blank, which systemd puts in that column for a failed
unit (Try it, step 6):

```c src/harness/jc_hp_watchgate.c:jc_hp_watchgate_run
                /* The unit is the line's first FIELD, not its first
                 * space-delimited token (M1178): `systemctl list-units
...
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
```

The third part was found by checking the fix the way the defect was found. The
supervisor replayed the 07:37 moment with the fixed binary, on the real LOG and
state. It waited, correctly, but its say line ended `.service)` where the
script's ended `.service )`: the script joins names with `tr '\n' ' '`, which
leaves a space after every name. The shadow compares say lines byte for byte,
so the first real busy event would have read as a disagreement. The port now
prints the script's bytes, and cell 15 checks the whole line. Each of the two
code changes, taken out alone, turns cell 15 red.

### Two more, measured while writing this chapter, and fixed in M1178

Both were found while this chapter was being written, by reading the code, and
both were confirmed by running it on 2026-11-13. Both are fixed in M1178, with
the cells of `tests/smoke/harness_gate_watch.sh` named below, each seen red on
the unfixed binary.

- **The withdrawal check read past its own line.** The tick passed
  `jc_gate_withdrawn` a pointer to a line *inside* the LOG buffer, and the
  function `strlen`ed it: it saw the rest of the file, where the reference's awk
  saw one line. A newline is not a sentence boundary, so lines ran together.
  The first draft of this chapter measured the cheap side: a withdrawal line
  with no `. ` after it, followed by `unrelated: 1234abc built fine`, was not
  withdrawn, and with 64 KiB or more of LOG after a withdrawal the `tl >= 65536`
  guard returned 0 (cell 24). Both are missed withdrawals. The fix's own test
  found the expensive side as well: a line saying only `the earlier plan is
  WITHDRAWN`, followed by a line naming the READY commit, ran together into one
  sentence that withdrew it when nobody had (cell 23). So the errors fell on
  both sides, and the rule's "an ambiguous sentence does not withdraw" could not
  protect against it, because the ambiguity was manufactured by the reader. The
  fix passes each line's own length:

```c src/harness/jc_hp_watchgate.c:jc_hp_watchgate_run
                if (jc_gate_withdrawn_n(p, ll, c7)) {
```

- **A LOG of 1 MiB or more read as no LOG.**
  `src/harness/jc_hp_watchgate.c:wgate_read_file` returned NULL at or above
  `WGATE_LOG_CAP`, which the tick treats as the quiet "no LOG file" case. A
  1,072,526-byte LOG ending in a READY line gave rc 0 and no output; a
  975,026-byte one reached the `DRY:` line. This error is the expensive,
  16.5-hour kind: never gated, and nothing says so. The drive's live LOG had
  reached 474,487 bytes, and a LOG only grows. The reader has no size cap now
  (cell 25 reads a 1,388,920-byte LOG).

The whole story, with the other two M1178 defects, is in
[`docs/analysis/2026-11-13-five-harness-port-defects.md`](../analysis/2026-11-13-five-harness-port-defects.md).

One lesson, three times: a port faithful line by line can still lose what the
original got from its tools (awk's blank skipping, awk's one line per `$0`,
grep's lack of a size limit). Rereading finds none of these; replaying real
inputs does.

## Check yourself

1. `notify` returns 0 even when the inject fails and the hook exits 1. Why is
   that the right rc, and which two writes have to succeed first?
2. A later sentence reads `abcd1234 is WITHDRAWN, see 20261105 and 9999999a`.
   Is READY `abcd1234` withdrawn? Which token decides it?
3. A dry tick sees a READY that is already on master. Which file changes?
4. The busy-filter cases went red first and still missed the defect. What did
   their red prove, and what did it not prove?

<details><summary>Answers</summary>

1. The verdict is delivered once the note and the digest line are on disk
   (either failing gives a non-zero rc). The inject and the hook are extra
   channels, logged and never fatal: a broken webhook cannot un-deliver it.
2. Not withdrawn. `20261105` has no letter `a`-`f`, so it is no token. But
   `9999999a` is 8 hex with a letter, which makes it a second commit. The
   sentence is ambiguous, and an ambiguous sentence does not withdraw.
3. The state file gains `<sha8> on-master <time>`. `--dry` returns early only
   in the withdrawal, `not-ff` and start arms.
4. The red proved that each case *could* fail, and it did, on a tree with no
   `watch` verb at all. It did not prove that the fixture's input looked like
   real input: until M1178, none of the cases ever saw systemd's two leading
   blanks.

</details>

*Next: [chapter 8 — `gate run`](tazuna-08-gate-run.md), the command the
started unit runs.*
