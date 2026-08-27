#!/bin/sh
# smoke lint: nothing in the publishable tree identifies a person or a machine,
# and nothing in it is a build artifact (M484).
#
# WHAT THIS EXISTS FOR. docs/plans/2026-08-public-snapshot.md section 2 lists six
# gates before the first public commit, and gate 3 -- "no secret has ever been in
# the tree ... confirm with a scan of the tree to be published" -- is a MANUAL
# AUDIT. This project's doctrine (docs/TEST_INTEGRITY.md) is prefer a lint to an
# audit, "because the audit found what it knew to look for", and the audit that
# produced this lint is the argument: it found three compiled binaries, six copies
# of the author's email address, nine absolute paths naming two real accounts,
# four ssh logins naming reachable machines, and a physical device's adb serial --
# none of which the plan predicted, in a plan written to predict exactly this.
#
# IT LINTS THE ARTIFACT, NOT A PROXY FOR IT. The scan runs against the output of
# scripts/make-snapshot.sh, so the lint and the producer cannot disagree about
# what ships. That matters more than it sounds: the selection rule is `git archive
# HEAD`, i.e. the git INDEX is the manifest, so a file's fate is decided by
# whether it is tracked and by nothing else. A lint reading `git ls-files` would
# be testing its own restatement of that rule instead of the rule.
#
# THE RULES ARE POSITIVE, WHICH IS WHY THIS FILE IS SAFE TO PUBLISH. A lint that
# banned the author's address by naming it would put the address into the tree it
# is protecting -- self-defeating, and the same shape as a password policy stored
# in the clear. So every content check is an ALLOWLIST: an email address must be
# an example or no-reply address, a /home/ path must name a placeholder account.
# Anything else fails, and the failure names the finding without this file having
# to know it in advance.
#
# EVERY MATCHER SELF-TESTS FIRST (checks 4-8 each plant a positive), because a
# clean result from a broken matcher is this tier's signature failure -- M479's
# lint reported "6 ok / 0 failures" on a platform where the tree did not build,
# and M482's fault drivers passed while injecting nothing.
# THE PLANTS ARE ASSEMBLED, NOT WRITTEN OUT, and that is not decoration. This
# file is IN the tree it scans, so a literal bad address, real-looking account or
# device serial sitting here as test data would be found by the very check it is
# testing -- measured: checks 5-8 all failed on their own plants the first time
# this ran against the working tree. Each printf below therefore splits the
# offending literal across a format string and its argument: the runtime string
# is exactly what the matcher must catch, and no substring of THIS file matches.
. "$(dirname "$0")/_smoke.sh"

SNAP_SH="$SMOKE_ROOT/scripts/make-snapshot.sh"

# The shipped tree is NOT a repository (M451 unpacked `git archive` into a plain
# directory and found four checks silently missing because of it). This lint's
# subject is the development repo's index, so outside one it has nothing to say
# -- and says so, rather than passing.
if ! git -C "$SMOKE_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    t_skip "not a git repository -- the snapshot's manifest is the index"
fi

t_plan 15

if [ ! -f "$SNAP_SH" ]; then
    t_fail "scripts/make-snapshot.sh is missing -- the snapshot has no producer"
    t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"
    t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"
    t_done
fi

TMP=$(smoke_tmp)

# --- 1: the producer refuses a destination inside the repository -------------
# pin-driver.sh refuses the same way. A snapshot built inside the tree it copies
# is reachable by the next `git add -A`, and a tree that can be committed back
# into the development repository is a mistake waiting for a hurried afternoon.
if sh "$SNAP_SH" --dest "$SMOKE_ROOT/snap-should-refuse" --dirty \
        >"$TMP/refuse.out" 2>&1; then
    t_fail "make-snapshot accepted a destination inside the repository"
    rm -rf "$SMOKE_ROOT/snap-should-refuse"
elif grep -q 'refusing a destination inside the repository' "$TMP/refuse.out"; then
    t_ok "make-snapshot refuses a destination inside the repository"
else
    t_fail "make-snapshot failed for the wrong reason: $(head_bytes 200 "$TMP/refuse.out")"
fi

