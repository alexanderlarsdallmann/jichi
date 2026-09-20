#!/bin/sh
# Run the unittest suite in test_tags.py. Exit 0 iff every test passes.
# The test module is the truth -- fix the FUNCTION, not the tests.
cd "$(dirname "$0")" || exit 1
command -v python3 >/dev/null 2>&1 || { echo "CANNOT RUN: python3 is not on PATH"; exit 77; }
python3 -m unittest test_tags
