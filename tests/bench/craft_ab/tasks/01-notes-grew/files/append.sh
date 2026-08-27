#!/bin/sh
# append.sh -- add a timestamped note.
#   ./append.sh "went to the archive, found nothing"
NOTES="${NOTES:-$HOME/notes/notes.txt}"
mkdir -p "$(dirname "$NOTES")"
printf '%s\t%s\n' "$(date -Iseconds)" "$*" >> "$NOTES"