# --- 2: --commit refuses while there is no LICENCE ---------------------------
# The plan's gate 1: "No public commit before this, because the first commit's
# contents are what people acquire rights to." Mechanical, so it cannot be
# forgotten in the excitement of the licence answer arriving. When a LICENSE does
# land this check flips to asserting that the commit was made.
# The rehearsal supplies GIT_AUTHOR_NAME/EMAIL: the first hosted CI run
# (2026-08-27, GitHub Actions run 33093305185) failed exactly here, because the
# M619 identity refusal had been validated only on machines that already had a
# configured git identity -- a hosted runner's checkout has none. The env pair
# is the producer's documented fallback; repository config still wins when set,
# and check 2c below proves the refusal still fires when BOTH sources are dark.
TREE="$TMP/tree"
if GIT_AUTHOR_NAME='snapshot rehearsal' GIT_AUTHOR_EMAIL='rehearsal@example.org' \
   sh "$SNAP_SH" --dest "$TREE" --dirty --commit >"$TMP/commit.out" 2>&1; then
    if [ -f "$SMOKE_ROOT/LICENSE" ]; then
        t_ok "make-snapshot --commit ran, and a LICENSE exists to justify it"
    else
        t_fail "make-snapshot --commit created a repository with no LICENSE in the tree"
    fi
elif grep -q 'refusing --commit with no LICENSE' "$TMP/commit.out"; then
    t_ok "make-snapshot refuses --commit while no LICENSE exists"
else
    t_fail "make-snapshot --commit failed for the wrong reason: $(head_bytes 200 "$TMP/commit.out")"
fi

# --- 2b: the rehearsal commit's author is the identity the producer RESOLVED --
# (repository config first, the env pair from check 2 as fallback) -- never the
# machine's ambient auto-detected one. Expected is computed here the same way
# the producer computes it, because a check must read every input exactly as
# the executor will (CLAUDE.md, M530). Two-state like check 2: while no LICENSE
# exists there is no commit to attribute and the refusal is the assertion.
if [ -f "$SMOKE_ROOT/LICENSE" ]; then
    _en=$(git -C "$SMOKE_ROOT" config user.name 2>/dev/null)
    _ee=$(git -C "$SMOKE_ROOT" config user.email 2>/dev/null)
    [ -n "$_en" ] || _en='snapshot rehearsal'
    [ -n "$_ee" ] || _ee='rehearsal@example.org'
    _got=$(git -C "$TREE" log -1 --format='%an <%ae>' 2>/dev/null)
    if [ "$_got" = "$_en <$_ee>" ]; then
        t_ok "the public commit's author is the deliberate identity ($_got)"
    else
        t_fail "public commit author is '$_got', expected '$_en <$_ee>'"
    fi
else
    t_ok "no public commit exists to attribute (licence still undecided)"
fi

# --- 2c: with NO identity anywhere, --commit still refuses --------------------
# The env fallback must not have widened into auto-detection. A local clone has
# no repo-local user.name/email, HOME/XDG point at an empty dir and
# GIT_CONFIG_NOSYSTEM hides /etc/gitconfig, so every config source is dark; the
# env pair is passed EMPTY. The WORKING TREE's producer is copied over the
# clone's, so this checks the code being committed, not the previous commit.
git clone -q "$SMOKE_ROOT" "$TMP/clone" 2>/dev/null
if [ -d "$TMP/clone/.git" ]; then
    mkdir -p "$TMP/nohome"
    cp "$SNAP_SH" "$TMP/clone/scripts/make-snapshot.sh"
    if HOME="$TMP/nohome" XDG_CONFIG_HOME="$TMP/nohome" GIT_CONFIG_NOSYSTEM=1 \
       GIT_AUTHOR_NAME= GIT_AUTHOR_EMAIL= \
       sh "$TMP/clone/scripts/make-snapshot.sh" --dest "$TMP/clone-tree" --dirty \
       --commit >"$TMP/noident.out" 2>&1; then
        t_fail "--commit succeeded with no identity anywhere -- auto-detection is back"
    elif grep -q 'refusing --commit with no author identity' "$TMP/noident.out"; then
        t_ok "--commit refuses when no config and no env identity exists"
    else
        t_fail "no-identity --commit failed for the wrong reason: $(head_bytes 200 "$TMP/noident.out")"
    fi
else
    t_fail "could not clone the repository for the no-identity probe"
fi

# The LICENCE refusal happens after extraction, so $TREE is populated either way.
# Rebuild only if some earlier failure left nothing behind.
if [ ! -d "$TREE" ] || [ -z "$(ls -A "$TREE" 2>/dev/null)" ]; then
    rm -rf "$TREE"
    sh "$SNAP_SH" --dest "$TREE" --dirty >/dev/null 2>&1
fi

