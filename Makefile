# Makefile for jichi - a C89 rewrite of the Continue CLI.
#
# First-party code is compiled strict ANSI C89 (-std=c89 -pedantic). The
# src/json/cJSON.c is OURS (API-compatible name, not a vendored copy) and is
# pedantic-clean: it gets the same -std=c89 -pedantic as everything else. The
# exemption this line used to describe was removed at M171.
# POSIX-only prototypes are exposed via -D_POSIX_C_SOURCE.
#
# Feature detection (run once per invocation):
#   - JC_HAVE_VSNPRINTF: set when the libc provides C99 vsnprintf (almost
#     always true); jc_snprintf falls back to a hand-rolled formatter if not.
#   - HAVE_CURL: set when <curl/curl.h> is available. Networking milestones
#     (M2+) require it. Without it, the core + test suite still build.

CC      ?= cc

# The C dialect, PROBED rather than assumed (M459). jichi's own code is C89 and
# stays C89 -- what varies is whether the PLATFORM'S OWN HEADERS can be parsed
# in strict C89 mode. On Android they cannot: bionic's kernel UAPI headers use
# `inline`, which C89 has no such keyword for, so `-std=c89` fails to parse
# <netinet/in.h> -> linux/in.h -> asm/byteorder.h -> linux/swab.h before any
# jichi source is read. That is a hard syntax error, not a warning, so no
# -W flag can reach it.
#
# Measured on TWO independent toolchains before this probe was written: the
# Android NDK's clang 19 cross-compiling (M456) and Termux's clang 21 compiling
# natively on-device (M459) produce the identical error in the identical header.
# It is a property of the headers, not of either compiler.
#
# So: use the strict dialect wherever it works, and fall back to gnu89 -- C89
# SEMANTICS plus GNU extensions -- only where the headers demand it. jichi's own
# translation units still compile clean under -pedantic either way, so this
# relaxes nothing about jichi's conformance; it only lets the platform's headers
# be read. `make info` reports which dialect was chosen, because a silent
# fallback would be exactly the kind of unreported degradation M458 objected to.
#
# If $(CC) does not exist at all the probe also fails and gnu89 is chosen, but
# the build then fails loudly on the missing compiler regardless -- the dialect
# is not what breaks it.
#
# ...and that last sentence named the ONE way this can go wrong that does NOT
# fail loudly, which is exactly how it went wrong (M476). Every probe below used
# `-o $(PROBE_OUT)`, and on Cygwin the linker cannot write a PE image there:
#
#   $ printf 'int main(void){return 0;}' | cc -xc - -o $(PROBE_OUT)
#   ld: final link failed: file truncated
#
# The compiler was gcc 14.4.0 and worked perfectly; only the OUTPUT PATH was
# unusable. So all EIGHT probes answered "no" at once and jichi built a
# maximally degraded binary -- gnu89, no libcurl (networking off), the bundled
# fallback formatter, coarse time(), no warning flags, and NO hardening flags --
# reporting success the whole way. `make info` did print `gnu89`, honouring M458,
# but with the reason "this platform's headers are not C89-parseable", which is
# false and sends the reader to inspect headers. A probe that cannot distinguish
# "the feature is absent" from "I could not run the test" is not a probe.
#
# PROBE_OUT is therefore a real file. It is per-PID ($$$$ is the shell's PID, so
# `make -j` and nested makes cannot collide) and `make clean` removes the family.
# On Cygwin the compiler appends .exe, hence the glob in `clean`.
PROBE_OUT = .jc_probe.$$$$.out
STD_C89_OK := $(shell printf '\043include <netinet/in.h>\nint main(void){return 0;}\n' \
  | $(CC) -std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) >/dev/null 2>&1 \
  && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
#
# IS $(CC) A C++ DRIVER? (M636) -- asked before the C89 verdict is trusted.
# `make CC=g++` is a documented property check (docs/CPP_BUILD.md) and
# `make CC=clang++` was simply broken: clang++ REFUSES -std=c89 outright
# ("invalid argument '-std=c89' not allowed with 'C++'") where g++ merely warns,
# so the build died on the first file. Worse, the C89 probe above answered
# *yes* for g++ and *no* for clang++ and then blamed the platform --
# "this platform's headers are not C89-parseable" -- which is a false statement
# about the machine when the truth is a fact about the compiler. Its `-xc`
# forces C on a driver that will not see `-xc` during the real build.
#
# So this probe asks the question the BUILD asks: given a file named .c and no
# -x flag, does this driver compile it as C++? The body is C++-only (a member
# function inside a struct), so gcc and clang reject it and g++ and clang++
# accept it. Nothing here is inferred from the program's NAME: a driver called
# `cc` that is a C++ compiler is classified correctly, and a cross-driver spelled
# with a target prefix needs no pattern to match.
#
# It also settles a question the name would have got WRONG. `zig c++` is
# classified as a C driver, and measuring says that is correct: given a file
# named .c it compiles C, exactly as `zig cc` does (probe exit status 1 for
# both, against 0 for g++ and clang++). So `make CC="zig c++"` builds the
# ordinary C89 jichi -- it is not a third C++ build, and only
# `make cpp-check CXX="zig c++"`, which passes -x c++, reaches zig's C++
# front-end at all.
PROBE_SRC = .jc_probe.$$$$.c
CC_IS_CXX := $(shell printf 'struct S { S(){} };\nint main(){ S s; (void)s; return 0; }\n' \
  > $(PROBE_SRC) 2>/dev/null; \
  $(CC) -c $(PROBE_SRC) -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; \
  rm -f $(PROBE_SRC) $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(CC_IS_CXX),yes)
  # C++17 for the same reason cpp-check uses it: it is the dialect the property
  # is claimed in. -pedantic is dropped -- the tree is C89 read through a C++
  # front-end, and pedantic C++ has opinions about C idioms that are not defects
  # here (see docs/CPP_BUILD.md on the -Wwrite-strings residue, which stays
  # visible because it is real).
  #
  # -x c++ is EXPLICIT, not implied. clang++ compiles a .c input as C++ only
  # under a deprecation warning -- 180 of them, one per translation unit, which
  # is noise today and a broken build the release that removes the behaviour.
  # Naming the language costs nothing and says what is meant.
  STD    = -x c++ -std=c++17
  STD_DIALECT = c++17 (this driver compiles .c as C++; not the shipped build)
else ifeq ($(STD_C89_OK),yes)
  STD    = -std=c89 -pedantic
  STD_DIALECT = c89 (strict)
else
  STD    = -std=gnu89 -pedantic
  STD_DIALECT = gnu89 (this platform's headers are not C89-parseable)
endif

# POSIX_UNGATE (M834, def-265): DJGPP's headers put ALL of POSIX inside
# `#ifndef __STRICT_ANSI__`, and -std=c89 defines that macro, so under the dialect
# this tree is written in, every POSIX type vanishes: pid_t, struct stat, DIR,
# O_CREAT, ENOENT, SIGKILL. The DJGPP step 0 measured 14 of 16 failing files on it,
# and three PROBES misread for the same reason (vsnprintf, lstat, TIOCGWINSZ), which
# would have built jichi with the width-less fallback formatter.
# -U__STRICT_ANSI__ tells such headers to show POSIX while the compiler stays in
# -std=c89 -pedantic: the macro steers what headers declare, not what the compiler
# accepts. ASKED, not assumed -- added only where pid_t is hidden under the build's
# dialect and visible without the macro -- a header knob of the kind
# -D_XOPEN_SOURCE=600 is for MiNTLib's lstat and -D__EXTENSIONS__ for illumos'
# winsize. Every probe below carries it, so each asks under the flags the build uses.
POSIX_TYPES_OK := $(shell printf '\043include <sys/types.h>\npid_t jc_p;\nint main(void){return (int)jc_p;}\n' \
  | $(CC) $(STD) -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(POSIX_TYPES_OK),yes)
  POSIX_UNGATE_OK := $(shell printf '\043include <sys/types.h>\npid_t jc_p;\nint main(void){return (int)jc_p;}\n' \
    | $(CC) $(STD) -D_POSIX_C_SOURCE=200112L -U__STRICT_ANSI__ -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(POSIX_UNGATE_OK),yes)
    POSIX_UNGATE = -U__STRICT_ANSI__
  endif
endif
# The warning set (M472). -Wall -Wextra was the whole of it, on 97k lines that
# already hold zero warnings at -Werror -- which is exactly the tree that has
# earned the right to turn on more, because the marginal cost of a flag is zero
# when the baseline is clean.
#
# The first four matter most in C89 specifically. An omitted or old-style
# prototype is LEGAL there and silently disables argument checking, so a call
# with the wrong argument type compiles quietly; -Wstrict-prototypes,
# -Wmissing-prototypes and -Wold-style-definition close that. -Wvla and -Walloca
# are pure tripwires: neither construct is legal C89 at all, so they can only ever
# fire on a mistake, and CONTRIBUTING.md's rule against them stops being a
# convention someone has to remember.
#
# Each was MEASURED against the tree before being added, one at a time, and only
# the ones already at zero are here -- so this commit adds diagnostics, not work.
# Three candidates were measured and deliberately left out because they are not
# free, with their counts recorded so the next person does not have to re-measure:
#   -Wwrite-strings   21   string literals assigned to char* (a real audit)
#   -Wshadow           8   worth doing; each needs a rename judgement
#   -Wcast-qual      169   mostly const-stripping in the vec/json layers
# They are follow-on work, one flag per commit, because a batch that trips three
# flags at once is a batch that gets reverted.
# --- optional warning flags: probed per compiler (M475) ---------------------
# -Wlogical-op, -Wduplicated-cond and -Wjump-misses-init are GCC-only. clang
# does not implement them, and clang only WARNS about an unknown -W option --
# so it compiles fine right up until -Werror promotes that warning to an error.
# That is exactly how `make ci`'s stage 2 (WERROR=1 CC=clang) died while every
# gcc build stayed green: the flags were measured against gcc when added, and
# nothing in the gate compiled them under clang until the clang stage ran.
# Found on WSL2 / Ubuntu 24.04 with clang 18; NOT WSL-specific -- any box whose
# clang lacks these reproduces it.
#
# The probe MUST pass -Werror. Measured on clang 18.1.3 / gcc 13.3.0:
#   clang -Wlogical-op         -> exit 0   a naive probe reports "supported"
#   clang -Werror -Wlogical-op -> exit 1   correct
#   gcc   -Werror -Wlogical-op -> exit 0   kept, unchanged from before
# Controls, so the probe is neither too strict nor vacuous: -Wpointer-arith,
# -Wundef and -Wvla survive on BOTH compilers (no coverage lost), and a bogus
# -Wthis-does-not-exist is rejected by both (the probe can actually detect).
#
# The trivial program carries no '#' so the \043 dance the other probes need
# (make 3.82 strips '#' inside $(shell ...)) is not required here.
#
# -Walloca is probed too, for a different reason (M722): it is not GCC-only but
# GCC-7-and-later, and gcc rejects an unknown -W option as an ERROR, with or
# without -Werror. M472 put it on the mandatory list below, measured against
# gcc 13 and clang 18, which both know it -- so from M472 on, no gcc before 7
# could compile one file of this tree, including the CentOS 6 / Debian 7 floor
# docs/INSTALL.md promises and two recorded rows (CentOS 7's gcc 4.8.5, Debian
# 9's gcc 6). Every compiler in the gate knows the flag, so nothing here could
# see it. Found by the FreeMiNT step 0 cross-compile with gcc 4.6.4
# (docs/plans/2026-09-freemint-aranym.md section 8): all 314 compiles died in 1.1 s.
# portability_lint check 26 holds the mandatory list to the flags gcc 4.6.4
# compiled with; check 27 keeps -Walloca probed here, so the compilers that have
# it keep the tripwire.
#
# M833 (the DJGPP step 0, plans/2026-09-29-the-fork-less-rows.md section 5): a flag
# is accepted only when the compiler takes it SILENTLY -- exit 0 and no output on
# either stream. DJGPP's gcc answers -fPIE with "-fPIC ignored (not supported for
# DJGPP)" on STDOUT and exit 0, even under -Werror; this probe used to discard only
# stderr, so the sentence became part of HARDENFLAGS and every compile died on a
# shell syntax error at "(". A flag the compiler says it ignores is not a flag worth
# passing, so silence is the test. Every feature probe below discards the
# compiler's stdout for the same reason (only the probe's own `echo yes` may reach
# the captured value). tests/smoke/probe_quiet_lint.sh holds both, red-first.
cc_warn_ok = $(shell o=$$(printf 'int main(void){return 0;}\n' \
  | $(CC) -Werror $(1) -xc - -o $(PROBE_OUT) 2>&1) && [ -z "$$o" ] && echo $(1); rm -f $(PROBE_OUT) $(PROBE_OUT).exe)

# -Wvla is probed as well since the userland ladder (2026-09-24): gcc gained it in
# 4.3, and Debian 4's gcc 4.1.2 rejected all 171 compiles on it -- the M722 defect
# again, one flag older. Every compiler that knows it keeps the tripwire.
WARN_OPTIONAL := $(call cc_warn_ok,-Wlogical-op) \
                 $(call cc_warn_ok,-Wduplicated-cond) \
                 $(call cc_warn_ok,-Wjump-misses-init) \
                 $(call cc_warn_ok,-Walloca) \
                 $(call cc_warn_ok,-Wvla)

WARN     = -Wall -Wextra \
           -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
           -Wpointer-arith -Wredundant-decls -Wundef \
           $(WARN_OPTIONAL)
POSIX    = -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE)

# LARGE_FILE_DEFS (M1140, D-10): 32-bit builds lack large-file support by
# default -- a file over 2 GiB makes stat/fopen fail with EOVERFLOW (reported
# as "not found"), and readdir can end a listing silently on 64-bit-inode
# filesystems. -D_FILE_OFFSET_BITS=64 makes the wide offset types the default
# for the whole build. ASKED, not assumed: the probe compiles a trivial
# program under the build's own dialect with the macro and answers yes only if
# it compiles; a libc that refuses it adds nothing (an LP64 build already has
# a 64-bit off_t, and a 32-bit libc that refuses the macro is left as-is
# rather than broken). The probe carries the dialect and the POSIX ungate, so
# it asks under the flags the build uses (POSIX_UNGATE's precedent, M834).
#
# _TIME_BITS=64 (glibc's 64-bit time_t, the 2038 half) is deliberately NOT
# probed here: the D-10 index row pins only the file-offset width, and the
# time-typing problem is E-09's own open card -- whose point is that the flags
# alone will not fix the long-typed interface. Flipping time_t in the 32-bit
# build without E-09's teeth would be a partial fix of a different finding,
# carried into this milestone's diff.
LARGE_FILE_OK := $(shell printf '\043include <sys/types.h>\nint main(void){return 0;}\n' \
  | $(CC) $(STD) -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -D_FILE_OFFSET_BITS=64 -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(LARGE_FILE_OK),yes)
  LARGE_FILE_DEFS = -D_FILE_OFFSET_BITS=64
endif

# --- sanitizer runtime options (M475) ---------------------------------------
# ASan aborts on an allocation above its max supported size rather than letting
# malloc return NULL. test_sb_reserve_bounds (M472) deliberately requests
# SIZE_MAX/2+2 to prove jc_sb_reserve returns JC_ERR_OOM rather than spinning
# forever, so under -fsanitize=address that check is unreachable and the ENTIRE
# suite ABORTS -- `make ci` stage 3 (SAN=1 CC=clang test) never ran to
# completion. Measured on clang 18.1.3: without this the suite aborts; with it,
# 12418 checks / 0 failures.
#
# This restores malloc's documented contract for oversized requests and
# disables no memory-error detection (use-after-free, overflow and leak
# checking are untouched). ASAN_OPTIONS is ignored by non-sanitized binaries,
# so the prefix is unconditional and a normal `make test` is unaffected.
SAN_RUN_ENV = ASAN_OPTIONS=allocator_may_return_null=1
INCLUDE  = -Iinclude -Isrc/json

