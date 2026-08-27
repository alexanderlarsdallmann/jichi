# Makefile for jichi - a C89 rewrite of the Continue CLI.
#
# First-party code is compiled strict ANSI C89 (-std=c89 -pedantic). The
# vendored cJSON is compiled C89 but without -pedantic to avoid style noise.
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
  | $(CC) -std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) 2>/dev/null \
  && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(STD_C89_OK),yes)
  STD    = -std=c89 -pedantic
  STD_DIALECT = c89 (strict)
else
  STD    = -std=gnu89 -pedantic
  STD_DIALECT = gnu89 (this platform's headers are not C89-parseable)
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
cc_warn_ok = $(shell printf 'int main(void){return 0;}\n' \
  | $(CC) -Werror $(1) -xc - -o $(PROBE_OUT) 2>/dev/null && echo $(1); rm -f $(PROBE_OUT) $(PROBE_OUT).exe)

WARN_OPTIONAL := $(call cc_warn_ok,-Wlogical-op) \
                 $(call cc_warn_ok,-Wduplicated-cond) \
                 $(call cc_warn_ok,-Wjump-misses-init)

WARN     = -Wall -Wextra \
           -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
           -Wvla -Walloca \
           -Wpointer-arith -Wredundant-decls -Wundef \
           $(WARN_OPTIONAL)
POSIX    = -D_POSIX_C_SOURCE=200112L

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
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) 2>/dev/null && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
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
  | $(CC) -std=c89 -Werror=implicit-function-declaration -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) 2>/dev/null && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_MALLOC_TRIM),yes)
  STD += -DJC_HAVE_MALLOC_TRIM
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
  | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) 2>/dev/null && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifneq ($(HAVE_CLOCK),yes)
  HAVE_CLOCK_RT := $(shell printf '$(CLOCK_PROBE)' \
    | $(CC) -std=c89 -D_POSIX_C_SOURCE=200112L -xc - -o $(PROBE_OUT) -lrt 2>/dev/null && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
  ifeq ($(HAVE_CLOCK_RT),yes)
    RT_LIBS = -lrt
  else
    STD += -DJC_NO_CLOCK_GETTIME
  endif
endif

# Prefer pkg-config for curl flags; fall back to a bare -lcurl probe.
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_PC_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)
HAVE_CURL := $(shell printf '\043include <curl/curl.h>\nint main(void){return 0;}\n' \
  | $(CC) -xc - $(CURL_CFLAGS) -o $(PROBE_OUT) $(CURL_PC_LIBS) -lcurl 2>/dev/null && echo yes; rm -f $(PROBE_OUT) $(PROBE_OUT).exe)
ifeq ($(HAVE_CURL),yes)
  ifeq ($(CURL_PC_LIBS),)
    CURL_LIBS = -lcurl
  else
    CURL_LIBS = $(CURL_PC_LIBS)
  endif
  STD += -DJC_HAVE_CURL $(CURL_CFLAGS)
endif

# libm for floor() in the JSON number printer.
LDLIBS = -lm $(RT_LIBS) $(CURL_LIBS)

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
ifeq ($(SIZE),1)
  SIZEFLAGS   = -Os -ffunction-sections -fdata-sections
  SIZELDFLAGS = -Wl,--gc-sections -s
endif
ifeq ($(LTO),1)
  SIZEFLAGS   += -flto
  SIZELDFLAGS += -flto
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
# without optimization (verified both ways). See the OPT note below.
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
  HARDENFLAGS := $(call cc_warn_ok,-fstack-protector-strong) \
                 $(call cc_warn_ok,-fstack-clash-protection) \
                 $(call cc_warn_ok,-fcf-protection) \
                 $(call cc_warn_ok,-Wformat -Werror=format-security) \
                 $(call cc_warn_ok,-fPIE)
  HARDENLDFLAGS := $(call cc_warn_ok,-Wl$(comma)-z$(comma)relro) \
                   $(call cc_warn_ok,-Wl$(comma)-z$(comma)now) \
                   $(call cc_warn_ok,-Wl$(comma)-z$(comma)noexecstack) \
                   $(call cc_warn_ok,-pie)
  # _FORTIFY_SOURCE only where there IS optimization, because it is a no-op
  # otherwise and glibc warns when it is set without one. -U first: a distro gcc
  # may already define it, and redefining is a warning this build treats as an
  # error under WERROR=1.
  ifneq ($(strip $(OPT)$(SIZEFLAGS)),)
    HARDENFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
  endif
