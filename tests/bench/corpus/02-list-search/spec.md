---
title: Locate a symbol definition by searching
audience: agent
verify: "grep -q 'src/b.c' found.txt"
points: 1
---
Find which file under `src/` defines the function `jc_widget_init`. Write that
file's path, relative to the project root, into a new file `found.txt`.
