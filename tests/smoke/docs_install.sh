#!/bin/sh
# smoke: M1197 (batch G, G2) -- `make install` ships the six core learner
# pages to $(PREFIX)/share/doc/jichi/, and the `jichi docs` surface defaults to
# that directory when no source is configured.
#
# Authority: docs/plans/2026-11-09-jichi-as-its-own-guide.md section G2
# (lines 104-108) and the operator's decision D-G2 (recorded in the 2026-11-17
# section). The behaviour contract, cell by cell, is the T1 spec in
# docs/internal/drive/2026-11-05-harness-port/notes/M1197.md. This driver is the
# SECOND SURFACE that must agree with the Makefile: the D-G2 list of six pages
# is spelled in the Makefile (G2_PAGES) and again HERE (PAGES). One source, one
# test that re-derives it (the M1196-catalog "two surfaces must agree" lesson).
#
# RED-FIRST (M1197). Cells 6 and 7 are RED on this commit: they assert the B4
# behaviour (`jichi docs` defaults to the installed directory when no source is
# configured), which T2 implements in src/main.c. The current run_docs prints
# the no-sources message, so `docs list` does NOT name jichi-docs (cell 6) and
# `docs search` does NOT return a hit from the installed page (cell 7). Cells
# 2, 3, 4, 5, 8 are GREEN: 2/3/5 prove the Makefile install (B1) lands exactly
# the six pages byte-identical with no compiler; 4 proves uninstall (B2)
# removes the six and never a stranger; 8 is a REGRESSION cell (B5b) that a
# configured array is authoritative and the synthetic source stays absent --
# true before T2 (trivially, nothing is synthetic yet) and after.
#
# RED-FIRST (2026-11-25, the operator's decision "have jichi docs find its
# installed documentation"). Cells 9-11 are RED on the commit that adds them:
# the installed directory was found only through readlink("/proc/self/exe"),
# which FreeBSD (no procfs by default), OpenBSD (no such interface) and illumos
# (/proc/self/path/a.out instead) do not have -- cells 6 and 7 failed on all
# three rigs. Linux cannot take /proc away from one process, but glibc's
# dynamic loader gives the same shape: run as `ld.so <program>`, the kernel's
# /proc/self/exe names the LOADER, while the program's argv[0] still names the
# program. Cell 9 passes the installed copy's absolute path as argv[0]; cell 10
# passes the bare name `jichi` (ld.so --argv0) with the fakeroot's bin first
# on PATH, the way a shell finds the command; cell 11 passes a relative argv[0]
# beside -C, which leaves the directory that argv[0] is relative to. All three
# SKIP where there is no glibc loader (a static build, musl, a BSD, illumos --
# where cells 6 and 7 meet the missing /proc/self/exe for real), and 10 where
# the loader has no --argv0 (glibc before 2.33).
#
# Cell 12 (2026-11-25, the operator: for 0.15.0; the idea is threadwork's
# P34): JICHI_BIN, the path jichi exports to its children, is the installed
# copy's canonical path when the copy is started by a relative argv[0] beside
# -C. Read the way a child reads it: a model turn (mockmodel) runs
# `printf 'JB<%s>' "$JICHI_BIN"` and the tool result is taken from the next
# request. On Linux the kernel answers /proc/self/exe, so the cell is green
# with or without the change; its red is on a kernel without that answer
# (FreeBSD, OpenBSD, illumos), where JICHI_BIN was the raw argv[0] read after
# -C left the directory it was relative to.
#
# RED-FIRST (2026-11-25, the operator: "Fix the JICHI_BIN loader case for
# 0.15.0., now."). Cell 13 is RED on the commit that adds it: started as
# `ld.so ./usr/local/bin/jichi`, the kernel's /proc/self/exe names the loader,
# and JICHI_BIN took that answer, so a child running "$JICHI_BIN" would start
# the loader. SKIP without a glibc loader, as cells 9-11.
#
# Shape copied from install_no_build.sh: the install runs in a COPY of the tree
# under DESTDIR so the real tree is never written; the M661 ${MAKE}/smoke_make
# idiom; the M593 clean-stamp fabricated in $tmp and passed as STAMP=... (the
# copy has no include/ tree, and fabricating keeps the verdict independent of
# whether the real tree has uncommitted work); and the no-compiler grep of
# check 4.
. "$(dirname "$0")/_smoke.sh"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
_mk=$(smoke_make)

