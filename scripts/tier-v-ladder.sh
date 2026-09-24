#!/bin/sh
# tier-v-ladder.sh - the userland floor, measured: build jichi and run its unit
# suite inside progressively older Linux distributions, one container each.
#
# WHY. docs/INSTALL.md named a floor -- libcurl 7.19.4, glibc 2.12, "CentOS 6 and
# Debian 7, exactly at the line" -- from upstream release dates, and nothing had
# ever built there. The first run of this ladder (2026-09-24) found that neither
# did: src/net/jc_http.c used three libcurl identifiers newer than the floor
# bare, and on glibc 2.7 the snprintf probe asked with fewer feature macros than
# the build uses, so the build took a C89 formatter that formats wrongly. The
# fixes are in the tree; this script is how the floor is re-measured instead of
# re-asserted.
#
# WHAT IT MEASURES, AND WHAT IT DOES NOT. Each rung is a distribution's own gcc,
# glibc and libcurl, installed from its archive, building `git archive REV` and
# running `make test` there. A container shares the host's kernel, so this is
# the USERLAND floor only; the kernel floor needs a guest kernel (tier-v-vm.sh's
# shape). The smoke tier is not run: it wants the tree's C helpers and a POSIX
# userland per rung, which is the VM rows' job.
#
# Usage:
#   scripts/tier-v-ladder.sh                  # every rung, REV = HEAD
#   scripts/tier-v-ladder.sh --rev M736       # a named commit
#   scripts/tier-v-ladder.sh --only centos:6  # one rung (repeatable)
#   scripts/tier-v-ladder.sh --dry-run        # print the rungs, run nothing
#
# Env: TIER_V_DIR (default ~/.cache/jichi-tier-v): results go to
#      $TIER_V_DIR/ladder-<stamp>/, never into the tree. TIER_V_ENGINE (podman).
#
# Wants: podman (or docker) that can pull the images and reach the distributions'
# archives. The images are the Debian EOL images and CentOS's own; CentOS 5 is
# listed and expected to fail to provision (its yum cannot reach the vault).
#
# Exit: 0 when the ladder ran (each rung's result is data, in results.txt),
#       2 on a usage error or a missing engine.
set -u

die() { echo "tier-v-ladder: $*" >&2; exit 2; }

REV=HEAD
ONLY=""
DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rev) REV=${2:?}; shift 2 ;;
        --only) ONLY="$ONLY $2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *) die "unknown argument: $1 (see --help)" ;;
    esac
done

REPO=$(cd "$(dirname "$0")/.." && pwd)
ENG="${TIER_V_ENGINE:-podman}"
DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}/ladder-$(date +%Y%m%d-%H%M%S)"

DEB='apt-get -o Acquire::Check-Valid-Until=false update -qq >/dev/null 2>&1; DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --allow-unauthenticated gcc make libcurl4-openssl-dev >/dev/null 2>&1 || DEBIAN_FRONTEND=noninteractive apt-get install -y --force-yes gcc make libcurl3-openssl-dev >/dev/null 2>&1'
VAULT="for f in /etc/yum.repos.d/CentOS-*.repo; do sed -e 's/^mirrorlist/#mirrorlist/' -e 's|^#baseurl=http://mirror.centos.org/centos/\$releasever|baseurl=http://vault.centos.org/VER|' -e 's|^#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|' \"\$f\" > \"\$f.new\" && mv \"\$f.new\" \"\$f\"; done; yum install -y -q gcc make PKG >/dev/null 2>&1"

# image | prep -- newest first, so the first failure found is the highest one.
RUNGS="docker.io/debian/eol:stretch|$DEB
docker.io/debian/eol:jessie|$DEB
docker.io/library/centos:7|$(echo "$VAULT" | sed 's/VER/7.9.2009/; s/PKG/libcurl-devel/')
docker.io/debian/eol:wheezy|$DEB
docker.io/library/centos:6|$(echo "$VAULT" | sed 's/VER/6.10/; s/PKG/libcurl-devel/')
docker.io/debian/eol:squeeze|$DEB
docker.io/debian/eol:lenny|$DEB
docker.io/library/centos:5|$(echo "$VAULT" | sed 's/VER/5.11/; s/PKG/curl-devel/')
docker.io/debian/eol:etch|$DEB"

if [ "$DRY" = 1 ]; then
    printf '%s\n' "$RUNGS" | while IFS='|' read -r img _p; do
        case "$ONLY" in ""|*" ${img##*/}"*|*" $img"*) echo "rung: $img" ;; esac
    done
    echo "tree: git archive $REV (from $REPO)"
    exit 0
fi
command -v "$ENG" >/dev/null 2>&1 || die "no $ENG on PATH"
git -C "$REPO" rev-parse --verify -q "$REV^{commit}" >/dev/null || die "no commit $REV"
mkdir -p "$DIR" || die "cannot create $DIR"
git -C "$REPO" archive --format=tar --prefix=jichi/ "$REV" > "$DIR/jichi.tar" || die "git archive failed"
R="$DIR/results.txt"
printf '# tier-v-ladder: %s (%s) on %s\n' "$REV" "$(git -C "$REPO" rev-parse --short "$REV")" "$(uname -sr)" > "$R"

printf '%s\n' "$RUNGS" | while IFS='|' read -r img prep; do
    case "$ONLY" in ""|*" ${img##*/}"*|*" $img"*) ;; *) continue ;; esac
    name=$(echo "$img" | tr ':/' '__')
    s=$(date +%s)
    timeout 1800 "$ENG" run --rm -v "$DIR:/l:Z" "$img" sh -c "
        $prep
        cd /tmp && tar xf /l/jichi.tar && cd jichi || exit 3
        echo \"GCC=\$(gcc --version 2>/dev/null | head -1)\"
        echo \"LIBC=\$(ldd --version 2>&1 | head -1)\"
        echo \"CURL=\$(curl-config --version 2>/dev/null || echo none)\"
        make CC=gcc -j4 -k > /l/$name.build.log 2>&1; echo BUILD_RC=\$?
        grep -E ': error:' /l/$name.build.log | sort -u | head -12
        make CC=gcc test > /l/$name.test.log 2>&1; echo TEST_RC=\$?
        grep -E 'checks, [0-9]+ failures' /l/$name.test.log | tail -1
        grep '  FAIL' /l/$name.test.log | head -12
    " > "$DIR/$name.log" 2>&1
    printf '%s\t%ss\t%s\t%s\t%s\t%s\t%s\t%s\n' "$img" "$(( $(date +%s) - s ))" \
        "$(sed -n 's/^GCC=//p' "$DIR/$name.log" | head -1)" \
        "$(sed -n 's/^LIBC=//p' "$DIR/$name.log" | head -1)" \
        "$(sed -n 's/^CURL=//p' "$DIR/$name.log" | head -1)" \
        "$(grep -m1 '^BUILD_RC=' "$DIR/$name.log")" "$(grep -m1 '^TEST_RC=' "$DIR/$name.log")" \
        "$(grep -E 'checks, [0-9]+ failures' "$DIR/$name.log" | tail -1)" >> "$R"
done
echo "tier-v-ladder: results in $R"
cat "$R"
exit 0
