#!/bin/sh
# wordtool build. `zig cc` compiles the C; a Zig translation unit joins via
# `zig build-obj` and links like any other object file.
set -e
cd "$(dirname "$0")"
zig cc -std=c89 -Wall -Wextra -Werror -o wordtool main.c stats.c