# --- 3: the floor -- the scan saw a tree, so checks 4-8 mean something --------
# Without this, a broken extraction reports five clean scans of nothing. The M479
# shape: a lint whose ground truth was empty took its cheerful branch six times.
NFILES=$(find "$TREE" -type f 2>/dev/null | wc -l | tr -d ' ')
if [ "$NFILES" -ge 1000 ]; then
    t_ok "scanning a $NFILES-file snapshot"
else
    t_fail "only $NFILES files in the snapshot -- the extraction is broken, not the tree clean"
    t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"
    t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"
    t_done
fi

# --- 4: no compiled executable ------------------------------------------------
# Distribution is source-only by the release checklist's own decision, which is
# what removes curl's binary-redistribution notice obligation. A tracked ELF also
# leaks more than its bytes: an unstripped one carries the build machine's
# absolute paths in DWARF, which is check 6's finding arriving by another route.
elf_magic() { od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'; }

printf '\177ELF\1\1\1' > "$TMP/plant.elf"
if [ "$(elf_magic "$TMP/plant.elf")" = "7f454c46" ]; then
    _elf=""
    find "$TREE" -type f > "$TMP/files"
    while IFS= read -r f; do
        if [ "$(elf_magic "$f")" = "7f454c46" ]; then
            _elf="$_elf
  ${f#"$TREE"/}"
        fi
    done < "$TMP/files"
    if [ -z "$_elf" ]; then
        t_ok "no compiled executable in the snapshot (source-only distribution)"
    else
        t_fail "compiled binaries in the publishable tree, each rebuilt by its own script:$_elf"
    fi
else
    t_fail "the ELF matcher does not recognise a planted ELF header -- check 4 is meaningless"
fi

# One text corpus for checks 5-8: 1,600-odd files times four patterns is a lot of
# greps, and the findings below are VALUES rather than locations, so the filename
# is not wanted. ELF files are left out -- grep reports "Binary file matches" and
# stops reading them, which would make check 6 blind to exactly the DWARF paths
# that motivate it; check 4 already refuses them outright.
CORPUS="$TMP/corpus"
: > "$CORPUS"
while IFS= read -r f; do
    [ "$(elf_magic "$f")" = "7f454c46" ] || cat "$f" >> "$CORPUS" 2>/dev/null
done < "$TMP/files"

# --- 5: every email address is on the allowlist -------------------------------
# Example and no-reply domains only. A real address in a published tree is a
# person's, and the six that were here arrived as `git show` "Author:" headers
# that nobody had thought of as text.
EMAIL_RE='[A-Za-z0-9._%+-][A-Za-z0-9._%+-]*@[A-Za-z0-9-][A-Za-z0-9.-]*\.[A-Za-z][A-Za-z]*'
email_allowed() {
    case "$1" in
        noreply@anthropic.com) return 0 ;;    # the Co-Authored-By trailer's address
        *@example.com|*@example.org|*@example.net) return 0 ;;
        *@example|*.example) return 0 ;;      # RFC 2606 reserves the .example TLD
        me@host.com) return 0 ;;              # docs/EMACS.md's illustrative user
        *) return 1 ;;
    esac
}
printf 'someone@a-real%s.de\n' '-domain' > "$TMP/plant.mail"
if [ -n "$(grep -oE "$EMAIL_RE" "$TMP/plant.mail" 2>/dev/null)" ]; then
    _hits=""
    for a in $(grep -oE "$EMAIL_RE" "$CORPUS" 2>/dev/null | sort -u); do
        email_allowed "$a" || _hits="$_hits $a"
    done
    if [ -z "$_hits" ]; then
        t_ok "every email address in the snapshot is an example or no-reply address"
    else
        t_fail "real email address(es) in the publishable tree:$_hits"
    fi
else
    t_fail "the email matcher does not flag a planted address -- check 5 is meaningless"
fi