# PRECONDITION, NAMED RATHER THAN DISCOVERED (M661). `make install` copies ALL
# THREE programs (jichi, jichi-convert, jichi-harness) plus the man page,
# completions, editor plugins and the six pages; a target that is missing any
# binary refuses before copying anything, so cell 2 (rc=0) would fail for the
# missing FILE rather than for anything about install. `make smoke` builds only
# $(BIN) and $(HARNESS_BIN) -- the net under that (check-target builds all).
# Cells 6-8 additionally need tests/tools/mockmodel (cell 7), so guard it too.
if [ ! -x "$ROOT/jichi-convert" ] || [ ! -x "$ROOT/jichi-harness" ] || \
   [ ! -x "$SMOKE_TOOLS/mockmodel" ]; then
    t_skip "jichi-convert and/or jichi-harness (or mockmodel) is not built, and \`make install\` \
installs all three programs -- these checks would fail for a missing file \
rather than for anything about install. Run \`make all\` (check-target does, \
since M661); mockmodel is tests/tools/mockmodel."
fi

t_plan 13
smoke_home
tmp=$(smoke_tmp)

# The D-G2 set -- spelled here as the second surface that must agree with the
# Makefile's G2_PAGES variable.
PAGES="TUTORIAL_BEGINNER SETUP_WIZARD CONFIG_TUTORIAL AGENTS_GUIDE VOCABULARY CURRICULUM"
NPAGES=6

# M593: fabricate a CLEAN stamp in $tmp and pass it as STAMP=... so the
# install's verdict does not depend on whether the real tree has uncommitted
# work. The copy below has no include/ tree, so the real $(STAMP) would read
# empty and the check would pass vacuously; the clean header makes the "what
# you install" check actually run on a clean rev. (install_no_build does the
# same.)
printf '#define JC_BUILD_REV "deadbee"\n' > "$tmp/clean_stamp.h"
CLEAN_STAMP="STAMP=$tmp/clean_stamp.h"

# --- 1: the install shape, copied into $tmp/tree -------------------------------
# Everything `install` reads, in a copy, so the real tree is never written and
# DESTDIR=$tmp/fakeroot keeps it out of the system. The three built programs
# plus docs/, man/, completions/, editors/ are the install's inputs; the stamp
# is passed on the command line (no include/ copied).
mkdir -p "$tmp/tree"
cp "$ROOT/Makefile" "$tmp/tree/Makefile"
cp "$ROOT/jichi" "$ROOT/jichi-convert" "$ROOT/jichi-harness" "$tmp/tree/"
cp -r "$ROOT/docs" "$ROOT/man" "$ROOT/completions" "$ROOT/editors" "$tmp/tree/"

# --- cell 1: install runs from the copy, rc=0 ---------------------------------
iout=$(cd "$tmp/tree" && "$_mk" install DESTDIR="$tmp/fakeroot" \
    PREFIX=/usr/local $CLEAN_STAMP 2>&1); irc=$?
if [ "$irc" -eq 0 ]; then
    t_ok "install (DESTDIR=$tmp/fakeroot, clean stamp) succeeded in the copy (rc=0)"
else
    t_fail "install in the copy failed (rc=$irc) -- the pages install is expected to \
run after the MANDIR install. Output:
$(printf '%s\n' "$iout" | head_bytes 300)"
fi

# --- cell 2: the six pages land, byte-identical to their source ----------------
# Each must exist under fakeroot/usr/local/share/doc/jichi/ and cmp clean
# against its source page. The missing page is NAMED in the failure.
DOCD="$tmp/fakeroot/usr/local/share/doc/jichi"
missing=""
for p in $PAGES; do
    if [ -f "$DOCD/$p.md" ] && cmp -s "$DOCD/$p.md" "$ROOT/docs/$p.md"; then
        :
    else
        missing="$missing $p"
    fi
done
if [ -z "$missing" ]; then
    t_ok "all six D-G2 pages land under share/doc/jichi/ byte-identical to their source"
else
    t_fail "the D-G2 pages missing or not byte-identical:$missing (expected each under \
$DOCD/, cmp-clean against docs/<PAGE>.md)"
fi

# --- cell 3: the doc directory holds EXACTLY the six, no extras ----------------
# Count + set-of-basenames. This is the B1 "nothing else" and the second-surface
# agreement: the installed set must equal the PAGES list spelled above.
installed_sorted=$( (ls "$DOCD" 2>/dev/null || true) | sort | tr '\n' ' ')
want_sorted=$(for p in $PAGES; do printf '%s\n' "$p.md"; done | sort | tr '\n' ' ')
ninst=$( (ls "$DOCD" 2>/dev/null || true) | grep -c . )
if [ "$ninst" -eq "$NPAGES" ] && [ "$installed_sorted" = "$want_sorted" ]; then
    t_ok "the doc directory holds exactly the $NPAGES D-G2 pages, no extras"
