# Hosting the agent service

The service (`cloud/`) runs on one server as a container, with Caddy in front for TLS. Testers' plugins reach it at `https://<domain>`. A tester token is needed for every plan, and each tester has limits (`docs/bridge-spec.md`, "Access").

| File | What |
| --- | --- |
| `Containerfile` | The image: `npm run serve` on `0.0.0.0:8787` with core's `fs-analyze`. Build context is the repo root (`.dockerignore` keeps it small). |
| `compose.yml` | The service and Caddy, in devarch's service shape (`x-devarch`, the shared `microservices-net` network). |
| `Caddyfile` | TLS for `FLOWSTATE_DOMAIN`, streaming without buffering, and no access log (headers carry tokens and keys). |
| `deploy.sh` | Runs on the server: pull a tag, recreate the service, wait for health, roll back on failure. |
| `../.github/workflows/service.yml` | Builds and smoke-tests the image on every change. With `deploy=true` on main, it pushes to GHCR and runs `deploy.sh` over SSH. |

## Deploy

```sh
npm run deploy:service      # deploys what is pushed to main, and follows the run
```

The image is tagged with the build ID (`<short sha>-<run number>`), and `/v1/health` reports it, so the workflow's last step checks that the public URL serves the new build. A failed health check puts the previous tag back.

## Testers

```sh
npm run -w cloud token -- mac-tester        # prints the token once, and the line for the tokens file
```

1. Add the printed line (`mac-tester <sha256>`) to `/etc/flowstate/testers` on the server. The service reads the file at start, so run `podman restart flowstate-service`. The file holds only hashes.
2. Give the tester the token privately.
   - macOS zip: `npm run pack:mac -- --service https://<domain> --token fst_...`. This puts a `service.json` in the zip, and `install.sh` installs it.
   - Windows: put a `service.json` (`{"url": "https://<domain>", "token": "fst_..."}`) next to `install.ps1`, or run `install.ps1 -Service https://<domain> -Token fst_...`.
3. To revoke a tester, delete their line and restart the service.

The plugin reads `service.json` from `%APPDATA%\Flowstate` (Windows) or `~/Library/Application Support/Flowstate` (macOS). `FLOWSTATE_SERVICE_URL` and `FLOWSTATE_SERVICE_TOKEN` override it. It sends the token only to an `https` URL or a loopback address. Without either, it uses the local `npm run serve` with no token, as before.

Limits per tester (environment, in `service.env`):
- `FLOWSTATE_SERVICE_CONCURRENT`: streams open at once (default 4, one generate with 4 variations)
- `FLOWSTATE_SERVICE_PER_HOUR`: requests started in any hour (default 120)

## Server setup (once)

A Hetzner server with rootless Podman, `podman-compose` (or the Compose plugin), and a DNS `A` record for the domain. Commands as the deploy user:

```sh
# Rootless Podman may bind 80 and 443, and containers come back after a reboot.
echo 'net.ipv4.ip_unprivileged_port_start=80' | sudo tee /etc/sysctl.d/90-flowstate.conf && sudo sysctl --system
sudo loginctl enable-linger "$USER"
systemctl --user enable --now podman-restart.service

sudo mkdir -p /opt/flowstate /etc/flowstate && sudo chown "$USER" /opt/flowstate /etc/flowstate && chmod 700 /etc/flowstate
cat > /opt/flowstate/.env <<'ENV'
FLOWSTATE_DOMAIN=flowstate.example.com
ACME_EMAIL=you@example.com
ENV
```

`/etc/flowstate/service.env` (mode 600) holds the planner's model and the managed keys. It is never in the repo:

```sh
FLOWSTATE_PLANNER_PROVIDER=deepseek
FLOWSTATE_PLANNER_MODEL=deepseek-flash
FLOWSTATE_PLANNER_REASONING=off
DEEPSEEK_API_KEY=...
# Optional: FLOWSTATE_SERVICE_MANAGED_MODELS, FLOWSTATE_FEATURE_BYOK=0, FLOWSTATE_SERVICE_CONCURRENT, FLOWSTATE_SERVICE_PER_HOUR
```

`/etc/flowstate/testers` (mode 644) holds one `tester sha256` line per tester. The container reads it as another user under rootless Podman. It holds only hashes, and the 700 folder keeps other users of the machine out. The first deploy copies `compose.yml`, `Caddyfile` and `deploy.sh` to `/opt/flowstate`. Later deploys refresh them.

On a server that already runs devarch, the service joins its `microservices-net` network. Caddy here binds 80 and 443; devarch's own Caddy uses 127.0.0.1:8005/8006, so they don't clash.

If the GHCR package is private, run `podman login ghcr.io` once on the server with a token that can read packages.

## CI settings

In the repo's `production` environment:

| Name | Kind | Value |
| --- | --- | --- |
| `DEPLOY_HOST` | secret | the server's address |
| `DEPLOY_USER` | secret | the deploy user |
| `DEPLOY_SSH_KEY` | secret | a private key for that user (only for deploys) |
| `DEPLOY_KNOWN_HOSTS` | secret | `ssh-keyscan <host>` output, checked, so the job won't talk to another machine |
| `FLOWSTATE_SERVICE_URL` | variable | `https://<domain>` |
| `DEPLOY_PATH` | variable, optional | default `/opt/flowstate` |

## Run the image locally

```sh
podman build -f deploy/Containerfile --build-arg FLOWSTATE_BUILD_ID=local -t flowstate-service .
podman run --rm -p 127.0.0.1:8787:8787 -e FLOWSTATE_PLANNER_PROVIDER=deepseek -e FLOWSTATE_PLANNER_MODEL=deepseek-flash \
  -e FLOWSTATE_PLANNER_REASONING=off -e DEEPSEEK_API_KEY -e FLOWSTATE_SERVICE_TOKENS_FILE=/run/testers \
  -v ./testers:/run/testers:ro flowstate-service
```