# --- 6: every /home/ path names a placeholder account -------------------------
# The banned form is not "an absolute path" -- the docs are full of legitimate
# ones -- but a path naming a REAL account, which identifies a person and a
# machine at once. Placeholders keep every sentence working; the list is short on
# purpose, so adding to it is a decision somebody makes rather than a habit.
home_allowed() {
    case "$1" in
        u|you|me|user|users|stud1|stud2|tierv|bench|USER) return 0 ;;
        *) return 1 ;;
    esac
}
printf '/home/%s/x\n' 'somebodyreal' > "$TMP/plant.home"
# The leading guard is the difference between a real path and a temp directory
# that happens to contain a subdirectory called "home": scripts/jhub-verify.sh
# writes "$J/home/nc.json", where the character before /home/ is part of a
# variable name. Anything alphanumeric (or a brace/paren closing an expansion)
# before /home/ means this is a fragment, not an absolute path.
HOME_RE='(^|[^A-Za-z0-9_})])/home/[A-Za-z][A-Za-z0-9._-]*'
if [ -n "$(grep -oE "$HOME_RE" "$TMP/plant.home" 2>/dev/null)" ]; then
    _hits=""
    for p in $(grep -oE "$HOME_RE" "$CORPUS" 2>/dev/null | sed 's|^[^/]||' | sort -u); do
        home_allowed "${p#/home/}" || _hits="$_hits $p"
    done
    if [ -z "$_hits" ]; then
        t_ok "every /home/ path in the snapshot names a placeholder account"
    else
        t_fail "real account name(s) in absolute paths:$_hits -- use a placeholder (u, you, me, bench)"
    fi
else
    t_fail "the home-path matcher does not flag a planted path -- check 6 is meaningless"
fi

# --- 7: no ssh login naming a private address ---------------------------------
# A bare RFC1918 address in prose is a documented constant (NetworkManager's
# shared-connection gateway is always 10.42.0.1; jc_http.c names 192.168.0.0/16
# because it fences it). A LOGIN@ADDRESS pair is different in kind: it is a
# machine on somebody's desk plus the account to try, which maps a home lab
# rather than instructing a reader. Name the ROLE the machine plays instead.
# A full dotted quad, and NOT the userinfo half of a URL: tests/test_http.c
# parses "https://user:pw@10.0.0.5:8080/x" to prove jc_url_host skips userinfo,
# which is a parser fixture rather than a machine. The guard is the character
# before the login -- ':' or '/' means it came out of a URL.
#
# The address then decides. Loopback and the RFC 5737 documentation nets are what
# a reader is SUPPOSED to see (scripts/fleet-run.sh already uses 192.0.2.10, and
# every QEMU rig forwards to 127.0.0.1); a real RFC 1918 address beside a login
# is somebody's actual machine. Bare addresses are untouched either way --
# NetworkManager's shared gateway is always 10.42.0.1 and jc_http.c names
# 192.168.0.0/16 because it fences it, and neither tells anyone where to log in.
LOGIN_RE='(^|[^A-Za-z0-9:/._-])[A-Za-z][A-Za-z0-9._-]*@[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*'
addr_documentary() {
    case "$1" in
        127.*) return 0 ;;                    # loopback: every QEMU rig forwards here
        192.0.2.*|198.51.100.*|203.0.113.*) return 0 ;;   # RFC 5737 TEST-NET-1/2/3
        *) return 1 ;;
    esac
}
printf 'ssh someuser@10.11.12.%s\n' '13' > "$TMP/plant.ssh"
if [ -n "$(grep -oE "$LOGIN_RE" "$TMP/plant.ssh" 2>/dev/null)" ]; then
    _hits=""
    for l in $(grep -oE "$LOGIN_RE" "$CORPUS" 2>/dev/null | sed 's|^[^A-Za-z]||' | sort -u); do
        addr_documentary "${l#*@}" || _hits="$_hits $l"
    done
    if [ -z "$_hits" ]; then
        t_ok "no login naming a real machine (loopback and RFC 5737 excepted)"
    else
        t_fail "ssh login(s) naming a reachable machine:$_hits -- name the ROLE, not the host"
    fi
else
    t_fail "the login matcher does not flag a planted login -- check 7 is meaningless"
fi

# --- 8: no physical device serial ---------------------------------------------
# An adb serial names one piece of hardware permanently and is useless to every
# reader who does not hold it.
SERIAL_RE='adb[^A-Za-z0-9]*[A-Z0-9][A-Z0-9]{11,}'
printf 'adb %s\n' 'ABCDEFGH12345' > "$TMP/plant.serial"
if [ -n "$(grep -oE "$SERIAL_RE" "$TMP/plant.serial" 2>/dev/null)" ]; then
    _hits=$(grep -oE "$SERIAL_RE" "$CORPUS" 2>/dev/null | sort -u | tr '\n' ' ')
    if [ -z "$_hits" ]; then
        t_ok "no device serial in the snapshot"
    else
        t_fail "device serial(s) in the publishable tree: $_hits"
    fi
