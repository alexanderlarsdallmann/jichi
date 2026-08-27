#!/bin/sh
# minimal-curl.sh - build the minimal single-TLS-backend libcurl that
# docs/LOW_MEMORY.md's <=64 MB tier RECOMMENDS but that nobody had ever built.
#
# WHY THIS EXISTS. That tier says: "Build a minimal, single-TLS-backend libcurl
# and link jichi statically against musl, with -Os and strip." M403 measured the
# two ENDS of that recipe and could not measure the middle:
#
#   default build, system libcurl+TLS  ->  1,654 KB on disk, 32 shared libs,
#                                          8.6 MB peak RSS on --version
#   static musl, SIZE=1, NO libcurl    ->  1,051 KB, 0 libs, 0.5 MB  -- but it
#                                          cannot make a model call at all
#
# so the recipe's real footprint was known only to lie "between 0.5 and 8.6 MB,
# and until somebody builds one that range is the whole of what is known."
# This script is that somebody. It builds the dependency, not jichi -- which is
# exactly why the item sat deferred: `zig cc` bundles libcs, not dependency
# trees (docs/DEFERRED.md, "Open -- RAM tiers and libcs never measured").
#
# NOT a gate, not in `make`. It downloads and compiles a third-party dependency,
# which no build of jichi may ever require: jichi vendors no third-party source
# and links exactly one library. It lives beside tests/measure/ram_floor.sh for
# the same reason -- its output is a measurement of THIS toolchain, not a
# property of jichi.
#
# The flag list is not invented here. It is copied from the recipe printed in
# docs/LOW_MEMORY.md so that what gets measured is what the page tells a reader
# to build; if the two ever diverge, the page is wrong or this script is.
#
# That comparison IS automated now (M445): tests/smoke/mincurl_recipe_lint.sh extracts
# the --disable-*/--without-* set from the `set --` list below and from the page's fenced
# recipe, and fails in BOTH directions. It deliberately ignores --prefix, --host and the
# --with-<tls> choice, which legitimately differ between a page teaching one build and a
# script parameterised over three rungs. The sentence above was, for a while, a promise
# that the lint existed when it did not -- which is worse than no promise, because a
# reader who believes it stops checking by hand (M431).
#
# Usage:
#   scripts/minimal-curl.sh                      # openssl, glibc, into the
#                                                #   default prefix
#   scripts/minimal-curl.sh --tls mbedtls        # the cheaper-to-cross backend
#   scripts/minimal-curl.sh --dry-run            # print every step, touch nothing
#   scripts/minimal-curl.sh --prefix /tmp/mc     # somewhere else
#
# Then measure jichi against it -- the Makefile finds a libcurl through
# pkg-config alone, so no Makefile change is needed:
#
#   make clean
#   PKG_CONFIG_PATH=<prefix>/lib/pkgconfig make SIZE=1
#   ldd ./jichi | wc -l
#   /usr/bin/time -v ./jichi --version 2>&1 | grep 'Maximum resident'
#
# Env: MINCURL_DIR (default ~/.cache/jichi-mincurl), MINCURL_JOBS (default 8).
set -eu

VERSION=8.18.0
MBEDTLS_VERSION=3.6.2
TLS=openssl
MUSL=0
DRY=0
PREFIX=""

DIR="${MINCURL_DIR:-$HOME/.cache/jichi-mincurl}"
JOBS="${MINCURL_JOBS:-8}"

while [ $# -gt 0 ]; do
    case "$1" in
        --version) shift; VERSION="${1:?--version needs a value}" ;;
        --tls)     shift; TLS="${1:?--tls needs openssl or mbedtls}" ;;
        --musl)    MUSL=1 ;;
        --prefix)  shift; PREFIX="${1:?--prefix needs a path}" ;;
        --dry-run) DRY=1 ;;
        -h|--help) awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *) echo "minimal-curl: unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

