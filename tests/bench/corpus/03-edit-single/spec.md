---
title: Change a string literal in place
audience: agent
verify: "grep -q 'Hello, Giessen' greet.c && ! grep -q 'Hello, World' greet.c"
points: 1
---
In `greet.c`, change the greeting so the program prints `Hello, Giessen`
instead of `Hello, World`. Change nothing else.
