#!/bin/sh
set -eu

# Build, test, push, and restart helper for hermes service.
#
# Usage:
#   ./scripts/hermes-push-restart.sh [project-user] [service-name] [branch]
# Example:
#   ./scripts/hermes-push-restart.sh hermes hermes main

PROJECT_USER="${1:-hermes}"
SERVICE_NAME="${2:-hermes}"
BRANCH="${3:-main}"
PROJECT_DIR="/home/${PROJECT_USER}/Projects/${PROJECT_USER}"

printf '%s\n' "[1/6] Checking project directory"
if [ ! -d "${PROJECT_DIR}" ]; then
	printf 'missing project directory: %s\n' "${PROJECT_DIR}" >&2
	exit 1
fi

printf '%s\n' "[2/6] Build + tests as ${PROJECT_USER}"
sudo -u "${PROJECT_USER}" sh -lc "cd '${PROJECT_DIR}' && make -B && make test"

printf '%s\n' "[3/6] Pushing branch ${BRANCH} as ${PROJECT_USER}"
sudo -u "${PROJECT_USER}" sh -lc "cd '${PROJECT_DIR}' && git push origin '${BRANCH}'"

printf '%s\n' "[4/6] Reloading systemd units"
sudo systemctl daemon-reload

printf '%s\n' "[5/6] Restarting service ${SERVICE_NAME}"
sudo systemctl restart "${SERVICE_NAME}"

printf '%s\n' "[6/6] Service status + recent logs"
sudo systemctl --no-pager --full status "${SERVICE_NAME}" || true
sudo journalctl -u "${SERVICE_NAME}" -n 120 --no-pager || true

printf '%s\n' "Done."
