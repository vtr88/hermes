# hermes

Hermes is an email-driven coding agent.

Each mailbox profile becomes one operator identity. A new email thread creates a
new project workspace, and later replies in the same thread continue that same
session.

## What It Needs

- Python 3.9+
- an OpenAI API key
- IMAP and SMTP credentials for each mailbox
- `git`

## Install

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -e .
```

## Configure

Set these environment variables:

```sh
OPENAI_API_KEY=sk-REPLACE_ME
HERMES_CONFIG_DIR=/etc/hermes/mailboxes.d
HERMES_STATE_DIR=/var/lib/hermes
HERMES_MODEL=gpt-5.4
HERMES_REASONING_EFFORT=medium
```

Create one mailbox profile per email identity.
Example `/etc/hermes/mailboxes.d/alice.toml`:

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

## Run

```sh
python3 -m hermes
```

Hermes loads every `*.toml` file in `HERMES_CONFIG_DIR`, polls those mailboxes,
and creates one workspace per email thread.

## Approval

Some commands are blocked until the user approves them by replying in the same
thread with:

```text
/approve TOKEN
```

## Recommended Server Model

Run Hermes as a dedicated non-root user named `hermes`.

Suggested paths:

- app: `/srv/hermes/app`
- projects: `/srv/hermes/projects`
- state: `/var/lib/hermes`
- config: `/etc/hermes`

## Deployment Files

- service file: `ops/hermes.service.example`
- host setup example: `ops/setup-hermes-host.example.sh`

## Checks

```sh
python3 -m compileall hermes tests
python3 -m unittest discover -s tests -p 'test_*.py' -v
```