else
    t_fail "the doc directory holds $ninst file(s), not exactly the $NPAGES D-G2 pages. \
Installed: '$installed_sorted' Expected: '$want_sorted' -- install must copy the six and \
nothing else (B1)"
fi

# --- cell 4: uninstall removes the six; a stranger survives -------------------
# B2's "never": plant EXTRA.md (not one of the six) before uninstalling; after,
# the six are gone and EXTRA.md SURVIVES (uninstall must not rmdir, must not
# remove a name that is not one of the six).
printf 'planted\n' > "$DOCD/EXTRA.md"
uout=$(cd "$tmp/tree" && "$_mk" uninstall DESTDIR="$tmp/fakeroot" \
    PREFIX=/usr/local 2>&1); urc=$?
gone=1
for p in $PAGES; do
    [ -e "$DOCD/$p.md" ] && gone=0
done
if [ "$urc" -eq 0 ] && [ "$gone" -eq 1 ] && [ -e "$DOCD/EXTRA.md" ]; then
    t_ok "uninstall removes the six pages and leaves a non-D-G2 file (EXTRA.md) in place"
else
    t_fail "uninstall (rc=$urc) did not remove the six and keep EXTRA.md: \
gone=$([ $gone -eq 1 ] && echo yes || echo no) extra_survives=$([ -e "$DOCD/EXTRA.md" ] && echo yes || echo no). \
B2: uninstall must remove the six and NEVER a stranger. Output:
$(printf '%s\n' "$uout" | head_bytes 300)"
fi

# --- cell 5: the install produced no compiler invocation ----------------------
# The pages are COPIED, not built: install_no_build check 4's no-compiler
# pattern, applied to the install output captured in cell 1.
ncc=$(printf '%s\n' "$iout" | grep -cE '^[[:space:]]*(cc|gcc|clang|\$(CC))[[:space:]]')
if [ "$ncc" -eq 0 ]; then
    t_ok "installing the six pages runs no compiler (they are copied)"
else
    t_fail "install ran the compiler $ncc time(s) -- the six pages are copied from the \
source tree, not built (B5a). Lines:
$(printf '%s\n' "$iout" | grep -E '^[[:space:]]*(cc|gcc|clang)' | head_bytes 200)"
fi

# --- the second fakeroot: an INSTALLED jichi, for the B4 cells ----------------
# Cells 6-8 run a jichi whose /proc/self/exe resolves to the fakeroot's
# bin/jichi (B4a: the doc directory is derived from the executable's own
# location). So copy the BUILT $ROOT/jichi into fr2/usr/local/bin/ and the six
# pages into fr2/usr/local/share/doc/jichi/, and run THAT copy -- never $BIN --
# with HOME=$tmp/h2 and a config carrying no docs array (unless the cell says
# otherwise). /proc/self/exe must resolve to the fakeroot's path, so we run the
# copy by its absolute fr2 path.
FR2="$tmp/fr2"
FR2J="$FR2/usr/local/bin/jichi"
mkdir -p "$FR2/usr/local/bin" "$FR2/usr/local/share/doc/jichi"
# Copy the BUILT binary ($BIN == $ROOT/jichi) into the fakeroot and run THAT
# copy, so /proc/self/exe resolves to the fakeroot's path (B4a). Never run $BIN
# directly -- its /proc/self/exe would be the repo root, whose share/doc/jichi
# is not installed, and cell 6/8 would test the build tree, not the install.
cp "$BIN" "$FR2J"
for p in $PAGES; do
    cp "$ROOT/docs/$p.md" "$FR2/usr/local/share/doc/jichi/$p.md"
done
mkdir -p "$tmp/h2"

