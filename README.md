# hermes

Hermes is a minimalist C daemon that runs an OpenCode session through email.

You send a normal email message, Hermes reads it via IMAP, treats it as the next user turn,
runs agent steps inside the project workdir, and replies in the same thread via SMTP.

## What Hermes Does

- Ongoing conversational coding session from normal email messages.
- Agentic multi-step execution (plan, run command, inspect, continue, respond).
- Approval-token flow only when OpenCode emits a pending approval.
- Response footer with execution/usage context and modified files.
- Runs as plain IMAP/SMTP client (no Postfix/Dovecot config edits required).

## Interaction Model

- Send plain language requests, exactly like talking to a coding assistant.
- Hermes keeps the session and continues from previous turns.
- If OpenCode requests approval, Hermes emails a token.
- Reply with either `approve <token>` or `/approve <token>`.

Example messages:

- `Read SESSION_HANDOFF.md and summarize where we are.`
- `Add line EOF under What Hermes Does in README and push to github.`

## Workdir Convention

By default, Hermes derives the project workdir from mailbox username:

`/home/<mail_user_localpart>/Projects/<mail_user_localpart>`

Example:

- mailbox user: `hermes@vitor.win`
- workdir: `/home/hermes/Projects/hermes`

You can override with `HERMES_WORKDIR`, but default convention is recommended.

## Prerequisites (Server)

- Linux server with your mail server already running.
- `opencode` CLI installed and available in PATH for service user.
- `cc`, `make`, `sqlite3` dev libs.
- A mailbox account matching Linux user convention.

Install build deps (Debian/Ubuntu):

```sh
sudo apt-get update
sudo apt-get install -y build-essential libsqlite3-dev rsync
```

## Project Bootstrap (Hermes User)

This is the canonical setup for project `hermes`.

### 1) Create Linux user and password

```sh
sudo useradd --create-home --shell /bin/bash hermes || true
sudo passwd hermes
```

### 2) Create project directories

```sh
sudo install -d -m 755 -o hermes -g hermes /home/hermes/Projects
sudo install -d -m 755 -o hermes -g hermes /home/hermes/Projects/hermes
```

### 3) Place source code in project path

Clone as project user:

```sh
sudo -u hermes git clone https://github.com/vtr88/hermes.git /home/hermes/Projects/hermes
```

### 4) Create runtime env file

```sh
sudo install -d -m 700 -o hermes -g hermes /home/hermes/.config/hermes
sudo cp /home/hermes/Projects/hermes/.env.example /home/hermes/.config/hermes/hermes.env
sudo chown hermes:hermes /home/hermes/.config/hermes/hermes.env
sudo chmod 600 /home/hermes/.config/hermes/hermes.env
```

### 5) Edit `/home/hermes/.config/hermes/hermes.env`

Required minimum:

```dotenv
# Optional. Leave unset for automatic per-thread session creation.
# HERMES_OPENCODE_SESSION_ID=ses_your_existing_session_id

HERMES_IMAP_URL=imaps://mail.vitor.win/INBOX
HERMES_SMTP_URL=smtps://mail.vitor.win:465
HERMES_MAIL_USER=hermes@vitor.win
HERMES_MAIL_PASS=<mailbox password>
HERMES_MAIL_FROM=hermes@vitor.win
HERMES_MAIL_TO=contato@vitor.win
HERMES_ALLOW_FROM=contato@vitor.win

HERMES_DB_PATH=build/hermes.db
HERMES_MAX_PROMPT_CHARS=12000
HERMES_TOOL_TIMEOUT_SEC=30
HERMES_POLL_SECONDS=30
```

Notes:

- Recommended default: keep `HERMES_OPENCODE_SESSION_ID` unset.
- Set `HERMES_OPENCODE_SESSION_ID` only when you intentionally want new threads to start from a known existing OpenCode session.
- Hermes now runs OpenCode-only; `HERMES_OPENAI_*` variables are no longer required.

Optional budget/cost footer values:

```dotenv
HERMES_BUDGET_USD=25
HERMES_INPUT_USD_PER_MTOK=1.25
HERMES_OUTPUT_USD_PER_MTOK=10.00
```

### 6) Build as project user

```sh
sudo -u hermes bash -lc 'cd /home/hermes/Projects/hermes && make clean && make && make test'
```

### 7) Create systemd service

Create `/etc/systemd/system/hermes.service`:

```ini
[Unit]
Description=Hermes daemon (hermes)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=hermes
Group=hermes
WorkingDirectory=/home/hermes/Projects/hermes
EnvironmentFile=/home/hermes/.config/hermes/hermes.env
ExecStart=/home/hermes/Projects/hermes/build/hermesd
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable/start:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now hermes
sudo systemctl status hermes --no-pager
sudo journalctl -u hermes -f
```

## Git Push Setup (Shared Key Helper)

If project users cannot push directly, configure shared SSH key access non-interactively.

Use helper script:

