#!/bin/sh
# runs as root in the guest: two real users and one shared, group-writable tree
set -eu
for u in stud1 stud2; do
    id "$u" >/dev/null 2>&1 || useradd -m -s /bin/bash "$u"
done
groupadd -f course
usermod -aG course stud1
usermod -aG course stud2
mkdir -p /srv/shared
chgrp course /srv/shared
chmod 2775 /srv/shared
echo "provisioned"