# A two-model config with NO docs array (cell 6/7's premise): roles chat+embed
# (the docs.sh pattern), resource features off, lowResource pinned false so the
# profile does not shift on a low-RAM box (smoke_lint 7/8), toolProfile full.
cat > "$tmp/fr2_nodocs.json" <<EOF
{"models":[
  {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:127.0.0.1","apiKey":"x","roles":["chat"]},
  {"name":"e","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:127.0.0.1","apiKey":"x","roles":["embed"]}
],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# --- cell 6 (RED before T2): `docs list` names the synthetic source -----------
# The installed directory exists, no docs array is configured -> the docs
# surface behaves as if one source named jichi-docs (path fr2/.../doc/jichi)
# were configured. On the CURRENT head this is red: run_docs prints the
# no-sources message and names nothing.
lout=$(HOME="$tmp/h2" "$FR2J" --config "$tmp/fr2_nodocs.json" \
    docs list < /dev/null 2>&1); lrc=$?
case "$lout" in
    *jichi-docs*"$FR2"*)
        t_ok "docs list with no configured source names jichi-docs at the installed path" ;;
    *)
        t_fail "docs list did NOT name the synthetic source jichi-docs at the installed \
path ($FR2/usr/local/share/doc/jichi). This is RED before T2: the current run_docs \
prints the no-sources message and has no B4 default. (rc=$lrc) Saw:
$(printf '%s\n' "$lout" | head_bytes 300)" ;;
esac

# --- cell 7 (RED before T2): `docs search` returns a hit from an installed page
# The docs.sh mockmodel pattern: one embed rule, the query's words weighted by
# their count in the page, so a page the query names ranks first. The query is
# "how do checkpoints work" -- checkpoint appears in TUTORIAL_BEGINNER.md (one
# of the six installed pages) and nowhere in the other five. A configured embed
# model is present, so the no-sources path -- NOT an embed-model error -- is the
# reason this is red before T2: the search cannot even see the installed
# source. On the current head run_docs returns the no-sources message BEFORE
# the subcommand is parsed, so the invocation shape below is irrelevant to the
# red (its output is the same either way).
#
# Invocation shape: `docs search "" <query words>`, NOT `docs search <query
# words>`. run_docs parses pos[2] as the SOURCE NAME and pos[3..] as the query;
# jc_docs_find's sole-source shortcut -- the only way an unnamed query reaches
# the single synthetic source once T2 lands -- fires only for a NULL/empty
# name. A bare `search <query>` would set the query text as the name and fail
# "unknown docs source" even after T2; the empty-string name resolves to the
# sole source whether T2 keeps the current signature or makes the name
# argument optional (B4d).
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  embed checkpoint hooks state
EOF
mm_start "$tmp/replies.mm" "$tmp/mmcap"
sed "s/127.0.0.1:127.0.0.1/127.0.0.1:$MM_PORT/g" "$tmp/fr2_nodocs.json" > "$tmp/fr2_nodocs_live.json"
sout=$(HOME="$tmp/h2" "$FR2J" --config "$tmp/fr2_nodocs_live.json" \
    docs search "" how do checkpoints work < /dev/null 2>&1); src=$?
mm_stop
case "$sout" in
    *TUTORIAL_BEGINNER.md*)
        t_ok "docs search with no configured source returns a hit from an installed page (TUTORIAL_BEGINNER.md)" ;;
    *)
        t_fail "docs search did NOT return a hit from an installed page. The query's word \
'checkpoint' is in TUTORIAL_BEGINNER.md (installed) and absent from the other five, so a \
working B4 default must surface it. This is RED before T2 -- for a DIFFERENT reason than \
cell 6: the embed model IS configured (no 'no embedding model' error), but the no-sources \
path means the search never sees the installed directory as a source. (rc=$src) Saw:
$(printf '%s\n' "$sout" | head_bytes 300)" ;;
esac

# --- cell 8 (green before and after T2): the configured array is authoritative
# B5b, both directions. A config with ONE docs entry ('mine' -> a planted
# directory) must show 'mine' and NOT the synthetic jichi-docs: a configured
# array (len >= 1) is always authoritative and the default never appears. This
# is a REGRESSION cell -- true before T2 (nothing is synthetic yet) and after
# (T2 must not override a configured array).
mkdir -p "$FR2/docs-elsewhere"
printf '# A custom page\nmine mine custom docs\n' > "$FR2/docs-elsewhere/mydoc.md"
cat > "$tmp/fr2_onedocs.json" <<EOF
{"models":[
  {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:127.0.0.1","apiKey":"x","roles":["chat"]}
],
"docs":[{"name":"mine","path":"$FR2/docs-elsewhere"}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
cout=$(HOME="$tmp/h2" "$FR2J" --config "$tmp/fr2_onedocs.json" \
    docs list < /dev/null 2>&1); crc=$?
case "$cout" in
    *mine*docs-elsewhere*)
        has_mine=yes ;;
    *)
        has_mine=no ;;
esac
case "$cout" in
    *jichi-docs*)
        has_synthetic=yes ;;
    *)
        has_synthetic=no ;;