# --- feature probes --------------------------------------------------------
# Probe for C99 (v)snprintf via snprintf (a varargs proxy for the same feature
# set; the two are always co-present). _POSIX_C_SOURCE matches the real build so
# <stdio.h> declares snprintf. (An earlier probe called vsnprintf with a bogus
# va_list argument, which gcc tolerated but clang rejected -- silently selecting
# the width-less fallback formatter under clang.)
# Two escapes here, both load-bearing (found by the M264 V-tier rows):
#   \043 not '#' : GNU make 3.82 (CentOS/RHEL 7) strips '#' as a comment even
#     inside $(shell ...), which truncated the line and made the whole Makefile
#     fail to PARSE -- "unterminated call to function `shell'". printf's octal
#     escape produces the '#' after make is done looking. make 4.x is unaffected,
#     which is why this went unnoticed.
#   %%d not %d  : the shell's printf consumes a bare %d as a conversion spec, so
#     the probe used to compile snprintf(b,sizeof b,"0",1) -- still a valid link
#     test, so the verdict never changed, but not the program the source claims,
#     and busybox printf warns "invalid number ''" on every build.
# Same -Werror=implicit-function-declaration as the malloc_trim probe below, and
# for the same reason (M449): without it a link-only test answers "yes" for a
# function the build's own flags cannot see declared. snprintf is not currently
# known to hide on any target jichi builds for, so this changes no verdict today;
# it removes the failure MODE, which is the one that cost a whole uClibc row.
HAVE_VSNPRINTF := $(shell printf '\043include <stdio.h>\nint main(void){char b[8];return snprintf(b,sizeof b,"%%d",1)<0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
# ...and ask again at the X/Open level before concluding there is none. An older
# glibc declares snprintf under -std=c89 only for C99 or X/Open: measured on
# Debian 5's glibc 2.7 by the userland ladder (2026-09-24), where the POSIX form
# above failed, so the build took the C89 fallback formatter -- which has no
# width, precision or flags -- and the unit suite reported 81 failures, all of
# them output formatted wrong. The declaration was there all along. The same
# -D_XOPEN_SOURCE=600 the lstat probe below may add, and added once.
ifneq ($(HAVE_VSNPRINTF),yes)
  HAVE_VSNPRINTF_XOPEN := $(shell printf '\043include <stdio.h>\nint main(void){char b[8];return snprintf(b,sizeof b,"%%d",1)<0;}\n' \
    | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -D_XOPEN_SOURCE=600 -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_VSNPRINTF_XOPEN),yes)
    HAVE_VSNPRINTF := yes
    XOPEN_ADDED := yes
    STD += -D_XOPEN_SOURCE=600
  endif
endif
ifeq ($(HAVE_VSNPRINTF),yes)
  STD += -DJC_HAVE_VSNPRINTF
endif

