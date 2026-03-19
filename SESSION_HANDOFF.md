# Hermes Session Handoff

This file captures the current project state and major decisions from the build session.
Use it as first-read context for any new agent operating by email.

## Mission

Build `hermes` as a minimalist C daemon that behaves as close as possible to an OpenCode/Codex CLI workflow, but over email:

- receive user prompts by IMAP
- continue a thread/session over email replies
- run coding/tool commands in project workdir
- require explicit approval for risky commands
- return practical assistant responses + runtime context footer

## Hard Constraints Agreed

- Never modify Postfix/Dovecot/mail-server config as part of Hermes operation.
- Hermes must act as a normal IMAP/SMTP client only.
- Support per-project identity model:
  - mailbox/user like `projectname@vitor.win`
  - Linux user `projectname`
  - project path `/home/projectname/Projects/projectname`
- Email responses should resemble OpenCode style (plain text, practical, concise).

## What Was Implemented

### Repository and docs

- `AGENTS.md` rewritten for C-only, minimalist/suckless style.
- `README.md` rewritten as a real project document with:
  - setup/build/run
  - email command mode
  - per-project user bootstrap guide
  - first-email examples

### Core daemon and modules

- `src/main.c`:
  - event loop
  - sender allowlist check
  - message dedupe
  - thread context injection
  - tool command handling path before LLM path
  - response footer append path

- `src/email.c`:
  - IMAP unseen polling/fetch
  - SMTP threaded replies
  - basic quoted-reply stripping and body cleaning

- `src/openai.c`:
  - OpenAI Responses API request path
  - plain-text extraction from response JSON variants
  - usage token extraction

- `src/db.c`:
  - SQLite message persistence
  - dedupe checks
  - thread context retrieval
  - pending approval token storage/consume
  - cumulative usage totals storage

- `src/tool_exec.c` (new):
  - `/run <command>` support
  - `/approve <token>` support
  - read-only commands run directly
  - write/destructive commands require approval token
  - command timeout support

### Config / build

- `Makefile` updated to include `src/tool_exec.c` and conditional sqlite/curl support.
- `.env.example` updated with relevant runtime knobs.
- Config now supports (via `src/config.c`):
  - `HERMES_ALLOW_FROM`
  - `HERMES_MAX_PROMPT_CHARS`
  - `HERMES_TOOL_TIMEOUT_SEC`
  - optional budget/cost vars
  - default auto workdir derivation from mailbox localpart

## Current Workdir Resolution Rule

If `HERMES_WORKDIR` is not set, Hermes derives:

`/home/<mail_user_localpart>/Projects/<mail_user_localpart>`

Example:

- `HERMES_MAIL_USER=hermes@vitor.win`
- default workdir -> `/home/hermes/Projects/hermes`

## Email Command Protocol (Implemented)

Put command in first line of email body:

- `/run git status`
- `/run make test`

If command is classified write-capable/destructive, Hermes returns token.
Approve by replying with:

- `/approve <token>`

## Response Footer Target (Implemented)

Each reply should append a footer with:

- per-turn token usage
- cumulative token usage
- budget/cost line
- effective workdir
- modified files snapshot (`git status --short`)

## Infrastructure State at End

- Remote repo configured and pushing to: `git@github.com:vtr88/hermes.git`
- Service path expected: `/etc/systemd/system/hermes.service`
- Project relocation target agreed:
  - from `/home/soth/Projects/hermes`
  - to `/home/hermes/Projects/hermes`

## Known Focus for Next Agent

1. Validate live email behavior after relocation and service restart.
2. Confirm OpenAI response extraction no longer returns raw JSON.
3. Verify footer appears in every reply.
4. Tighten command classifier and approval categories.
5. Improve MIME parsing and robust thread continuity.

## Per-Project Bootstrap Pattern (Canonical)

For project `newproject`:

1. Create Linux user + password.
2. Create `/home/newproject/Projects/newproject`.
3. Deploy Hermes code there.
4. Configure env with `HERMES_MAIL_USER=newproject@vitor.win`.
5. Start per-project service pointing to that path.
6. Hermes automatically uses that project workdir unless overridden.

## Important Security Reminder

The user accidentally pasted an `sk-proj-...` key during session.
Treat as compromised; rotate/revoke and do not reuse.
