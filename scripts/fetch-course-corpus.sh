#!/bin/sh
# fetch-course-corpus.sh -- put a language's OFFICIAL documentation on this
# machine, once, in a form jichi can index and a learner can cite.
#
# WHAT THIS IS FOR. docs/plans/2026-09-language-course.md: a self-learner works
# through an official tutorial with jichi as the tutor and the documents as
# jichi's references. That needs the documents to be HERE -- offline, pinned to a
# version, and the same bytes for every learner on that version -- because a
# course whose corpus changes under it cannot be graded and a citation into it
# cannot be checked.
#
# WHAT IT REFUSES TO DO, and why each refusal is a decision:
#
#   It does not CRAWL. Every recipe names an archive the project itself
#   publishes. A crawl is impolite to the host, unversioned, and produces a
#   different corpus on every run -- so two learners would be reading different
#   books with the same name.
#
#   It does not GUESS the version. The recipe names an index page and a pattern;
#   the version is whatever that page says today. This is not caution for its own
#   sake: the first hand-run of this fetch guessed `python-3.13-docs-text.tar.bz2`
#   from the version the author expected and got HTTP 404. The published version
#   was 3.14.
#
#   It does not put anything in the repository. The corpus is somebody's
#   downloaded documentation, not this project's source.
#
# WHAT IT WRITES. The snapshot, plus MANIFEST.json beside it recording source
# URL, version, retrieval date, sha256, licence and the file counts. The manifest
# is what makes the course honest: a learner can say what their answer was
# checked against, and a second learner can obtain the same thing.
#
# Usage:
#   scripts/fetch-course-corpus.sh python
#   scripts/fetch-course-corpus.sh python --out ~/development/course-corpora
#   scripts/fetch-course-corpus.sh --list
#   scripts/fetch-course-corpus.sh python --dry-run
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
RECIPES="$HERE/corpora"
OUT="${JC_COURSE_CORPORA:-$HOME/development/course-corpora}"
LANG_NAME=""
DRY=0

