---
title: Locate a file whose path is not given
audience: agent
verify: "grep -qx '12' answer.txt"
points: 3
---
This project has a configuration file somewhere. Find it and write the value of
its `max_connections` setting into a new file `answer.txt` at the project root.
The file must contain only that number.
