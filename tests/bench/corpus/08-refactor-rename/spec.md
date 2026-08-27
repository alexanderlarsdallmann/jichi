---
title: Rename a function across every file that uses it
audience: agent
verify: "! grep -rq buf_reset . && grep -q buf_clear buf.h && grep -q buf_clear buf.c && [ \"$(grep -c buf_clear app.c)\" = \"2\" ]"
points: 3
---
Rename the function `buf_reset` to `buf_clear` everywhere it appears:
its declaration in `buf.h`, its definition in `buf.c`, and both call sites in
`app.c`. No occurrence of the old name may remain.
