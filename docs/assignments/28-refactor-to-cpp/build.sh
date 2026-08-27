#!/bin/sh
# wordtool build. The C stays C (cc, strict C89); a C++ translation unit
# compiles with c++ and joins at link time -- linking WITH c++ so the C++
# runtime comes along.
set -e
cd "$(dirname "$0")"
cc -std=c89 -pedantic -Wall -Wextra -Werror -c main.c stats.c
cc -o wordtool main.o stats.o