# Probe for glibc's malloc_trim/mallopt (M218): jichi pins M_MMAP_THRESHOLD so
# large transient request bodies are mmap'd (returned to the OS on free instead
# of ratcheting the dynamic threshold and growing brk forever) and sweeps free
# heap back at turn boundaries. Probed, not #ifdef __GLIBC__: uClibc-ng defines
# glibc compat macros without necessarily providing the functions. No-op
# elsewhere.
#
# -Werror=implicit-function-declaration is LOAD-BEARING, and this comment used to
# claim the probe "compiles+links with the real flags so it cannot lie" -- which
# was false, and it lied on the first uClibc that ever ran it (M449). uClibc-ng
# declares malloc_trim only under `#ifdef __USE_GNU`; jichi compiles with
# _POSIX_C_SOURCE and no _GNU_SOURCE, so the declaration is HIDDEN while the
# symbol still LINKS. A probe without this flag therefore compiled an implicitly
# declared call, linked it, and answered "yes" -- after which every translation
# unit failed under the build's own -Werror. The question a capability probe must
# ask is "is it DECLARED under the flags I build with", not "does the symbol
# exist somewhere in libc".
HAVE_MALLOC_TRIM := $(shell printf '\043include <malloc.h>\nint main(void){mallopt(M_MMAP_THRESHOLD,131072);return malloc_trim(0)<0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_MALLOC_TRIM),yes)
  STD += -DJC_HAVE_MALLOC_TRIM
endif

# Probe for prctl(PR_SET_DUMPABLE) (M770). jichi makes itself non-dumpable at
# startup so a model-issued child -- running as the same user -- cannot read the
# API key jichi still holds in its own environment through /proc/<pid>/environ
# (the M130 scrub clears the CHILD's environment, not the parent's). Linux-only.
# Where it is absent the call does nothing and the build is unaffected, but the
# protection is not in force, and doctor warns. A wrong "yes" fails the compile
# loudly; a wrong "no" is silent here and shows only in doctor, which is why doctor
# reports the state. Same -Werror=implicit-function-declaration as the malloc_trim probe:
# the question is whether PR_SET_DUMPABLE is DECLARED under the flags jichi builds
# with (_POSIX_C_SOURCE, no _GNU_SOURCE), not whether the symbol exists somewhere.
# Verified on glibc 2026-09-25 that <sys/prctl.h> declares it under those flags.
HAVE_SET_DUMPABLE := $(shell printf '\043include <sys/prctl.h>\nint main(void){return prctl(PR_SET_DUMPABLE,1,0,0,0)<0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_SET_DUMPABLE),yes)
  STD += -DJC_HAVE_SET_DUMPABLE
endif

# HAVE_POSIX_SPAWN: the fork-free program-launch backend (M832). Where <spawn.h>
# provides posix_spawnp with the SETPGROUP/SETSIGDEF attributes and the dup2/open
# file actions -- the exact set jc_spawn.c's second backend uses -- the build
# carries it and JICHI_SPAWN_BACKEND=spawn selects it at run time; without the
# probe the env var is inert and jc_spawn() always forks. The probe asks for the
# whole set at once, so a libc with a partial spawn.h (early MiNTLib, some
# uclibc configs) answers no rather than failing the build later.
HAVE_POSIX_SPAWN := $(shell printf '\043include <spawn.h>\n\043include <signal.h>\n\043include <fcntl.h>\nextern char **environ;\nint main(void){posix_spawn_file_actions_t f;posix_spawnattr_t a;sigset_t s;pid_t p;char *v[2];v[0]=(char*)"true";v[1]=0;posix_spawn_file_actions_init(&f);posix_spawn_file_actions_adddup2(&f,1,2);posix_spawn_file_actions_addopen(&f,0,"/dev/null",O_RDONLY,0);posix_spawnattr_init(&a);posix_spawnattr_setflags(&a,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGDEF);posix_spawnattr_setpgroup(&a,0);sigemptyset(&s);sigaddset(&s,SIGPIPE);posix_spawnattr_setsigdefault(&a,&s);return posix_spawnp(&p,"true",&f,&a,v,environ)!=0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_POSIX_SPAWN),yes)
  STD += -DJC_HAVE_POSIX_SPAWN
endif

# JC_HAVE_FCHDIR (M1116, B-09): fchdir() is NOT declared under jichi's flags
# (-std=c89 -D_POSIX_C_SOURCE=200112L); on glibc it needs _DEFAULT_SOURCE or
# _XOPEN_SOURCE>=700, and some libcs have no fchdir at all. The portable pattern
# here for such a symbol is a LINK probe plus a local extern declaration (as the
# pid_t note at jc_spawn.c:22 does for MiNTLib). So this probe answers the LINK
# question, not the declaration question: it declares fchdir itself (exactly the
# way jc_spawn.c will) and calls it, and reports "yes" only if the symbol
# actually resolves in libc. A wrong "no" just keeps the path-based restore; a
# wrong "yes" would fail the build loudly (the extern is load-bearing, per
# -Werror=implicit-function-declaration elsewhere).
JC_HAVE_FCHDIR := $(shell printf 'extern int fchdir(int);\nint main(void){return fchdir(0)==0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(JC_HAVE_FCHDIR),yes)
  STD += -DJC_HAVE_FCHDIR
endif

# HAVE_PROC_TRACE_CTL: FreeBSD's analog of PR_SET_DUMPABLE (M771). Linux hides a
# process's /proc/<pid>/environ from same-user readers by clearing the dumpable
# flag; FreeBSD has no procfs by default, but a same-user process can still read
# another's environment through `sysctl kern.proc.env` / `ps -e`, gated by the
# same p_candebug() check that ptrace uses. procctl(PROC_TRACE_CTL,
# PROC_TRACE_CTL_DISABLE) makes the process untraceable, so that check fails for
# other same-user processes and the environment read returns EPERM. Like
# PR_SET_DUMPABLE it is inherited across fork and RESET on a non-setuid exec, so
# the children jichi starts (the model's shell) stay readable as before. The
# probe asks whether procctl and the constant are DECLARED under jichi's flags,
# not merely present -- <sys/procctl.h> needs no _GNU_SOURCE and is FreeBSD-only,
# so the probe answers "no" on every other platform, which is correct: this is
# the FreeBSD row. See src/util/jc_proc.c and docs/HARDENING.md.
# The probe MIRRORS jc_proc.c: __BSD_VISIBLE 1 on FreeBSD before the includes
# (the header uses u_int, hidden under our _POSIX_C_SOURCE), and <sys/types.h>
# first. Without both it answers "absent" on FreeBSD although the mechanism is
# there (measured 2026-09-25). On every non-FreeBSD platform <sys/procctl.h>
# does not exist, so the probe answers "no" -- correct, this is the FreeBSD row.
HAVE_PROC_TRACE_CTL := $(shell printf '\043if defined(__FreeBSD__)\n\043define __BSD_VISIBLE 1\n\043endif\n\043include <sys/types.h>\n\043include <sys/procctl.h>\nint main(void){int m=PROC_TRACE_CTL_DISABLE;return procctl(P_PID,0,PROC_TRACE_CTL,&m)<0;}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_PROC_TRACE_CTL),yes)
  STD += -DJC_HAVE_PROC_TRACE_CTL
endif

# Probe what clock_gettime needs to LINK (M326u). jc_now_millis guarded the call
# with `#if defined(CLOCK_MONOTONIC)` alone -- but that macro is declared in
# <time.h> on every glibc, INCLUDING the ones where the function itself lives in
# librt rather than libc (glibc < 2.17, Dec 2012 -- RHEL/CentOS 6, Debian 7).
# There the code compiled and the LINK failed with `undefined reference to
# clock_gettime`, because a compile-time guard cannot see a linker's symbol
# table. Nothing in LDLIBS ever asked for -lrt, so that was an unstated build
# floor of glibc 2.17 with no diagnostic and no mention in INSTALL.md.
#
# Three outcomes, probed in order:
#   links bare      -> nothing to do (glibc >= 2.17, musl, uClibc-ng, BSDs)
#   links with -lrt -> add it (glibc 2.12-2.16)
#   links neither   -> -DJC_NO_CLOCK_GETTIME; jc_now_millis takes the coarse
#                      time() fallback it already had
#
# The macro is NEGATIVE, unlike JC_HAVE_VSNPRINTF/JC_HAVE_MALLOC_TRIM, and that
# asymmetry is deliberate: a positive JC_HAVE_ gate would make anyone compiling
# by hand (without this Makefile) silently drop to one-second resolution for
# every latency measurement. Absence of the flag keeps today's behaviour; the
# flag is set only when the probe has PROVED the symbol unreachable.
CLOCK_PROBE := \043include <time.h>\nint main(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t);}\n
HAVE_CLOCK := $(shell printf '$(CLOCK_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_CLOCK),yes)
  HAVE_CLOCK_RT := $(shell printf '$(CLOCK_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) -lrt >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_CLOCK_RT),yes)
    RT_LIBS = -lrt
  else
    STD += -DJC_NO_CLOCK_GETTIME
  endif
endif

# HAVE_SYS_SELECT_H and HAVE_NANOSLEEP (M834, def-266): two POSIX facilities DJGPP
# lacks. POSIX 2001 gave select() its own header; DJGPP declares select() in
# <time.h> and fd_set in <sys/types.h>, the older placement, so include/jc_select.h
# includes <sys/select.h> only where this says it exists. nanosleep belongs to POSIX's
# optional Timers option, and DJGPP has neither it nor struct timespec: jc_sleep_ms
# sleeps with select() there. Both probes carry -Werror=implicit-function-declaration
# (the platform rule: ask whether it is DECLARED under the build's flags).
HAVE_SYS_SELECT_H := $(shell printf '\043include <sys/select.h>\nint main(void){return 0;}\n' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_SYS_SELECT_H),yes)
  STD += -DJC_NO_SYS_SELECT_H
endif
HAVE_NANOSLEEP := $(shell printf '\043include <time.h>\nint main(void){struct timespec t; t.tv_sec = 0; t.tv_nsec = 1; return nanosleep(&t, 0);}\n' \
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_NANOSLEEP),yes)
  STD += -DJC_NO_NANOSLEEP
endif

# Prefer pkg-config for curl flags; fall back to a bare -lcurl probe.
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_PC_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)
HAVE_CURL := $(shell printf '\043include <curl/curl.h>\nint main(void){return 0;}\n' \
  | $(CC) -xc - $(CURL_CFLAGS) -o $(PROBE_OUT) $(CURL_PC_LIBS) -lcurl >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_CURL),yes)
  ifeq ($(CURL_PC_LIBS),)
    CURL_LIBS = -lcurl
  else
    CURL_LIBS = $(CURL_PC_LIBS)
  endif
  STD += -DJC_HAVE_CURL $(CURL_CFLAGS)
endif

# Solaris/illumos hide every interface that is not in POSIX when
# _POSIX_C_SOURCE is defined -- and this build defines it globally -- unless
# __EXTENSIONS__ is defined alongside it. The first casualty is the terminal's
# column query: `struct winsize` and TIOCGWINSZ vanish, and src/tui/jc_term.c
# fails with "storage size of 'ws' isn't known" (M657c, the first illumos row).
# Probed rather than keyed on `uname`, because the probe asks the question the
# compiler will ask; on glibc, musl and the BSDs the first form succeeds and the
# macro is never defined. Minimal reproduction, run on the guest before this was
# written: four lines including <sys/ioctl.h> and <termios.h>, failing under
# -D_POSIX_C_SOURCE=200112L and compiling with -D__EXTENSIONS__ added.
WINSZ_PROBE := \043include <sys/ioctl.h>\n\043include <termios.h>\nint main(void){struct winsize w;return ioctl(0,TIOCGWINSZ,&w);}\n
HAVE_WINSZ := $(shell printf '$(WINSZ_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_WINSZ),yes)
  HAVE_WINSZ_EXT := $(shell printf '$(WINSZ_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -D__EXTENSIONS__ -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_WINSZ_EXT),yes)
    STD += -D__EXTENSIONS__
  endif
endif

# Sockets are not in libc everywhere. On illumos/Solaris accept(), listen() and
# bind() live in libsocket and the name-service calls in libnsl; on Linux and
# the BSDs they are in libc and -lsocket does not exist at all, so this cannot
# be added unconditionally. The illumos link named its own cause --
# "accept ... (symbol belongs to implicit dependency /lib/amd64/libsocket.so.1)"
# -- and the probe asks exactly that question (M657c).
SOCK_PROBE := \043include <sys/socket.h>\nint main(void){return socket(AF_INET,SOCK_STREAM,0)<0;}\n
HAVE_SOCK := $(shell printf '$(SOCK_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_SOCK),yes)
  HAVE_SOCK_LIBS := $(shell printf '$(SOCK_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) -lsocket -lnsl >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_SOCK_LIBS),yes)
    SOCK_LIBS = -lsocket -lnsl
  else
    # Haiku puts the BSD socket API (socket/bind/accept/connect, inet_pton) in
    # libnetwork, not libc and not libsocket (M829, first verified Haiku build).
    # Same probe, different library: ask who resolves socket(), never the uname.
    HAVE_SOCK_NET := $(shell printf '$(SOCK_PROBE)' \
      | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) -lnetwork >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
    ifeq ($(HAVE_SOCK_NET),yes)
      SOCK_LIBS = -lnetwork
    endif
  endif
endif

# No socket API at all (M835). DJGPP on FreeDOS has no <sys/socket.h>: TCP/IP
# there is an add-on library (Watt-32), not part of the C library. Asked only
# when no library above resolved socket(), and asked of the HEADER rather than
# the link: a system whose header exists but whose library the probes above do
# not know must keep failing at link time, loudly and naming the symbols,
# instead of quietly building without the daemon. With the flag, the daemon,
# --connect, `jichi control` and --control refuse with a message, `doctor` says
# so, and the SSRF classifier reads IPv4 literals itself (src/net/jc_http.c).
ifeq ($(filter yes,$(HAVE_SOCK) $(HAVE_SOCK_LIBS) $(HAVE_SOCK_NET)),)
  SOCK_HDR_PROBE := \043include <sys/types.h>\n\043include <sys/socket.h>\nint main(void){return 0;}\n
  HAVE_SOCK_HDR := $(shell printf '$(SOCK_HDR_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifneq ($(HAVE_SOCK_HDR),yes)
    NO_SOCKETS = yes
    STD += -DJC_NO_SOCKETS
  endif
endif

# STREAMS ptys. On illumos/Solaris a freshly opened pty slave is not a terminal
# until ptem/ldterm are pushed onto the stream -- measured at M661: tcgetattr
# fails and isatty returns 0 before the push, both succeed after. Only the test
# pty driver needs this, and only where <stropts.h> and I_PUSH exist; the probe
# asks that, rather than asking who shipped the kernel.
STREAMS_PROBE := \043include <stropts.h>\n\043include <sys/ioctl.h>\nint main(void){return ioctl(0, I_PUSH, "ldterm");}\n
HAVE_STREAMS_PTY := $(shell printf '$(STREAMS_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) $(if $(filter yes,$(HAVE_WINSZ)),,-D__EXTENSIONS__) -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_STREAMS_PTY),yes)
  STD += -DJC_HAVE_STREAMS_PTY
endif

# MiNTLib (FreeMiNT) hides lstat, readlink and symlink under strict
# _POSIX_C_SOURCE=200112L: its headers predate POSIX.1-2001 moving them into the
# base, and still want __USE_BSD or __USE_XOPEN_EXTENDED. -D_XOPEN_SOURCE=600 --
# the X/Open level that matches this build's POSIX level -- exposes all three
# (M723, measured with gcc 4.6.4 and MiNTLib 0.60.1). Probed like __EXTENSIONS__
# above, and WITH -Werror=implicit-function-declaration: the symbols ARE in MiNT's
# libc and only the declarations are hidden, so a probe that merely links says
# yes there and the build then fails -- measured, and M449's lesson in a new
# place. On glibc, musl, the BSDs and illumos the first form succeeds and nothing
# is added. portability_lint check 31 holds both probes to the flag.
LSTAT_PROBE := \043include <sys/stat.h>\n\043include <unistd.h>\nint main(void){struct stat s;char b[2];return lstat(".",&s)+(int)readlink(".",b,1)+symlink(".",".");}\n
HAVE_LSTAT := $(shell printf '$(LSTAT_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) $(if $(filter yes,$(HAVE_WINSZ)),,-D__EXTENSIONS__) -Werror=implicit-function-declaration -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_LSTAT),yes)
  HAVE_LSTAT_XOPEN := $(shell printf '$(LSTAT_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -D_XOPEN_SOURCE=600 -Werror=implicit-function-declaration -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_LSTAT_XOPEN),yes)
    ifneq ($(XOPEN_ADDED),yes)
      STD += -D_XOPEN_SOURCE=600
    endif
  endif
endif

# No mmap on FreeMiNT: MiNTLib has neither <sys/mman.h> nor the symbol (M723).
# jc_index maps its vector blob where it can and already reads a copy when a
# mapping fails (M141), so absence needs only a compile-time guard. NEGATIVE,
# like JC_NO_CLOCK_GETTIME and for the same reason: without the flag, today's
# behaviour stands, and it is set only when the probe has proved mmap
# unreachable. `make ci` compiles the guarded path (fallbacks-compile), because
# nothing else in the gate would.
MMAP_PROBE := \043include <sys/mman.h>\nint main(void){void *p=mmap(0,1,PROT_READ,MAP_PRIVATE,-1,0);return p==MAP_FAILED;}\n
HAVE_MMAP := $(shell printf '$(MMAP_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) $(if $(filter yes,$(HAVE_WINSZ)),,-D__EXTENSIONS__) -Werror=implicit-function-declaration -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_MMAP),yes)
  STD += -DJC_NO_MMAP
endif

# Long file names on DOS (M838). Plain DOS keeps 8.3 names: `.jichi.d` cannot exist
# and a session file's name is cut to eight characters, so the unit suite fails 271
# checks there instead of 108 (M836's rig). Whether a drive has long names is a fact
# of the running system -- DOSLFN loaded or not -- so the Makefile does not decide
# it: it asks whether the C library can tell (DJGPP's _use_lfn), and jichi asks at
# run time. WITH -Werror=implicit-function-declaration, as every declaration probe
# here: an undeclared call must read "absent", not link by accident (M449).
LFN_PROBE := \043include <fcntl.h>\nint main(void){return _use_lfn(".") ? 0 : 1;}\n
HAVE_USE_LFN := $(shell printf '$(LFN_PROBE)' \
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L $(POSIX_UNGATE) -Werror=implicit-function-declaration -xc - -o $(PROBE_OUT) >/dev/null 2>&1 && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_USE_LFN),yes)
  STD += -DJC_HAVE_USE_LFN
endif

# libm for floor() in the JSON number printer.
LDLIBS = -lm $(RT_LIBS) $(CURL_LIBS) $(SOCK_LIBS)

# -MMD -MP emits a .d file beside each .o recording its header prerequisites,
# so a changed header forces a rebuild of every object that includes it (these
# are GCC/Clang extensions, not used during the actual compile of the sources).
DEPFLAGS  = -MMD -MP

# --- opt-in build knobs (used by `make ci`) --------------------------------
#   WERROR=1  treat warnings as errors. Applies to EVERY translation unit: as of
#             M171 there is no vendored code and therefore no -pedantic
#             exemption -- src/json/cJSON.c is ours and is pedantic-clean.
#   SAN=1     build with AddressSanitizer + UndefinedBehaviorSanitizer.
#   FAULT=1   compile in deterministic fault injection for error-path tests
#             (M198). Inert in default builds; drive with JICHI_FAULT_ALLOC_AFTER
#             / _READ_AFTER / _WRITE_AFTER. See include/jc_fault.h.
#   SIZE=1    size-optimized build for low-resource / embedded targets: -Os,
#             per-function/data sections + link-time section GC, and a stripped
#             binary. Off by default (the normal build passes no -O at all), so
#             `make clean && make SIZE=1` is the smallest jichi. Compose with a
#             minimal single-TLS-backend libcurl and static musl for the leanest
#             result -- see docs/LOW_MEMORY.md. (M20d)
#   LTO=1     add link-time optimization (-flto), typically with SIZE=1, for a
#             further size/speed win; needs an LTO-capable toolchain.
ifeq ($(WERROR),1)
  WARN += -Werror
endif
# float-cast-overflow is named EXPLICITLY because gcc does not include it in
# `-fsanitize=undefined` while clang does -- measured both ways (M470):
#
#   gcc   -fsanitize=undefined                        (int)1e300 -> not caught
#   gcc   -fsanitize=undefined,float-cast-overflow     ->  CAUGHT
#   clang -fsanitize=undefined                         ->  CAUGHT
#
# M469 found real undefined behaviour of exactly this shape in the JSON number
# parser -- `(int)val` where val exceeds int -- on big-endian ARM, because
# `zig cc` traps UB by default. This gate had ASan+UBSan and could not have
# caught it under gcc no matter what input it was given. Naming the check is a
# no-op where it is already implied and the difference between a green gate and
# a real one where it is not.
#
# -fno-sanitize-recover=all because UBSan RECOVERS by default: it prints
# "runtime error: ..." and carries on, so the run still exits 0 and a gate that
# greps for a failure sees none. Measured: with the clamp reverted, the fuzz
# corpus printed the diagnostic and the runner still reported
# "1 target(s) ok". A sanitizer that reports and passes is barely better than no
# sanitizer. ASan already aborts; this makes UBSan behave the same. Verified not
# to newly break anything: the full unit suite is 11,661 checks / 0 failures with
# it on (298 compile lines carrying the flag, checked rather than assumed).
ifeq ($(SAN),1)
  SANFLAGS = -fsanitize=address,undefined,float-cast-overflow \
             -fno-omit-frame-pointer -fno-sanitize-recover=all
endif
# FUZZ=1: instrument every TU for coverage-guided fuzzing (libFuzzer, M269).
# "fuzzer-no-link" on the compile side so the library objects get the coverage
# counters without libFuzzer's main(); only run_fuzz_lf links the runtime. Never
# referenced by `ci` -- this is the opt-in depth pass. Requires clang; see the
# `libfuzz` target, which probes for it.
ifeq ($(FUZZ),1)
  SANFLAGS = -fsanitize=fuzzer-no-link,address,undefined,float-cast-overflow -fno-omit-frame-pointer
endif
# FAULT=1: compile in the deterministic fault injector (M198 #4). Off by default,
# so a release binary carries no injection surface at all -- every JC_FAULT_HIT
# expands to a constant 0. Configure at runtime via JICHI_FAULT_*_AFTER.
# A clean rebuild is required when toggling (like SAN): see include/jc_fault.h.
# WHICH COMMIT this binary came from (M495), so a stale install cannot pass for a
# fresh one. Measured 2026-08-19: an install 12 days and ~50 milestones behind the
# tree reported the same `jichi 0.9.0` as the tree, and the difference surfaced only
# as a rejected flag and a doctor check that printed NOTHING -- absence being
# indistinguishable from agreement.
#
# `2>/dev/null` twice and no `.PHONY` trickery: a tarball with no repository, or a
# machine with no git, must build EXACTLY as before. Both commands then produce
# nothing, BUILD_REV is empty, no -D is passed, and jc_build_rev() returns NULL, so
# --version prints no build line at all. An absent stamp is honest; "unknown" is
# noise, and a faked one is worse than either.
#
# Only src/util/jc_buildrev.o carries the define, so a new commit rebuilds one 20-line
# file rather than everything that includes a common header.
# The value reaches the compiler through a GENERATED HEADER, not a -D on the command
# line, and that is a correction to this feature's own first cut. With `-D`, make sees
# no changed prerequisite when only the commit moves -- the .c file is untouched -- so
# `make` after a commit left the OLD stamp in the binary. The check added in the same
# milestone caught it immediately: "binary was built from '60aa940' but this tree is at
# '7281db7'". A stamp that goes stale silently is the very defect this feature exists
# to remove.
#
# So: $(STAMP) is rewritten only when its CONTENT changes (cmp -s, then mv), its recipe
# always runs via a phony prerequisite, and jc_buildrev.o depends on the file. A no-op
# `make` therefore runs one tiny shell recipe and relinks nothing, while a commit
# rebuilds exactly one 20-line translation unit. No .git coupling (worktrees and packed
# refs would break that), and no special case for a tarball: with no git the header is
# generated EMPTY, jc_build_rev() returns NULL, and nothing is printed.
STAMP = include/jc_buildrev_stamp.h

# PIN THE DEFAULT GOAL. make takes the FIRST target in the file as the default, so
# adding the $(STAMP) rule below silently made `make` build a header and nothing else
# -- no binary, no error, exit 0. Found within minutes because the operator asked for
# `make clean && make install` and the clean build produced no binary; found at all
# only because something else was checked afterwards. `all` is 270 lines further down,
# so its position is not a safe place to encode this: state it.
.DEFAULT_GOAL := all

$(STAMP): FORCE
	@rev=`git rev-parse --short HEAD 2>/dev/null`; \
	 if [ -n "$$rev" ] && [ -n "`git status --porcelain 2>/dev/null | head -n 1`" ]; then \
	     rev="$$rev-dirty"; \
	 fi; \
	 if [ -n "$$rev" ]; then \
	     printf '#define JC_BUILD_REV "%s"\n' "$$rev" > $@.new; \
	 else \
	     printf '/* no repository at build time: see jc_buildrev.c */\n' > $@.new; \
	 fi; \
	 cmp -s $@.new $@ 2>/dev/null || mv -f $@.new $@; \
	 rm -f $@.new

FORCE:
.PHONY: FORCE

src/util/jc_buildrev.o: $(STAMP)

ifeq ($(FAULT),1)
  FAULTFLAGS = -DJC_FAULT
endif
# NOFORK=1: a build in which jichi creates no process (M769). fork() and popen()
# are renamed at compile time to stand-ins that refuse with ENOSYS, so every
# feature that needs a process fails where it would on a system without fork,
# and whatever still works can be seen to. A MEASURING build for the fork-free
# design (docs/analysis/2026-09-25-a-fork-free-jichi.md), never a product. The
# smoke tools keep their own TT_CFLAGS, so the harness still forks. A clean
# rebuild is required when toggling, as for FAULT and SAN.
ifeq ($(NOFORK),1)
  NOFORKFLAGS = -DJC_NOFORK -Dfork=jc_nofork_fork -Dpopen=jc_nofork_popen
endif
ifeq ($(SIZE),1)
  SIZEFLAGS   = -Os -ffunction-sections -fdata-sections
  SIZELDFLAGS = -Wl,--gc-sections -s
endif
ifeq ($(LTO),1)
  SIZEFLAGS   += -flto
  SIZELDFLAGS += -flto
endif

# RELEASE=1: the named release mode (L-01, hardening chapter 1 section 8 step 3).
# The documented install path (plain `make` + `make install`) ships the -O0 build,
# and L-01 measured that installed binary with ZERO __*_chk _FORTIFY_SOURCE symbols
# -- unoptimised and unfortified, on purpose for the developer loop. The default
# stays that way; this gives the RELEASE its own name so `make install` can point
# at a fortified, optimised binary instead of whatever the last edit produced.
#
# ORDERING IS LOAD-BEARING (do not move this block). Line below, `OPTLEVEL :=
# $(filter -O1 -O2 -O3 -Os -Oz,$(OPT) $(SIZEFLAGS))`, is IMMEDIATE: make expands
# $(OPT) at PARSE time, here. The M1071 fortify gate (further down) keys on
# OPTLEVEL, not on "OPT non-empty". So if this block set OPT= -O2 AFTER that
# line, OPTLEVEL would already have been computed empty, the fortify gate would
# see no optimisation level, and _FORTIFY_SOURCE would silently stay inert even
# in a RELEASE build -- the exact defect L-01 describes, reproduced one knob
# away. The block therefore sits BEFORE OPTLEVEL, with the other mode toggles,
# and feeds the existing fortify probe FOR FREE: it does not re-implement it.
#
# OPT ?= (not =): an explicit `make RELEASE=1 OPT=-O3` keeps the user's level;
# `?=` only fills the default. When RELEASE is unset this block is skipped, so
# the default build's OPT stays empty and the binary is byte-identical to today.
#
# RELEASE_CFLAGS: the one release-only candidate flag this milestone takes,
# -ftrivial-auto-var-init=zero (zero-init trivial automatics; reads of an
# uninitialised stack slot then see zero instead of a stale canary). It is
# PROBED with cc_warn_ok, never assumed: a toolchain that rejects it (pre-gcc-12
# / pre-clang-16) simply does not use it. It is a COMPILE-time flag and belongs
# on the JC_CFLAGS line, NOT in the fortify block (which is about
# _FORTIFY_SOURCE). The finding's other two candidates are deliberately NOT
# taken here:
# -mbranch-protection=standard is aarch64-only (this and every x86 row would
# reject it) and -fhardened is a GCC-14 umbrella -- both marked [GK] in L-01
# chapter 8 and out of scope for the smallest correct fix. TAVI is the one that
# is in scope, portable, and probed.
#
# `:=` (immediate), not `=` (recursive): every other cc_warn_ok probe in this
# block uses `:=` (WARN_OPTIONAL, HARDENFLAGS, HARDENLDFLAGS) for the same
# reason. JC_CFLAGS is recursive and expands once PER translation unit, so a
# recursive RELEASE_CFLAGS would re-run the TAVI probe ~188 times, and under
# -j4 all those probes write the same $(PROBE_OUT) (`.jc_probe.<pid>.out`) at
# once -- a race. `:=` runs it ONCE at parse time, inside this ifeq block (so
# it is present exactly when RELEASE=1), and captures the result before
# JC_CFLAGS is ever used. The ordering the fortify gate needs is unaffected:
# this block still precedes OPTLEVEL.
ifeq ($(RELEASE),1)
  OPT ?= -O2
  RELEASE_CFLAGS := $(call cc_warn_ok,-ftrivial-auto-var-init=zero)
endif

# --- security hardening flags (M472) ---------------------------------------
# jichi used to set NONE of these, and the shipped binary had most of them anyway
# -- because Ubuntu's gcc turns on -fstack-protector-strong, PIE and full RELRO in
# its own specs. That is hardening we neither asked for nor measured, and this
# project runs on five libcs and three non-Linux kernels (docs/PLATFORMS.md) where
# the defaults differ. Every other axis here is probed; this one was inherited.
#
# One mitigation really was absent rather than merely unrequested: _FORTIFY_SOURCE.
# The binary carried no __*_chk symbols at all, and it would have been inert even
# if set, because the default build passes no -O and glibc's fortify does nothing
# without optimization (verified both ways). See the OPT note below. M1071 (E-05):
# the flag is now keyed on an actual optimisation LEVEL (OPTLEVEL, defined above),
# not on "OPT or SIZEFLAGS non-empty", and its LEVEL is probed (3 then 2) rather
# than pinned, so an Ubuntu gcc 13 build is not silently lowered from its own
# default of 3.
#
# PROBED, not assumed, one flag at a time: -fstack-clash-protection is gcc 8+,
# -fcf-protection is x86-only, and a musl or bionic or hand-built toolchain may
# have neither. A flag the compiler rejects is simply not used, so a new row cannot
# fail to build because of this block. The probe LINKS as well as compiles, which is
# what a -Wl, flag needs.
#
# A literal comma, so it survives $(call ...) -- where an unescaped one separates
# arguments and truncates every -Wl, flag to "-Wl". It must be defined BEFORE the
# block below: HARDENLDFLAGS uses ':=', which expands immediately, so a comma
# defined afterwards is empty and every linker probe fails silently. `make info`
# showing "-pie" alone is what caught that.
comma := ,

# M1071 (E-05): the optimisation LEVEL actually in play, keyed for _FORTIFY_SOURCE.
# The old gate tested "is OPT or SIZEFLAGS non-empty", which is TRUE for LTO=1
# alone (SIZEFLAGS = -flto, no -O) and for OPT=-O0 or OPT=-g -- so it set
# _FORTIFY_SOURCE at -O0, where glibc >= 2.39 ignores it silently (features.h
# needs __OPTIMIZE__) and older glibc #warning's it, which WERROR=1 turns into an
# error. And `make info` then claimed fortify was "live" on exactly those rows.
#
# -O0, -g and bare -flto are NOT an optimisation level, so they are excluded. The
# list is exactly the levels the finding names (-O1 -O2 -O3 -Os -Oz). Bare `-O`
# (== -O1) and -Og are deliberately left out: they are not knobs this tree
# documents, and -Og is a debugging level, not a release one -- the finding says
# keep the list to the named levels. $(filter) matches whole words, so "-O2"
# does not match "-O20" or "-Os".
OPTLEVEL := $(filter -O1 -O2 -O3 -Os -Oz,$(OPT) $(SIZEFLAGS))

# The `make info` OPT-line note (M1071): default is the inert case (no
# optimisation level in play). The HARDEN=1 block below overwrites it when a
# level is actually set or probed; the HARDEN!=1 block after OPT ?= overwrites
# it again for the "user turned hardening off" case. Every case is therefore
# named, so the line always says the truth.
FORTIFY_NOTE := _FORTIFY_SOURCE inert (no optimisation level in play -- see the Makefile note)

# HARDEN=0 turns the block off (a bisect, or a toolchain whose probe lies).
HARDEN ?= 1
ifeq ($(HARDEN),1)
  # Probed with cc_warn_ok (defined above), NOT a private probe of its own -- and
  # the difference is -Werror, which is the entire bug this replaced (M479).
  #
  # The original probe here asked "does the compiler ACCEPT this flag". OpenBSD's
  # clang 19 accepts -fstack-clash-protection and then ignores it, so the probe
  # said yes and every translation unit failed under the build's own -Werror:
  #
  #   cc: error: argument unused during compilation: '-fstack-clash-protection'
  #       [-Werror,-Wunused-command-line-argument]
  #
  # That is M449's lesson, written down 23 milestones before M472 repeated it:
  # "the question a capability probe must ask is 'is it DECLARED under the flags I
  # build with', not 'does the symbol exist somewhere in libc'". And the sequence is
  # worse than a simple repeat -- M476 then added cc_warn_ok, which asks it
  # CORRECTLY, forty lines above this broken one, for the GCC-only warning flags,
  # without noticing that the probe below it was the same question asked wrong. Two
  # probes for one question is how they drift, so there is now one.
  #
  # M833: the stack protector is probed ONCE and put on BOTH lines. The probe itself
  # was right (it compiles and links in one step, and gcc's driver adds the runtime,
  # libssp, whenever the flag is on the command line), but the build split the two
  # and put the flag in CFLAGS only. glibc and musl carry the runtime in libc, so the
  # link never needed it and nothing showed; DJGPP keeps it in libssp, and the link
  # failed on 5,397 references to __stack_chk_guard/__stack_chk_fail. The same
  # compile-and-link pairing -fPIE/-pie already had below.
  HARDEN_SSP := $(call cc_warn_ok,-fstack-protector-strong)
  HARDENFLAGS := $(HARDEN_SSP) \
                 $(call cc_warn_ok,-fstack-clash-protection) \
                 $(call cc_warn_ok,-fcf-protection) \
                 $(call cc_warn_ok,-Wformat -Werror=format-security) \
                 $(call cc_warn_ok,-fPIE)
  HARDENLDFLAGS := $(HARDEN_SSP) \
                   $(call cc_warn_ok,-Wl$(comma)-z$(comma)relro) \
                   $(call cc_warn_ok,-Wl$(comma)-z$(comma)now) \
                   $(call cc_warn_ok,-Wl$(comma)-z$(comma)noexecstack) \
                   $(call cc_warn_ok,-pie)
  # M1071 (E-05): _FORTIFY_SOURCE only where there IS an optimisation LEVEL in
  # play (OPTLEVEL, above) -- not merely where OPT or SIZEFLAGS is non-empty.
  # It is a no-op without one, and glibc >= 2.39 ignores it silently (features.h
  # needs __OPTIMIZE__) while older glibc #warning's it -- which WERROR=1 turns
  # into an error. The old "non-empty" gate set it for LTO=1 alone (SIZEFLAGS =
  # -flto, no -O) and for OPT=-O0, both of which are -O0 builds.
  #
  # -U first: a distro gcc may already define it, and redefining is a warning
  # this build treats as an error under WERROR=1.
  #
  # The LEVEL is PROBED, not pinned. Try 3 (catches run-time-sized overflows --
  # the arena buffers jichi uses everywhere; needs glibc >= 2.34 + gcc >= 12),
  # fall back to 2 (old gcc/glibc), then to nothing (an ancient toolchain where
  # even 2 fails -- the compiler default is kept, and `make info` says so). Each
  # level is accepted only if a program that INCLUDES <string.h> compiles
  # SILENTLY (M479: silence, not acceptance) under the build's own dialect and
  # OPT/SIZEFLAGS. The include is so glibc's features.h processes the
  # _FORTIFY_SOURCE level (it is where a level the toolchain cannot honour
  # #warning's); it does NOT instantiate fortify -- the probe calls no
  # fortifiable function, so it proves the level is ACCEPTED, not that it fires.
  # That the level actually instruments is harden_flags_lint check 10's job
  # (its self-test calls snprintf and requires a __*_chk import).
  ifneq ($(strip $(OPTLEVEL)),)
    FORTIFY_LEVEL := $(shell ok=""; \
      for _lv in 3 2; do \
        o=$$(printf '\043include <string.h>\nint main(void){char b[16];b[0]=0;return b[0];}\n' \
          | $(CC) -std=c89 -pedantic $(POSIX) $(OPT) $(SIZEFLAGS) -Werror -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=$$_lv -xc - -o $(PROBE_OUT) 2>&1) && [ -z "$$o" ] && ok=$$_lv && break; \
      done; \
      rm -f $(PROBE_OUT) $(PROBE_OUT).exe; \
      if [ -n "$$ok" ]; then echo $$ok; else echo default; fi)
    ifeq ($(FORTIFY_LEVEL),default)
      FORTIFY_NOTE := _FORTIFY_SOURCE probed, but levels 3 and 2 both failed -- the compiler default is kept, nothing is set
    else
      FORTIFY_FLAGS := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=$(FORTIFY_LEVEL)
      FORTIFY_NOTE := _FORTIFY_SOURCE $(FORTIFY_LEVEL) is live (probed)
      HARDENFLAGS += $(FORTIFY_FLAGS)
    endif
  endif
endif
# OPT: optimization for the default build. EMPTY by default, which keeps the
# historical behaviour exactly (the tree has always built at -O0 for build speed
# and debuggability, and `make info` says so -- the recorded trade-off,
# HARDENING.md section 7b, unchanged). Set it -- `make OPT=-O2` -- to get an
# optimized build AND, with it, a live _FORTIFY_SOURCE at the LEVEL THE TOOLCHAIN
# SUPPORTS (probed 3 then 2, not pinned). SIZE=1 already implies -Os and
# therefore fortify.
OPT ?=
# The `make info` OPT-line note (M1071). The HARDEN=1 block above sets it on
# EVERY branch (live / both-levels-failed / inert), so it is only defined here
# for the HARDEN!=1 case, where the whole block is skipped: nothing
# fortify-related is on, and that is the user's choice (HARDEN=0).
ifneq ($(HARDEN),1)
  FORTIFY_NOTE := _FORTIFY_SOURCE not set (HARDEN=$(HARDEN))
endif

# Four of the warning flags above are C-only, and a C++ driver says so once per
# translation unit: `make CC=g++` printed 913 warnings, of which 900 were five
# such notices x 180 files and 13 were the real -Wwrite-strings residue. Dropping
# the four that cannot apply is what makes the remaining 13 readable -- the whole
# point of running a second front-end. (M636)
ifeq ($(CC_IS_CXX),yes)
  WARN := $(filter-out -Wstrict-prototypes -Wmissing-prototypes \
                       -Wold-style-definition -Wjump-misses-init,$(WARN))
endif

# EXTRA_DEFS: extra -D defines appended to JC_CFLAGS. EMPTY by default, so it
# changes no default build. The one stage that sets it is smoke-push-boundary
# (def-350), which lowers PUSH_MAX_TIPS/PUSH_MAX_EXCL and announces the build
# with -DJC_PUSH_BOUNDARY. A clean rebuild is required when toggling, as for
# FAULT and SAN.
EXTRA_DEFS =
# GNU convention (E-06, M1074): JC_CFLAGS is the PROJECT's compile line; CFLAGS
# and CPPFLAGS are the USER's, empty by default, and are honored AFTER it on the
# build line. The capability probes intentionally do not see user flags: they
# answer "does this compiler accept flag X?", not "does the user's CFLAGS
# break X?" -- a different question, with a different flag set.
JC_CFLAGS = $(STD) $(WARN) $(POSIX) $(LARGE_FILE_DEFS) $(INCLUDE) $(DEPFLAGS) $(SANFLAGS) $(SIZEFLAGS) $(FAULTFLAGS) $(NOFORKFLAGS) $(EXTRA_DEFS) $(OPT) $(HARDENFLAGS) $(RELEASE_CFLAGS)

# --- curl's own header, read under THIS build's dialect ----------------------
# libcurl types `curl_off_t` as `long long` on every non-LP64 target -- the
# stock <curl/system.h> does it in a block predicated on __i386__ / __arm__ /
# __mips__ / __powerpc__ / __ILP32__ / __SIZEOF_LONG__ == 4 -- and this build is
# -std=c89 -pedantic, where `long long` is an extension. So WERROR=1 with
# libcurl fails INSIDE A THIRD-PARTY HEADER on every 32-bit platform, on a line
# no first-party source can reach. Measured 2026-09-21 by cross-compiling
# src/net/jc_http.c alone: x86, riscv32 and powerpc fail, x86_64 is clean --
# which is why no 64-bit row here ever saw it and a 32-bit board with
# libcurl-dev cannot run this gate today.
#
# Two fixes that do NOT work, measured rather than assumed: the gnu89 fallback
# warns identically (-pedantic objects in either dialect), and curl 8.18 ignores
# --disable-largefile for this typedef.
#
# So ASK THE QUESTION THE BUILD ASKS (M636's rule, argued above for the C++
# driver probe): compile a program that includes the header with the dialect,
# warning set and POSIX level the objects get, under -Werror. The probe runs
# only when HAVE_CURL said yes, because a probe that cannot tell "the header is
# unclean" from "there is no header" is the M476 defect, and it answers about
# the header rather than about a platform name.
#
# THE RELAXATION IS DELIBERATELY ONE OBJECT WIDE. src/net/jc_http.c is the only
# translation unit in the tree that includes <curl/curl.h>, and
# portability_lint check 19 holds that true -- because tree-wide
# -Wno-long-long would leave check 6's ban on `long long` in first-party code
# enforced by nothing but a grep. Scoped, the compiler still refuses the type in
# every other unit. `make info` prints the verdict: a silent relaxation is the
# unreported degradation M458 objected to.
ifeq ($(HAVE_CURL),yes)
  CURL_HDR_CLEAN := $(shell printf '\043include <curl/curl.h>\nint main(void){return 0;}\n' \
    | $(CC) $(STD) $(WARN) $(POSIX) -Werror -xc - -c -o $(PROBE_OUT) >/dev/null 2>&1 \
    && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifneq ($(CURL_HDR_CLEAN),yes)
    CURL_HDR_CFLAGS = $(call cc_warn_ok,-Wno-long-long)
  endif
endif
src/net/jc_http.o: JC_CFLAGS += $(CURL_HDR_CFLAGS)

LDLIBS   += $(SANFLAGS)
# Link-time flags. The size profile adds section GC + strip (and -flto with LTO);
# they sit before the objects on the link line. `-ffunction-sections
# -fdata-sections` in JC_CFLAGS is what makes --gc-sections effective.
# GNU convention (E-06, M1074): JC_LDFLAGS is the PROJECT's link line; the
# user's LDFLAGS is empty by default and is honored AFTER it on the link line.
JC_LDFLAGS = $(SIZELDFLAGS) $(HARDENLDFLAGS)

# --- static build mode (E-07, M1075) ------------------------------------------
# STATIC=1 is a NAMED build mode: the two docs' static recipes used to be
# `LDLIBS="-lm -lcurl -static"`, which puts `-static` AFTER `-pie` on the link
# line (`JC_LDFLAGS` carries `-pie`, the user's LDLIBS is appended after it).
# Measured on gcc 15.2 / glibc: `gcc -pie ... -static` yields Type EXEC --
# `ELF executable, statically linked`, NOT PIE -- while `make info` still lists
# `-pie`. The override also dropped $(SANFLAGS)/$(RT_LIBS)/$(SOCK_LIBS) from
# LDLIBS (harmless on Linux, wrong on illumos/Haiku).
#
# This block sits AFTER the HARDENLDFLAGS `:=` block (so it can filter -pie out
# of JC_LDFLAGS and -fPIE out of JC_CFLAGS) and after the RELEASE=1 block
# (composable: `make RELEASE=1 STATIC=1`). It is orthogonal to SIZE/RELEASE.
#
# DEFAULT-BUILD INVARIANT (M1073's discipline): when STATIC is unset this
# ifeq block is skipped entirely -- no probe runs, no flag changes, and the
# default build is byte-identical to before (verified: `make -n -B jichi`
# unchanged with vs. without the knob).
#
# PROBE `-static-pie` the way the finding's style probes LINK flags: cc_warn_ok
# compiles but does not link against the project's LDLIBS, so a `-static-pie`
# ACCEPTANCE must be probed by actually compiling+linking a small program with
# the flag. `:=` (immediate), inside the ifeq block, so it runs at most once at
# parse time -- the RELEASE_CFLAGS note above explains why a per-TU recursive
# probe would race on $(PROBE_OUT).
ifeq ($(STATIC),1)
  STATIC_PIE := $(shell printf 'int main(void){return 0;}' \
    | $(CC) -static-pie -xc - -o $(PROBE_OUT) >/dev/null 2>&1 \
    && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(STATIC_PIE),yes)
    # -static-pie works here. Keep -pie (a static-pie build is pie-executable,
    # Type DYN, static-pie linked) and add -static-pie to the link line.
    STATIC_MODE := static-pie (probed)
    STATICLDFLAGS := -static-pie
    JC_LDFLAGS += $(STATICLDFLAGS)
  else
    # No -static-pie on this toolchain: fall back to -static, but the fallback
    # CANNOT keep -pie in JC_LDFLAGS nor -fPIE in JC_CFLAGS. Measured: -fPIE
    # objects + -static with NO -pie is a link FAILURE on this toolchain (the
    # objects are position-independent but -static refuses to build a PIE from
    # them without -pie), so the compile flag must be dropped too.
    STATIC_MODE := static (fallback: no -pie, -fPIE dropped -- it would not link)
    STATICLDFLAGS := -static
    JC_LDFLAGS := $(filter-out -pie,$(JC_LDFLAGS)) $(STATICLDFLAGS)
    JC_CFLAGS := $(filter-out -fPIE,$(JC_CFLAGS))
  endif
endif

BIN  = jichi
TEST = run_tests

# --- sources ---------------------------------------------------------------
# Networking sources are only compiled when libcurl headers are present.
CORE_SRC = \
  src/platform/jc_platform_posix.c \
  src/platform/jc_snprintf.c \
  src/util/jc_mem.c \
  src/util/jc_str.c \
  src/util/jc_vec.c \
  src/util/jc_log.c \
  src/util/jc_uuid.c \
  src/util/jc_sha256.c \
  src/util/jc_md.c \
  src/util/jc_cli.c \
  src/util/jc_utf8.c \
  src/util/jc_msg.c \
  src/util/jc_priv.c \
  src/util/jc_reread.c \
  src/util/jc_gradecore.c \
  src/util/jc_kinetic.c \
  src/util/jc_argpath.c \
  src/util/jc_sound.c \
  src/util/jc_lease.c \
  src/util/jc_inhist.c \
  src/util/jc_delegreport.c \
  src/util/jc_toolloop.c \
  src/util/jc_relay.c \
  src/util/jc_diskguard.c \
  src/util/jc_landlock.c \
  src/util/jc_spawn.c \
  src/util/jc_audit.c \
  src/util/jc_auditview.c \
  src/util/jc_runsview.c \
  src/util/jc_control_proto.c \
  src/util/jc_jsonrepair.c \
  src/util/jc_buildrev.c \
  src/util/jc_confbench.c \
  src/util/jc_packages.c \
  src/util/jc_agentjson.c \
  src/util/jc_daemon_proto.c \
  src/util/jc_assign.c \
  src/util/jc_assignlist.c \
  src/util/jc_outcome.c \
  src/util/jc_reach.c \
  src/util/jc_plan.c \
  src/util/jc_progress.c \
  src/util/jc_meminfo.c \
  src/util/jc_memtrim.c \
  src/util/jc_fault.c \
  src/util/jc_improve.c \
  src/util/jc_selfheal.c \
  src/util/jc_cacheaudit.c \
  src/util/jc_workflow.c \
  src/util/jc_testparse.c \
  src/util/jc_complete.c \
  src/util/jc_suggest.c \
  src/util/jc_fim.c \
  src/util/jc_fmtcmd.c \
  src/util/jc_patch.c \
  src/util/jc_workerpool.c \
  src/util/jc_rss.c \
  src/util/jc_diff.c \
  src/util/jc_doctor.c \
  src/util/jc_assetval.c \
  src/util/jc_mdrender.c \
  src/util/jc_lineno.c \
  src/util/jc_path.c \
  src/util/jc_base64.c \
  src/util/jc_image.c \
  src/util/jc_audio.c \
  src/util/jc_multipart.c \
  src/util/jc_proc.c \
  src/util/jc_pdf.c \
  src/util/jc_eventlog.c \
  src/util/jc_telemetry.c \
  src/util/jc_dispatch.c \
  src/util/jc_insights.c \
  src/util/jc_learn.c \
  src/util/jc_calib.c \
  src/util/jc_prefix.c \
  src/util/jc_toolout.c \
  src/util/jc_promptcache.c \
  src/util/jc_rewind.c \
  src/util/jc_queryrewrite.c \
  src/util/jc_notify.c \
  src/util/jc_untrusted.c \
  src/util/jc_voice.c \
  src/util/jc_toolprobe.c \
  src/json/jc_json.c \
  src/json/cJSON.c \
  src/convert/jc_yaml.c \
  src/config/jc_config.c \
  src/config/jc_config_trust.c \
  src/config/jc_configedit.c

# src/net compiles UNCONDITIONALLY (M190): only jc_http.c touches libcurl,
# and it guards itself -- without JC_HAVE_CURL it compiles runtime stubs
# that return JC_ERR_HTTP ("built without libcurl"). The rest of the
# directory is pure (jc_sse, URL helpers) or built on jc_http's interface.
# The old wholesale HAVE_CURL gate here had drifted the documented
# "core + tests build without libcurl" into ~28 link errors (found by the
# M189 zig musl cross attempt).
NET_SRC = $(wildcard src/net/*.c)

# Additional subsystem sources picked up as they are added in later milestones.
# src/mcp holds the Model Context Protocol client; its pure protocol layer and
# the stdio transport build without libcurl, while the http transport is
# guarded by JC_HAVE_CURL internally.
EXTRA_SRC = $(wildcard src/chat/*.c src/provider/*.c src/tools/*.c \
                       src/session/*.c src/tui/*.c src/index/*.c \
                       src/mcp/*.c src/command/*.c src/lsp/*.c \
                       src/snapshot/*.c src/skill/*.c src/acp/*.c \
                       src/scaffold/*.c src/setup/*.c)

LIB_SRC = $(CORE_SRC) $(NET_SRC) $(EXTRA_SRC)
LIB_OBJ = $(LIB_SRC:.c=.o)

MAIN_OBJ = src/main.o

# Config-converter tool: the YAML parser + conversion core are shared with the
# test suite; jc_convert_main.c holds its own main().
CONVERT_BIN  = jichi-convert
# jc_yaml moved into CORE_SRC (used by jc_md for command/agent frontmatter);
# the converter core is the mapping logic plus the JSONC pre-pass and the
# per-source mappers. Linked into both jichi-convert and the test binary.
CONVERT_CORE = src/convert/jc_convert_core.o \
               src/convert/jc_jsonc.o \
               src/convert/jc_convert_continue.o \
               src/convert/jc_convert_opencode.o \
               src/convert/jc_convert_claude.o \
               src/convert/jc_convert_assets.o
CONVERT_MAIN = src/convert/jc_convert_main.o

# The drive harness: jichi's third C89 program (M1049, H0a). Its own main() in
# src/harness/, linked against $(LIB_OBJ) so it reuses src/util, src/json and
# src/platform (the spawn primitive, the environment scrub, the redactor, the
# memory protection) without re-declaring them -- D4's reason for C89.
HARNESS_BIN  = jichi-harness
HARNESS_MAIN = src/harness/jc_harness_main.o
# M1051 (H1a): the harness configuration loader. Deliberately NOT in
# LIB_OBJ/LIB_SRC so it is not linked into `jichi` itself as dead weight
# (M1049 keeps the harness standalone); it is a prerequisite of the harness
# binary (so H1b's `doctor` can call it) and of the unit suite (so the loader
# test links).
HARNESS_CONFIG = src/harness/jc_harness_config.o
# M1052 (H1b): the doctor command, the offline report on the harness config.
# Like HARNESS_CONFIG it is deliberately NOT in LIB_OBJ/LIB_SRC (it must not be
# linked into `jichi` itself as dead weight); it is a prerequisite of the
# harness binary only, where doctor is wired into the dispatch.
HARNESS_DOCTOR = src/harness/jc_harness_doctor.o
# M1053 (H2): the init command, the C89 port of the drive's setup-dir.sh.
# Like HARNESS_DOCTOR it is deliberately NOT in LIB_OBJ/LIB_SRC; it is a
# prerequisite of the harness binary only, where init is wired into the
# dispatch (it runs git through jc_proc_capture).
HARNESS_INIT = src/harness/jc_harness_init.o
# M1055 (H3b): the `mirror fetch` / `mirror head` commands, the C89 port of
# the drive's mirror.sh mirror_fetch + mirror_head (the push is M1056 / H3c).
# Like HARNESS_INIT it is deliberately NOT in LIB_OBJ/LIB_SRC; it is a
# prerequisite of the harness binary only, where `mirror` is wired into the
# dispatch (it runs git through jc_proc_capture, M2-neutral).
HARNESS_MIRROR = src/harness/jc_harness_mirror.o
# M1056 (H3c): the `mirror push` command, the C89 port of the drive's
# mirror.sh mirror_push + pre-push-keyscan.sh + pre-push-sizecheck.sh.
# Like HARNESS_MIRROR it is deliberately NOT in LIB_OBJ/LIB_SRC; it is a
# prerequisite of the harness binary only, where `mirror push` is wired into
# the dispatch (it runs git through jc_proc_capture, M2-neutral).
HARNESS_PUSH = src/harness/jc_harness_push.o
# M1056 (H3c): the `credential-helper` subcommand, the C89 port of the
# drive's git-credential-from-file. It is a top-level verb (git spawns it,
# like init), NOT under `mirror`; it is a prerequisite of the harness binary
# only, and is deliberately NOT in LIB_OBJ/LIB_SRC.
HARNESS_CRED = src/harness/jc_harness_cred.o
# M1057 (H4): the `run` command, one lead run in Mode A, the C89 port of the
# drive's run-lead.sh. Like HARNESS_INIT it is deliberately NOT in
# LIB_OBJ/LIB_SRC (it must not be linked into `jichi` itself as dead weight);
# it is a prerequisite of the harness binary only, where `run` is wired into
# the dispatch (it spawns the pinned jichi through jc_spawn, reading jichi's
# stdout/stderr into the run's records).
HARNESS_RUN = src/harness/jc_harness_run.o
# M1059 (H5): the `drive` command, the drive's outer loop until its end, the
# C89 port of the drive's loop.sh (the container half dropped, Mode A). Like
# HARNESS_RUN it is deliberately NOT in LIB_OBJ/LIB_SRC; it is a prerequisite
# of the harness binary only, where `drive` is wired into the dispatch (it
# spawns the run as its own child and calls the in-process mirror
# fetch/push, M1055/M1056).
HARNESS_DRIVE = src/harness/jc_harness_drive.o
# M1058 (H4): the handoff body, the C89 port of the drive's handoff.py (the
# "What the previous run left you" section, built from the previous run's own
# records). Like HARNESS_RUN it is deliberately NOT in LIB_OBJ/LIB_SRC; it is
# a prerequisite of the harness binary only, where `run` calls
# jc_harness_build_handoff before the spawn.
HARNESS_HANDOFF = src/harness/jc_harness_handoff.o

TEST_SRC = $(wildcard tests/test_*.c)
TEST_OBJ = $(TEST_SRC:.c=.o)

# Test-only helper tools (tests/tools, M209): the C89 instruments behind the
# Python-free smoke tier (tests/smoke/). Their pure cores are also linked
# into run_tests (tests/test_ttools.c exercises them); the binaries are
# built by `make smoke-tools`, never installed. ptydrive needs the XSI pty
# APIs (posix_openpt/grantpt), which _POSIX_C_SOURCE=200112L alone does not
# expose -- hence _XOPEN_SOURCE=600 (which subsumes it) for the whole group.
TT_CFLAGS  = $(STD) $(WARN) -D_XOPEN_SOURCE=600 $(INCLUDE)
TT_BIN     = tests/tools/mockmodel tests/tools/ptydrive tests/tools/jsonq \
             tests/tools/sockq tests/tools/mocklsp tests/tools/lo_up \
             tests/tools/udpblack tests/tools/rsshwm tests/tools/keyprobe
TT_CORE_OBJ = tests/tools/mm_core.o tests/tools/pd_core.o tests/tools/jq_core.o \
              tests/tools/tt_mult.o

# Fuzzing (M123): the in-tree deterministic driver + the target set. The
# libFuzzer shim (tests/fuzz/jc_fuzz_libfuzzer.c, M269) is NOT linked here -- it
# carries its own entry point and is built by the `libfuzz` target under FUZZ=1.
FUZZ_TARGET_OBJ = tests/fuzz/jc_fuzz_targets.o tests/fuzz/jc_fuzz_targets_fs.o
FUZZ_OBJ = tests/fuzz/jc_fuzz_main.o $(FUZZ_TARGET_OBJ)
FUZZ_BIN = run_fuzz
ITERS   ?= 3000
TARGET  ?=

# --- targets ---------------------------------------------------------------
.PHONY: all test clean info ci install install-check uninstall e2e \
        elisp-compile elisp-test fuzz libfuzz examples cpp-check fallbacks-compile


# Install locations (override with `make install PREFIX=... DESTDIR=...`).
PREFIX     ?= /usr/local
DESTDIR    ?=
BINDIR      = $(DESTDIR)$(PREFIX)/bin
MANDIR      = $(DESTDIR)$(PREFIX)/share/man/man1
BASHCOMPDIR = $(DESTDIR)$(PREFIX)/share/bash-completion/completions
ZSHCOMPDIR  = $(DESTDIR)$(PREFIX)/share/zsh/site-functions
EMACSDIR    = $(DESTDIR)$(PREFIX)/share/emacs/site-lisp
VIMDIR      = $(DESTDIR)$(PREFIX)/share/vim/vimfiles/plugin
EMACS      ?= emacs

all: $(BIN) $(CONVERT_BIN) $(HARNESS_BIN)

# Optional lint tier (M188): a second compiler front-end over the whole tree.
# The sources stay C89; g++'s stricter type system is a free static check
# (implicit void*/const conversions are errors there). Not part of `make ci`.
# CXX is overridable so a SECOND C++ front-end can be run over the same tree:
#   make cpp-check                 # g++
#   make cpp-check CXX=clang++     # a different set of opinions, for free
#   make cpp-check CXX="zig c++"   # a third, bundled with its own libc++
#
# M636 repaired four defects, all found by running the published commands from a
# CLEAN tree -- which is how CPP_BUILD.md publishes them, and how nobody had run
# them for 154 commits:
#
#   1. $(STAMP) IS A PREREQUISITE. src/util/jc_buildrev.c includes the generated
#      jc_buildrev_stamp.h (M495), and `make clean` deletes it (M593). This rule
#      declared no dependency on it, so cpp-check was green only when some other
#      build had already generated the header, and red when run as documented.
#      A check whose result depends on what you did before it is not a check.
#   2. -x c++ IS EXPLICIT. Every driver here dispatches on the .c extension and
#      then meets -std=c++17: g++ tolerated it, clang++ warned -Wdeprecated, and
#      `zig c++` refused outright ("invalid argument '-std=c++17' not allowed
#      with 'C'"). Naming the language is what makes the target front-end-neutral
#      rather than g++-shaped.
#   3. $(firstword $(CXX)) IN THE PROBE. `command -v "zig c++"` looks up a
#      command whose name contains a space, finds nothing, and reports the
#      compiler missing -- a false NOT FOUND for any multi-word driver.
#   4. -fsyntax-only IS PROBED, NOT ASSUMED. `zig c++` accepts the flag, injects
#      its own -c behind it, and then fails every file with a bare
#      "src/util/jc_str.c:1:1: error: FileNotFound" -- a message that names the
#      source and says nothing true about it. The probe compiles a three-token
#      program and reads the EXIT STATUS (measured: g++ 0, clang++ 0,
#      `zig c++` 1), then falls back to `-c -o /dev/null`, which all three
#      accept. Asking "does this driver accept the flag" would have answered
#      yes for zig; asking "does it compile something with it" answers no.
#
# tests/smoke/cppcheck_lint.sh guards all four. It stays out of `make ci` (the
# M188 decision holds: the C gate is the gate) -- the lint is what replaces the
# gate slot, at no build-time cost.
CXX ?= g++
# The file list is overridable so the lint can exercise the RECIPE (its
# prerequisite, its -x c++, its probe) against every installed front-end for the
# price of one translation unit. A full three-front-end sweep is 6 s + 8 s + 41 s;
# nothing that costs 55 s runs in the smoke tier, and a tier that is too slow to
# run is the failure mode this milestone is about.
CPPCHECK_SRC ?= $(LIB_SRC) $(MAIN_OBJ:.o=.c) $(TEST_SRC)
cpp-check: $(STAMP)
	@command -v $(firstword $(CXX)) >/dev/null || { echo "cpp-check: $(CXX) not found"; exit 1; }
	@d=$$(mktemp -d); printf 'int main(){return 0;}\n' > $$d/probe.cc; \
	if $(CXX) -x c++ -std=c++17 -fsyntax-only $$d/probe.cc >/dev/null 2>&1; then \
	    mode="-fsyntax-only"; \
	else \
	    mode="-c -o /dev/null"; \
	fi; rm -rf $$d; \
	fails=0; n=0; log=$$(mktemp); for f in $(CPPCHECK_SRC); do \
	  n=$$((n+1)); \
	  $(CXX) -x c++ -std=c++17 $$mode $(POSIX) $(INCLUDE) \
	      -DJC_HAVE_VSNPRINTF $(if $(HAVE_CURL),-DJC_HAVE_CURL $(CURL_CFLAGS)) \
	      $$f 2>$$log || { echo "cpp-check FAIL: $$f"; sed 's/^/    /' $$log; fails=1; }; \
	done; rm -f $$log; \
	[ $$fails -eq 0 ] && echo "cpp-check: OK ($(CXX) $$mode, $$n files parse as C++17)" || exit 1

# The pre-rename `jlu_continue` / `jlu-convert` aliases were removed at M487.
# They existed so wrappers in sibling projects could resolve
# `../jlu_continue/jlu_continue` by path, which is a concern local to one
# machine -- and `install` was symlinking both names into the user's $PREFIX/bin,
# so every stranger who ran `sudo make install` got two commands named after a
# project that has never existed publicly. The runtime state migration in
# src/main.c stays: that one protects real users' sessions and is smoke-tested.

# M586: `install` DOES NOT BUILD, and that is the whole point.
#
# It used to read `install: all`. Since the documented way to install is
# `sudo make install`, make then rebuilt the ENTIRE TREE AS ROOT before
# installing -- leaving every .o, every .d and both binaries owned by root.
# Measured on the maintainer's own tree after three such installs in one day:
# **352 root-owned files**, after which the next ordinary `make` died with
#
#     error: unable to open output file 'src/util/jc_diff.o': Operation not permitted
#
# The failure is invisible where it is caused: `sudo make install` SUCCEEDS.
# The damage only surfaces at the next build, by which time the cause is an hour
# and several commits away. That is why it survived unnoticed.
#
# So install installs. If the binaries are not there it says what to do rather
# than doing it, because the fix -- run `make` as yourself first -- is exactly
# what must NOT happen under sudo. The wording rule is M342/M360's: name the
# corrective action, never only the cause.
#
# Recovery, if a tree already has root-owned objects: plain `make clean` is
# enough and needs no sudo. Removing a file depends on write permission on its
# DIRECTORY, not on the file's owner -- checked rather than assumed.
install:
	@if [ ! -x "$(BIN)" ] || [ ! -x "$(CONVERT_BIN)" ] || [ ! -x "$(HARNESS_BIN)" ]; then \
	  echo "make: *** nothing to install: $(BIN), $(CONVERT_BIN) and/or $(HARNESS_BIN) are not built." >&2; \
	  echo "" >&2; \
	  echo "  Build them first AS YOUR NORMAL USER, then install:" >&2; \
	  echo "" >&2; \
	  echo "      make -j4" >&2; \
	  echo "      sudo make install" >&2; \
	  echo "" >&2; \
	  echo "  This target deliberately does not build. Building under sudo would" >&2; \
	  echo "  leave every object file owned by root and your next plain \`make\`" >&2; \
	  echo "  would fail with 'Operation not permitted' (M586). If that has" >&2; \
	  echo "  already happened, plain \`make clean\` fixes it -- no sudo needed." >&2; \
	  exit 1; \
	fi
# M593: WHAT you are about to install, checked before it is copied.
#
# THE DEFECT, reported by the operator: `sudo make install` after a gate run put a
# binary stamped `<rev>-dirty` into /usr/local/bin. M586 taught this target to
# refuse when there is NOTHING to install; it had nothing to say about installing
# the WRONG thing. The sequence that produces it is ordinary and ends in success
# at every step: build (tree still has uncommitted work) -> gate -> commit ->
# install. The binary is then stamped from a tree that no longer exists, and
# `jichi --version` reports a revision nobody can check out.
#
# The check reads the GENERATED STAMP, not git. `install` runs under sudo, and git
# as root in a user-owned repository can refuse with "detected dubious ownership"
# -- a check that silently skips under the one condition it exists for is worse
# than no check (this project's own finding, repeatedly). Reading the header that
# was compiled into the binary needs no git, no network and no ownership rules.
#
# ALLOW_DIRTY=1 installs anyway, for the case that is genuinely intended: trying
# an uncommitted change on the real PATH binary.
	@stamped=`sed -n 's/^#define JC_BUILD_REV "\(.*\)"$$/\1/p' $(STAMP) 2>/dev/null`; \
	 case "$$stamped" in \
	   *-dirty) \
	     if [ -z "$(ALLOW_DIRTY)" ]; then \
	       echo "make: *** refusing to install a binary built from a dirty tree." >&2; \
	       echo "" >&2; \
	       echo "  $(BIN), $(CONVERT_BIN) and $(HARNESS_BIN) are stamped '$$stamped' --" >&2; \
	       echo "  compiled while the tree had uncommitted changes, so their" >&2; \
	       echo "  \`--version\` will report a revision that cannot be checked out," >&2; \
	       echo "  and nobody -- including you in a week -- can tell which code is" >&2; \
	       echo "  on the PATH." >&2; \
	       echo "" >&2; \
	       echo "  Commit (or stash) your work, then AS YOUR NORMAL USER:" >&2; \
	       echo "" >&2; \
	       echo "      make -j4" >&2; \
	       echo "      sudo make install" >&2; \
	       echo "" >&2; \
	       echo "  To install it anyway:  sudo make install ALLOW_DIRTY=1" >&2; \
	       exit 1; \
	     fi; \
	     echo "install: WARNING -- installing '$$stamped', built from a dirty tree (ALLOW_DIRTY=1)." >&2 ;; \
	   "") : ;; \
	   *) echo "install: $(BIN) is stamped '$$stamped'." ;; \
	 esac
	install -d $(BINDIR)
# ONE FILE PER CALL, with an explicit destination path -- the form every other
# install line here already uses, and the only portable one. `install SRC1 SRC2
# DIR` is a GNU/BSD extension: illumos ships Sun's /usr/bin/install, which takes
# a single file, so this line installed jichi, SILENTLY SKIPPED jichi-convert and
# exited 0. Measured on OmniOS at M661 -- `make install` reported success and put
# one of the two binaries in place, which is the failure mode M586 and M593 built
# this target to avoid: an install that succeeds and is wrong.
	install -m755 $(BIN) $(BINDIR)/$(BIN)
	install -m755 $(CONVERT_BIN) $(BINDIR)/$(CONVERT_BIN)
	install -m755 $(HARNESS_BIN) $(BINDIR)/$(HARNESS_BIN)
	install -d $(MANDIR)
	install -m644 man/jichi.1 $(MANDIR)/jichi.1
	install -d $(BASHCOMPDIR)
	install -m644 completions/jichi.bash $(BASHCOMPDIR)/jichi
	install -d $(ZSHCOMPDIR)
	install -m644 completions/jichi.zsh $(ZSHCOMPDIR)/_jichi
	install -d $(EMACSDIR)
	install -m644 editors/emacs/jichi.el $(EMACSDIR)/jichi.el
	install -d $(VIMDIR)
	install -m644 editors/vim/jichi.vim $(VIMDIR)/jichi.vim
	install -m755 editors/nano/jichi-nano $(BINDIR)/jichi-nano
	@echo "installed to $(DESTDIR)$(PREFIX)"

# Post-install smoke check: confirm the binary runs and report health + PATH.
install-check:
	@if command -v jichi >/dev/null 2>&1; then \
	  echo "jichi on PATH: $$(command -v jichi)"; \
	  jichi --version 2>/dev/null || true; \
	  jichi doctor || true; \
	else \
	  echo "jichi is NOT on PATH."; \
	  echo "Add $(BINDIR) to PATH (and $(MANDIR) to MANPATH), then re-run"; \
	  echo "  make install-check"; \
	  echo "Or run the built binary directly: ./$(BIN) doctor"; \
	fi

uninstall:
	@# jlu_* are no longer installed (M487); still removed here so an
	@# install predating that milestone can be cleaned up by this target.
	rm -f $(BINDIR)/$(BIN) $(BINDIR)/$(CONVERT_BIN) $(BINDIR)/$(HARNESS_BIN) $(BINDIR)/jichi-nano \
	      $(BINDIR)/jlu_continue $(BINDIR)/jlu-convert \
	      $(MANDIR)/jichi.1 \
	      $(BASHCOMPDIR)/jichi $(ZSHCOMPDIR)/_jichi \
	      $(EMACSDIR)/jichi.el $(VIMDIR)/jichi.vim

$(BIN): $(MAIN_OBJ) $(LIB_OBJ) $(CONVERT_CORE)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(MAIN_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(LDLIBS) -o $@

$(CONVERT_BIN): $(CONVERT_MAIN) $(CONVERT_CORE) $(LIB_OBJ)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(CONVERT_MAIN) $(CONVERT_CORE) $(LIB_OBJ) $(LDLIBS) -o $@

$(HARNESS_BIN): $(HARNESS_MAIN) $(HARNESS_CONFIG) $(HARNESS_DOCTOR) $(HARNESS_INIT) $(HARNESS_MIRROR) $(HARNESS_PUSH) $(HARNESS_CRED) $(HARNESS_RUN) $(HARNESS_HANDOFF) $(HARNESS_DRIVE) $(CONVERT_CORE) $(LIB_OBJ)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(HARNESS_MAIN) $(HARNESS_CONFIG) $(HARNESS_DOCTOR) $(HARNESS_INIT) $(HARNESS_MIRROR) $(HARNESS_PUSH) $(HARNESS_CRED) $(HARNESS_RUN) $(HARNESS_HANDOFF) $(HARNESS_DRIVE) $(CONVERT_CORE) $(LIB_OBJ) $(LDLIBS) -o $@

# First-party objects.
%.o: %.c
	$(CC) $(JC_CFLAGS) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Test harness: links every test file against the library objects (no main.o).
test: $(TEST)
	$(SAN_RUN_ENV) ./$(TEST)

$(TEST): $(TEST_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(TT_CORE_OBJ) $(HARNESS_CONFIG)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(TEST_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(TT_CORE_OBJ) \
	      $(HARNESS_CONFIG) $(LDLIBS) -o $@

# The helper binaries: whole-program compiles (no shared .o with the product
# link; jc_snprintf.c is recompiled standalone so a SAN/SIZE build of the
# product never leaks flags into the helpers).
tests/tools/mockmodel: tests/tools/mockmodel.c tests/tools/mm_core.c \
                       tests/tools/tt_mult.c \
                       src/json/cJSON.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/mockmodel.c tests/tools/mm_core.c \
	      tests/tools/tt_mult.c \
	      src/json/cJSON.c src/platform/jc_snprintf.c -o $@ -lm $(SOCK_LIBS)

tests/tools/ptydrive: tests/tools/ptydrive.c tests/tools/pd_core.c \
                      tests/tools/tt_mult.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/ptydrive.c tests/tools/pd_core.c \
	      tests/tools/tt_mult.c src/platform/jc_snprintf.c -o $@ $(SOCK_LIBS)

tests/tools/jsonq: tests/tools/jsonq.c tests/tools/jq_core.c \
                   src/json/cJSON.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/jsonq.c tests/tools/jq_core.c \
	      src/json/cJSON.c src/platform/jc_snprintf.c -o $@ -lm

tests/tools/sockq: tests/tools/sockq.c tests/tools/tt_mult.c
	$(CC) $(TT_CFLAGS) tests/tools/sockq.c tests/tools/tt_mult.c -o $@ $(SOCK_LIBS)

# mocklsp (M930): the stdio LSP mock behind lsp_diag_grace.sh. Threads (the
# staged re-index and version re-sync) need -pthread; the pure I/O is the
# same shape as the other helpers, so it links tt_mult.c for the deadline
# multiplier only.
tests/tools/mocklsp: tests/tools/mocklsp.c tests/tools/tt_mult.c
	$(CC) $(TT_CFLAGS) tests/tools/mocklsp.c tests/tools/tt_mult.c -o $@ -pthread

# lo_up (M1062-c): brings up the netns loopback for the resolve_timeout_connect
# e2e cell. It uses the Linux ifreq ioctl (SIOCGIFFLAGS/SIOCSIFFLAGS, IFF_UP |
# IFF_RUNNING), which glibc hides under the group's strict -D_XOPEN_SOURCE=600;
# -D_GNU_SOURCE exposes them (the task's documented escape hatch -- no per-file
# flag hack, the define is the group's own TT_CFLAGS plus one GNU source macro).
tests/tools/lo_up: tests/tools/lo_up.c
	$(CC) $(TT_CFLAGS) -D_GNU_SOURCE tests/tools/lo_up.c -o $@

# udpblack (M1062-c): a UDP black hole (bind 127.0.0.1:PORT, recvfrom-loop,
# never answer) for the same cell's DNS resolver. select()/recvfrom() are
# exposed by TT_CFLAGS' _XOPEN_SOURCE=600, so no extra define is needed.
tests/tools/udpblack: tests/tools/udpblack.c
	$(CC) $(TT_CFLAGS) tests/tools/udpblack.c -o $@

# rsshwm (M1092-c): fork+exec the given command, wait4() it and print the
# child's rusage.ru_maxrss ("maxrss_kb N") -- the kernel's high-water mark
# of the child's resident set, the measurement the http_resp_cap driver's
# 50 ms /proc sampler could not catch (SV:0302 R2). wait4 is an SVR4/BSD
# extension that glibc hides once _XOPEN_SOURCE=600 (in TT_CFLAGS) is the
# only source macro; -D_DEFAULT_SOURCE restores it (the same feature set
# repomap_peak.sh's inline C uses, compiled without _XOPEN_SOURCE). No
# _GNU_SOURCE (lo_up's documented escape) is needed -- wait4 alone.
tests/tools/rsshwm: tests/tools/rsshwm.c
	$(CC) $(TT_CFLAGS) -D_DEFAULT_SOURCE tests/tools/rsshwm.c -o $@

# keyprobe (M1097-c): print the per-workspace store key a path derives --
# `keyprobe sha256 P` (the fixed 64-hex SHA-256 key) and `keyprobe djb2 P`
# (the old unsigned-long djb2). Pure C89 (no _DEFAULT_SOURCE), so it links the
# in-tree SHA-256 core (src/util/jc_sha256.c) directly and stays byte-for-byte
# on the product's own digest. Used by checkpoint_key_collision.sh to prove the
# collision pair's old keys are equal while its new ones separate.
tests/tools/keyprobe: tests/tools/keyprobe.c src/util/jc_sha256.c
	$(CC) $(TT_CFLAGS) tests/tools/keyprobe.c src/util/jc_sha256.c -o $@

.PHONY: smoke-tools smoke check-target
smoke-tools: $(TT_BIN)

# POSIX-sh smoke tier (M209): validates a build with NO python3 -- the
# on-target answer for old / low-resource systems. The full Python e2e
# suite (make e2e) remains the product gate on dev/CI machines.
smoke: $(BIN) $(HARNESS_BIN) smoke-tools
	sh tests/smoke/run.sh

# def-350 (an H3c follow-up, milestone M1056): the committed SAN cell for
# list_pushed's argv sizing. It is a REGRESSION cell for the fix at 29df6d33
# (the `+5` argv sizing): GREEN on the current tree, and it goes RED when the
# sizing is reverted to `+4`. WHY SAN, NOT FAULT: the defect is an out-of-bounds
# NULL-terminator write, caught by UBSan (`index 6 out of bounds for type
# char *[6]`), not an injected fault -- fault injection cannot make a correct
# array write wrong, but UBSan catches the one past the end. WHY LOWERED CAPS:
# at the REAL caps (PUSH_MAX_TIPS=24/PUSH_MAX_EXCL=48) the boundary is UNREACHABLE
# through a git fixture -- git_child refuses nargs>40 (push.c:448) and git_m2
# refuses nargs>8 (push.c:486), so a 76-argument rev-list never runs. The build
# therefore lowers the caps to 1/1 (EXTRA_DEFS), which lands the fixture at
# ntips=1, excl->n=1, an==6==T+E+4 (the legal max): the NULL terminator fits
# under `+5`, and under `+4` it is one past the end. The leading/trailing clean
# keep the tree in a normal 24/48 state outside this stage -- the lowered caps
# must not leak into the rest of ci.
#
# JC_PUSH_BOUNDARY_STAGE=1 is exported only around the driver run. A driver
# that t_skips here has vacuously passed the stage (the tooth is gone but the
# gate is green), so the driver reads the var and t_fails instead of t_skips
# at EITHER gate when it is set. Outside the stage (run.sh, ordinary smoke)
# the var is unset and the gates keep skipping.
.PHONY: smoke-push-boundary
smoke-push-boundary:
	$(MAKE) clean
	$(MAKE) WERROR=1 SAN=1 CC=gcc EXTRA_DEFS='-DPUSH_MAX_TIPS=1 -DPUSH_MAX_EXCL=1 -DJC_PUSH_BOUNDARY' jichi-harness
	JC_PUSH_BOUNDARY_STAGE=1 sh tests/smoke/harness_push_argv_boundary.sh
	$(MAKE) clean

# On-target validation for boxes without python3 (docs/LOW_MEMORY.md).
# `all`, not just test+smoke (M661). check-target is the documented on-target
# validation entry point, and the smoke tier includes install_no_build, which
# runs `make install` -- and install copies ALL THREE programs while `smoke`
# builds only $(BIN) and $(HARNESS_BIN) -- not jichi-convert -- which is why
# check-target builds `all`. So on every device row the driver
# failed for want of jichi-convert: "install refused a CLEAN stamp (rc=2)", which reads as a defect
# in the install target on that platform and is nothing of the kind. Measured on
# the Pi 400 (M658) and reproduced on the host by moving one file aside (M661).
# A documented target should provide what its own tier tests.
check-target: all test smoke

# Tier V row V6 (docs/plans/2026-07-hardware-testing.md): the X11 keystroke
# injector behind scripts/tier-v-terminals.sh, which drives jichi inside REAL
# terminal emulators -- the one thing a pty harness cannot do. Deliberately
# OUTSIDE smoke-tools, check-target and ci: it needs a live X server and it
# takes the keyboard focus while it runs. libX11/libXtst are dlopen'd at
# runtime, so this adds no build dependency -- only -ldl.
.PHONY: xdrive
xdrive: tests/tools/xdrive
tests/tools/xdrive: tests/tools/xdrive.c
	$(CC) $(TT_CFLAGS) tests/tools/xdrive.c -o $@ -ldl

# Tier V row V6, the virtual-console cell (M274): drives the REAL Linux VC via
# /dev/uinput + /dev/vcsa, behind scripts/tier-v-console.sh. Outside
# smoke-tools/check-target/ci for the same reason as xdrive, plus one more: it
# needs root and it switches the active VT while it runs.
.PHONY: vtdrive
vtdrive: tests/tools/vtdrive
tests/tools/vtdrive: tests/tools/vtdrive.c tests/tools/pd_core.c \
                     tests/tools/tt_mult.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/vtdrive.c tests/tools/pd_core.c \
	      tests/tools/tt_mult.c src/platform/jc_snprintf.c -o $@

# Fuzz sweep (M123): always under ASan+UBSan -- that is what catches the bugs.
# `make fuzz`                        bounded sweep over every target
# `make fuzz TARGET=json ITERS=1e7`  hunt one target hard
# `make fuzz JC_FUZZ_SEED=0x1234`    reproduce a specific run (env var)
# A clean rebuild forces SAN objects (the shared LIB_OBJ is otherwise non-SAN).
#
# Corpus: every run first replays tests/fuzz/corpus/<target>/* (override the
# root with JC_FUZZ_CORPUS), so a past find is a permanent regression. Workflow
# after a crash: save the exact bytes as corpus/<target>/crash-<desc> and commit
# them; `JC_FUZZ_ITERS=0 ./run_fuzz <target>` then replays the corpus alone,
# which is also how to prove the entry has teeth.
fuzz:
	$(MAKE) clean
	$(MAKE) SAN=1 $(FUZZ_BIN)
	JC_FUZZ_ITERS=$(ITERS) ./$(FUZZ_BIN) $(TARGET)

$(FUZZ_BIN): $(FUZZ_OBJ) $(LIB_OBJ) $(CONVERT_CORE)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(FUZZ_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(LDLIBS) -o $@

# Coverage-guided fuzzing (libFuzzer, M269) -- the opt-in depth pass over the
# SAME target registry, deliberately outside `ci`. The deterministic driver is a
# blind mutator: M269's broken path-fence boundary check survived 20,000 of its
# iterations because mutating a relative seed never synthesizes the absolute root
# prefix the broken branch needed. Coverage feedback is the tool for that.
#
# `make libfuzz TARGET=prop_pathfence`             hunt until a crash
# `make libfuzz TARGET=json LF_ARGS=-runs=100000`  bounded
# `make libfuzz ... LF_LDFLAGS=-L/path/to/stdc++`  see the note below
#
# Requires clang: libFuzzer is a clang runtime, and gcc has no equivalent. The
# runtime is C++, so linking needs a `libstdc++.so` the linker can find -- on a
# box with only the versioned runtime (libstdc++.so.6, no -dev package) the link
# fails with "cannot find -lstdc++"; point LF_LDFLAGS at a directory holding a
# libstdc++.so symlink rather than installing a toolchain to run one target.
# Findings go in the same corpus dirs the deterministic runner replays forever.
LF_BIN      = run_fuzz_lf
LF_OBJ      = tests/fuzz/jc_fuzz_libfuzzer.o $(FUZZ_TARGET_OBJ)
LF_SAN      = -fsanitize=fuzzer,address,undefined
LF_LDFLAGS ?=
LF_ARGS    ?=

libfuzz:
	@$(CC) --version 2>/dev/null | grep -qi clang || { \
	  echo "libfuzz: needs clang (libFuzzer is a clang runtime)."; \
	  echo "  make libfuzz CC=clang TARGET=<name>"; exit 1; }
	@test -n "$(TARGET)" || { \
	  echo "libfuzz: set TARGET=<name> (see ./$(FUZZ_BIN) with no args)"; \
	  exit 1; }
	$(MAKE) clean
	$(MAKE) FUZZ=1 $(LF_BIN)
	mkdir -p tests/fuzz/corpus/$(TARGET)
	JC_FUZZ_TARGET=$(TARGET) ./$(LF_BIN) $(LF_ARGS) \
	    tests/fuzz/corpus/$(TARGET)

$(LF_BIN): $(LF_OBJ) $(LIB_OBJ) $(CONVERT_CORE)
	$(CC) $(JC_LDFLAGS) $(LDFLAGS) $(LF_LDFLAGS) $(LF_OBJ) $(LIB_OBJ) $(CONVERT_CORE) \
	    $(LDLIBS) $(LF_SAN) -o $@

# Full local quality gate, mirroring the per-commit checks: build + unit tests
# under gcc and clang with -Werror, an ASan/UBSan test run, and valgrind. Needs
# gcc, clang and valgrind installed. (The repo is local-git-only; .github/
# workflows/ci.yml runs this same target if a remote is ever added.)
ci:
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc test
# M996 (L-27 follow-up): the one OPTIMISED, FORTIFIED, -Werror build stage.
# The default `test` builds with no -O, so glibc's `warn_unused_result`
# attribute never fires and an unchecked ftruncate/write/open sailed through
# every gate (the drain rewrite's unchecked ftruncate was the instance:
# the -O2 fortified build named it at jc_bg.c:377, which no stage compiled).
# This stage is red-first on exactly that class: a clean -O2 build before the
# fix aborted at test_harness.c:127 / jc_bg.c:377 / test_snapshot.c:470, all
# [-Werror=unused-result]. About the cost of one more test build.
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc OPT=-O2 test
	$(MAKE) clean && $(MAKE) WERROR=1 CC=clang test
# M871 (DEFERRED def-307, the operator's decision of 2026-10-02): the C++ build is
# GATED. M188 kept it out on purpose ("the C gate is the gate"); M636 found it
# rotted because nothing ran it; it rotted again within ten days -- a link error
# from M690 (jc_outcome.h, with no extern "C" guard of its own, included inside
# jc_agent.h's) and a compile error from M709 (an implicit void * conversion) --
# found only when the operator asked for every compiler (M867). cpp-check alone
# could not have held it: it compiles but never LINKS. So jichi and the unit suite
# are built and linked as C++ with g++, the suite runs, and cpp-check parses the
# tree. No WERROR: the -Wwrite-strings residue is real and documented
# (docs/CPP_BUILD.md). About 40 s.
	$(MAKE) clean && $(MAKE) CC=g++ jichi jichi-harness test
	$(MAKE) cpp-check
	$(MAKE) clean && $(MAKE) SAN=1 CC=clang test
# M693: the SUBCOMMANDS under LeakSanitizer, using the sanitizer build the line
# above just made. The unit suite never enters main()'s subcommand dispatch, so
# nothing in this gate had ever run `jichi doctor` under a leak checker -- and
# when somebody finally did there were two direct leaks sitting in it. The cost
# was never the bytes (the process exits); it was that a leak checker could not
# be USED on these paths, because a new leak would arrive as more lines in a
# report that already had some.
	$(MAKE) SAN=1 CC=clang jichi jichi-harness && sh scripts/leakcheck.sh ./jichi ./jichi-harness
# M694: and ONE REAL AGENT TURN under the same binary. leakcheck.sh covers
# seven read-only subcommands; none of them calls a model, so the provider /
# SSE / tool-execute / loop path -- the part that is actually jichi -- had no
# leak coverage anywhere in this gate. The driver skips unless the binary is a
# sanitizer build, which is why it runs HERE and not in the smoke tier.
	$(MAKE) smoke-tools && sh tests/smoke/leak_turn.sh
# M1127 (R01-N2): the M919 stranded-call loop under ASan -- two single-call
# rounds then a four-call round under --max-tool-calls 3, the second of the
# three stranded appends reallocs the history vector and dangles `am`. Skips
# unless the binary is a sanitizer build, so it runs HERE beside leak_turn.
	$(MAKE) smoke-tools && sh tests/smoke/stranded_call_uaf.sh
# def-350: the committed SAN argv-boundary cell for list_pushed (regression for
# the +5 sizing fixed at 29df6d33). SAN, not FAULT, because it is a real
# out-of-bounds write UBSan catches; lowered caps (1/1) because the boundary is
# unreachable at the real 24/48 (git_child refuses nargs>40). Self-contained:
# it cleans, builds its own lowered+SAN jichi-harness, runs the driver, and
# cleans again, so it sits beside leak_turn without disturbing the 24/48 tree.
	$(MAKE) smoke-push-boundary
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc $(TEST)
	valgrind --error-exitcode=1 --leak-check=full \
	         --suppressions=tests/valgrind.supp \
	         --errors-for-leak-kinds=definite,indirect ./$(TEST)
	$(MAKE) clean && $(MAKE) SAN=1 WERROR=1 CC=clang $(FUZZ_BIN) && \
	         $(SAN_RUN_ENV) JC_FUZZ_ITERS=2000 ./$(FUZZ_BIN)
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc all
# The CURL-FREE build must still link the unit suite (M447). This is the
# configuration Tier V row V0 and every static/cross recipe use
# (docs/DEPLOYMENT.md 3e), and nothing else in this gate compiles it: the
# regression that prompted this line put a PURE predicate
# (jc_http_conn_reusable) inside #ifdef JC_HAVE_CURL, which tests/test_http.c
# links unconditionally -- so `make HAVE_CURL= run_tests` failed with an
# undefined symbol while every curl-enabled build stayed green. M189 had to
# repair this same drift once before (~28 link errors then, one now), and both
# times it was found by someone cross-building, not by the gate. A text lint
# cannot see it; only a link can.
#
# And it RUNS UNDER 512 KB OF STACK (M727), the stack jichi gives itself on
# FreeMiNT, which runs exactly this build. MiNT does not grow a stack: `_stksize`
# in src/platform/jc_platform_posix.c fixes it (portability_lint 32 holds it at
# 512 KB or more, and 33 holds this limit to it), and past its end is the heap.
# The path resolver recursed once per symlink hop at ~16.5 KB a call, so the
# suite's `loop -> loop` case needed ~700 KB: a bus error in the guest and
# nothing at all under a host's 8 MB. Before the fix this stage segfaulted
# here; with it, the suite passes from 112 KB up.
	$(MAKE) clean && $(MAKE) WERROR=1 HAVE_CURL= $(TEST) && \
	         (ulimit -s 512 && ./$(TEST))
# M728: the suite refuses a TMPDIR longer than its fixtures were measured clean
# under (JC_TEST_TMPDIR_MAX), and it refuses BEFORE touching the filesystem, so
# a path one character too long that does not exist proves it. Past the limit
# the fixture buffers truncate, and a truncated `rm -rf` once removed the whole
# TMPDIR. The length is read from the source, not repeated here.
	n=$$(sed -n 's/^#define JC_TEST_TMPDIR_MAX \([0-9]*\).*/\1/p' tests/test_main.c) && \
	  TMPDIR=$$(printf "/%0$${n}d" 0) ./$(TEST) > /dev/null; test $$? -eq 2
# M729: and the suite COMPLETES on a TMPDIR it cannot write, reporting failures
# rather than dying of a signal, and writes nothing into the directory it runs
# from. The TMPDIR is a path below a regular file, which fails with ENOTDIR even
# for root. On one, the suite segfaulted in test_config (a JC_CHECK that read
# NULL on the next line, five more behind it), and test_gradecore, whose chdir
# had failed, wrote seven spec files into the tree. That is M452's host without
# a writable /tmp, which the gate had never been.
	t=$${TMPDIR:-/tmp}/jc_unwritable.$$$$ && : > $$t.file && ls -A | sort > $$t.before && \
	  { TMPDIR=$$t.file/x ./$(TEST) > $$t.log 2>&1; rc=$$?; }; \
	  ls -A | sort > $$t.after; \
	  if grep -q '^[0-9]* checks, [0-9]* failures$$' $$t.log && [ $$rc -eq 1 ] && \
	     cmp -s $$t.before $$t.after; then rm -f $$t.file $$t.before $$t.after $$t.log; \
	  else echo "unwritable TMPDIR: rc=$$rc, and the run did not complete or wrote into ."; \
	       echo "  log: $$t.log (M729)"; exit 1; fi
# The fallback paths the probes switch on, compiled under the gate's own flags
# (M723). Each is taken only on a platform the gate does not run -- FreeMiNT has
# neither clock_gettime nor mmap -- so without this line the first such platform
# is where a fallback's compile error is found. INSTALL.md said the clock
# fallback "is compiled under -Werror"; until M723 no stage of this gate did it.
	$(MAKE) WERROR=1 fallbacks-compile
# The FAULT=1 tier (M482). It is a SEPARATE BUILD, which is why it was missing:
# three drivers exist for jichi's error paths, each SKIPS on a normal binary, and
# no stage of this gate ever built one -- so they ran only when somebody typed
# `make clean && make FAULT=1` by hand, and nothing said they hadn't. Their first
# run in an unknown number of milestones found a real defect (a failed session
# enumeration reported as "(no saved sessions)" with exit 0, and as a well-formed
# empty array on the STABLE json interface) and three vacuous checks that could
# not have caught it. Placed before the final `all` so this gate still ends with
# an ordinary binary in the tree.
	$(MAKE) smoke-faults
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc all
	$(MAKE) WERROR=1 examples
	$(MAKE) WERROR=1 smoke
	$(MAKE) smoke-mutant
	$(MAKE) e2e
# M1129 (SV:1057, red-first): the RELEASE-mode stage -- a from-clean
# optimised, fortified, -Werror build of BOTH binaries plus the unit
# suite. No earlier stage built the harness at RELEASE (M1073's knob was
# exercised by its own driver only), so the three harness warnings
# (init.c:63 / run.c:96 -Wredundant-decls, run.c:670 -Wunused-result,
# both real under fortify+-O) reached no gate. -k is deliberate: on the
# unfixed tree this stage is red on all three and -k lets them surface
# in one run; the defect-A fix lands in a later commit.
	$(MAKE) clean && $(MAKE) -k RELEASE=1 WERROR=1 jichi jichi-harness test
# M1129 (SV:1057, red-first): the 32-bit stage -- jichi and the unit
# suite at -m32, curl-free (HAVE_CURL=: the 32-bit libcurl is not
# present, matching SV:1053's measured `gcc -m32 HAVE_CURL=`). Guarded
# by a multilib probe: where gcc -m32 compiles a trivial main the stage
# runs; where it does not it prints a SKIP line naming why and does not
# fail. -k is deliberate: on the unfixed tree this stage is red on the
# two -Wlong-long test literals and -k surfaces both; the defect-B fix
# lands in a later commit. It runs at the END of the gate (M1129-S-b):
# its 32-bit residue cannot reach the smoke/e2e stages, which reuse the
# tree's binary through up-to-date prerequisites.
	if echo 'int main(void){return 0;}' | gcc -m32 -x c - -o /dev/null 2>/dev/null; then \
	  echo 'ci: 32-bit stage (gcc -m32, HAVE_CURL=, WERROR=1)' && \
	  $(MAKE) clean && $(MAKE) -k CC='gcc -m32' HAVE_CURL= WERROR=1 jichi test; \
	else \
	  echo 'ci: SKIP 32-bit stage: no gcc -m32 multilib (a trivial main does not compile)'; \
	fi
# The gate's ordinary binary, restored after the two M1129 stages above
# self-cleaned the tree: ci still ends with an ordinary build (M723's
# FAULT-stage placement note).
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc all
	@echo "ci: OK (gcc + clang build/test, c++ build/test, asan/ubsan, leakcheck, valgrind, curl-free link, release, 32-bit, fallbacks, faults, smoke, mutant, e2e)"

# Compile each probe-switched fallback with its macro forced on (M723). -o
# /dev/null and no DEPFLAGS: this checks that the path compiles and leaves
# nothing behind, not even a .d file.
fallbacks-compile:
	$(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NO_CLOCK_GETTIME -c src/platform/jc_platform_posix.c -o /dev/null
	$(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NO_MMAP -c src/index/jc_index.c -o /dev/null
	@# M834: the DJGPP fallbacks compile everywhere -- select() for a sleep, and every
	@# file that includes jc_select.h (found by grep) without <sys/select.h>.
	$(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NO_NANOSLEEP -c src/platform/jc_platform_posix.c -o /dev/null
	for f in $$(grep -l '"jc_select.h"' src/*.c src/*/*.c tests/*.c); do \
	    $(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NO_SYS_SELECT_H -c $$f -o /dev/null || exit 1; \
	done
	@# M835: the no-sockets build compiles -- every file that names JC_NO_SOCKETS,
	@# found by grep, WITHOUT libcurl: a socket-less system has no libcurl, and the
	@# pair is an #error in jc_http.c. posix_utils_lint 6d holds every socket
	@# include to the guard, which a compile on a system WITH the headers cannot.
	for f in $$(grep -l 'JC_NO_SOCKETS' src/*.c src/*/*.c tests/*.c); do \
	    $(CC) $(filter-out $(DEPFLAGS) -DJC_HAVE_CURL,$(JC_CFLAGS)) -DJC_NO_SOCKETS -c $$f -o /dev/null || exit 1; \
	done
	@# M835: every positive probe answer taken away. For each JC_HAVE_* macro this
	@# build defines, every file that names it compiles with the macro undefined --
	@# the build of a system that lacks the thing. The DJGPP build found two test
	@# helpers unused without posix_spawn: a warning no gate machine could show,
	@# because every one of them has posix_spawn.
	for m in $(patsubst -D%,%,$(filter -DJC_HAVE_%,$(STD))); do \
	    for f in $$(grep -l "$$m" src/*.c src/*/*.c tests/*.c); do \
	        $(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -U$$m -c $$f -o /dev/null || exit 1; \
	    done; \
	done
	@# M769: the NOFORK measuring build compiles -- the platform file that holds
	@# the stand-ins, and every file that calls fork() or popen(), found by grep so
	@# that a new call site is checked without anyone adding it to a list.
	$(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NOFORK -Dfork=jc_nofork_fork -Dpopen=jc_nofork_popen -c src/platform/jc_platform_posix.c -o /dev/null
	for f in $$(grep -l -E '(^|[^a-z_])(fork|popen) *\(' src/*.c src/*/*.c); do \
	    $(CC) $(filter-out $(DEPFLAGS),$(JC_CFLAGS)) -DJC_NOFORK -Dfork=jc_nofork_fork -Dpopen=jc_nofork_popen -c $$f -o /dev/null || exit 1; \
	done

# Can a driver notice the product disappearing? `make smoke-mutant`.
#
# Runs each CHANGED driver against a binary that prints nothing and exits 0. A
# driver that stays green is measuring its own fixtures rather than jichi -- the
# defect class this project keeps finding in its own checks, six times in one
# session, and the one a syntactic lint could not detect because the vulnerable
# unit is a check and grep can only see a file (scripts/mutant-sweep.sh states the
# measurement that killed that approach).
#
# CHANGED-ONLY BY DEFAULT, and that is the design rather than a compromise: a
# vacuous check is introduced when a driver is written, so sweeping what git
# reports as touched catches it at the moment of authorship for a few seconds,
# where the full sweep costs ~9 minutes. `MUTANT_ALL=1 make smoke-mutant` does
# everything, and is worth a run before a release. A batch of commits gated by one
# `make ci` passes `MUTANT_BASE=<last pushed commit>`: a clean checkout has no
# changed files, and HEAD's diff is only the last commit of the batch (M842).
#
# It comes AFTER `smoke`, so a driver that is simply broken fails there first with
# a useful message rather than here as "stayed green".
.PHONY: smoke-mutant
smoke-mutant:
	sh scripts/mutant-sweep.sh

# The error-path tier, runnable on its own: `make smoke-faults`.
#
# It cleans first because FAULT=1 changes JC_CFLAGS (via FAULTFLAGS) for every
# translation unit, so a tree built without it would link stale objects and the
# drivers would skip on a binary that looks built.
# Each driver is named explicitly rather than globbed:
# smoke_lint asserts this list matches the drivers that require the build, so
# adding a fourth fault driver fails the gate until it is wired in here.
# M642: the unit suite runs here too, because `#ifdef JC_FAULT` tests
# (test_session.c since M198, test_diff.c since M642) are compiled out of every
# other stage of `make ci` -- the M198 one had never run under the gate.
.PHONY: smoke-faults
smoke-faults:
	$(MAKE) clean
	$(MAKE) WERROR=1 FAULT=1 $(BIN) smoke-tools
	sh tests/smoke/faults.sh
	sh tests/smoke/faults_net.sh
	sh tests/smoke/faults_net_midstream.sh
	sh tests/smoke/lease_race.sh
	sh tests/smoke/board_concurrent.sh
	sh tests/smoke/fault_merge_copy.sh
	sh tests/smoke/relay_rss_guard.sh
	sh tests/smoke/write_fail_restores.sh
	sh tests/smoke/inplace_rewrite.sh
	sh tests/smoke/bg_spool_rewrite.sh
	sh tests/smoke/resolve_stall_connect.sh
	# C-15 (M1133): the libcurl capability driver is a NORMAL run.sh driver,
	# but its check-4 probe leg only fires under FAULT=1 (JICHI_FAULT_NO_TLS),
	# so it is named here too -- its probe leg runs in the faults tier.
	sh tests/smoke/libcurl_capability.sh
	# M1087 (SV:1804, GAP 1): the F-02 driver was named in run.sh but ran
	# NOWHERE in `make ci` -- it skips without a FAULT=1 binary, so no gate
	# stage ever built the one it needs.
	sh tests/smoke/session_save_failure_says_so.sh
	# M1107 (D-25): a failed config-set save keeps the original; the driver
	# skips on a normal binary, so it is named here or smoke_lint check 16
	# fires (the orphan-of-the-faults-tier check).
	sh tests/smoke/fault_config_loss.sh
	# C-16 (M1134): the pid-scoped HOME fallback is CHECKED, not assumed; the
	# driver skips on a normal binary (JICHI_FAULT_HOME_NOPASSWD is compiled out
	# there), so it is named here or smoke_lint check 16 fires (the
	# orphan-of-the-faults-tier check).
	sh tests/smoke/state_root_pid_fallback.sh
	$(MAKE) WERROR=1 FAULT=1 test
	@echo "smoke-faults: OK (the fault drivers + the unit suite, FAULT=1)"

# Compile the shipped example programs under the project's strict flags so a
# reference artifact can't rot. Currently the C89 autonomous-loop supervisor.
examples:
	$(MAKE) -C examples/autonomous-loop WERROR=$(WERROR) CC=$(CC)
	$(MAKE) -C examples/autonomous-loop clean

# Offline interactive/CLI E2E (network-free). Set JC_E2E_MODEL=<id> to also run
# the live-model headless check. Needs python3 -- this is the full product
# gate; the Python-free build-validation tier is `make smoke` (M209).
e2e: $(BIN) elisp-test
	sh tests/e2e/run.sh

# Optional external tools, probed at parse time (M475). These gate targets
# that must NO-OP -- never fail -- when the tool is absent. The probe lives
# here rather than in the recipe because a shell `exit 0` inside one recipe
# line cannot stop make from running the NEXT line: that was the bug, and it
# broke `make ci`'s last stage on any box without emacs while printing
# "skipping" as though it had worked. A make-level conditional omits the
# lines outright.
EMACS_OK := $(shell command -v $(EMACS) >/dev/null 2>&1 && echo yes)
MARP_OK  := $(shell { command -v npx >/dev/null 2>&1 || command -v marp >/dev/null 2>&1; } && echo yes)

# Emacs integration (editors/emacs/jichi.el): byte-compile with warnings-as-errors
# and run the ERT suite against a stub binary (offline, no network). Both are a
# no-op when emacs is not installed, so they never break the CI matrix.
elisp-compile:
ifeq ($(EMACS_OK),yes)
	$(EMACS) -Q --batch -L editors/emacs \
	  --eval '(setq byte-compile-error-on-warn t)' \
	  -f batch-byte-compile editors/emacs/jichi.el
	@rm -f editors/emacs/jichi.elc
else
	@echo "emacs not found; skipping elisp-compile"
endif

elisp-test: elisp-compile
ifeq ($(EMACS_OK),yes)
	$(EMACS) -Q --batch -L editors/emacs -L tests/elisp \
	  -l tests/elisp/jichi-tests.el -f ert-run-tests-batch-and-exit
else
	@echo "emacs not found; skipping elisp-test"
endif

# Render the Marp presentation decks to HTML in docs/presentations/out/: the
# English decks (docs/presentations/*.md) at the top level, and every localized
# deck (docs/i18n/<lang>/presentations/*.md) into a per-language subdir
# out/<lang>/. A no-op with a note when marp/npx is unavailable, so it never
# breaks a build or CI (same pattern as the elisp targets).
MARP ?= npx --yes @marp-team/marp-cli
slides:
ifneq ($(MARP_OK),yes)
	@echo "npx/marp not found; skipping slides (see docs/presentations/README.md)"
else
	@mkdir -p docs/presentations/out
	@for f in docs/presentations/[0-9]*.md; do \
	  echo "rendering $$f"; \
	  $(MARP) "$$f" -o "docs/presentations/out/$$(basename $$f .md).html" || exit 1; \
	done
	@for d in docs/i18n/*/presentations; do \
	  [ -d "$$d" ] || continue; \
	  lang=$$(basename $$(dirname "$$d")); \
	  mkdir -p "docs/presentations/out/$$lang"; \
	  for f in "$$d"/[0-9]*.md; do \
	    [ -e "$$f" ] || continue; \
	    echo "rendering $$f"; \
	    $(MARP) "$$f" -o "docs/presentations/out/$$lang/$$(basename $$f .md).html" || exit 1; \
	  done; \
	done
	@echo "decks in docs/presentations/out/ (localized under out/<lang>/)"
endif

info:
	@echo "CC             = $(CC)"
	@echo "STD_DIALECT    = $(STD_DIALECT)"
	@echo "HAVE_VSNPRINTF = $(HAVE_VSNPRINTF)$(if $(filter yes,$(HAVE_VSNPRINTF_XOPEN)), (declared only at -D_XOPEN_SOURCE=600))$(if $(filter yes,$(HAVE_VSNPRINTF)),, -- the C89 fallback formatter has no width/precision/flags)"
	@echo "HAVE_CURL      = $(HAVE_CURL)"
	@echo "CURL_HEADER    = $(if $(filter yes,$(HAVE_CURL)),$(if $(filter yes,$(CURL_HDR_CLEAN)),clean under this dialect,needs $(strip $(CURL_HDR_CFLAGS)) for src/net/jc_http.o),n/a (no libcurl))"
	@echo "HAVE_MALLOC_TRIM = $(HAVE_MALLOC_TRIM)"
	@echo "SET_DUMPABLE   = $(if $(filter yes,$(HAVE_SET_DUMPABLE)),declared (jichi makes itself non-dumpable at startup; doctor shows it took),absent (the keys are not hidden from same-user processes; doctor warns))"
	@echo "POSIX_SPAWN    = $(if $(filter yes,$(HAVE_POSIX_SPAWN)),declared (JICHI_SPAWN_BACKEND=spawn starts children without fork),absent (jc_spawn always forks; the env var is inert))"
	@echo "FCHDIR         = $(if $(filter yes,$(JC_HAVE_FCHDIR)),present (the posix_spawn backend restores jichi's cwd through a held dirfd),absent (the cwd restore falls back to the path-based chdir; B-09's window survives there))"
	@echo "PROC_TRACE_CTL = $(if $(filter yes,$(HAVE_PROC_TRACE_CTL)),declared (FreeBSD: jichi makes itself untraceable at startup; doctor shows it took),$(if $(filter yes,$(HAVE_SET_DUMPABLE)),n/a (not FreeBSD; PR_SET_DUMPABLE covers this row),absent (no same-user environment protection on this platform; doctor warns)))"
	@echo "POSIX_UNGATE   = $(if $(POSIX_UNGATE),$(POSIX_UNGATE) (this libc gates POSIX behind __STRICT_ANSI__; the compiler stays -std=c89),none needed)"
	@echo "LARGE_FILE_DEFS = $(if $(LARGE_FILE_DEFS),$(LARGE_FILE_DEFS) (compiles under this dialect; 32-bit builds get large-file support, D-10; the 64-bit time_t is E-09's card, not probed here),none (this libc refuses the macro; large-file support is the platform's default))"
	@echo "SYS_SELECT_H   = $(if $(filter yes,$(HAVE_SYS_SELECT_H)),present,absent (jc_select.h uses <time.h>))"
	@echo "NANOSLEEP      = $(if $(filter yes,$(HAVE_NANOSLEEP)),declared,absent (jc_sleep_ms sleeps with select()))"
	@echo "CLOCK_GETTIME  = $(if $(filter yes,$(HAVE_CLOCK)),in libc,$(if $(RT_LIBS),needs -lrt,absent (coarse time() fallback)))"
	@echo "LSTAT          = $(if $(filter yes,$(HAVE_LSTAT)),declared under strict POSIX,$(if $(filter yes,$(HAVE_LSTAT_XOPEN)),needs -D_XOPEN_SOURCE=600 (MiNTLib),ABSENT -- the build will not compile))"
	@echo "MMAP           = $(if $(filter yes,$(HAVE_MMAP)),in libc,absent (jc_index reads a copy))"
	@echo "WINSIZE        = $(if $(filter yes,$(HAVE_WINSZ)),visible under strict POSIX,$(if $(filter yes,$(HAVE_WINSZ_EXT)),needs -D__EXTENSIONS__ (illumos/Solaris),ABSENT))"
	@echo "SOCKET_LIBS    = $(if $(filter yes,$(HAVE_SOCK)),in libc,$(if $(SOCK_LIBS),$(SOCK_LIBS),$(if $(NO_SOCKETS),none -- no <sys/socket.h>; JC_NO_SOCKETS: no daemon or control channel,UNRESOLVED)))"
	@echo "LONG_NAMES     = $(if $(filter yes,$(HAVE_USE_LFN)),asked at run time (_use_lfn: DOS keeps 8.3 names without DOSLFN),always (no 8.3 limit here))"
	@echo "STREAMS_PTY    = $(if $(filter yes,$(HAVE_STREAMS_PTY)),yes (pty slave needs ptem/ldterm pushed),no (a pty slave is a terminal already))"
	@echo "SIZEFLAGS      = $(SIZEFLAGS)"
	@echo "WARN_OPTIONAL  = $(if $(strip $(WARN_OPTIONAL)),$(WARN_OPTIONAL),(none: this compiler has no GCC-only warning flags))"
	@echo "EMACS_OK       = $(if $(EMACS_OK),yes,no (elisp-compile/elisp-test will no-op))"
	@echo "MARP_OK        = $(if $(MARP_OK),yes,no (slides will no-op))"
	@echo "HARDEN         = $(HARDEN)"
	@echo "HARDENFLAGS    = $(HARDENFLAGS)"
	@echo "HARDENLDFLAGS  = $(HARDENLDFLAGS)"
# M693: the note has to follow the FLAG, not OPT. Under SIZE=1 the optimization
# arrives as SIZEFLAGS (-Os), so _FORTIFY_SOURCE IS set and this line still said
# "inert" -- a diagnostic contradicting the build it describes, which is the
# defect this session kept finding elsewhere.
# M1071 (E-05): the note is now driven by the SAME OPTLEVEL gate the build uses,
# and it names the PROBED level, not a hard-coded "-Os". The old text said
# "(none, but SIZEFLAGS carries -Os so _FORTIFY_SOURCE is live)" whenever SIZEFLAGS
# was non-empty -- FALSE for LTO=1 alone (SIZEFLAGS carries -flto, not -Os) and for
# OPT=-O0/-g, where no optimisation level is in play and fortify is inert anyway.
	@echo "OPT            = $(OPT)$(if $(strip $(OPT)),,$(if $(strip $(OPTLEVEL)), (none, but SIZEFLAGS carries $(OPTLEVEL)),)) ($(FORTIFY_NOTE))"
	@echo "JC_CFLAGS      = $(JC_CFLAGS)"
	@echo "CFLAGS (user)  = $(CFLAGS)"
	@echo "CPPFLAGS (user)= $(CPPFLAGS)"
	@echo "JC_LDFLAGS     = $(JC_LDFLAGS)"
ifeq ($(STATIC),1)
	@echo "STATIC_MODE    = $(STATIC_MODE)"
endif
	@echo "LDFLAGS (user) = $(LDFLAGS)"
	@echo "LIB_SRC        = $(LIB_SRC)"
	@echo "TEST_SRC       = $(TEST_SRC)"

clean:
	rm -f $(BIN) $(CONVERT_BIN) $(HARNESS_BIN) $(TEST) $(MAIN_OBJ) $(LIB_OBJ) \
	      $(TEST_OBJ) $(CONVERT_CORE) $(CONVERT_MAIN) $(HARNESS_MAIN) \
	      $(HARNESS_CONFIG) $(HARNESS_DOCTOR) $(HARNESS_INIT) $(HARNESS_MIRROR) \
	      $(HARNESS_PUSH) $(HARNESS_CRED) $(HARNESS_RUN) $(HARNESS_HANDOFF) \
	      $(HARNESS_DRIVE) \
	      editors/emacs/jichi.elc \
	      $(FUZZ_BIN) $(FUZZ_OBJ) $(LF_BIN) $(LF_OBJ) \
	      $(TT_BIN) $(TT_CORE_OBJ) tests/tools/xdrive \
	      jlu_continue jlu-convert
	rm -f .jc_probe.*.out .jc_probe.*.out.exe .jc_probe.*.c
# M593: the stamp too. Its recipe replaces it only when the CONTENT changes, so
# after a build under sudo it is the one file left root-owned by `make clean` --
# the cleanup M586 points people at to undo exactly that damage.
	rm -f $(STAMP) $(STAMP).new
	find . -name '*.d' -delete

# Header-dependency files emitted by -MMD (regenerated on every compile).
-include $(LIB_OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d) \
         $(CONVERT_MAIN:.o=.d) $(CONVERT_CORE:.o=.d) $(TT_CORE_OBJ:.o=.d) \
         $(HARNESS_MAIN:.o=.d)
