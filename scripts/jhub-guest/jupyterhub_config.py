# A course hub's configuration, reduced to the parts that matter for jichi.
c.Authenticator.allowed_users = {"stud1", "stud2"}
c.Spawner.default_url = "/lab"

# Each spawned user gets the hub-wide jichi config by PATH. Note what is
# ABSENT and why:
#
#   * no literal API key -- the config names an env var instead;
#   * no fixed logging.path -- a shared path would merge every student's
#     telemetry into one file;
#   * no `"JICHI_API_KEY": ""`. An earlier version had it, and it does
#     NOTHING: jichi's resolve_key() consults getenv(apiKeyEnv) only when the
#     value is NON-EMPTY, and otherwise falls through to OPENAI_API_KEY. A
#     line that looks like it reserves a variable but does not is worse than
#     no line, and the fall-through could pick up a stray key in the image.
#
# The key is per-user, so it belongs in the user's own environment -- but see
# docs/JUPYTERHUB.md 9: a JupyterLab terminal is a LOGIN shell, so ~/.bashrc
# is read only if ~/.profile sources it.
c.Spawner.environment = {
    "JC_CONFIG": "/srv/jichi/config.json",
}
c.JupyterHub.bind_url = "http://127.0.0.1:8000"
