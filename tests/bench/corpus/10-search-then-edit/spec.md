---
title: Find a constant, then rename it only in the sources
audience: agent
verify: "! grep -rq DEFAULT_PORT src/ && grep -q 'LISTEN_PORT 8080' src/net.h && grep -q LISTEN_PORT src/net.c && grep -q LISTEN_PORT src/main.c && grep -q DEFAULT_PORT docs/notes.md"
points: 3
---
Rename the constant `DEFAULT_PORT` to `LISTEN_PORT` everywhere it appears in
the C sources under `src/`. The files under `docs/` are historical and must
keep the old name.