else
    t_fail "the serial matcher does not flag a planted serial -- check 8 is meaningless"
fi

# --- 9: every institutional host is one a reader is meant to see --------------
# The maintainer develops against a university gateway, and naming it is a
# decision taken deliberately: the README explains the relationship, and the
# gateway host is a wire value a JLU reader needs. An INTERNAL machine behind it
# is a different thing -- it is that organisation's infrastructure, disclosed by
# a project they do not control. The findings measured against that gateway were
# reported to its operators privately and are withheld for the same reason
# (see docs/analysis/2026-08-09-hrz-gateway-findings.md, which keeps the lessons).
#
# Allowlist, so this check never has to name the host it excludes.
INST_RE='[A-Za-z0-9][A-Za-z0-9.-]*\.uni-giessen\.de'
inst_host_allowed() {
    case "$1" in
        api.hrz.uni-giessen.de) return 0 ;;      # the gateway; a wire value
        gitlab.hrz.uni-giessen.de) return 0 ;;   # the development remote
        hrz.uni-giessen.de|www.uni-giessen.de) return 0 ;;  # the institution, in prose
        *) return 1 ;;
    esac
}
printf 'https://internal-node-7%s/x\n' '.uni-giessen.de' > "$TMP/plant.host"
if [ -n "$(grep -oE "$INST_RE" "$TMP/plant.host" 2>/dev/null)" ]; then
    _hits=""
    for h in $(grep -oE "$INST_RE" "$CORPUS" 2>/dev/null | sort -u); do
        inst_host_allowed "$h" || _hits="$_hits $h"
    done
    if [ -z "$_hits" ]; then
        t_ok "every institutional host in the snapshot is one a reader is meant to see"
    else
        t_fail "internal host(s) of a third party in the publishable tree:$_hits"
    fi
else
    t_fail "the institutional-host matcher does not flag a planted host -- check 9 is meaningless"
fi

# --- 10: examples/ points at a placeholder, not at somebody's institution -----
# Decided 2026-08-19: the real gateway stays in the docs that measured it, and
# examples/ -- which README.md routes strangers to as copy-paste material -- uses
# a placeholder. A newcomer following the front page must not land on an endpoint
# they have no relationship with, and config.openai.json / config.anthropic.json
# exist so that the reachable case is the one they meet first.
if [ ! -d "$TREE/examples" ]; then
    t_fail "no examples/ in the snapshot -- this check has nothing to scan"
else
    _exc="$TMP/examples"
    : > "$_exc"
    find "$TREE/examples" -type f -exec cat {} + >> "$_exc" 2>/dev/null
    _hits=$(grep -oE "$INST_RE" "$_exc" 2>/dev/null | sort -u | tr '\n' ' ')
    if [ -z "$_hits" ]; then
        t_ok "examples/ names no institutional endpoint (placeholders only)"
    else
        t_fail "examples/ points a copy-paste config at: $_hits -- use api.example.edu"
    fi
fi

