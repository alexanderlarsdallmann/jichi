#!/bin/sh
# smoke lint: every jichi `--flag` the documentation mentions must exist
# (M158; port of tests/e2e/docs_flags.py, M210 -- python-free).
#
# The valid set is every "--..." string literal in the three programs'
# sources -- jichi's (src/main.c), jichi-convert's
# (src/convert/jc_convert_main.c) and jichi-harness's (src/harness, M1053:
# the harness is the third program, and a doc for it may name its flags) --
# where the parsed flags AND help-text mentions live as literals, so a doc
# can never reference a flag the programs don't know. Scanned: docs/,
# examples/, completions/, README.md, man/jichi.1. Excluded: proposals/,
# plans/, analysis/, ROADMAP.md, DEFERRED_LOCAL_GPU.md (their subject is
# designs/history, including flags that do not exist) -- and DECISIONS.md
# (M326i), for the same reason made structural: a decision register records the
# alternatives that were REJECTED, so naming a flag that does not exist is what
# the page is FOR. M326h had to reword three such mentions to get past this
# lint, which is the wrong trade: the register should read naturally and the
# lint should know why. The cost is stated rather than hidden -- a genuine flag
# typo in DECISIONS.md is now unlinted, and that page is prose about choices
# rather than instructions a reader would copy. docs/internal/reviews/ is excluded
# for the analysis/ reason (2026-09-29): a reviewer's report quotes other tools'
# commands verbatim as the record of what it checked, not as instructions.
# docs/internal/drive/ is excluded for the same reason (2026-10-20, the
# hardening drive's CI follower): the drive LOGs record git, valgrind and
# foreign commands as they were RUN (the 01:00 repair's `--soft`/`--autostash`/
# `--onto`, valgrind's `--suppressions`), and a per-flag allowlist for a
# run log that gains a new command every milestone is the cat-and-mouse this
# exclusion exists to stop. Docs also show
# OTHER tools' flags -- allowlisted in the FOREIGN table below; add one
# line when documenting a new foreign flag.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

# --- the valid set: string literals in the sources -------------------------
grep -o '"--[a-z][a-z0-9-]*' \
    "$SMOKE_ROOT/src/main.c" "$SMOKE_ROOT/src/convert/jc_convert_main.c" \
    "$SMOKE_ROOT/src/harness/jc_harness_main.c" \
    "$SMOKE_ROOT/src/harness/jc_harness_doctor.c" \
    "$SMOKE_ROOT/src/harness/jc_harness_init.c" \
    2>/dev/null | sed 's/^[^"]*"//' | sort -u > "$tmp/valid"
nvalid=$(grep -c . "$tmp/valid")
# The floor was 40 (M158, main.c + the converter: 158 flags). src/harness
# joined the universe on 2026-10-24 (M1053, batch H2): the harness is a third
# program and docs/HARNESS.md names its flags, so its literals are part of what
# the docs may name. Count floored at 164 then -- the 6 new flags (--bare,
# --branch, --jichi-bin, --no-local, --orphan, --short); the harness's other
# literals (--config, --help, --output, --version and friends) were already in
# the 158. The floor itself stays 40 (it guards the extraction, not the count).
if [ "$nvalid" -ge 40 ]; then
    t_ok "extracted $nvalid valid flags from the sources"
else
    t_fail "suspiciously few flags extracted ($nvalid) -- source layout moved?"
fi