```sh
cd /home/hermes/Projects/hermes
./scripts/setup-shared-github-key.sh hermes "YOUR_NAME" "YOUR_EMAIL" /home/hermes/Projects/hermes /home/soth/.ssh/id_ed25519_github
```

This script:

- reuses `/home/soth/.ssh/id_ed25519_github` as shared key
- grants access via `hermesgit` group
- writes `/home/<user>/.ssh/config` for GitHub
- seeds `/home/<user>/.ssh/known_hosts` for `github.com`
- configures repo-local git `user.name`/`user.email`
- tests SSH auth

## Redeploy Workflow (Recommended)

Use helper script to build, enforce git identity, and restart service:

```sh
cd /home/hermes/Projects/hermes
./scripts/hermes-redeploy.sh
```

For build + test + push + restart in one command:

```sh
./scripts/hermes-push-restart.sh hermes hermes main
```

Custom form:

```sh
./scripts/hermes-redeploy.sh <project-user> <service-name> <git-name> <git-email>
```

Example:

```sh
./scripts/hermes-redeploy.sh hermes hermes "Vitor Hugo" "vtr88@yahoo.com.br"
```

## Restart Commands (Manual)

If you already built and only need to restart service:

```sh
sudo systemctl daemon-reload
sudo systemctl restart hermes
sudo systemctl status hermes --no-pager
sudo journalctl -u hermes -n 120 --no-pager
```

If you changed code and want full rebuild + restart:

```sh
cd /home/hermes/Projects/hermes
make -B && make test
./scripts/hermes-redeploy.sh hermes hermes "Vitor Hugo" "vtr88@yahoo.com.br"
```

## First Email to Start Session

Send to `hermes@vitor.win`:

```text
Subject: Start session

Read SESSION_HANDOFF.md and summarize current project status.
Then tell me what to do next.
```

## New Project Pattern (Future)

For project user `newproject`:

- Linux user: `newproject`
- mailbox: `newproject@vitor.win`
- project dir: `/home/newproject/Projects/newproject`
- service: `hermes-newproject.service`

Repeat same bootstrap steps with `newproject` substitutions.

## Project Factory Workflow (Recommended)

Use this when creating each new app/project account on your VPN server.

1) Create Linux user + home + login shell:

```sh
sudo useradd --create-home --shell /bin/bash <project>
sudo passwd <project>
```

2) Create mailbox for that same account (mail server admin side):

- mailbox: `<project>@vitor.win`

3) Create project directory:

```sh
sudo install -d -m 755 -o <project> -g <project> /home/<project>/Projects
sudo install -d -m 755 -o <project> -g <project> /home/<project>/Projects/<project>
```

4) Clone repo + set env:

```sh
sudo -u <project> git clone https://github.com/vtr88/hermes.git /home/<project>/Projects/<project>
sudo install -d -m 700 -o <project> -g <project> /home/<project>/.config/hermes
sudo cp /home/<project>/Projects/<project>/.env.example /home/<project>/.config/hermes/hermes.env
sudo chown <project>:<project> /home/<project>/.config/hermes/hermes.env
sudo chmod 600 /home/<project>/.config/hermes/hermes.env
```

5) Edit `/home/<project>/.config/hermes/hermes.env`:

- `HERMES_MAIL_USER=<project>@vitor.win`
- `HERMES_MAIL_PASS=<mailbox password>`
- `HERMES_MAIL_FROM=<project>@vitor.win`
- `HERMES_ALLOW_FROM=<your-controller-email>`
- keep `HERMES_OPENCODE_SESSION_ID` unset by default

6) Build + test + service:

```sh
sudo -u <project> bash -lc 'cd /home/<project>/Projects/<project> && make -B && make test'
```

Create `/etc/systemd/system/hermes-<project>.service` from current service template and set:

- `User=<project>`
- `Group=<project>`
- `WorkingDirectory=/home/<project>/Projects/<project>`
- `EnvironmentFile=/home/<project>/.config/hermes/hermes.env`
- `ExecStart=/home/<project>/Projects/<project>/build/hermesd`

Then enable:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now hermes-<project>
```

7) Start project by email:

- send first instruction email to `<project>@vitor.win`
- Hermes creates a fresh OpenCode session for that thread automatically
- continue iterating by replying in the same thread

## Current Files

- `src/` daemon/runtime modules
- `include/hermes.h` core interfaces/types
- `tests/` test binaries
- `ops/` service example
- `scripts/hermes-redeploy.sh` sync/build/restart helper
- `scripts/hermes-push-restart.sh` build/test/push/restart helper
- `scripts/setup-shared-github-key.sh` shared SSH key + git identity helper
- `SESSION_HANDOFF.md` state snapshot for future agents

## Security Notes

- Keep `.env` and runtime env files private (`chmod 600`).
- Never commit mailbox passwords or other secrets.
- Rotate leaked credentials immediately.
- Hermes must remain mail-client-only unless explicitly changed.
