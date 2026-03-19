# hermes

Hermes is a minimalist C daemon that lets you run an OpenCode-like coding workflow over email.

You send an email, Hermes reads it via IMAP, decides whether it is a chat request or actionable command,
executes safely inside the project workdir when needed, and replies in the same thread via SMTP.

## What Hermes Does

- Ongoing thread context per email conversation.
- Natural-language coding requests (for common intents) and explicit command mode.
- Approval-token flow for write/destructive operations.
- Response footer with execution/usage context and modified files.
- Runs as plain IMAP/SMTP client (no Postfix/Dovecot config edits required).

## Command Modes

### 1) Natural language mode

Examples:

- `please check git status`
- `run tests`
- `please commit to github`

Hermes maps known intents to safe command flows. For risky actions it asks for approval.

### 2) Explicit mode

- `/run <command>`
- `/approve <token>`

Examples:

```text
/run git status
```

```text
/run git add -A && git commit -m "msg" && git push origin main
```

If approval is required, Hermes replies with a token. Confirm with:

```text
/approve 1a2b3c4d5e6f
```

## Workdir Convention

By default, Hermes derives the project workdir from mailbox username:

`/home/<mail_user_localpart>/Projects/<mail_user_localpart>`

Example:

- mailbox user: `hermes@vitor.win`
- workdir: `/home/hermes/Projects/hermes`

You can override with `HERMES_WORKDIR`, but default convention is recommended.

## Prerequisites (Server)

- Linux server with your mail server already running.
- `cc`, `make`, `libcurl`, `sqlite3` dev libs.
- A mailbox account matching Linux user convention.

Install build deps (Debian/Ubuntu):

```sh
sudo apt-get update
sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev rsync
```

## Project Bootstrap (Hermes User)

This is the canonical setup for project `hermes`.

### 1) Create Linux user and password

```sh
sudo useradd --create-home --shell /usr/sbin/nologin hermes || true
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
HERMES_OPENAI_KEY=sk-proj-...
HERMES_OPENAI_MODEL=gpt-5.3-codex
HERMES_OPENAI_URL=https://api.openai.com/v1/responses
HERMES_OPENAI_SYSTEM=You are Hermes. Reply in plain text, concise and practical.

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

## Git Push Setup for nologin Users

Because project users use `nologin`, configure git and SSH non-interactively.

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

Custom form:

```sh
./scripts/hermes-redeploy.sh <project-user> <service-name> <git-name> <git-email>
```

Example:

```sh
./scripts/hermes-redeploy.sh hermes hermes "Vitor Hugo" "contato@vitor.win"
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

## Current Files

- `src/` daemon/runtime modules
- `include/hermes.h` core interfaces/types
- `tests/` test binaries
- `ops/` service example
- `scripts/hermes-redeploy.sh` sync/build/restart helper
- `scripts/setup-shared-github-key.sh` shared SSH key + git identity helper
- `SESSION_HANDOFF.md` state snapshot for future agents

## Security Notes

- Keep `.env` and runtime env files private (`chmod 600`).
- Never commit API keys/passwords.
- Rotate any leaked keys immediately.
- Hermes must remain mail-client-only unless explicitly changed.