# --- foreign tools' flags the docs legitimately show ------------------------
cat > "$tmp/foreign" <<'EOF'
# zig build / its test runner, quoted verbatim in ANECDOTES.md #42 -- `--listen=-`
# is the load-bearing detail of that entry (the mode under which any stderr output
# fails the step), so the quoted command line cannot be reworded away.
--cache-dir
--seed
--listen
--maxrss
--data
# Godot 4, quoted verbatim in GAME_DEVELOPMENT.md. `--path` selects the project
# directory so the command runs from anywhere, and `--check-only` is the static
# parse gate that page recommends as the cheap first check -- both are load-bearing
# in commands a learner copies, so neither can be reworded away.
--path
--check-only
# git, in the clone lines that fetch the Godot documentation snapshot.
--depth
# git, in the drive log's WIP ledger (2026-10-17): the quoted `git diff --name-only`
# command is the ledger's extraction form -- the file list without the content, which
# is what a "what is unverified, and where" ledger records. Quoted verbatim there.
--name-only
# jichi's own --progress, in the drive log's describe_fields_lint record
# (LOG.md): the flag name is that record's subject -- the batch-progress
# heartbeat's event -- so it cannot be reworded away.
--progress
# clang++, in CPP_BUILD.md's M867 section: Ubuntu's clang 18 selects GCC 14, whose
# libstdc++ may not be installed; pointing it at the GCC that has one is the
# load-bearing workaround a reader copies, so it cannot be reworded away.
--gcc-install-dir
--data-binary
--cflags
--libs
--no-auto-compile
--eval
--test
--edition
# Other CLI coding agents', from COMPARED.md's landscape section (M648). That
# section compares jichi against surveyed tools, so it necessarily names THEIR
# command surfaces: octomind's `--format jsonl`/`--schema`/`--daemon`, hax's
# documented `--json` stream, tmuxlet's `--output-format json`. Rewording them
# away would mean describing a comparison without the thing being compared.
--json
--schema
--daemon
--output-format
# charlm's, the slm-lab bench's own program (examples/slm-lab/charlm.c, M764a).
# Its knobs are the lab's subject: the skills explain them, the commands use them,
# and a learner types them. They are charlm's flags, not jichi's. M764's gate was
# red on exactly these eight, found by this driver, which the pre-gate sweep had
# not run because its name does not end in _lint.
--ctx
--emb
--hidden
--steps
--batch
--lr
--every
--sample
# rustup's, from BIBLIOGRAPHY.md's Rust section (M636c): `rustup doc --book` is
# how a reader puts The Rust Programming Language on a bench with no route out,
# which is the load-bearing half of that entry for this project's offline-first
# audience -- so the command cannot be reworded away either.
--book
--background
--python
--headless
--quit
--export-release
--fail
--max-time
--show-error
--silent
--body
--label
--mode
--only
--trials
--url
--needed
--exclude-dir
--include
# GNU grep's, quoted verbatim in the D-36/M1110 record row
# (findings-index.md): the guard stats the path instead of relying on
# grep's --devices=skip, which does not cover the top-level-path case.
--devices
--branch
--cached
--hard
--is-inside-work-tree
--abbrev-ref
--oneline
--prune
--short
--staged
--stat
# the M2 plan (the 2026-10-16 plan) names jichi's own snapshot diff form
# `git diff --unified=0` (jc_snapshot_worktree_diff, jc_snapshot.c): git's
# flag, not jichi's.
--unified
--no-psqlrc
--error-exitcode
--errors-for-leak-kinds
--leak-check
# valgrind's suppression-file option, in the hardening drive's LOG.md M989
# record (run 0016, 6c5bc8c9): the verbatim command that reproduced the
# ECHILD unit red under valgrind. The .supp path is the load-bearing detail --
# the same command without it would run with a different leak policy, which is
# not what the record says happened -- so it cannot be reworded away.
--suppressions
--tool
--fixture-bytes
--fails-per-call
--history-bytes
--args-bytes
--turns
--rss-ceiling-kb
--sample-every
--variant
--gc-sections
--release
--now
--arg
# tests/tools/mockmodel, invoked by docs/reading/traces/capture.sh -- the scripted
# model behind the Tsuiseki trace chapters (M508). These are the mock's flags, not
# jichi's, and the script that names them is executable rather than prose: this
# tier's own tool, documented where a reader can run it.
--script
--capture
--port-file
--max-requests
# M882 gave the mock --keep-alive; docs/TESTING_RUNBOOK.md §3c names it (M883).
--keep-alive
--junitxml
--stdio
--summary
--backends-path
--localai-config-dir
--models-path
--preview
--git-dir
# `git status --porcelain`, quoted verbatim in ANECDOTES.md #100 -- the stray
# file named `=` is only visible in that output, so the command cannot be
# reworded without losing the evidence the entry turns on.
--porcelain
# git, in the internal handoff's N30 (2026-09-29): M816's resume applies a kept
# state with `git cherry-pick --no-commit <ref>`, and M803 preserves one with
# `git commit --allow-empty`. The fault is that pair -- one commit's diff picked
# from a ref whose parent is the child's own last commit -- so the note names both.
--no-commit
--allow-empty
# git, in docs/RULES.md (M884): the walk's root is the directory
# `git rev-parse --show-toplevel` names, found without running git.
--show-toplevel
# git, in docs/internal/scripts/pre-push-ff-only.sh (M916, F-J1): the draft
# master-protection hook tests a fast-forward with
# `git merge-base --is-ancestor` and tells the operator the remedy is
# `git push --ff-only` -- both git's flags, named because they ARE the fix.
--is-ancestor
--ff-only
--work-tree
--port
--gpus
--install
--prefix
--pdf
--pptx
# scripts/make-snapshot.sh, quoted in ANECDOTES.md #76 -- the first hosted CI
# run failed in its --commit path, and the entry quotes the failing lint line
# verbatim (the zig rule above: the quoted command line cannot be reworded
# away). The flag belongs to the release script, not to the jichi binary.
--commit
--allow-remote
--root
--token
--log-dir
--server
--ramp
--retries
--jichi
--instances
--requests
--prompt
--out
--parallel
--batch-size
--loglevel
# The M430 footprint harnesses, documented in LOW_MEMORY.md's "Verifying your
# footprint" and its tier sections: tests/measure/ram_floor.sh --workload,
# scripts/tier-v-tiny.sh --bin/--turn, scripts/minimal-curl.sh --musl/--tls.
# These are measurement scripts, not jichi -- the page has to show the exact
# invocation for the numbers to be reproducible, which is what this table is for.
--workload
--turn
--bin
--musl
--tls
# scripts/tier-v-arch.sh --drive and scripts/minimal-curl.sh --target: the
# architecture rows became Driven on 2026-09-21, and the recipe has to be
# reproducible for that verdict to mean anything. --build is autoconf's, named
# because binfmt_misc makes autoconf believe a cross build is native and that one
# flag is the whole fix -- a reader who hits the wedged conftest needs the word.
--drive
--target
--build
# scripts/tier-b-device.sh --cc, documented in LOW_MEMORY.md's Guix section
# (M458): Guix ships neither cc nor c99, so a device row there needs CC=gcc and
# the page must show how. Not a jichi flag.
--cc
# `guix shell --pure`, in install/guix.md's build steps (M779). Guix is non-FHS,
# so the toolchain comes from a `guix shell`, and --pure isolates it fully from
# the user's profile -- worth showing a self-learner whose profile leaks a stray
# library into the build. Guix's flag, not jichi's.
--pure
# scripts/tier-v-openbsd.sh --reuse, named in PLATFORMS.md's OpenBSD section
# (M464) as how cheaply that row can be re-run to confirm the lost-first-send
# explanation. A claim that a verification is cheap should say what to type.
--reuse
# The rigs' --ref-secs (REQUIRED: this bench's `make WERROR=1` seconds, the
# denominator of JC_SMOKE_TIMEOUT_MULT) and --dirty (ship the WORKING tree
# instead of `git archive HEAD`), documented in BUILD.md and SESSION_RUNBOOK.md
# §5 (M466). Both are rig flags, not jichi flags. They are documented because
# without --dirty the loop "find a portability defect on the target, fix it,
# verify the fix there" cannot be run at all, and a runbook that omits it sends
# the next session to commit a fix untested.
--ref-secs
--dirty
# The rigs' --live-port / --live-model (M667), named in PLATFORMS.md's FreeBSD
# row as the command that reproduces a DRIVEN row. They are rig flags, not jichi
# flags. Documented because a row that claims the agent loop ran on a kernel has
# to say how to re-run it -- "driven" without a reproducible command is the kind
# of claim the Driven verdict exists to stop.
--live-port
--live-model
# The device rig's --live-key-env (M957), named in PLATFORMS.md's VisionFive 2
# row and the m955 handoff recipe as the flag that drives a board against a
# KEYED endpoint (the HRZ jlu gateway) instead of a keyless loopback model
# server. A rig flag, not a jichi flag: it names the host env var whose value
# the rig pipes (over stdin) into the board's isolated .home as the .jichi.env
# the live config's apiKeyEnv resolves. Documented because a Driven row that
# says "over the gateway" must say how the key got to the board without
# landing in a ps or a log.
--live-key-env
# tests/tools/ptydrive.c --presend (M672), named in DEFERRED.md where the
# preprompt_discard diagnosis is recorded. A test-tool flag, not jichi's. It is
# documented because the finding is only reproducible if a reader knows which
# option carries it -- "a fixture race" without the mechanism is a story rather
# than a diagnosis.
--presend
# podman/docker --rm, documented in PLATFORM_TESTING.md's rung 1 (M682). A
# container runtime's flag, not jichi's. The page must show the exact command
# because the rung's whole value is that it costs minutes -- and a reader who
# omits --rm accumulates a stopped container per attempt, which is how a
# cheap rung acquires a reputation for being messy.
--rm
# scripts/ansi-to-png.py --cols/--title, documented in ILLUSTRATION.md (M680).
# Not jichi flags: that script renders a CAPTURED terminal transcript to a PNG.
# The page must show the exact invocation because the whole point of the capture
# path is that a reader can reproduce the image -- an image nobody can reproduce
# is decoration, and a recipe missing its width flag produces a different one.
--cols
--title
# local-ai run --address, documented in ILLUSTRATION.md's setup (M680). LocalAI's
# own flag, not jichi's. Documented because the address is the difference between
# a loopback-only model server and one exposed on the LAN, and this project binds
# loopback on purpose everywhere else.
--address
# tests/bench/craft_ab/craft_ab.py --pairs, documented in CRAFT_AB_TUTORIAL.md
# (M665). The harness's own flag, not jichi's. The tutorial has to show the
# exact invocation because `--pairs` is the knob that decides what a session
# COSTS -- roughly 600k input tokens per pair -- and a page that tells a learner
# to run an experiment without showing how to run a small one first is how a
# 5-6M-token session gets started by accident.
--pairs
# man-db's `man --warnings`, named in PLATFORMS.md's FreeBSD section (M665) as
# the option that does NOT exist there: FreeBSD's man is mandoc-based and answers
# "Illegal option --", which is why man_page_lint now selects a renderer. Naming
# the flag is the point of the sentence -- a reader who hits the same wall needs
# to know which option was the problem. Not a jichi flag.
--warnings
# M478, JUPYTERHUB.md: the Jupyter/pip/apt toolchain a hub operator is told to
# run. `--to` and `--execute` are nbconvert's, `--set-formats` is jupytext's,
# `--python-version` is pip's (it builds the cp311 wheelhouse for a Debian 12
# guest -- a wheelhouse is interpreter-specific), and `--no-install-recommends`
# is apt-get's, inside the single-user image Dockerfile.
--set-formats
--to
--execute
--python-version
--no-install-recommends
# ...and one more of OUR OWN rig flags, for the same reason as --ref-secs above:
# the valid set is main.c's literals, i.e. the BINARY's flags, and scripts/ has
# its own. scripts/jhub-verify.sh --negative-control is named in JUPYTERHUB.md
# because a reader must run it before trusting a green result -- four versions
# of that probe were green and wrong, and the control is what caught them.
--negative-control
# scripts/make-snapshot.sh and tar, in the public-advance handoff under
# docs/internal/ (M716). `--rev`/`--dest` name the commit and a destination
# OUTSIDE the repository, which the script insists on; tar's `--exclude=./.git`
# is what stops a tested snapshot's own .git from being copied over the public
# checkout's -- load-bearing in commands a publisher copies.
--rev
--dest
--exclude
# ...and a measurement script's: tests/measure/success_repeats.py --per-turn (M731)
# writes the per-turn rows D1's threshold is fitted on; the 2026-09-24 coordination
# handoff under docs/internal/ quotes it as the command the other machine runs for
# D1's cross-check, so it cannot be reworded away.
--per-turn
# scripts/tier-v-arch.sh's `--arch TRIPLE` (one row of the architecture rig), which
# PLATFORMS.md's Driven table cites with the exact command that drove each musl-static
# row (M736), so a reader can re-run the row it records.
--arch
# ...and scripts/tier-v-freemint.sh's `--step N` (1 boot, 2 the unit suite, 4 the driven
# task), cited on the FreeMiNT rows with the command that produced each (M737).
--step
# ...and tests/measure/scanner_corpus.py's `--against OLD_BIN` (M743): CONSTRAINTS.md and
# the DEFERRED row give it as the whole procedure for measuring a scanner change.
--against
# podman's, in the contained-drive prototype under docs/internal/ (2026-09-25). Each
# flag is one boundary the prototype measured -- the network mode, the uid mapping,
# the privilege and resource limits -- so the flags are the design and cannot be
# reworded away.
--network
--name
--userns
--security-opt
--cap-drop
--memory
--pids-limit
# podman's --init (run an init as PID 1 to reap zombies), which M821 / N9 names in
# CONTAINER_TUTORIAL.md section 8 precisely to warn that it makes PID 1, not jichi,
# hold an -e key -- so the flag is the lesson and cannot be reworded away.
--init
# ...and pasta's, the rootless network that CONTAINER_TUTORIAL.md teaches: the page
# quotes the arguments podman passes (read from podman's debug log) and explains the
# two loopback mappings, so the options are the lesson and cannot be reworded away.
--config-net
--dns-forward
--no-map-gw
--netns
--map-guest-addr
--map-host-loopback
# ...and git's, in that page's Containerfile: `git config --system --add
# safe.directory /work` is what lets git inside the image work on a mounted tree.
--system
--add
# ...and git's fetch options, in the demos-drive handoff under docs/internal/ (N11): the
# command fetches a container-written workspace through git-upload-pack served from a
# container, so that the host runs no git in it. The command is the fix, so its options
# cannot be reworded away.
--no-tags
--upload-pack
# ...and git's, tar's and podman's, in the hardening drive's harness under docs/internal/drive/
# 2026-10-17-hardening/ (2026-10-17): setup-dir.sh clones the team's tree --no-local, makes
# the mirror --bare, starts the records branch --orphan and unpacks the harness from the plan
# branch with tar --strip-components; the push size check walks `git rev-list --objects ... --not
# --remotes` into `git cat-file --batch-check`. The commands are the harness, so their options cannot
# be reworded away. podman's --cpus and --signal bound and stop the team's container; git's
# --no-checkout clones without running a checkout's filters, and --trailer writes the agent block.
--no-local
--bare
--orphan
--strip-components
--not
--remotes
--objects
--batch-check
--cpus
--no-checkout
--signal
--trailer
# python's `http.server --bind 127.0.0.1`, in the Haiku rig provisioning page under
# docs/internal/ (2026-09-28): the host serves the ssh pubkey on loopback so the
# guest can pull it (VNC has no paste). --bind is load-bearing -- without it the
# server listens on all interfaces, which this project binds loopback everywhere on
# purpose. python's flag, not jichi's.
--bind
# the GNU screen crash reproducer's own options, in its note under docs/internal/
# (2026-09-29): `--screen` names the screen binary to test, which is the point of the
# script (another machine runs it against its own build), and `--runs` the repeat count.
# The reproducer's flags, not jichi's.
--screen
--runs
# the scaffolding-review handoff under docs/internal/ (2026-09-29): ctest's
# `--no-tests=error` (a gate must fail when no test is registered) and TypeScript 7's
# `tsc --lsp --stdio` (its own language server) are the fixes it hands over.
--no-tests
--lsp
# ...and Godot's, in the scaffolding decisions page under docs/internal/ (2026-09-29):
# `--editor` with `--lsp-port` is option 6d, a headless editor serving GDScript's
# language server. Godot's flags, not jichi's.
--editor
--lsp-port
# the merge handoffs under docs/internal/ (2026-09-29) test a merge before doing it
# with `git merge-tree --write-tree`, which computes the result without touching the
# working tree or the index. git's flag, not jichi's.
--write-tree
# the parallel_stash_kept driver names the product's worktree idiom,
# `git worktree add --detach <path> <commit>` (jc_snapshot_worktree_add,
# jc_snapshot.c:929) -- git's flag, not jichi's.
--detach
# the branch-cleanup table under docs/internal/ (2026-10-15) publishes the
# deletion sweep: `git push origin --delete <ref>` removes a remote branch and
# `git rev-list --count` measures how far ahead it is -- git's flags, not
# jichi's.
--delete
--count
# the M955 next-session handoff (2026-10-15) publishes the merge form for the
# other agent's docs branch: `git merge --squash <branch>` replays the delta
# without a merge commit -- git's flag, not jichi's.
--squash
# the 32-bit glibc findings (2026-10-15) quote the i386 container command
# verbatim: `podman run --platform linux/386 docker.io/i386/debian:...`.
# podman's flag, not jichi's.
--platform
# the qwen-code/opencode/Continue lessons (2026-10-15) document the git-hooks
# environment class with qwen's own neutralising command, `git
# --no-ext-diff --no-textconv ...`: both disable git calling EXTERNAL programs
# (diff drivers, textconv filters) with the environment in hand. git's flags,
# not jichi's.
--no-ext-diff
--no-textconv
# the LicheeRV Nano's manual under docs/internal/ (2026-10-15) quotes its own
# provisioning commands verbatim: `blockdev --flushbufs` makes the verify `cmp`
# read the card, not the cache; `journalctl --no-pager` keeps the USB link-up
# log readable on a serial console; iptables' `-m conntrack --ctstate
# ESTABLISHED,RELATED` is the NAT back-path of the USB subnet; and `pkg-config
# --modversion libcurl` reports the board's libcurl for the run's record.
# Foreign tools' flags, not jichi's.
--flushbufs
--no-pager
--ctstate
--modversion
# the branch-review findings note under docs/internal/ (2026-09-30) records that
# `wsl.exe --cd <dir>` is in WSL's own usage text, for the desktop design's Windows path.
# WSL's flag, not jichi's.
--cd
# FORMATTING.md (M847) shows `prettier --write {}` as a formatCommand whose `{}` sits
# OUTSIDE quotes -- an external formatter's flag, not jichi's.
--write
# curl's, in the demos-drive reply's §18 (docs/internal, 2026-10-02): a probe that
# times a TLS handshake out with --connect-timeout is the measurement itself, and
# the drive is asked to run it verbatim, so the command cannot be reworded away.
--connect-timeout
# git's `git remote get-url --push`, quoted in the M1056 records (README.md,
# docs/HARNESS.md): the URL guard's effective-push-URL half is that command's
# output, so the flag names the guard itself. git's flag, not jichi's.
--push
# the drive's gateway-up.sh, quoted in the M1057 deferred row def-357
# (docs/DEFERRED.md): the gateway-park probe's option, an external script's
# flag, not jichi's. The probe is H5's loop concern, deferred.
--wait
EOF