endif
# OPT: optimization for the default build. EMPTY by default, which keeps the
# historical behaviour exactly (the tree has always built at -O0 for build speed
# and debuggability, and `make info` says so). Set it -- `make OPT=-O2` -- to get
# an optimized build AND, with it, a live _FORTIFY_SOURCE. SIZE=1 already implies
# -Os and therefore fortify.
OPT ?=

CFLAGS    = $(STD) $(WARN) $(POSIX) $(INCLUDE) $(DEPFLAGS) $(SANFLAGS) $(SIZEFLAGS) $(FAULTFLAGS) $(OPT) $(HARDENFLAGS)

LDLIBS   += $(SANFLAGS)
# Link-time flags. The size profile adds section GC + strip (and -flto with LTO);
# they sit before the objects on the link line. `-ffunction-sections
# -fdata-sections` in CFLAGS is what makes --gc-sections effective.
LDFLAGS   = $(SIZELDFLAGS) $(HARDENLDFLAGS)

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
  src/util/jc_delegreport.c \
  src/util/jc_toolloop.c \
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
             tests/tools/sockq
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
        elisp-compile elisp-test fuzz libfuzz examples cpp-check


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

all: $(BIN) $(CONVERT_BIN)

# Optional lint tier (M188): a second compiler front-end over the whole tree.
# The sources stay C89; g++'s stricter type system is a free static check
# (implicit void*/const conversions are errors there). Not part of `make ci`.
# CXX is overridable so a SECOND C++ front-end can be run over the same tree:
#   make cpp-check              # g++
#   make cpp-check CXX=clang++  # a different set of opinions, for free
CXX ?= g++
cpp-check:
	@command -v $(CXX) >/dev/null || { echo "cpp-check: $(CXX) not found"; exit 1; }
	@fails=0; log=$$(mktemp); for f in $(LIB_SRC) $(MAIN_OBJ:.o=.c) $(TEST_SRC); do \
	  $(CXX) -std=c++17 -fsyntax-only $(POSIX) $(INCLUDE) \
	      -DJC_HAVE_VSNPRINTF $(if $(HAVE_CURL),-DJC_HAVE_CURL $(CURL_CFLAGS)) \
	      $$f 2>$$log || { echo "cpp-check FAIL: $$f"; sed 's/^/    /' $$log; fails=1; }; \
	done; rm -f $$log; \
	[ $$fails -eq 0 ] && echo "cpp-check: OK ($(CXX), whole tree parses as C++17)" || exit 1

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
	@if [ ! -x "$(BIN)" ] || [ ! -x "$(CONVERT_BIN)" ]; then \
	  echo "make: *** nothing to install: $(BIN) and/or $(CONVERT_BIN) are not built." >&2; \
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
	       echo "  $(BIN) is stamped '$$stamped'. It was compiled while the tree" >&2; \
	       echo "  had uncommitted changes, so \`jichi --version\` will report a" >&2; \
	       echo "  revision that cannot be checked out, and nobody -- including you" >&2; \
	       echo "  in a week -- can tell which code is on the PATH." >&2; \
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
	install -m755 $(BIN) $(CONVERT_BIN) $(BINDIR)
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
	rm -f $(BINDIR)/$(BIN) $(BINDIR)/$(CONVERT_BIN) $(BINDIR)/jichi-nano \
	      $(BINDIR)/jlu_continue $(BINDIR)/jlu-convert \
	      $(MANDIR)/jichi.1 \
	      $(BASHCOMPDIR)/jichi $(ZSHCOMPDIR)/_jichi \
	      $(EMACSDIR)/jichi.el $(VIMDIR)/jichi.vim

