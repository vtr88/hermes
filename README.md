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
It expects a non-root service user, a config directory, and per-mailbox project
roots owned by that service user.