# jichi flags DESIGNED but not built (HARDENING/SELF_IMPROVEMENT/
# AUTONOMOUS_LOOPS discuss them); move an entry out when it ships.
cat > "$tmp/future" <<'EOF'
--loop
--fix
--resume-run
EOF

# --- the scan targets --------------------------------------------------------
{
    for f in README.md man/jichi.1; do
        [ -f "$SMOKE_ROOT/$f" ] && printf '%s\n' "$SMOKE_ROOT/$f"
    done
    find "$SMOKE_ROOT/docs" "$SMOKE_ROOT/examples" \
         "$SMOKE_ROOT/completions" \
        \( -path "$SMOKE_ROOT/docs/proposals" \
           -o -path "$SMOKE_ROOT/docs/plans" \
           -o -path "$SMOKE_ROOT/docs/analysis" \
           -o -path "$SMOKE_ROOT/docs/internal/reviews" \
           -o -path "$SMOKE_ROOT/docs/internal/drive" \
           -o -path "$SMOKE_ROOT/docs/internal/2026-10-08-review-and-relay" \) -prune -o \
        -type f \( -name '*.md' -o -name '*.sh' -o -name '*.json' \
                   -o -name '*.service' -o -name '*.example' \
                   -o -name '*.bash' -o -name '*.zsh' -o -name '*.1' \) \
        -print
} | grep -v -e '/docs/ROADMAP\.md$' -e '/docs/DEFERRED_LOCAL_GPU\.md$' \
     -e '/docs/DECISIONS\.md$' \
  > "$tmp/targets"

