# hermes

Hermes is a minimalist C daemon that runs OpenCode through email.
It polls an IMAP inbox, executes a turn with `opencode run --format json`,
and replies over SMTP in the same thread.

## Features

- Thread-aware replies with per-thread OpenCode session continuity
- Plain-text email responses (no raw event dump in normal replies)
- Approval flow support when OpenCode returns a pending approval
- SQLite-backed message/session/usage state
- Strict sender allowlist support

## Requirements

- Linux host
- C compiler with C17 support (`cc`)
- `make`
- `libcurl` development headers/libraries (IMAP/SMTP)
- `sqlite3` development headers/libraries
- `opencode` available in `PATH`

## Build and Test

```sh
make
make test
```

Useful targets:

```sh
make release
make lint
make clean
```

## Configuration

Copy `.env.example` values into your runtime environment (or service env file)
and set at least the required mail fields.

Key variables:

- `HERMES_IMAP_URL` (example: `imaps://mail.example.com/INBOX`)
- `HERMES_SMTP_URL` (example: `smtps://mail.example.com:465`)
- `HERMES_MAIL_USER`
- `HERMES_MAIL_PASS`
- `HERMES_MAIL_FROM` (required)
- `HERMES_MAIL_TO` (optional; defaults to replying to sender)
- `HERMES_ALLOW_FROM` (comma-separated allowlist)
- `HERMES_WORKDIR` (working directory used by OpenCode)
- `HERMES_DB_PATH` (default: `build/hermes.db`)
- `HERMES_TOOL_TIMEOUT_SEC` (`0` means no timeout)
- `HERMES_POLL_SECONDS` (default: `30`)
- `HERMES_BUDGET_USD` (optional usage budget)

## Run

```sh
make run
```

In production, run Hermes with a process manager (typically systemd).

## Service Notes

- Hermes is designed to run as a regular service user (for example `hermes`).
- Keep credentials outside git (environment file, secret manager, etc.).
- For email-triggered restart behavior, configure restricted sudoers entries for
  `systemctl restart hermes` and `systemctl is-active hermes`.
