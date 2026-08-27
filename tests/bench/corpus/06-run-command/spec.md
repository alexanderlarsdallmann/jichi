---
title: Count files with a shell command
audience: agent
verify: "grep -qx '3' count.txt"
points: 1
---
Count how many files under `src/` end in `.c`. Use a shell command to count
them; do not guess. Write only the number into a new file `count.txt`.