esac
if [ "$has_mine" = yes ] && [ "$has_synthetic" = no ]; then
    t_ok "a configured docs array is authoritative: shows 'mine' and NOT jichi-docs (B5b)"
else
    t_fail "a configured docs array must show 'mine' and NOT the synthetic jichi-docs: \
mine=$has_mine jichi-docs_absent=$([ $has_synthetic = no ] && echo yes || echo no). B5b: a \
config with len>=1 is always authoritative. (rc=$crc) Saw:
$(printf '%s\n' "$cout" | head_bytes 300)"
fi

# --- cells 9-10 (RED before the argv[0] answer): the kernel's own-path answer
# does not name jichi. The glibc loader of the installed copy, read from ldd's
# list (a path ending in ld-linux*); none means no glibc loader here. The
# printed path is derived from the canonical one, so match the fakeroot's
# canonical spelling (FR2C): a TMPDIR behind a symlink must not read as a miss.
LDSO=""
if command -v ldd > /dev/null 2>&1; then
    LDSO=$(ldd "$FR2J" 2> /dev/null | awk '{ for (i = 1; i <= NF; i++)
        if ($i ~ /^\/.*\/ld-linux[^\/]*$/) { print $i; exit } }')
fi
FR2C=$(cd "$FR2" && pwd -P)

if [ -z "$LDSO" ] || [ ! -x "$LDSO" ]; then
    t_skip_one "no glibc dynamic loader for the installed copy (ldd names no ld-linux: a static \
build, musl, or not Linux); cells 6 and 7 meet a missing /proc/self/exe for real there"
else
    gout=$(HOME="$tmp/h2" "$LDSO" "$FR2J" --config "$tmp/fr2_nodocs.json" \
        docs list < /dev/null 2>&1); grc=$?
    case "$gout" in
        *jichi-docs*"$FR2C"*)
            t_ok "docs list run through the loader ($LDSO: /proc/self/exe names the loader) \
still names jichi-docs at the installed path, from argv[0]" ;;
        *)
            t_fail "docs list run as \`$LDSO $FR2J\` did NOT name jichi-docs at the installed \
path ($FR2C/usr/local/share/doc/jichi). Under the loader /proc/self/exe names the loader, \
so the only answer left is argv[0] -- the one FreeBSD and OpenBSD have. (rc=$grc) Saw:
$(printf '%s\n' "$gout" | head_bytes 300)" ;;
    esac
fi

if [ -z "$LDSO" ] || [ ! -x "$LDSO" ]; then
    t_skip_one "no glibc dynamic loader for the installed copy (see cell 9)"
elif ! "$LDSO" --help 2>&1 | grep -q -e '--argv0'; then
    t_skip_one "the loader $LDSO has no --argv0 (glibc before 2.33), so a bare argv[0] \
cannot be given through it"
else
    pout=$(HOME="$tmp/h2" PATH="$FR2/usr/local/bin:$PATH" "$LDSO" --argv0 jichi "$FR2J" \
        --config "$tmp/fr2_nodocs.json" docs list < /dev/null 2>&1); prc=$?
    case "$pout" in
        *jichi-docs*"$FR2C"*)
            t_ok "docs list with argv[0] the bare name jichi finds the installed copy through \
PATH and names jichi-docs at its installed path" ;;
        *)
            t_fail "docs list run with argv[0] \`jichi\` (the fakeroot's bin first on PATH) did \
NOT name jichi-docs at the installed path ($FR2C/usr/local/share/doc/jichi): a bare argv[0] \
must be looked up in PATH, as the shell that started jichi did. (rc=$prc) Saw:
$(printf '%s\n' "$pout" | head_bytes 300)" ;;
    esac
fi

# Cell 11: a RELATIVE argv[0] with -C. A relative argv[0] means "from the
# directory jichi started in", and -C leaves that directory before dispatch,
# so argv[0] must be resolved before the chdir: started in $FR2 as
# `./usr/local/bin/jichi -C $tmp/h2`, the installed path must still be found.
if [ -z "$LDSO" ] || [ ! -x "$LDSO" ]; then
    t_skip_one "no glibc dynamic loader for the installed copy (see cell 9)"