# --- token scan (awk): emulate the Python lookbehind -------------------------
awk -v validf="$tmp/valid" -v foreignf="$tmp/foreign" \
    -v futuref="$tmp/future" '
BEGIN {
    while ((getline l < validf) > 0) if (l != "") V[l] = 1
    while ((getline l < foreignf) > 0) if (l != "") V[l] = 1
    while ((getline l < futuref) > 0) if (l != "") V[l] = 1
}
{
    line = $0
    if (FILENAME ~ /\.1$/)
        gsub(/\\-/, "-", line)      # groff escaping in the man page
    s = line
    off = 0
    while (match(s, /--[a-z][a-z0-9-]*/)) {
        tok = substr(s, RSTART, RLENGTH)
        pre = (RSTART > 1) ? substr(s, RSTART - 1, 1) : ""
        s = substr(s, RSTART + RLENGTH)
        # skip when preceded by [A-Za-z0-9_.<>/\\$-] (mid-word, a path, a
        # <placeholder>, $VAR, or a longer ---)
        if (pre ~ /[A-Za-z0-9_.<>\/\\$-]/) continue
        if (tok in V) continue
        if (tok ~ /^--(disable|enable|with|without)-/) continue
        # a trailing hyphen is a family reference in prose ("the
        # --budget-* flags" tokenizes as "--budget-"): accept when a real
        # flag has that prefix
        if (tok ~ /-$/) {
            fam = 0
            for (v in V) if (index(v, tok) == 1) { fam = 1; break }
            if (fam) continue
        }
        print FILENAME ":" FNR ": unknown flag " tok
    }
}' $(cat "$tmp/targets") < /dev/null > "$tmp/problems"

