# Native ARK OS deployment guide

This guide describes ways to keep the C++ runtime available while the
development laptop is asleep. It does not promise a zero-maintenance service:
local model availability, Docker, database backups, and provider credentials
remain operational responsibilities. The REST API binds to `127.0.0.1`; use
an SSH tunnel or a separately secured reverse proxy for remote access.

## Choose a host

| Option | What stays online | Limits |
| --- | --- | --- |
| Existing desktop or laptop | C++ API, local databases, local inference | The machine must remain awake; model inference stops when the host is off. |
| Home mini PC | Same native runtime and local services on a dedicated host | Confirm its RAM, storage, cooling, and local model performance for your workload before buying. No device-specific benchmark is claimed here. |
| Raspberry Pi or ARM board | Potentially the API, scheduler, Redis, and small local data services | The guide has not validated the complete C++ dependency stack or model inference on a Pi. Treat it as an experimental orchestration host, not a tested full deployment. |
| Cloud VM | C++ API and selected data services | Cloud cost, GPU availability, data locality, provider terms, and backup charges depend on the chosen provider. The repository has no tested cloud provisioning script. |

The C++ runtime does not require a Python interpreter. Some workloads still
require Ollama or llama.cpp, Redis, Qdrant, Neo4j, Docker, or external provider
access. Start only the services needed for the workflows you intend to use.

## Build and local service stack

Install the native build dependencies listed in
[`native_cpp.md`](native_cpp.md#build-and-run), then build and run the suite.
The Compose file is intended for local development. Its published service
ports bind to loopback, so another machine cannot connect directly by using
the host's LAN address.

```bash
docker compose config -q
docker compose up -d qdrant redis neo4j
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/ark-cpp serve 8000
```

MLflow is optional; start it only for workflows that use tracking. The sample
Compose file contains development defaults, including a fixed Neo4j password
and floating image tags. Do not expose it as a production stack. Before
keeping user data in the services, choose strong service credentials, pin
reviewed image versions, restrict host access, and schedule tested backups for
the named volumes.

## Keep the API running with systemd

Create a dedicated Unix account for the runtime. Add it to the Docker access
group only if coding, terminal, or security workflows need the local Docker
socket; Docker socket access grants broad control over the host. Store the
environment file with owner-only permissions and do not put secrets in the
unit file or command arguments.

Create `~/.config/ark/ark.env` for the service account with the settings needed
by this host, for example:

```ini
ARK_API_PORT=8000
ARK_REDIS_PORT=6379
ARK_QDRANT_URL=http://127.0.0.1:6333
ARK_OLLAMA_URL=http://127.0.0.1:11434
ARK_EPISODIC_DB=/var/lib/ark/episodic_memory.db
ARK_SCHEDULE_DB=/var/lib/ark/schedules.db
ARK_PROCEDURAL_DB=/var/lib/ark/procedural_memory.db
ARK_STATE_PATH=/var/lib/ark/STATE.md
OLLAMA_NO_CLOUD=1
```

Set ownership and permissions for the service account. Create
`~/.config/systemd/user/ark.service` with the correct absolute executable and
working-directory paths:

```ini
[Unit]
Description=ARK OS native API and task scheduler
After=network.target

[Service]
Type=simple
WorkingDirectory=/absolute/path/to/Ark
EnvironmentFile=%h/.config/ark/ark.env
ExecStart=/absolute/path/to/Ark/build/ark-cpp serve 8000
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ReadWritePaths=/var/lib/ark /absolute/path/to/Ark/config

[Install]
WantedBy=default.target
```

Create `/var/lib/ark` and make it owned by the service account. Adjust
`ReadWritePaths` to match any additional configured output paths; systemd will
prevent writes outside the listed paths. Start Docker's system service
separately if ARK needs the Docker socket. If the user's systemd
manager is not started at boot, enable lingering for the service account using
the host's normal administrator process.

```bash
systemctl --user daemon-reload
systemctl --user enable --now ark.service
systemctl --user status ark.service
journalctl --user -u ark.service -f
```

The API listens only on loopback. To access it from a workstation, create an
SSH tunnel to the host's loopback port rather than changing the application to
listen on every interface. If using a reverse proxy, configure TLS,
authentication, request limits, and an explicit access policy before exposing
it.

From the workstation, a basic SSH forward is:

```bash
ssh -N -L 8000:127.0.0.1:8000 ark-host
```

Replace `ark-host` with a host alias configured in the workstation's SSH
configuration. The ARK client then connects to `http://127.0.0.1:8000` on the
workstation while the tunnel is active.

## Schedule and recovery

The native scheduler stores schedules in SQLite and is started by `ark-cpp
serve`. It therefore needs a persistent writable `ARK_SCHEDULE_DB`. Test a
one-time schedule, restart the service, and verify the schedule is still
present before depending on unattended jobs. For long SAGE runs, also verify
that the selected workflow's checkpoint store and external services persist
across restarts.

Back up SQLite databases with a SQLite-aware backup mechanism and back up
Qdrant/Neo4j volumes using their supported procedures while writes are
quiesced or through a consistent snapshot. Periodically restore backups into
a separate test location; a successful backup command alone is not a restore
test. Monitor disk space, service health, and restart loops. Keep API and model
logs private because task inputs may contain sensitive information.

## Remote model host

If inference runs on another trusted machine, configure its URL in the ARK
environment and restrict the model server to a private network or tunnel.
Do not expose an unauthenticated Ollama or llama.cpp endpoint to the public
internet. Confirm the model server's access controls and firewall rules before
sending prompts. In privacy or airgap mode, check the provider allowlist and
the actual network path; a local ARK process alone does not guarantee that a
remote model endpoint is local.

## Verification boundaries

`docker compose config -q` validates the Compose file syntax, not service
health. The C++ CTest suite checks local components and skips browser and
Phase 2 tests when their service gates are absent. The current checkout has
not been installed on a clean machine, run as a boot-time systemd service,
restored from a backup, or validated as an always-on cloud/Pi deployment.
Complete those checks on the chosen target before relying on unattended jobs.
