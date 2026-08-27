#!/bin/sh
# runs as root in a PRISTINE Debian 12 guest: what does it actually take to get
# jichi into a JupyterHub single-user image, when there is no package to install?
#
# jichi is released as SOURCE. Whoever deploys it builds it. This measures that
# path from nothing: what is already present, what must be added, how long it
# takes, how big the result is, and -- the question a container image actually
# turns on -- whether the BUILD dependencies can then be REMOVED.
set -eu
export DEBIAN_FRONTEND=noninteractive

echo "=== what a stock Debian 12 cloud image already has ==="
for t in cc gcc make git curl pkg-config; do
    printf '%-12s ' "$t"; command -v "$t" 2>/dev/null || echo "ABSENT"
done
printf 'curl headers  '; test -f /usr/include/curl/curl.h && echo present || echo ABSENT
echo

echo "=== installing the build dependencies ==="
t0=$(date +%s)
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq gcc make libcurl4-openssl-dev >/tmp/apt.log 2>&1
t1=$(date +%s)
echo "apt took $((t1 - t0))s"
dpkg-query -W -f='${Package} ${Version}\n' gcc make libcurl4-openssl-dev 2>/dev/null || true
echo

echo "=== building ==="
cd /tmp && rm -rf build && mkdir build && tar -xf /tmp/jichi-src.tar -C build
cd /tmp/build/jichi
t0=$(date +%s)
make -s jichi >/tmp/build.log 2>&1 || { echo "BUILD FAILED"; tail -20 /tmp/build.log; exit 1; }
t1=$(date +%s)
echo "build took $((t1 - t0))s on $(nproc) cpu"
ls -l jichi | awk '{print "binary size: " $5 " bytes"}'
# `wc -c <` not `stat -c %s`: BSD stat spells it -f with different verbs, and
# posix_utils_lint.sh rejects the GNU form (rightly -- this tree has FreeBSD and
# OpenBSD rows). `wc -c` is POSIX and needs no format string.
cp jichi /tmp/j.stripped && strip /tmp/j.stripped
size_stripped=$(wc -c < /tmp/j.stripped | tr -d ' ')
echo "stripped:    $size_stripped bytes"
install -m 0755 jichi /usr/local/bin/jichi
echo "version:     $(/usr/local/bin/jichi --version)"
echo

echo "=== THE CONTAINER QUESTION: remove the build deps, does it still run? ==="
apt-get remove -y -qq gcc make libcurl4-openssl-dev >/tmp/rm.log 2>&1 || true
apt-get autoremove -y -qq >>/tmp/rm.log 2>&1 || true
printf 'gcc after removal:  '; command -v gcc 2>/dev/null || echo ABSENT
printf 'curl headers:       '; test -f /usr/include/curl/curl.h && echo present || echo ABSENT
printf 'libcurl runtime:    '; dpkg-query -W -f='${Package}\n' libcurl4 2>/dev/null || echo ABSENT
echo "--- ldd ---"
ldd /usr/local/bin/jichi 2>&1 | sed 's/^/  /'
echo "--- does it still run? ---"
/usr/local/bin/jichi --version 2>&1 && echo "RUNS_AFTER_REMOVAL" || echo "BROKEN_AFTER_REMOVAL"
echo "--- and does it still see libcurl? ---"
/usr/local/bin/jichi doctor 2>&1 | grep -iE "libcurl|curl" | head -3 || true
echo "BUILD_MEASURE_DONE"
