#!/bin/sh
# smoke lint: a documentation table row fits on one line of a wide screen.
#
# THE RULE (docs-structure plan 2026-09-25, DOC_REVIEW.md "Writing so the review
# passes"). A table is for looking a value up, not for reading a paragraph
# sideways. A row over 200 characters is a story wearing a table's clothes; the
# story belongs under a heading, and the row links to it. The operator set the
# limit at 200 on 2026-09-25.
#
# HOW IT HOLDS THE LINE WITHOUT A FLAT DAY OF REWRITING. There are 929 rows over
# the limit today across 60 top-level pages, most of them in the two registers
# (the two registers), which S4 of the plan rewrites to one line
# per entry. So the lint carries a PER-PAGE FLOOR, each page's exact count on
# 2026-09-25 (post-M771). The floor is a CEILING that only falls: a page may not
# gain an over-long row, a page absent from the list may have none, and a page
# revised to zero must be REMOVED from the list. As S2-S4 shorten pages, their
# numbers come down; when the list is empty the limit is absolute.
#
# UNIVERSE: top-level docs/*.md only (the pages a reader browses), the same scope
# as milestone_currency_lint. docs/plans/, docs/analysis/ and the other subdirs
# are working/archival and out of scope. A "row" is a line beginning with `|`.
#
# NOT A PROSE LINT. It measures one mechanical thing -- a line's length -- which
# the argument DOC_REVIEW.md 4 allows: prose is not lintable, a character count
# is. Perturbed per check: 1 fires on a synthetic 201-char row, 2 on an over-long
# row added to any page, 3 on a floor entry left for a page that no longer needs
# it.
. "$(dirname "$0")/_smoke.sh"
t_plan 3
root=$SMOKE_ROOT
LIMIT=200

# Per-page floor: exact count of table rows over the limit, 2026-09-25 (post-M771).
# A page not listed has floor 0. Lower a number when you shorten a page; delete a
# line when a page reaches 0. DECISIONS.md and DEFERRED.md are the registers S4
# rewrites; their floors fall then.
# M817: PLATFORMS.md 46 -> 47 for the new macOS row -- a newly VERIFIED platform is
# a legitimate matrix row, a dense-fact peer of the Hurd/illumos/Cygwin rows already
# floored here (its per-platform history lives under the M400 finding + the driven
# matrix, not in the row). This is the one sanctioned way the floor rises: a real
# new platform, not a story creeping into an existing row.
# 2026-10-15: PLATFORMS.md 45 -> 51 for the three physical-board rows (VisionFive 2
# v1.3B, LicheeRV Nano, Raspberry Pi 500+) and their three Driven-register
# peers -- the campaign's first hardware rows, dense-fact.
FLOORS=$(cat <<'EOF'
ACCESSIBILITY.md 2
ACP.md 1
AGENT_MODES.md 1
AGENTS_GUIDE.md 1
ANECDOTES.md 4
ARGUMENT.md 2
AUTONOMOUS_LOOPS.md 3
AUTONOMY.md 5
BIBLIOGRAPHY.md 1
BUILD.md 14
CHOOSING_A_MODEL.md 7
CODE_REVIEW.md 5
COMPACTION.md 2
COMPARED.md 5
CONFIG_TUTORIAL.md 1
CONSTRAINTS.md 1
CONTAINER_TUTORIAL.md 1
CONTROL.md 2
CURRICULUM.md 5
DECISIONS.md 9
DEFERRED.md 3
DEPLOYMENT.md 3
DISTRIBUTED.md 7
DOC_REVIEW.md 1
DOCTOR.md 18
DRIVE_LOG.md 3
GATE_INTEGRITY.md 5
GROUNDED_DISCOURSE.md 5
HARDENING.md 2
INSTALL.md 5
JUPYTERHUB.md 7
LEARNER_STUDY.md 1
LOW_MEMORY.md 21
MCP.md 6
MEDIA_GEN.md 1
NOTICES.md 3
OBSERVABILITY.md 15
PLATFORM_RETEST.md 15
PLATFORMS.md 51
PORTING_WINDOWS.md 4
PROJECT_TIMELINE.md 10
README.md 1
REFERENCES.md 7
ROADMAP.md 29
ROBOTICS_BRINGLIST.md 5
ROUTING.md 4
SCAFFOLDING.md 7
SESSION_RUNBOOK.md 1
SETUP_WIZARD.md 5
SQLITE.md 1
STATE.md 4
SUBAGENTS.md 3
TEACHING.md 3
TELEMETRY.md 10
TESTING.md 1
TEST_INTEGRITY.md 10
TOOL_DECISIONS.md 2
TUI_RENDER.md 1
VERIFY_A_PLATFORM.md 4
WORKFLOWS.md 4
EOF
)

# count of table rows over $LIMIT characters in one file.
count_over() {
    awk -v lim="$LIMIT" 'length($0) > lim && /^\|/ { n++ } END { print n + 0 }' "$1"
}
# the recorded floor for a basename (0 if unlisted).
floor_of() {
    printf '%s\n' "$FLOORS" | awk -v p="$1" '$1 == p { print $2; f = 1 } END { if (!f) print 0 }'
}

# --- 1: the instrument fires -- a 201-char row is caught, a 199-char one is not --
st=$(smoke_tmp)
long=$(awk 'BEGIN { s = "|"; while (length(s) < 201) s = s "x"; print s }')   # 201 chars
short=$(awk 'BEGIN { s = "|"; while (length(s) < 199) s = s "y"; print s }')  # 199 chars
printf '%s\n%s\n| ok |\n' "$long" "$short" > "$st/probe.md"
got=$(count_over "$st/probe.md")
if [ "$got" = 1 ]; then
    t_ok "the counter catches a row over $LIMIT and not one under it (self-test)"
else
    t_fail "the length counter is broken: a file with one 201-char row and one \
199-char row counted $got over-limit rows, not 1 -- checks 2-3 would be vacuous"
fi

# --- 2: no top-level docs page carries more over-long rows than its floor --------
over=""
for f in "$root"/docs/*.md; do
    b=$(basename "$f")
    c=$(count_over "$f")
    fl=$(floor_of "$b")
    if [ "$c" -gt "$fl" ]; then
        over="$over $b(has $c, floor $fl)"
    fi
done
if [ -z "$over" ]; then
    t_ok "no docs/*.md page has more than its floor of rows over $LIMIT characters"
else
    t_fail "a table row over $LIMIT characters was added; put its story under a \
heading and link to it, or -- only if the page is being shortened -- lower its \
floor in this lint:$over"
fi

# --- 3: the floor is honest -- every listed page exists and still needs its line -
# A floor entry for a page with zero over-long rows is stale: the page has been
# fixed and its line must be deleted, or the limit is not really enforced there.
stale=""
printf '%s\n' "$FLOORS" | while IFS=' ' read -r p n; do
    [ -n "$p" ] || continue
    if [ ! -f "$root/docs/$p" ]; then
        echo "$p(gone)" >> "$st/stale"
    elif [ "$(count_over "$root/docs/$p")" -eq 0 ]; then
        echo "$p(now 0)" >> "$st/stale"
    fi
done
[ -f "$st/stale" ] && stale=$(tr '\n' ' ' < "$st/stale")
if [ -z "$stale" ]; then
    t_ok "every floored page exists and still carries at least one over-long row"
else
    t_fail "a floor entry is stale -- delete it, the page no longer needs it:$stale"
fi

t_done