if [ ! -s "$tmp/problems" ]; then
    t_ok "all documented --flags exist ($(grep -c . "$tmp/targets") files scanned)"
else
    t_fail "unknown documented flag(s): see below"
    sed 's/^/# /' "$tmp/problems" | head -40
fi

# --- reverse direction: --help vs the man page ------------------------------
# Every flag `--help` advertises must appear in man/jichi.1 OR be a conscious
# omission listed in the TERSE table below (the man page is deliberately
# terse; the long-form companions live under docs/). When adding a help flag,
# either give it a man entry or add one line here -- the lint forces the
# decision either way. (The forward scan above cannot catch an omission:
# it only proves documented flags exist, not that existing flags are
# documented -- which is how --type-ahead and --accessible went missing.)
sed -n '/^static void print_help/,/^}/p' "$SMOKE_ROOT/src/main.c" \
    | grep -o -- '--[a-z][a-z0-9-]*[a-z0-9]' | sort -u > "$tmp/helpflags"
sed 's/\\-/-/g' "$SMOKE_ROOT/man/jichi.1" \
    | grep -o -- '--[a-z][a-z0-9-]*[a-z0-9]' | sort -u > "$tmp/manflags"

# help flags deferred to the long-form docs; move a line out when it gains
# a man entry.
cat > "$tmp/terse" <<'EOF'
--advisor
--budget-panel
--lease
--agent
--attempt
--auto-context
--bell
--cache-audit
--config-json
--config-json-b64
--config-stdin
--cost-model
--no-cost-model
--with-rules
--connect
--context-limit
--design
--from-global
--idle-dream
--image
--import
--inherit
--kinetic-commands
--learn-on-stop
--lite
--live
--log
--log-level
--low-memory
--max-reads
--no-auto-context
--no-fuzzy-edit
--no-hooks
--no-learn-on-stop
--no-lite
--no-path-fence
--no-prompt-cache
--no-route-on-context
--no-route-on-stall
--notify
--onboard
--output-style
--parallel-verify
--path-fence
--preset
--privileged-commands
--prompt-b64
--prompt-cache
--reference-root
--resume
--revert-out-of-scope
--route-on-context
--route-on-stall
--run-timeout
--socket
--spec
--timeout-connect
--timeout-request
--timeout-stall
--tool-profile
--verify-every
--workspace
EOF

