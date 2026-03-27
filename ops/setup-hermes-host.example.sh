#!/bin/sh
set -eu

# Example host bootstrap script for a dedicated hermes service account.
# Review and adapt paths before using on a real server.

useradd --system --create-home --home-dir /srv/hermes --shell /usr/sbin/nologin hermes || true

install -d -o hermes -g hermes /srv/hermes/app
install -d -o hermes -g hermes /srv/hermes/projects
install -d -o hermes -g hermes /var/lib/hermes
install -d -o root -g hermes -m 0750 /etc/hermes
install -d -o root -g hermes -m 0750 /etc/hermes/mailboxes.d

printf '%s\n' "Next steps:"
printf '%s\n' "1. Copy the repo to /srv/hermes/app"
printf '%s\n' "2. Run: sudo -u hermes python3 -m venv /srv/hermes/app/.venv"
printf '%s\n' "3. Run: sudo -u hermes /srv/hermes/app/.venv/bin/pip install -e /srv/hermes/app"
printf '%s\n' "4. Create /etc/hermes/hermes.env"
printf '%s\n' "5. Create mailbox profiles in /etc/hermes/mailboxes.d/"
printf '%s\n' "6. Install ops/hermes.service.example as /etc/systemd/system/hermes.service"
