#!/bin/sh
# Grader for task 77: replace a file atomically -- a temp in the same directory,
# rename()d into place, 0600 when the content is a secret, nothing left behind.
#
# HOW A CRASH IS SIMULATED, honestly. The plan for this task named jichi's own
# FAULT=1 tier; that tier is a separate build of jichi, not of your code, so it
# cannot reach into savestate.c. Instead the grader compiles YOUR file with a
# header (-include) that renames the libc calls -- fopen, open, fwrite, write,
# fclose, rename, remove, unlink, fsync, mkstemp -- to hooks. The hooks call the
# real functions, record which path you opened for writing, and, when the probe
# says so, make the FIRST write fail the way a full disk or a pulled plug would
# make it fail. That is coarser than killing the process mid-write (your code
# still gets to run its error path), and it is exactly what a save must survive:
# an interrupted write, with the old file still readable afterwards.
#
# What the grader cannot see, and says so: durability. fsync() is hooked only so
# that it compiles; whether the bytes reach the medium is not observable here.
cd "$(dirname "$0")" || exit 1
trap 'rm -rf saveprobe _probe.c _hooks.h _hooks.c _ss.o _fx _err.txt' EXIT
cc --version >/dev/null 2>&1 || { echo "CANNOT RUN: a C compiler (cc) is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 77; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200112L -fsanitize=address -fno-sanitize-recover=all"

rm -rf _fx && mkdir _fx || { echo "FAIL: cannot create the fixture directory"; exit 1; }

# The hooks. Included before savestate.c's own includes, so the system headers
# are already in and the renames apply only to the calls in YOUR translation
# unit. Object-like macros, so a call like open(p, f, m) becomes
# probe_open(p, f, m) without C99's variadic macros.
cat > _hooks.h <<'HDR'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
FILE  *probe_fopen(const char *path, const char *mode);
int    probe_open(const char *path, int flags, ...);
size_t probe_fwrite(const void *p, size_t sz, size_t n, FILE *f);
long   probe_write(int fd, const void *buf, size_t n);
int    probe_fclose(FILE *f);
int    probe_rename(const char *a, const char *b);
int    probe_remove(const char *p);
int    probe_unlink(const char *p);
int    probe_fsync(int fd);
int    probe_mkstemp(char *tmpl);
#define fopen   probe_fopen
#define open    probe_open
#define fwrite  probe_fwrite
#define write   probe_write
#define fclose  probe_fclose
#define rename  probe_rename
#define remove  probe_remove
#define unlink  probe_unlink
#define fsync   probe_fsync
#define mkstemp probe_mkstemp
HDR

cat > _hooks.c <<'HKS'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Set by the probe before each call under test. */
const char *probe_target = "";     /* the path state_save was asked to replace */
int         probe_fail_write = 0;  /* make the next write fail                 */
/* Recorded by the hooks. */
int         probe_inplace = 0;     /* the TARGET itself was opened for writing */
int         probe_wrong_dir = 0;   /* a temp was opened outside target's dir    */
int         probe_nwrites = 0;     /* write-opens seen (any path)               */

static void note_write_open(const char *path)
{
    const char *ts = strrchr(probe_target, '/');
    const char *ps = strrchr(path, '/');
    size_t tl = ts ? (size_t)(ts - probe_target) : 0;
    size_t pl = ps ? (size_t)(ps - path) : 0;
    probe_nwrites++;
    if (strcmp(path, probe_target) == 0) {
        probe_inplace = 1;
        return;
    }
    if (tl != pl || strncmp(path, probe_target, tl) != 0) {
        probe_wrong_dir = 1;
    }
}

FILE *probe_fopen(const char *path, const char *mode)
{
    if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')) {
        note_write_open(path);
    }
    return fopen(path, mode);
}

int probe_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if ((flags & O_WRONLY) || (flags & O_RDWR)) {
        note_write_open(path);
    }
    return open(path, flags, mode);
}

int probe_mkstemp(char *tmpl)
{
    note_write_open(tmpl);
    return mkstemp(tmpl);
}

size_t probe_fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
    if (probe_fail_write) {
        probe_fail_write = 0;
        errno = EIO;
        return 0;
    }
    return fwrite(p, sz, n, f);
}

long probe_write(int fd, const void *buf, size_t n)
{
    if (probe_fail_write) {
        probe_fail_write = 0;
        errno = EIO;
        return -1;
    }
    return (long)write(fd, buf, n);
}

int probe_fclose(FILE *f)               { return fclose(f); }
int probe_rename(const char *a, const char *b) { return rename(a, b); }
int probe_remove(const char *p)         { return remove(p); }
int probe_unlink(const char *p)         { return unlink(p); }
int probe_fsync(int fd)                 { return fsync(fd); }
HKS

cat > _probe.c <<'PRB'
#include "savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

extern const char *probe_target;
extern int probe_fail_write, probe_inplace, probe_wrong_dir, probe_nwrites;

static int read_all(const char *path, char *buf, size_t cap, size_t *got)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    *got = fread(buf, 1, cap, f);
    fclose(f);
    return 0;
}

/* How many entries the directory holds besides . and .. -- a leftover temp
   file is a failure the user pays for on every save. */