die() { echo "fetch-course-corpus: $*" >&2; exit 2; }
say() { echo "== $*"; }
ok()  { echo "ok - $*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --out)     OUT="$2"; shift ;;
        --dry-run) DRY=1 ;;
        --list)
            echo "recipes in $RECIPES:"
            for r in "$RECIPES"/*.recipe; do
                [ -f "$r" ] || continue
                echo "  $(basename "$r" .recipe)"
            done
            exit 0 ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        -*) die "unknown option: $1" ;;
        *)  LANG_NAME="$1" ;;
    esac
    shift
done

[ -n "$LANG_NAME" ] || die "name a language (try --list)"
RECIPE="$RECIPES/$LANG_NAME.recipe"
[ -f "$RECIPE" ] || die "no recipe: $RECIPE (try --list)"

# Read the recipe. Plain key=value, comments ignored; no eval of file contents,
# because a recipe is data and a downloaded page is somebody else's text.
rv() { sed -n "s/^$1=//p" "$RECIPE" | head -1; }
TITLE=$(rv title);        INDEX_URL=$(rv index_url)
ARCHIVE_RE=$(rv archive_re); ARCHIVE_BASE=$(rv archive_base)
STRIP=$(rv strip);        TUT_DIR=$(rv tutorial_dir)
REF_DIR=$(rv reference_dir); LICENCE=$(rv licence)
LICENCE_URL=$(rv licence_url)
KIND=$(rv kind); [ -n "$KIND" ] || KIND=archive
LOCATE=$(rv locate_cmd); IS_HTML=$(rv html)

# --- kind=distribution: the documentation is already on this machine ---------
# Some projects publish no archive at all and ship their documentation with the
# distribution (Racket: docs.racket-lang.org is a Scribble site with no bundle,
# and the release listing has no doc tarball). Downloading is then the wrong
# verb -- the honest action is to ASK THE INSTALLATION where its documents are,
# and to say so plainly when it is not installed.
if [ "$KIND" = distribution ]; then
    [ -n "$LOCATE" ] || die "recipe is kind=distribution but names no locate_cmd"
    mkdir -p "$OUT"
    say "$TITLE -- asking the installation where its documentation is"
    if [ "$DRY" -eq 1 ]; then echo "+ $LOCATE"; exit 0; fi
    # Keep stderr: "not installed" and "your locate_cmd is broken" are
    # different answers and the script used to give the first for both.
    DOCDIR=$(sh -c "$LOCATE" 2>"$OUT/.locate.err" | tail -1) || true
    if [ -z "$DOCDIR" ] && [ -s "$OUT/.locate.err" ]; then
        echo "fetch-course-corpus: the recipe's locate_cmd failed:" >&2
        sed 's/^/    /' "$OUT/.locate.err" >&2
    fi
    [ -n "$DOCDIR" ] && [ -d "$DOCDIR" ] || die "$TITLE does not appear to be \
installed, or its doc directory was not found. This recipe reads the docs from \
the installed distribution because the project publishes no archive: install \
$TITLE first, then re-run."
    ok "documentation directory: $DOCDIR"
    NFILES=$(find "$DOCDIR" -type f 2>/dev/null | wc -l | tr -d ' ')
    NTUT=0
    [ -z "$TUT_DIR" ] || NTUT=$(find "$DOCDIR/$TUT_DIR" -type f 2>/dev/null | wc -l | tr -d ' ')
    mkdir -p "$OUT"
    MAN="$OUT/$LANG_NAME-MANIFEST.json"
    cat > "$MAN" <<MANIFEST
{
  "language": "$LANG_NAME",
  "title": "$TITLE",
  "kind": "distribution",
  "doc_dir": "$DOCDIR",
  "retrieved": "$(date +%Y-%m-%d)",
  "licence": "$LICENCE",
  "licence_url": "$LICENCE_URL",
  "files": $NFILES,
  "tutorial_dir": "$TUT_DIR",
  "tutorial_files": $NTUT,
  "reference_dir": "$REF_DIR",
  "html": ${IS_HTML:-0}
}
MANIFEST
    ok "manifest written: $MAN"
    ok "$NFILES files in the doc tree, $NTUT of them in $TUT_DIR/"
    echo
    echo "Point a jichi config at the TUTORIAL only (docs/DOCS.md):"
    echo
    echo "  \"docs\": [ { \"name\": \"$LANG_NAME-$TUT_DIR\", \"path\": \"$DOCDIR/$TUT_DIR\" } ]"
    echo
    if [ "${IS_HTML:-0}" = "1" ]; then
        echo "NOTE: these pages are HTML. jichi reduces HTML to prose before"
        echo "indexing a docs source (M667), so what gets embedded is the text"
        echo "and the citation still points at the real .html file. Measured on"
        echo "this corpus: 72 s before the reduction, 7 s after."
    fi
    exit 0
fi

# Only an ARCHIVE recipe needs these; a distribution recipe has already exited.
[ -n "$INDEX_URL" ] || die "recipe names no index_url (and kind is not distribution)"
command -v curl >/dev/null 2>&1 || die "curl is required"

say "$TITLE -- reading $INDEX_URL for the current version"
if [ "$DRY" -eq 1 ]; then
    echo "+ curl -sSL $INDEX_URL | grep -oE '$ARCHIVE_RE'"
    echo "+ would write under $OUT"
    exit 0
fi

REL=$(curl -sSL --max-time 60 "$INDEX_URL" 2>/dev/null \
      | grep -oE "$ARCHIVE_RE" | head -1) || true
[ -n "$REL" ] || die "no archive matching '$ARCHIVE_RE' on $INDEX_URL -- \
the page's layout may have changed; read it before editing the pattern"
URL="$ARCHIVE_BASE$REL"
# The version is taken FROM THE LINK, never from an expectation.
VERSION=$(printf '%s' "$REL" | grep -oE '[0-9]+\.[0-9]+' | head -1)
ok "published version is $VERSION ($REL)"

mkdir -p "$OUT"
DEST="$OUT/$LANG_NAME-$VERSION"
TARBALL="$OUT/$(basename "$REL")"

if [ -f "$TARBALL" ]; then
    ok "archive already here: $(basename "$TARBALL")"
else
    say "downloading $URL"
    curl -sSL --max-time 900 -o "$TARBALL.part" "$URL" \
        || die "download failed"
    mv "$TARBALL.part" "$TARBALL"
    ok "downloaded $(wc -c < "$TARBALL" | tr -d ' ') bytes"
fi

SHA=$(sha256sum "$TARBALL" 2>/dev/null | cut -d' ' -f1) \
    || SHA=$(shasum -a 256 "$TARBALL" 2>/dev/null | cut -d' ' -f1) || SHA=""

if [ -d "$DEST" ]; then
    ok "snapshot already unpacked: $DEST"
else
    say "unpacking into $DEST"
    mkdir -p "$DEST"
    case "$TARBALL" in
        *.tar.bz2) tar xjf "$TARBALL" -C "$DEST" --strip-components="${STRIP:-0}" ;;
        *.tar.gz)  tar xzf "$TARBALL" -C "$DEST" --strip-components="${STRIP:-0}" ;;
        *.zip)     command -v unzip >/dev/null 2>&1 || die "unzip is required"
                   unzip -q "$TARBALL" -d "$DEST" ;;
        *) die "unknown archive type: $TARBALL" ;;
    esac
fi

NFILES=$(find "$DEST" -type f 2>/dev/null | wc -l | tr -d ' ')
NTUT=0
[ -z "$TUT_DIR" ] || NTUT=$(find "$DEST/$TUT_DIR" -type f 2>/dev/null | wc -l | tr -d ' ')

cat > "$DEST/MANIFEST.json" <<MANIFEST
{
  "language": "$LANG_NAME",
  "title": "$TITLE",
  "version": "$VERSION",
  "source": "$URL",
  "index_url": "$INDEX_URL",
  "retrieved": "$(date +%Y-%m-%d)",
  "sha256": "$SHA",
  "licence": "$LICENCE",
  "licence_url": "$LICENCE_URL",
  "files": $NFILES,
  "tutorial_dir": "$TUT_DIR",
  "tutorial_files": $NTUT,
  "reference_dir": "$REF_DIR"
}
MANIFEST
ok "manifest written: $DEST/MANIFEST.json"
ok "$NFILES files, $NTUT of them in $TUT_DIR/"

echo
echo "Point a jichi config at it (docs/DOCS.md):"
echo
echo "  \"docs\": ["
echo "    { \"name\": \"$LANG_NAME-tutorial\", \"path\": \"$DEST/$TUT_DIR\" }"
[ -z "$REF_DIR" ] || \
echo "    , { \"name\": \"$LANG_NAME-reference\", \"path\": \"$DEST/$REF_DIR\" }"
echo "  ]"
echo
echo "The reference source is listed second and commented on purpose: it is"
echo "$NFILES files against $NTUT, and embedding it to answer a tutorial"
echo "question is the difference between a course you start and one you abandon."
