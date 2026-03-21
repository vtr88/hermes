#!/bin/sh
set -eu

# Redeploy helper:
# 1) stop service
# 2) build + test as project user
# 3) enforce repo-local git identity
# 4) start service and print status/logs

# Usage:
#   ./scripts/hermes-redeploy.sh [project-user] [service-name] [git-name] [git-email]
# Example:
#   ./scripts/hermes-redeploy.sh hermes hermes "Vitor Hugo" "vtr88@yahoo.com.br"

PROJECT_USER="${1:-hermes}"
SERVICE_NAME="${2:-hermes}"
PROJECT_DIR="/home/${PROJECT_USER}/Projects/${PROJECT_USER}"
GIT_NAME="${3:-Vitor Hugo}"
GIT_EMAIL="${4:-vtr88@yahoo.com.br}"

printf '%s\n' "[1/6] Stopping service: ${SERVICE_NAME}"
sudo systemctl stop "${SERVICE_NAME}" || true

printf '%s\n' "[2/6] Ensuring target directory exists"
sudo install -d -m 755 -o "${PROJECT_USER}" -g "${PROJECT_USER}" "/home/${PROJECT_USER}/Projects"

printf '%s\n' "[3/6] Building in: ${PROJECT_DIR}"
sudo -u "${PROJECT_USER}" sh -lc "cd '${PROJECT_DIR}' && make clean && make && make test"

printf '%s\n' "[4/6] Ensuring repo-local git identity"
sudo -u "${PROJECT_USER}" git -C "${PROJECT_DIR}" config user.name "${GIT_NAME}"
sudo -u "${PROJECT_USER}" git -C "${PROJECT_DIR}" config user.email "${GIT_EMAIL}"

printf '%s\n' "[5/6] Starting service: ${SERVICE_NAME}"
sudo systemctl start "${SERVICE_NAME}"

printf '%s\n' "[6/6] Service status + recent logs"
sudo systemctl --no-pager --full status "${SERVICE_NAME}" || true
sudo journalctl -u "${SERVICE_NAME}" -n 40 --no-pager || true

printf '%s\n' "Done."