# --- 11: the tree carries what a public repository is expected to carry ------
# A stranger looks for these four before reading any code, and a project without
# them reads as a dump rather than something maintained. None existed until M487:
# CONTRIBUTING.md was 163 lines of C89 style rules that never said how to submit
# anything, there was no way to report a vulnerability privately in a tool that
# executes model-issued shell commands, and 138 doc pages had no index.
#
# LICENSE is deliberately NOT in this list. Its absence is the release gate, and
# make-snapshot refuses --commit without it (check 2) -- putting it here as well
# would make every run of this lint red for a reason nobody in this repository can
# fix, which is how a check gets ignored.
#
# Each is checked for a MINIMUM SIZE as well as existence, because the failure this
# guards against is not deletion but a stub: a two-line SECURITY.md saying "TBD" is
# worse than none, since it answers the reader's question with nothing.
_want="CONTRIBUTING.md:4000 SECURITY.md:1500 CODE_OF_CONDUCT.md:1000 docs/README.md:3000"
_thin=""
for _w in $_want; do
    _f=${_w%:*}; _min=${_w#*:}
    if [ ! -f "$TREE/$_f" ]; then
        _thin="$_thin
  $_f is missing"
    else
        _sz=$(wc -c < "$TREE/$_f" | tr -d ' ')
        [ "$_sz" -lt "$_min" ] && _thin="$_thin
  $_f is $_sz bytes, under the $_min-byte floor -- a stub answers the reader with nothing"
    fi
done
# and CONTRIBUTING must actually answer the question its name implies
if [ -f "$TREE/CONTRIBUTING.md" ] \
   && ! grep -qiE 'pull request|patch|issue' "$TREE/CONTRIBUTING.md"; then
    _thin="$_thin
  CONTRIBUTING.md never mentions issues, patches or pull requests -- it is a style guide, not a contribution guide"
fi
if [ -z "$_thin" ]; then
    t_ok "the publishable tree carries the community files, none of them a stub"
else
    t_fail "what a public repository is expected to carry:$_thin"
fi

# --- 12: the tree under test is the tree about to be committed ---------------
# THE DEFECT THIS EXISTS FOR, and it was mine (M492). The M484 producer builds the
# snapshot from the INDEX -- a scratch copy plus `git add -u` -- which is the right
# selection rule, because an untracked file genuinely does not ship. But `add -u`
# stages modifications to TRACKED files only, so a brand-new file is invisible to
# it. I wrote docs/plans/2026-08-19-merging-hrz-model-info.md, ran this lint (green)
# and the whole smoke tier (green, 212 drivers), and only THEN `git add`ed and
# committed. Every check had measured a tree without the file in it.
#
# The file spelled out an ssh login against a private-range address -- in the very
# sentence warning that check 7 forbids that shape -- so check 5 went red the moment
# it was staged, and master stayed red until someone else ran the tier and told me.
#
# THREE WRITERS REPRODUCED THE TOKEN WHILE DESCRIBING IT: the coordination document,
# the other session's ROADMAP entry reporting the failure, and this comment. A check
# whose finding QUOTES the offending value teaches the next writer to paste it back,
# and the next writer is usually the one documenting the fix. Describe the shape; do
# not instantiate it. (The failure message still quotes the value, and should -- the
# reader needs to find it. It is prose ABOUT the check that must not.)
#
# This is the sibling of the defect M484 itself fixed: there, --dirty tolerated a
# dirty tree while archiving HEAD, so the lint reported on the wrong tree. Fixed
# that, then walked one step over into "--dirty covers tracked changes only".
#
# The rule is not wrong and is not being changed. What was missing is that the lint
# could not tell it was being asked about a tree nobody was about to publish. So it
# refuses to answer, rather than answering about the wrong subject.
_untracked=$(git -C "$SMOKE_ROOT" ls-files --others --exclude-standard 2>/dev/null \
             | head -n 20)
if [ -z "$_untracked" ]; then
    t_ok "no untracked files -- the snapshot under test is the tree about to be committed"
else
    t_fail "untracked non-ignored file(s) -- this run measured a tree WITHOUT them, so a green
  result says nothing about what you are about to commit. Stage them first:
$(printf '%s\n' "$_untracked" | sed 's/^/  /')"
fi

# --- 13: the repository root holds only files that belong there --------------
# A scratch file at the root ships, and no other check sees it: it is text, so
# check 4 passes it; it carries no address, account or host, so checks 5-9 pass it.
# `.jc_chmodrow.txt` -- a DEFERRED table row someone was drafting -- rode in exactly
# that way and was referenced by nothing.
#
# The root is the one directory small enough to enumerate, which is what makes this
# checkable at all; anywhere deeper the allowlist would be the maintenance burden it
# is meant to prevent. Adding a legitimate root file means adding a line here, which
# is the point: the root of a published repository is its front page, and a file
# arriving there should be a decision.
root_allowed() {
    case "$1" in
        .gitignore|.gitattributes) return 0 ;;
        LICENSE|NOTICE|COPYING|AUTHORS|CREDITS) return 0 ;;   # the licence lands here
        CREDITS.md) return 0 ;;   # M497: markdown, so it renders on a forge
        README.md|CHANGELOG.md|CONTRIBUTING.md|SECURITY.md) return 0 ;;
        CODE_OF_CONDUCT.md|CLAUDE.md|TUTORIAL.md) return 0 ;;
        Makefile) return 0 ;;
        *) return 1 ;;
    esac
}
_stray=""
for f in $(find "$TREE" -maxdepth 1 -type f 2>/dev/null | sed "s|^$TREE/||" | sort); do
    root_allowed "$f" || _stray="$_stray $f"
done
if [ -z "$_stray" ]; then
    t_ok "the repository root holds only files that belong there"
else
    t_fail "unexpected file(s) at the root of the publishable tree:$_stray -- a scratch file
  ships like any other, and no other check in this driver can see it"
fi

t_done