static int dir_entries(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (d == NULL) return -1;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) n++;
    }
    closedir(d);
    return n;
}

int main(int argc, char **argv)
{
    const char *dir, *path;
    static const char OLD[] = "version=1\nowner=alex\n";
    static const char NEW[] = "version=2\nowner=alex\nmode=strict\n";
    static const char SEC[] = "token=s3cr3t-do-not-share\n";
    static const char BAD[] = "version=3 -- this write is going to fail\n";
    char buf[512];
    size_t got;
    struct stat st;
    int rc;

    if (argc != 3) return 99;
    dir = argv[1]; path = argv[2];
    probe_target = path;

    /* 1. an ordinary save replaces the contents, completely, and leaves the
          directory with exactly one file in it. */
    rc = state_save(path, NEW, sizeof NEW - 1, 0);
    if (rc != SAVE_OK)                       return 21;
    if (read_all(path, buf, sizeof buf, &got) != 0) return 22;
    if (got != sizeof NEW - 1 || memcmp(buf, NEW, got) != 0) return 22;
    if (dir_entries(dir) != 1)               return 23;
    if (probe_inplace)                       return 24;
    if (probe_wrong_dir)                     return 25;
    if (probe_nwrites == 0)                  return 26;

    /* 2. a secret is created 0600 -- and that mode carries to the target. */
    rc = state_save(path, SEC, sizeof SEC - 1, 1);
    if (rc != SAVE_OK)                       return 27;
    if (stat(path, &st) != 0)                return 27;
    if ((st.st_mode & 0777) != 0600)         return 28;

    /* 3. the write fails part-way: the OLD contents must survive, byte for
          byte, and nothing may be left behind. */
    probe_fail_write = 1;
    rc = state_save(path, BAD, sizeof BAD - 1, 1);
    probe_fail_write = 0;
    if (rc == SAVE_OK)                       return 29;
    if (read_all(path, buf, sizeof buf, &got) != 0) return 30;
    if (got != sizeof SEC - 1 || memcmp(buf, SEC, got) != 0) return 30;
    if (dir_entries(dir) != 1)               return 31;
    if (probe_inplace)                       return 24;

    (void)OLD;
    return 0;
}
PRB

# _fx holds exactly one file, the target: the probe counts its entries after
# every save to catch a temporary left behind, so nothing else may be put there.
printf 'version=1\nowner=alex\n' > _fx/state.txt

$CC $SAN -include _hooks.h -c savestate.c -o _ss.o 2>/dev/null || {
    echo "FAIL: savestate.c does not compile as C89 under -Wall -Wextra (with the grader's hooks included first)"; exit 1; }
$CC $SAN -o saveprobe _probe.c _hooks.c _ss.o 2>/dev/null || {
    echo "FAIL: the probe does not link against savestate.c (grader bug, or a changed signature)"; exit 1; }

./saveprobe _fx _fx/state.txt 2>_err.txt
rc=$?

if grep -qE "(AddressSanitizer|LeakSanitizer|runtime error)" _err.txt; then
    echo "FAIL: the sanitizer stopped the run. Its own summary:"
    grep -E "ERROR:|SUMMARY:" _err.txt | sed 's/^/    /' | head -4
    sed 's/^/    /' _err.txt | head -8
    exit 1
fi

case $rc in
  0)  : ;;
  21) echo "FAIL: an ordinary save did not return SAVE_OK"; exit 1 ;;
  22) echo "FAIL: after a save the file does not hold exactly the new contents"; exit 1 ;;
  23) echo "FAIL: after a successful save something else is left in the directory -- the temporary was not renamed or removed"; exit 1 ;;
  24) echo "FAIL: the target itself was opened for writing -- fopen(path, \"w\") truncates the old contents before the new ones exist; write a temporary and rename() it into place"; exit 1 ;;
  25) echo "FAIL: the temporary was created outside the target's directory -- rename() is atomic only within one filesystem, so the temp must live beside the file it replaces"; exit 1 ;;
  26) echo "FAIL: nothing was ever opened for writing, yet the save reported success"; exit 1 ;;
  27) echo "FAIL: saving a secret did not succeed"; exit 1 ;;
  28) echo "FAIL: a secret was not saved with mode 0600 -- create it with that mode (open(..., O_CREAT|O_EXCL, S_IRUSR|S_IWUSR)); do not chmod it afterwards"; exit 1 ;;
  29) echo "FAIL: the write was made to fail and state_save still reported SAVE_OK -- check fwrite's byte count AND fclose's return value"; exit 1 ;;
  30) echo "FAIL: the write failed and the OLD contents are gone -- this is the data loss the task exists to prevent"; exit 1 ;;
  31) echo "FAIL: the write failed and a temporary file was left behind -- remove it on every error path"; exit 1 ;;
  99) echo "FAIL: probe called wrongly (grader bug, not yours)"; exit 1 ;;
  *)  echo "FAIL: the probe exited $rc, which is not one of its own codes -- it was killed."
      sed 's/^/    /' _err.txt | head -8; exit 1 ;;
esac

echo "PASS: the old file survives an interrupted save, the secret is 0600 from creation, and nothing is left behind"
exit 0