case "$TLS" in
    openssl)  TLS_FLAG=--with-openssl ;;
    mbedtls)  TLS_FLAG=--with-mbedtls ;;
    *) echo "minimal-curl: --tls must be openssl or mbedtls (got '$TLS')" >&2; exit 2 ;;
esac

FLAVOUR=$TLS
[ "$MUSL" -eq 1 ] && FLAVOUR="$TLS-musl"
[ -n "$PREFIX" ] || PREFIX="$DIR/prefix-$FLAVOUR"
SRC="$DIR/curl-$VERSION"
TARBALL="$DIR/curl-$VERSION.tar.xz"
URL="https://curl.se/download/curl-$VERSION.tar.xz"

say() { echo "== $*"; }
run() { echo "+ $*"; [ "$DRY" -eq 1 ] || "$@"; }

# --- the musl cross-toolchain -----------------------------------------------
# WHY zig: musl-gcc is not installed and needs root to be; `zig cc` ships a musl
# libc and headers and is already the tool M403 used for the curl-free end of
# this recipe, so using it here keeps both ends of the comparison on one
# toolchain. What zig does NOT ship is a dependency tree -- which is the whole
# reason docs/DEFERRED.md parked this item ("zig cc bundles libcs, not
# dependency trees"), and the reason the TLS library has to be built too.
#
# WHY mbedTLS rather than OpenSSL for the musl rung: OpenSSL's Configure wants a
# perl-driven target triplet and its own assembler conventions, which is an
# afternoon of yak-shaving per target; mbedTLS is plain CMake and cross-builds
# with a CC override. For a <=64 MB target mbedTLS is also the better-matched
# library, so this is not merely the cheaper path. The glibc rung above stays on
# OpenSSL, because that is what docs/LOW_MEMORY.md's recipe literally says and
# the system already has it -- so the two rungs answer two different questions
# and must not be quoted as one number.
# mbedTLS is not in a default search path when we build it ourselves, so the
# musl rung's flag must carry the prefix. Recomputed here rather than at option
# parse time because $PREFIX is only known now -- and appending a SECOND
# --with-mbedtls instead would leave configure taking whichever came last, which
# works by luck and reads as a mistake.
if [ "$MUSL" -eq 1 ] && [ "$TLS" = mbedtls ]; then
    TLS_FLAG="--with-mbedtls=$PREFIX"
fi

if [ "$MUSL" -eq 1 ]; then
    command -v zig >/dev/null 2>&1 || {
        echo "minimal-curl: --musl needs zig on PATH (it supplies the musl libc)" >&2
        exit 2; }
    # zig is a MULTI-CALL binary: the tool name is its first argument. Build
    # systems pass the archiver its flags positionally, so handing CMake
    # `CMAKE_AR=/path/to/zig` produces `zig qc libfoo.a ...` and zig answers
    # "unknown command: qc" (measured 2026-08-13, mbedTLS's p256-m/everest
    # sub-libraries). Autoconf has the same problem via $AR. One-line wrapper
    # scripts are the fix that works for both, so generate them rather than
    # trying to smuggle a two-word command through a variable that means one.
    TC="$DIR/toolchain"
    mkdir -p "$TC"
    # -fno-sanitize=undefined: zig cc turns UBSan on by default, so the
    # dependency's objects reference __ubsan_handle_* and every link fails with
    # "undefined symbol: __ubsan_handle_type_mismatch_v1" from libmbedcrypto.a
    # (measured 2026-08-13 in curl's own conftest). We are cross-building a
    # third-party library for a size measurement, not instrumenting it.
    printf '#!/bin/sh\nexec zig cc -target x86_64-linux-musl -fno-sanitize=undefined "$@"\n' > "$TC/zcc"
    printf '#!/bin/sh\nexec zig ar "$@"\n'                           > "$TC/zar"
    printf '#!/bin/sh\nexec zig ranlib "$@"\n'                       > "$TC/zranlib"
    chmod +x "$TC/zcc" "$TC/zar" "$TC/zranlib"
    MUSL_CC="$TC/zcc"
    MUSL_AR="$TC/zar"
    MUSL_RANLIB="$TC/zranlib"