$(BIN): $(MAIN_OBJ) $(LIB_OBJ) $(CONVERT_CORE)
	$(CC) $(LDFLAGS) $(MAIN_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(LDLIBS) -o $@

$(CONVERT_BIN): $(CONVERT_MAIN) $(CONVERT_CORE) $(LIB_OBJ)
	$(CC) $(LDFLAGS) $(CONVERT_MAIN) $(CONVERT_CORE) $(LIB_OBJ) $(LDLIBS) -o $@

# First-party objects.
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test harness: links every test file against the library objects (no main.o).
test: $(TEST)
	$(SAN_RUN_ENV) ./$(TEST)

$(TEST): $(TEST_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(TT_CORE_OBJ)
	$(CC) $(LDFLAGS) $(TEST_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(TT_CORE_OBJ) \
	      $(LDLIBS) -o $@

# The helper binaries: whole-program compiles (no shared .o with the product
# link; jc_snprintf.c is recompiled standalone so a SAN/SIZE build of the
# product never leaks flags into the helpers).
tests/tools/mockmodel: tests/tools/mockmodel.c tests/tools/mm_core.c \
                       tests/tools/tt_mult.c \
                       src/json/cJSON.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/mockmodel.c tests/tools/mm_core.c \
	      tests/tools/tt_mult.c \
	      src/json/cJSON.c src/platform/jc_snprintf.c -o $@ -lm

tests/tools/ptydrive: tests/tools/ptydrive.c tests/tools/pd_core.c \
                      tests/tools/tt_mult.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/ptydrive.c tests/tools/pd_core.c \
	      tests/tools/tt_mult.c src/platform/jc_snprintf.c -o $@

tests/tools/jsonq: tests/tools/jsonq.c tests/tools/jq_core.c \
                   src/json/cJSON.c src/platform/jc_snprintf.c
	$(CC) $(TT_CFLAGS) tests/tools/jsonq.c tests/tools/jq_core.c \
	      src/json/cJSON.c src/platform/jc_snprintf.c -o $@ -lm

tests/tools/sockq: tests/tools/sockq.c tests/tools/tt_mult.c
	$(CC) $(TT_CFLAGS) tests/tools/sockq.c tests/tools/tt_mult.c -o $@

.PHONY: smoke-tools smoke check-target
smoke-tools: $(TT_BIN)

# POSIX-sh smoke tier (M209): validates a build with NO python3 -- the
# on-target answer for old / low-resource systems. The full Python e2e
# suite (make e2e) remains the product gate on dev/CI machines.
smoke: $(BIN) smoke-tools
	sh tests/smoke/run.sh

# On-target validation for boxes without python3 (docs/LOW_MEMORY.md).
check-target: test smoke

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
	$(CC) $(LDFLAGS) $(FUZZ_OBJ) $(LIB_OBJ) $(CONVERT_CORE) $(LDLIBS) -o $@

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
	$(CC) $(LDFLAGS) $(LF_LDFLAGS) $(LF_OBJ) $(LIB_OBJ) $(CONVERT_CORE) \
	    $(LDLIBS) $(LF_SAN) -o $@

# Full local quality gate, mirroring the per-commit checks: build + unit tests
# under gcc and clang with -Werror, an ASan/UBSan test run, and valgrind. Needs
# gcc, clang and valgrind installed. (The repo is local-git-only; .github/
# workflows/ci.yml runs this same target if a remote is ever added.)
ci:
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc test
	$(MAKE) clean && $(MAKE) WERROR=1 CC=clang test
	$(MAKE) clean && $(MAKE) SAN=1 CC=clang test
	$(MAKE) clean && $(MAKE) WERROR=1 CC=gcc $(TEST)
	valgrind --error-exitcode=1 --leak-check=full \
	         --suppressions=tests/valgrind.supp \
	         --errors-for-leak-kinds=definite,indirect ./$(TEST)
	$(MAKE) clean && $(MAKE) SAN=1 CC=clang $(FUZZ_BIN) && \
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
	$(MAKE) clean && $(MAKE) WERROR=1 HAVE_CURL= $(TEST) && ./$(TEST)
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
	@echo "ci: OK (gcc + clang build/test, asan/ubsan, valgrind, curl-free link, faults, smoke, mutant, e2e)"

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
# everything, and is worth a run before a release.
#
# It comes AFTER `smoke`, so a driver that is simply broken fails there first with
# a useful message rather than here as "stayed green".
.PHONY: smoke-mutant
smoke-mutant:
	sh scripts/mutant-sweep.sh

# The error-path tier, runnable on its own: `make smoke-faults`.
#
# It cleans first because FAULT=1 changes CFLAGS for every translation unit, so a
# tree built without it would link stale objects and the drivers would skip on a
# binary that looks built. Each driver is named explicitly rather than globbed:
# smoke_lint asserts this list matches the drivers that require the build, so
# adding a fourth fault driver fails the gate until it is wired in here.
.PHONY: smoke-faults
smoke-faults:
	$(MAKE) clean
	$(MAKE) WERROR=1 FAULT=1 $(BIN) smoke-tools
	sh tests/smoke/faults.sh
	sh tests/smoke/faults_net.sh
	sh tests/smoke/faults_net_midstream.sh
	@echo "smoke-faults: OK (3 drivers, FAULT=1)"

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
	@echo "HAVE_VSNPRINTF = $(HAVE_VSNPRINTF)"
	@echo "HAVE_CURL      = $(HAVE_CURL)"
	@echo "HAVE_MALLOC_TRIM = $(HAVE_MALLOC_TRIM)"
	@echo "CLOCK_GETTIME  = $(if $(filter yes,$(HAVE_CLOCK)),in libc,$(if $(RT_LIBS),needs -lrt,absent (coarse time() fallback)))"
	@echo "SIZEFLAGS      = $(SIZEFLAGS)"
	@echo "WARN_OPTIONAL  = $(if $(strip $(WARN_OPTIONAL)),$(WARN_OPTIONAL),(none: this compiler has no GCC-only warning flags))"
	@echo "EMACS_OK       = $(if $(EMACS_OK),yes,no (elisp-compile/elisp-test will no-op))"
	@echo "MARP_OK        = $(if $(MARP_OK),yes,no (slides will no-op))"
	@echo "HARDEN         = $(HARDEN)"
	@echo "HARDENFLAGS    = $(HARDENFLAGS)"
	@echo "HARDENLDFLAGS  = $(HARDENLDFLAGS)"
	@echo "OPT            = $(OPT)$(if $(strip $(OPT)),, (none: _FORTIFY_SOURCE inert -- see the Makefile note))"
	@echo "LDFLAGS        = $(LDFLAGS)"
	@echo "LIB_SRC        = $(LIB_SRC)"
	@echo "TEST_SRC       = $(TEST_SRC)"

clean:
	rm -f $(BIN) $(CONVERT_BIN) $(TEST) $(MAIN_OBJ) $(LIB_OBJ) \
	      $(TEST_OBJ) $(CONVERT_CORE) $(CONVERT_MAIN) editors/emacs/jichi.elc \
	      $(FUZZ_BIN) $(FUZZ_OBJ) $(LF_BIN) $(LF_OBJ) \
	      $(TT_BIN) $(TT_CORE_OBJ) tests/tools/xdrive \
	      jlu_continue jlu-convert
	rm -f .jc_probe.*.out .jc_probe.*.out.exe
# M593: the stamp too. Its recipe replaces it only when the CONTENT changes, so
# after a build under sudo it is the one file left root-owned by `make clean` --
# the cleanup M586 points people at to undo exactly that damage.
	rm -f $(STAMP) $(STAMP).new
	find . -name '*.d' -delete

# Header-dependency files emitted by -MMD (regenerated on every compile).
-include $(LIB_OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d) \
         $(CONVERT_MAIN:.o=.d) $(CONVERT_CORE:.o=.d) $(TT_CORE_OBJ:.o=.d)
