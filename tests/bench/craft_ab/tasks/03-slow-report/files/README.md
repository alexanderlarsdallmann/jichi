# access-log summary

`summarise.py access.log` prints one line per known user, most active first.

`users.txt` is the list of usernames we care about; anything else in the log
(crawlers, anonymous traffic) is ignored.

`access.log` here is a small sample -- about 4,000 lines. The real ones are
around 6 million lines a month now; last year they were about 150,000.