fi

say "minimal libcurl $VERSION, TLS backend: $TLS$([ "$MUSL" -eq 1 ] && echo ', static musl')"
echo "   source : $SRC"
echo "   prefix : $PREFIX"
echo "   jobs   : $JOBS"
[ "$MUSL" -eq 1 ] && echo "   cc     : $MUSL_CC"
echo

# ------------------------------------------------------------------ 1. sources
run mkdir -p "$DIR"
if [ ! -f "$TARBALL" ]; then
    say "download $URL"
    run curl -fL --progress-bar -o "$TARBALL.part" "$URL"
    run mv "$TARBALL.part" "$TARBALL"
else
    say "tarball already staged: $TARBALL"
fi

# Unpack fresh every time: a configure cache from a different backend is the
# kind of stale state that makes a measurement quietly wrong.
run rm -rf "$SRC"
run tar -C "$DIR" -xf "$TARBALL"

# ------------------------------------------------- 1b. the TLS library, if musl
# Only for the musl rung: the glibc rung links the system OpenSSL, which is
# already installed and is what the documented recipe names.
if [ "$MUSL" -eq 1 ] && [ "$TLS" = mbedtls ]; then
    MB_SRC="$DIR/mbedtls-$MBEDTLS_VERSION"
    MB_TAR="$DIR/mbedtls-$MBEDTLS_VERSION.tar.bz2"
    MB_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
    if [ ! -f "$MB_TAR" ]; then
        say "download mbedTLS $MBEDTLS_VERSION"
        run curl -fL --progress-bar -o "$MB_TAR.part" "$MB_URL"
        run mv "$MB_TAR.part" "$MB_TAR"
    else
        say "mbedTLS tarball already staged"
    fi
    run rm -rf "$MB_SRC"
    run mkdir -p "$MB_SRC"
    if [ "$DRY" -eq 1 ]; then
        echo "+ tar -C $MB_SRC --strip-components=1 -xf $MB_TAR"
        echo "+ cmake -DCMAKE_C_COMPILER=... -DENABLE_TESTING=Off -DUSE_SHARED_MBEDTLS_LIBRARY=Off"
        echo "+ cmake --build && cmake --install -> $PREFIX"
    else
        tar -C "$MB_SRC" --strip-components=1 -xf "$MB_TAR" || exit 1
        # MBEDTLS_FATAL_WARNINGS=Off is mbedTLS's own knob and it is needed, not
        # cosmetic: zig 0.16 bundles a clang newer than mbedTLS 3.6.2, which
        # turns on -Wunterminated-string-initialization; the TLS 1.3 label
        # arrays are deliberately exact-sized and trip it, and mbedTLS compiles
        # with -Werror by default. Third-party code meeting a newer compiler --
        # nothing to do with jichi's own -Wall -Wextra -pedantic discipline.
        say "build mbedTLS for musl (static only)"
        ( cd "$MB_SRC" && cmake -S . -B build \
            -DCMAKE_C_COMPILER="$MUSL_CC" \
            -DCMAKE_AR="$MUSL_AR" \
            -DCMAKE_RANLIB="$MUSL_RANLIB" \
            -DCMAKE_SYSTEM_NAME=Linux \
            -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DENABLE_TESTING=Off -DENABLE_PROGRAMS=Off \
            -DMBEDTLS_FATAL_WARNINGS=Off \
            -DUSE_SHARED_MBEDTLS_LIBRARY=Off \
            -DUSE_STATIC_MBEDTLS_LIBRARY=On \
            >"$DIR/mbedtls-cmake.log" 2>&1 \
          && cmake --build build -j"$JOBS" >>"$DIR/mbedtls-cmake.log" 2>&1 \
          && cmake --install build >>"$DIR/mbedtls-cmake.log" 2>&1 ) || {
            echo "minimal-curl: mbedTLS build FAILED for musl." >&2
            echo "  tail of $DIR/mbedtls-cmake.log:" >&2
            tail -20 "$DIR/mbedtls-cmake.log" >&2
            echo "  HONEST LABEL if this cannot be fixed cheaply: the musl rung of" >&2
            echo "  the <=64 MB recipe remains UNBUILT, and docs/LOW_MEMORY.md must" >&2
            echo "  keep saying so. The glibc rung (no --musl) is unaffected." >&2
            exit 1
        }
        echo "   mbedTLS installed: $(ls "$PREFIX/lib/"libmbed*.a 2>/dev/null | tr '\n' ' ')"
    fi
