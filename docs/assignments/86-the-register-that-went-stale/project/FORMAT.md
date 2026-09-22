# The wire format

One record per line, UTF-8, newline-terminated. Fields are separated by a
single tab and appear in this order:

| # | Field | Type | Notes |
|---|---|---|---|
| 1 | `timestamp` | integer | seconds since the epoch, UTC |
| 2 | `device` | text | no tabs, no newlines |
| 3 | `value` | decimal | `.` as the decimal separator, always |

A reader that meets a field it does not know must keep the line and ignore the
field, so an older reader still works against a newer writer.
