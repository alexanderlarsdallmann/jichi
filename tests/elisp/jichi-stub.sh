#!/bin/sh
# jichi-stub.sh - a stand-in for the jichi binary, used by the Elisp tests.
#
# It speaks just enough of the headless contract for jichi.el's tests: it reads
# the prompt from stdin (the real binary does too with `-p -`) and writes a
# canned answer to stdout, so the tests never touch the network or the real
# agent. The leading args mirror what jichi.el passes; we ignore all of them
# except a recognised mode token in $JICHI_STUB_MODE.
#
#   JICHI_STUB_MODE=answer   (default) drain stdin, print a fenced answer, exit 0
#   JICHI_STUB_MODE=echo     print the prompt back (proves stdin reached us)
#   JICHI_STUB_MODE=error    write to stderr and exit 1
#   JICHI_STUB_MODE=interrupt exit 130 (as if SIGINT'd)

mode="${JICHI_STUB_MODE:-answer}"

case "$mode" in
  echo)
    cat -
    ;;
  error)
    cat - >/dev/null 2>&1
    echo "stub: simulated failure" >&2
    exit 1
    ;;
  interrupt)
    cat - >/dev/null 2>&1
    exit 130
    ;;
  *)
    # Drain stdin (the prompt) so the writer's EOF is consumed, then answer.
    cat - >/dev/null 2>&1
    printf '```elisp\n(stub-answer)\n```\n'
    ;;
esac
