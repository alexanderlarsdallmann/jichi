# notes

Two shell scripts and a text file.

- `append.sh "text"` adds a line: an ISO-8601 timestamp, a tab, then the text.
- `search.sh PATTERN` greps for it and shows the last 50 matches.

The file lives at `$NOTES`, default `~/notes/notes.txt`. Nothing else reads it,
except me with an editor, and the cron entry in `crontab.fragment` that appends
one line a day.

I have never deleted anything from it. Some of the entries from the first months
are the only record I have of what happened on a couple of projects.
