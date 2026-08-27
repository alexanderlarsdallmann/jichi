---
title: Create a C89 header from a description
audience: agent
verify: "grep -q '#ifndef JC_UTIL_H' util.h && grep -q '#define JC_UTIL_H' util.h && grep -q 'int jc_add' util.h && grep -q '#endif' util.h"
points: 1
---
Create a new header file `util.h`. It must use a C89 include guard named
`JC_UTIL_H` and declare a single function: `int jc_add(int a, int b);`.
