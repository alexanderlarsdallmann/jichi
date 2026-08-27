#!/bin/sh
# search.sh -- find notes matching a pattern.
#   ./search.sh archive
#   ./search.sh -i ARCHIVE
NOTES="${NOTES:-$HOME/notes/notes.txt}"
grep "$@" "$NOTES" | tail -50