sort -u "$tmp/manflags" "$tmp/terse" > "$tmp/covered"
comm -23 "$tmp/helpflags" "$tmp/covered" > "$tmp/undocumented"

if [ ! -s "$tmp/undocumented" ]; then
    t_ok "every --help flag is in man/jichi.1 or consciously deferred"
else
    t_fail "help flag(s) missing from man/jichi.1 (add an entry or a TERSE line)"
    sed 's/^/# /' "$tmp/undocumented" | head -20
fi

# --- 4 (M370): the coverage direction -- every flag main.c PARSES must be
# documented somewhere a user reads. Checks 1-3 keep the docs from lying;
# they cannot see a knob that ships undiscoverable, and three --no-* inverses
# had (--no-learn-on-stop, --no-strict-green, --no-voice -- the off-switch of
# a safety-relevant gate among them). The M305 config-keys twin. The search
# set is ALL of docs/ + README + the man page -- lenient on purpose, since
# the failure being prevented is "documented NOWHERE"; the parsed set is the
# strcmp() dispatch, not help-text literals, so the floor is the real flag
# count.
grep -ohE 'strcmp\(a, "--[a-z-]+"' "$SMOKE_ROOT/src/main.c" \
        "$SMOKE_ROOT/src/convert/jc_convert_main.c" 2>/dev/null \
    | grep -oE '"--[a-z-]+"' | tr -d '"' | sort -u > "$tmp/parsed"
