#!/bin/sh
# runs as root: install the hub config + the hub-wide jichi config, then start
# the hub briefly and see whether it comes up.
set -eu
mkdir -p /etc/jupyterhub /srv/jichi
cp /tmp/guest/jupyterhub_config.py /etc/jupyterhub/jupyterhub_config.py
cat > /srv/jichi/config.json <<'CFG'
{"maxParallelAgents":1,"memBudgetMb":512,"lowResource":true,
 "toolProfile":"auto","pathFence":1,
 "models":[{"name":"course-chat","provider":"openai","model":"mock",
            "apiBase":"http://127.0.0.1:1/v1","apiKeyEnv":"JICHI_API_KEY",
            "contextLength":32000,"roles":["chat"]}]}
CFG
chmod 0644 /srv/jichi/config.json
cd /etc/jupyterhub
timeout 40 /opt/jhub/bin/jupyterhub -f /etc/jupyterhub/jupyterhub_config.py \
    > /tmp/hub.log 2>&1 || true
echo "--- hub log ---"
tail -12 /tmp/hub.log 2>/dev/null || true
echo "--- can a spawned user read the hub-wide config? ---"
su - stud1 -c 'test -r /srv/jichi/config.json && echo READABLE || echo UNREADABLE'
su - stud1 -c 'grep -q apiKeyEnv /srv/jichi/config.json && echo NAMES_ENV_VAR'
su - stud1 -c 'grep -q "\"apiKey\"" /srv/jichi/config.json && echo HAS_LITERAL_KEY || echo NO_LITERAL_KEY'
echo "HUB_DONE"
