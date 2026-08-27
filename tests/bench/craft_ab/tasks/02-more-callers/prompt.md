---
title: Three more callers next quarter
unstated: |
  "Do what you think is needed" is the whole specification. Nothing names an
  artifact: not a doc, not a test, not a version scheme, not an error-handling
  convention. The module has three real problems (a returned pointer to a static
  buffer, a silent truncation, an unchecked malloc) and a reader who only skims
  will find at most one.
observable_without_a_grader: |
  The fixture PRINTS the bug: `make && ./report` outputs
  `host=sc1-w1-1 run=sc1-w1-1`, because both kv_get results alias one static
  buffer and the second call overwrites the first before printf reads either.
  It is wrong single-threaded, before threads enter the story at all. Whether a
  run notices this is a clean discriminator, and it needs no grader: it depends
  entirely on whether the agent ran the thing before changing it.
tensions:
  - the static return buffer breaks the moment two callers interleave
  - fixing the API means changing its one existing caller, which is not asked for
  - "needed" could mean docs, tests, thread safety, or an ABI promise
---
`kv.c` is a tiny key/value store one of our tools uses. Next quarter three more
teams want to use it, and one of them runs several threads.

Do what you think is needed.