else
    rout=$(cd "$FR2" && HOME="$tmp/h2" "$LDSO" ./usr/local/bin/jichi -C "$tmp/h2" \
        --config "$tmp/fr2_nodocs.json" docs list < /dev/null 2>&1); rrc=$?
    case "$rout" in
        *jichi-docs*"$FR2C"*)
            t_ok "docs list with a relative argv[0] and -C still names jichi-docs at the \
installed path (argv[0] read against the starting directory)" ;;
        *)
            t_fail "docs list started in $FR2 as \`./usr/local/bin/jichi -C $tmp/h2\` (through the \
loader) did NOT name jichi-docs at the installed path ($FR2C/usr/local/share/doc/jichi): a relative \
argv[0] must be resolved against the directory jichi started in, before -C leaves it. (rc=$rrc) Saw:
$(printf '%s\n' "$rout" | head_bytes 300)" ;;
    esac
fi

# Cell 12: JICHI_BIN as a child sees it, started in $FR2 as
# `./usr/local/bin/jichi -C $tmp/h2` (no loader: the kernel's own answer where
# there is one, argv[0] where there is none).
cat > "$tmp/jbin.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"printf 'JB%s<%s>' X \"$JICHI_BIN\""}
rule
  count 2
  text JBIN_DONE
EOF
mm_start "$tmp/jbin.mm" "$tmp/jbincap"
sed "s/127.0.0.1:127.0.0.1/127.0.0.1:$MM_PORT/g" "$tmp/fr2_nodocs.json" > "$tmp/fr2_jbin_live.json"
jout=$(cd "$FR2" && HOME="$tmp/h2" ./usr/local/bin/jichi -C "$tmp/h2" \
    --config "$tmp/fr2_jbin_live.json" -q --no-session --auto -p "where are you" < /dev/null 2>&1); jrc=$?
mm_stop
jsaw=$(grep -o 'JBX<[^>]*>' "$tmp/jbincap/req.2" 2>/dev/null | head -1)
if [ "$jsaw" = "JBX<$FR2C/usr/local/bin/jichi>" ]; then
    t_ok "JICHI_BIN, as the child's shell read it, is the installed copy's canonical path ($jsaw) after a \
relative argv[0] and -C"
else
    t_fail "JICHI_BIN, as the child's shell read it, was '${jsaw:-nothing captured}', not \
JBX<$FR2C/usr/local/bin/jichi>: started as \`./usr/local/bin/jichi -C $tmp/h2\` in $FR2, the exported path must be \
the canonical one, read before -C left the starting directory. (rc=$jrc) Saw:
$(printf '%s\n' "$jout" | head_bytes 300)"
fi

# Cell 13: JICHI_BIN under the loader. Started as `ld.so ./usr/local/bin/jichi`,
# the kernel's /proc/self/exe names the LOADER; a child running "$JICHI_BIN"
# would start the loader with jichi's arguments. JICHI_BIN must name the
# program (argv[0], canonical), not the loader. Same mockmodel turn as cell 12.
if [ -z "$LDSO" ] || [ ! -x "$LDSO" ]; then
    t_skip_one "no glibc dynamic loader for the installed copy (see cell 9)"
else
    mm_start "$tmp/jbin.mm" "$tmp/jbincap13"
    sed "s/127.0.0.1:127.0.0.1/127.0.0.1:$MM_PORT/g" "$tmp/fr2_nodocs.json" > "$tmp/fr2_jbin13_live.json"
    lout=$(cd "$FR2" && HOME="$tmp/h2" "$LDSO" ./usr/local/bin/jichi -C "$tmp/h2" \
        --config "$tmp/fr2_jbin13_live.json" -q --no-session --auto -p "where are you" < /dev/null 2>&1); lrc=$?
    mm_stop
    lsaw=$(grep -o 'JBX<[^>]*>' "$tmp/jbincap13/req.2" 2>/dev/null | head -1)
    if [ "$lsaw" = "JBX<$FR2C/usr/local/bin/jichi>" ]; then
        t_ok "JICHI_BIN under the loader names the program, not the loader ($lsaw)"
    else
        t_fail "JICHI_BIN under the loader was '${lsaw:-nothing captured}', not JBX<$FR2C/usr/local/bin/jichi>: \
started as \`$LDSO ./usr/local/bin/jichi\`, /proc/self/exe names the loader, and a child running \"\$JICHI_BIN\" \
would start the loader with jichi's arguments. (rc=$lrc) Saw:
$(printf '%s\n' "$lout" | head_bytes 300)"
    fi
fi

t_done
