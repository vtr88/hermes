#!/bin/sh
set -eu

# Shared GitHub key + repo identity setup for nologin project users.
#
# This script reuses one existing key and makes it readable by project users
# through a dedicated group, then configures SSH + git identity per repo.
#
# Usage:
#   ./scripts/setup-shared-github-key.sh <project-user> [git-name] [git-email] [repo-path] [source-key]
# Example:
#   ./scripts/setup-shared-github-key.sh hermes "YOUR_NAME" "YOUR_EMAIL" /home/hermes/Projects/hermes /home/soth/.ssh/id_ed25519_github

PROJECT_USER="${1:-}"
GIT_NAME="${2:-YOUR_NAME}"
GIT_EMAIL="${3:-YOUR_EMAIL}"
REPO_PATH="${4:-}"
SOURCE_KEY="${5:-/home/soth/.ssh/id_ed25519_github}"

if [ -z "${PROJECT_USER}" ]; then
	printf '%s\n' "Usage: $0 <project-user> [git-name] [git-email] [repo-path] [source-key]" >&2
	exit 1
fi

if [ -z "${REPO_PATH}" ]; then
	REPO_PATH="/home/${PROJECT_USER}/Projects/${PROJECT_USER}"
fi

SOURCE_PUB="${SOURCE_KEY}.pub"
SHARED_DIR="/etc/hermes/ssh"
SHARED_KEY="${SHARED_DIR}/id_ed25519_github"
SHARED_PUB="${SHARED_DIR}/id_ed25519_github.pub"

printf '%s\n' "[1/8] Creating group hermesgit"
sudo groupadd -f hermesgit

printf '%s\n' "[2/8] Ensuring user ${PROJECT_USER} is in hermesgit"
sudo usermod -aG hermesgit "${PROJECT_USER}"

printf '%s\n' "[3/8] Installing shared key directory"
sudo install -d -m 750 -o root -g hermesgit "${SHARED_DIR}"

printf '%s\n' "[4/8] Copying shared GitHub key"
sudo cp "${SOURCE_KEY}" "${SHARED_KEY}"
sudo cp "${SOURCE_PUB}" "${SHARED_PUB}"
sudo chown root:hermesgit "${SHARED_KEY}" "${SHARED_PUB}"
sudo chmod 640 "${SHARED_KEY}"
sudo chmod 644 "${SHARED_PUB}"

printf '%s\n' "[5/8] Writing SSH config for ${PROJECT_USER}"
sudo -u "${PROJECT_USER}" install -d -m 700 "/home/${PROJECT_USER}/.ssh"
sudo -u "${PROJECT_USER}" sh -lc "cat > /home/${PROJECT_USER}/.ssh/config <<'EOF'
Host github.com
  IdentityFile /etc/hermes/ssh/id_ed25519_github
  IdentitiesOnly yes
EOF"
sudo -u "${PROJECT_USER}" chmod 600 "/home/${PROJECT_USER}/.ssh/config"

printf '%s\n' "[6/8] Configuring repo-local git identity"
sudo -u "${PROJECT_USER}" git -C "${REPO_PATH}" config user.name "${GIT_NAME}"
sudo -u "${PROJECT_USER}" git -C "${REPO_PATH}" config user.email "${GIT_EMAIL}"

printf '%s\n' "[7/8] Verifying git identity"
sudo -u "${PROJECT_USER}" git -C "${REPO_PATH}" config --get user.name
sudo -u "${PROJECT_USER}" git -C "${REPO_PATH}" config --get user.email

printf '%s\n' "[8/8] Testing GitHub SSH auth"
sudo -u "${PROJECT_USER}" ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new -T git@github.com || true

printf '%s\n' "Done."