fi

# ---------------------------------------------------------------- 2. configure
# THE RECIPE, verbatim from docs/LOW_MEMORY.md "Build-time footprint reduction".
# Every --disable/--without below removes a library that shows up in a stock
# build's `ldd`; that is the entire point -- the resident-set floor is the TLS
# and auth chain, not jichi's own code.
set -- \
    "$TLS_FLAG" \
    --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
    --disable-telnet --disable-tftp --disable-pop3 --disable-imap \
    --disable-smtp --disable-gopher --disable-mqtt \
    --without-librtmp --without-libssh2 --without-brotli --without-zstd \
    --without-libpsl --without-libidn2 --without-nghttp2 --without-gssapi \
    --prefix="$PREFIX"

# The musl rung adds: a cross `--host` (so configure stops trying to RUN its test
# programs), static-only, and the mbedTLS prefix. `--with-mbedtls=$PREFIX` points
# at what step 1b just installed.
if [ "$MUSL" -eq 1 ]; then
    set -- "$@" --host=x86_64-linux-musl --disable-shared --enable-static
fi

say "configure"
if [ "$DRY" -eq 1 ]; then
    echo "+ ( cd $SRC && ./configure $* )"
else
    if [ "$MUSL" -eq 1 ]; then
        CC="$MUSL_CC"; AR="$MUSL_AR"; RANLIB="$MUSL_RANLIB"
        export CC AR RANLIB
    fi
    ( cd "$SRC" && ./configure "$@" >"$DIR/configure.log" 2>&1 ) || {
        echo "minimal-curl: configure FAILED -- tail of $DIR/configure.log:" >&2
        tail -25 "$DIR/configure.log" >&2
        exit 1
    }
fi

# ------------------------------------------------------------ 3. build+install
say "build (make -j$JOBS) and install"
if [ "$DRY" -eq 1 ]; then
    echo "+ make -C $SRC -j$JOBS && make -C $SRC install"
else
    make -C "$SRC" -j"$JOBS" >"$DIR/build.log" 2>&1 || {
        echo "minimal-curl: build FAILED -- tail of $DIR/build.log:" >&2
        tail -25 "$DIR/build.log" >&2
        exit 1
    }
    make -C "$SRC" install >>"$DIR/build.log" 2>&1 || {
        echo "minimal-curl: install FAILED -- see $DIR/build.log" >&2
        exit 1
    }
fi

# ------------------------------------------------------------------ 4. the row
# Report what was actually produced, so the number in the docs can be traced to
# a build rather than to a claim. `curl-config --configure` echoes the flags the
# library was really built with -- the honest record of the recipe.
say "result"
if [ "$DRY" -eq 1 ]; then
    echo "+ $PREFIX/bin/curl-config --version --configure"
    exit 0
fi

"$PREFIX/bin/curl-config" --version
echo
echo "protocols: $("$PREFIX/bin/curl-config" --protocols | tr '\n' ' ')"
echo "features : $("$PREFIX/bin/curl-config" --feature | tr '\n' ' ')"
echo "libcurl  : $(ls -l "$PREFIX/lib/libcurl.so.4."* 2>/dev/null | awk '{print $5, $9}')"
echo
echo "measure jichi against it with:"
echo "  make clean && PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig make SIZE=1"
echo "  ldd ./jichi | wc -l"
echo "  /usr/bin/time -v ./jichi --version 2>&1 | grep 'Maximum resident'"
