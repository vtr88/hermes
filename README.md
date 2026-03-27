# hermes

Hermes is a multi-mailbox email coding agent built in Python.
Each mailbox profile acts like a Codex-style operator account: a new email thread
creates a fresh project workspace, later replies continue the same session, and
the agent can inspect files, plan work, edit code, run local commands, and ask
for approval before risky actions.

## What It Does

- polls one or more IMAP inboxes
- maps each email thread to a persistent coding session
- stores session state in SQLite, including the last OpenAI response id
- creates one local workspace per email thread
- uses the OpenAI Responses API with configurable models
- exposes Codex-like tools to the model:
  - file listing
  - file reading
  - text search
  - plan updates
  - file writes
  - patch application
  - shell commands with approval gates
- sends plain-text replies over SMTP

## Requirements

- Python 3.9 or newer
- an OpenAI API key
- IMAP and SMTP credentials for each mailbox user
- `git` available in `PATH`

## Install

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -e .
```

## Recommended Server Layout

Run Hermes as a dedicated unprivileged system user named `hermes`.
Do not run it as `root`.

Suggested layout:

- application code: `/srv/hermes/app`
- per-thread workspaces: `/srv/hermes/projects`
- SQLite state: `/var/lib/hermes`
- config and secrets: `/etc/hermes`

Hermes should create and manage its own workspaces under `/srv/hermes/projects`
instead of writing into arbitrary user home directories.

## Configuration

Global settings come from environment variables:

- `OPENAI_API_KEY`
- `HERMES_CONFIG_DIR` default `mailboxes.d`
- `HERMES_STATE_DIR` default `var`
- `HERMES_MODEL` default `gpt-5.4`
- `HERMES_REASONING_EFFORT` default `medium`
- `HERMES_MAX_TOOL_ROUNDS` default `24`
- `HERMES_MAX_WORKERS` default `8`

Each mailbox is configured by a TOML profile in `mailboxes.d/`.
Example:

```toml
name = "alice"
email_address = "alice@example.com"
username = "alice@example.com"
password = "REPLACE_WITH_APP_PASSWORD"
imap_host = "imap.example.com"
smtp_host = "smtp.example.com"
allowed_from = ["you@example.com"]
projects_root = "/srv/hermes/projects/alice"

# Optional overrides.
model = "gpt-5.4"
reasoning_effort = "medium"
folder = "INBOX"
poll_seconds = 20
smtp_port = 465
imap_port = 993
```

## Running

```sh
python3 -m hermes
```

Hermes loads every `*.toml` file from `HERMES_CONFIG_DIR`, starts one mailbox
poller per profile, and processes multiple project threads concurrently.

## Mailbox Users And Project Sessions

To create an operator mailbox user:

1. Create a normal mail account on your provider, such as `alice@example.com`.
2. Generate an app password or mailbox-specific password.
3. Create a mailbox TOML profile for that user in `mailboxes.d/`.
4. Set `projects_root` to a writable directory owned by the service user.
5. Start Hermes with the profile present.

From then on:

1. A new email thread to that mailbox creates a new workspace under that
   mailbox's `projects_root`.
2. The workspace is initialized as a git repository so the agent can apply
   patches and inspect diffs cleanly.
3. Replies in the same mail thread continue the same workspace and OpenAI
   response chain.
4. Multiple mailbox users can be active at the same time because each mailbox
   has its own poller and its own project root.
5. Multiple project threads can also run at the same time inside a mailbox.

## Server Setup

Create the dedicated service account and directories:

```sh
sudo useradd --system --create-home --home-dir /srv/hermes --shell /usr/sbin/nologin hermes
sudo install -d -o hermes -g hermes /srv/hermes/app
sudo install -d -o hermes -g hermes /srv/hermes/projects
sudo install -d -o hermes -g hermes /var/lib/hermes
sudo install -d -o root -g hermes -m 0750 /etc/hermes
```

Copy the repository to `/srv/hermes/app`, create the virtualenv there, and
install Hermes:

```sh
sudo -u hermes python3 -m venv /srv/hermes/app/.venv
sudo -u hermes /srv/hermes/app/.venv/bin/pip install -e /srv/hermes/app
```

Create `/etc/hermes/hermes.env`:

```sh
OPENAI_API_KEY=sk-REPLACE_ME
HERMES_CONFIG_DIR=/etc/hermes/mailboxes.d
HERMES_STATE_DIR=/var/lib/hermes
HERMES_MODEL=gpt-5.4
HERMES_REASONING_EFFORT=medium
HERMES_MAX_TOOL_ROUNDS=24
HERMES_MAX_WORKERS=8
```

Create the mailbox config directory and one TOML file per mailbox:

```sh
sudo install -d -o root -g hermes -m 0750 /etc/hermes/mailboxes.d
sudoedit /etc/hermes/mailboxes.d/alice.toml
```

Example mailbox profile:

```toml
name = "alice"
email_address = "alice@example.com"
username = "alice@example.com"
password = "REPLACE_WITH_APP_PASSWORD"
imap_host = "imap.example.com"
smtp_host = "smtp.example.com"
allowed_from = ["you@example.com"]
projects_root = "/srv/hermes/projects/alice"

model = "gpt-5.4"
reasoning_effort = "medium"
folder = "INBOX"
poll_seconds = 20
imap_port = 993
smtp_port = 465
imap_ssl = true
smtp_ssl = true
```

Create the per-mailbox project root and hand it to the `hermes` user:

```sh
sudo install -d -o hermes -g hermes /srv/hermes/projects/alice
```

If you want multiple mailbox identities active at once, add more TOML files and
create their matching project directories under `/srv/hermes/projects/`.

## systemd Deployment

Install the service file:

```sh
sudo cp ops/hermes.service.example /etc/systemd/system/hermes.service
sudo systemctl daemon-reload
sudo systemctl enable --now hermes
```

Check status and logs:

```sh
sudo systemctl status hermes
sudo journalctl -u hermes -f
```

The service should run as `hermes`, not `root`. If you ever need Hermes to work
with an existing repository owned by another user, grant access only to that
specific path instead of giving Hermes blanket write access to `/home`.

## Approval Flow

Safe commands such as reading files, running tests, or asking `git status` can
run immediately. Riskier commands create an approval token. Reply to the same
email thread with:

```text
/approve TOKEN
```

Hermes will resume the session and allow the approved command once.

## OpenAI Model Selection

Hermes defaults to `gpt-5.4`, but you can set `model` per mailbox profile or
override the global default with `HERMES_MODEL`. This lets you switch to a
different GPT-5 family model later without changing the code.

## Local Commands

```sh
python3 -m compileall hermes tests
ruff check hermes tests
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 -m hermes
```

## systemd Example

An example unit file lives at `ops/hermes.service.example`.
It expects a dedicated `hermes` service user, `/etc/hermes` for config, and
workspace/state directories owned by that service user.
