# Hermes Session Handoff

This file is the canonical handoff context for the email agent.

## Current Product Direction

Hermes is now an **email wrapper around OpenCode session execution**.

Target behavior:

- User sends plain email instructions (no `/run` needed).
- Hermes treats each email as next turn of an OpenCode-like coding session.
- Hermes executes in project workdir and replies with assistant-style output.
- Session persists by thread, so replies continue context.
- Footer includes runtime context and `opencode stats` output.

## Key Decisions

1. Removed legacy intent routing and explicit command-first UX.
2. `src/tool_exec.c` now calls `opencode run --format json` directly.
3. Session persistence added in DB via `thread_sessions` table.
4. If a thread has no saved session, Hermes can use seeded env session id.
5. Keep Hermes mail-client-only (no Postfix/Dovecot config edits).

## Seed Session ID

Use this session id as bootstrap when thread has no stored session:

- `ses_2ed9abc99ffew9VyabNwJMXVJL`

Configured via:

- `HERMES_OPENCODE_SESSION_ID=ses_2ed9abc99ffew9VyabNwJMXVJL`

## Expected Runtime Paths

- mailbox user: `hermes@vitor.win`
- project path: `/home/hermes/Projects/hermes`
- service: `/etc/systemd/system/hermes.service`

## Git Identity Standard

Use this email for commits:

- `vtr88@yahoo.com.br`

Do not use previous `contato@vitor.win` anymore.

## What To Verify After Deploy

1. Plain email request triggers OpenCode backend execution.
2. Reply reflects assistant output from `opencode run --format json`.
3. Thread continuity persists across follow-up emails.
4. Footer includes:
   - turn/cumulative token context
   - budget line
   - modified files
   - `opencode stats`

## Next Work if Behavior Drifts

If Hermes falls back to generic “I can’t access repo” text:

1. Ensure deployed binary is latest commit.
2. Confirm `opencode` binary is available to service user.
3. Validate session id persistence (`thread_sessions`).
4. Inspect JSON parsing path in `src/tool_exec.c`.
