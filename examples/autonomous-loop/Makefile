# Build the standalone C89 supervisor. Mirrors the main project's discipline:
# strict C89, warnings on, POSIX feature macro for fork/exec/dirent/snprintf.
# No -O by default, matching the top-level build (its WERROR gate is unoptimized
# too); -O2 would enable gcc's FORTIFY-driven -Wformat-truncation heuristics that
# flag purely theoretical path truncation. Add -O yourself for a release build.
CC     ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra
CPPFLAGS += -D_POSIX_C_SOURCE=200112L
# The top-level `make examples` passes WERROR=1 to gate this like first-party code.
ifeq ($(WERROR),1)
CFLAGS += -Werror
endif

jichi-supervisor: jichi-supervisor.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ jichi-supervisor.c

clean:
	rm -f jichi-supervisor

.PHONY: clean
