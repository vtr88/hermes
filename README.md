# hermes

Hermes is a minimalist C daemon intended to bridge email threads to an LLM.

Current state: functional base with conditional backends.

- Event loop daemon (`hermesd`)
- SQLite message store when `sqlite3` dev headers are present
- OpenAI Responses API client when `libcurl` dev headers are present
- SMTP reply sender via `libcurl` when configured
- Safe IMAP NOOP connectivity poll via `libcurl`

## Build

Requirements:

- C17 compiler (`cc`)
- Optional: `libcurl` development headers/library
- Optional: `sqlite3` development headers/library

If optional dependencies are missing, Hermes still builds in fallback mode:

- without `libcurl`: OpenAI/email use local fallback behavior
- without `sqlite3`: message ledger uses flat-file fallback

Debian/Ubuntu packages:

```sh
sudo apt-get update
sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev
```

Build debug:

```sh
make
```

Build release:

```sh
make release
```

## Run

Create env file and export variables:

```sh
cp .env.example .env
set -a; . ./.env; set +a
make run
```

## Tests

Run all tests:

```sh
make test
```

Run one test:

```sh
make test TEST=test_defaults
```

## Security and Ops

- Use a dedicated mailbox account for Hermes.
- Use app-password auth for SMTP/IMAP.
- Keep `.env` untracked.
- Run as dedicated user `hermes` when possible.
- Hermes must act only as mail client; do not modify server mail config.

Hermes does not require Postfix/Dovecot configuration changes.

## Next Steps

1. Implement IMAP unseen fetch and MIME parsing.
2. Extract `thread_key` from `In-Reply-To`/`References`.
3. Store rolling session context and truncation strategy.
4. Add systemd unit and log rotation.