np=$(grep -c . "$tmp/parsed")
undoc=""
# One corpus, built once. See smoke_md_corpus: the --include= form this used to
# carry made BSD grep error on every iteration, and the `!` turned each error
# into "undocumented" -- 146 of 153 flags, on docs that were fine.
smoke_md_corpus "$tmp/corpus" "$SMOKE_ROOT/docs"
cat "$SMOKE_ROOT/README.md" "$SMOKE_ROOT/man/jichi.1" >> "$tmp/corpus" 2>/dev/null
while IFS= read -r f; do
    grep -q -e "$f" "$tmp/corpus" 2>/dev/null || undoc="$undoc $f"
done < "$tmp/parsed"
if [ "$np" -ge 130 ] && [ -z "$undoc" ]; then
    t_ok "all $np parsed flags are documented somewhere a user reads"
else
    t_fail "parsed flags documented nowhere ($np parsed):$undoc"
fi

# --- 5 (M382): the man page HEADER. `.TH JICHI_CONTINUE` survived the M170
# rename for ~200 milestones on the first rendered line of `man jichi`,
# because every man check read the body (flags above, describe's contract
# subcommands) and none read the title macro.
if head -1 "$SMOKE_ROOT/man/jichi.1" | grep -q '^\.TH JICHI 1 '; then
    t_ok "the man page title macro is JICHI (no rename fossil)"
else
    t_fail "man title: $(head -1 "$SMOKE_ROOT/man/jichi.1")"
fi

t_done
