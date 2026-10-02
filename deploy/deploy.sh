#!/usr/bin/env bash
# Runs on the server, from its folder (/opt/flowstate): deploys one image tag of the agent service.
#
#   ./deploy.sh <image tag>
#
# Pulls the image, recreates the service with it, starts Caddy if needed, and waits for the health check.
# If the new service isn't healthy within 60 s, it goes back to the previous tag and exits 1.
# .env holds FLOWSTATE_DOMAIN and ACME_EMAIL (deploy/README.md); this script keeps FLOWSTATE_SERVICE_TAG in it.
set -euo pipefail
cd "$(dirname "$0")"

die() { echo "error: $*" >&2; exit 1; }
tag=${1:?usage: deploy.sh <image tag>}
[[ $tag =~ ^[A-Za-z0-9_.-]{1,128}$ ]] || die "bad image tag '$tag'"
[ -f .env ] || die "no .env here; see deploy/README.md"
grep -q '^FLOWSTATE_DOMAIN=.' .env || die ".env needs FLOWSTATE_DOMAIN"
grep -q '^ACME_EMAIL=.' .env || die ".env needs ACME_EMAIL"
secrets=$(sed -n 's/^FLOWSTATE_SECRETS_DIR=//p' .env); secrets=${secrets:-/etc/flowstate}
[ -r "$secrets/service.env" ] || die "$secrets/service.env is missing (provider keys and model)"
[ -r "$secrets/testers" ] || die "$secrets/testers is missing (npm run -w cloud token -- <tester>)"
# The container's user is a subordinate UID under rootless Podman, so it reads the file as "other". It holds
# only hashes; the folder (mode 700) keeps other users of this machine out.
[ -n "$(find "$secrets/testers" -perm -o=r)" ] || die "the service can't read $secrets/testers: chmod 644 it (it holds only hashes)"

image=$(sed -n 's/^FLOWSTATE_SERVICE_IMAGE=//p' .env); image=${image:-ghcr.io/prospect-ogujiuba/flowstate-service}
previous=$(sed -n 's/^FLOWSTATE_SERVICE_TAG=//p' .env)

set_tag() {
  grep -v '^FLOWSTATE_SERVICE_TAG=' .env > .env.next || true
  echo "FLOWSTATE_SERVICE_TAG=$1" >> .env.next
  mv .env.next .env
}

healthy() {
  for _ in $(seq 1 30); do
    if podman healthcheck run flowstate-service >/dev/null 2>&1; then return 0; fi
    sleep 2
  done
  return 1
}

run() { echo "+ $*"; "$@"; }

podman network exists "${DEVARCH_NETWORK:-microservices-net}" || run podman network create "${DEVARCH_NETWORK:-microservices-net}"
# Build IDs are unique, so an image already here is the right one (and a local test needs no registry).
podman image exists "$image:$tag" || run podman pull "$image:$tag"
set_tag "$tag"
run podman compose up -d --force-recreate --no-deps flowstate-service
run podman compose up -d caddy
if healthy; then
  echo "deployed $image:$tag"
  exit 0
fi

echo "error: $image:$tag isn't healthy; its last log lines:" >&2
podman logs --tail 30 flowstate-service >&2 || true
if [ -n "$previous" ] && [ "$previous" != "$tag" ]; then
  echo "rolling back to $previous" >&2
  set_tag "$previous"
  run podman compose up -d --force-recreate --no-deps flowstate-service
  healthy && echo "rolled back to $image:$previous" >&2
fi
exit 1
