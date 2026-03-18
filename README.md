# hermes

Hermes is a minimalist C daemon that turns an email thread into an ongoing coding-assistant session.

You send an email, Hermes reads it over IMAP, asks the LLM, and replies over SMTP in the same thread.
When you answer again, Hermes continues the conversation context.

## Goals

- Keep the implementation small, readable, and Unix-first.
- Run safely as a normal IMAP/SMTP client.
- Never require changes to Postfix/Dovecot config.
- Feel like an OpenCode/Codex workflow, but over email.

## Current Features

- C17 daemon (`build/hermesd`) with poll loop.
- IMAP unseen polling and message fetch (libcurl).
- SMTP threaded replies (libcurl).
- OpenAI Responses API call (libcurl).
- Plain-text assistant extraction from API responses.
- Basic quoted-reply stripping from incoming emails.
- Sender allowlist (`HERMES_ALLOW_FROM`).
- Prompt size clamp (`HERMES_MAX_PROMPT_CHARS`).
- Message dedupe/session persistence (SQLite, with fallback mode).

## Project Layout

- `src/` runtime source
- `include/` public headers
- `tests/` unit tests
- `ops/` service example
- `build/` generated artifacts

## Build

Required:

- `cc` (C17)

Recommended:

- `libcurl` dev package
- `libsqlite3` dev package

Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev
```

Commands:

```sh
make          # debug build
make release  # optimized build
make test     # test suite
make lint     # static checks (if clang-tidy exists)
```

## Quick Start

1) Create config file:

```sh
cp .env.example .env
```

2) Edit `.env` with real credentials.

3) Run:

```sh
set -a; . ./.env; set +a
make run
```

4) Send email to Hermes mailbox, then reply in-thread to continue session.

## Environment Variables

Core:

- `HERMES_OPENAI_KEY`
- `HERMES_OPENAI_MODEL`
- `HERMES_OPENAI_URL`
- `HERMES_OPENAI_SYSTEM`

Mail:

- `HERMES_IMAP_URL` (example: `imaps://mail.example.com/INBOX`)
- `HERMES_SMTP_URL` (example: `smtps://mail.example.com:465`)
- `HERMES_MAIL_USER`
- `HERMES_MAIL_PASS`
- `HERMES_MAIL_FROM`
- `HERMES_MAIL_TO`

Safety:

- `HERMES_ALLOW_FROM` (comma-separated allowlist)
- `HERMES_MAX_PROMPT_CHARS` (default: `12000`)

Runtime:

- `HERMES_DB_PATH`
- `HERMES_POLL_SECONDS`

## Production Notes

- Run under dedicated Unix user `hermes`.
- Use dedicated mailbox account (recommended).
- Keep env file private (`chmod 600`).
- Hermes is client-only: IMAP/SMTP auth, no mail-server config edits.

## systemd

Service template:

- `ops/hermes.service.example`

Typical setup:

```sh
sudo cp ops/hermes.service.example /etc/systemd/system/hermes.service
sudo systemctl daemon-reload
sudo systemctl enable --now hermes
sudo journalctl -u hermes -f
```

## Status and Roadmap

Implemented now:

- end-to-end email -> model -> email loop
- basic session continuity and dedupe

Planned next:

- stronger MIME parsing (multipart/plain extraction)
- richer thread context persistence
- tighter output formatting for OpenCode-like replies
- retry/backoff and delivery robustness
