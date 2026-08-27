#!/bin/sh
# smoke lint: every jichi `--flag` the documentation mentions must exist
# (M158; port of tests/e2e/docs_flags.py, M210 -- python-free).
#
# The valid set is every "--..." string literal in src/main.c + the
# converter (parsed flags AND help-text mentions live there as literals),
# so a doc can never reference a flag main.c doesn't know. Scanned: docs/,
# examples/, completions/, README.md, man/jichi.1. Excluded: proposals/,
# plans/, analysis/, ROADMAP.md, DEFERRED_LOCAL_GPU.md (their subject is
# designs/history, including flags that do not exist) -- and DECISIONS.md
# (M326i), for the same reason made structural: a decision register records the
# alternatives that were REJECTED, so naming a flag that does not exist is what
# the page is FOR. M326h had to reword three such mentions to get past this
# lint, which is the wrong trade: the register should read naturally and the
# lint should know why. The cost is stated rather than hidden -- a genuine flag
# typo in DECISIONS.md is now unlinted, and that page is prose about choices
# rather than instructions a reader would copy. Docs also show
# OTHER tools' flags -- allowlisted in the FOREIGN table below; add one
# line when documenting a new foreign flag.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

# --- the valid set: string literals in the sources -------------------------
grep -o '"--[a-z][a-z0-9-]*' \
    "$SMOKE_ROOT/src/main.c" "$SMOKE_ROOT/src/convert/jc_convert_main.c" \
    2>/dev/null | sed 's/^[^"]*"//' | sort -u > "$tmp/valid"
nvalid=$(grep -c . "$tmp/valid")
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
--data-binary
--cflags
--libs
--no-auto-compile
--eval
--test
--edition
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
--no-psqlrc
--error-exitcode
--errors-for-leak-kinds
--leak-check
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
--junitxml
--stdio
--summary
--backends-path
--localai-config-dir
--models-path
--preview
--git-dir
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
# scripts/tier-b-device.sh --cc, documented in LOW_MEMORY.md's Guix section
# (M458): Guix ships neither cc nor c99, so a device row there needs CC=gcc and
# the page must show how. Not a jichi flag.
--cc
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
           -o -path "$SMOKE_ROOT/docs/analysis" \) -prune -o \
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
}' $(cat "$tmp/targets") > "$tmp/problems"

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
