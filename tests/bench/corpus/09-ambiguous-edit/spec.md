---
title: Edit one of two identical lines
audience: agent
verify: "[ \"$(awk '$0==\"[client]\"{s=1} $0==\"[server]\"{s=2} s==1 && /^timeout/{print $3}' settings.ini)\" = \"30\" ] && [ \"$(awk '$0==\"[client]\"{s=1} $0==\"[server]\"{s=2} s==2 && /^timeout/{print $3}' settings.ini)\" = \"60\" ]"
points: 2
---
In `settings.ini`, change the timeout in the `[server]` section to 60. The
`[client]` section must keep its timeout of 30.
