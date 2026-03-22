# Hermes Session Handoff

This file is the canonical context for email-driven agent continuation.

## Current State

Hermes now runs as an **opencode-only email wrapper**:

- Each inbound email is treated as the next user turn.
- Hermes runs `opencode run --format json` in project workdir.
- Reply text is extracted from JSON event stream and emailed back.
- Session continuity is persisted per email thread (`thread_sessions`).
- Metrics footer is appended to every reply.
- Slash-command UX is not primary; default interaction is natural language email turns.

## Active Session Seed

Recommended runtime default is **no forced seed**:

- keep `HERMES_OPENCODE_SESSION_ID` unset,
- let Hermes create/store session ids per thread in `thread_sessions`.

Use `HERMES_OPENCODE_SESSION_ID=<ses_...>` only when intentionally bootstrapping from a known valid existing session.

## What Was Just Completed

1. Removed legacy OpenAI fallback flow from runtime message handling.
2. Wired runtime to tool path only (`tool_try_handle`) and propagated usage stats.
3. Hardened opencode invocation in `src/tool_exec.c`:
   - prompts trimmed,
   - blank prompt fallback,
   - `--` separator before prompt to avoid argument misparse.
4. Added approval round-trip support:
   - detect pending approval from opencode JSON,
   - store token/command in `pending_actions`,
   - allow resume with `approve <token>` or `/approve <token>`.
5. Updated config/tests/docs for opencode-only operation.
6. Added auto-retry without session id when OpenCode reports `Session not found`.
7. Added email turn wrapper prompt to reduce meta responses and improve direct execution style.

## Key Runtime Files

- `src/main.c`: polling loop, sender allowlist, footer, message dispatch.
- `src/tool_exec.c`: opencode process execution, JSON parse, approval flow.
- `src/config.c`: env loading (mail vars + opencode session seed).
- `src/db.c`: messages, usage totals, pending approvals, thread sessions.
- `include/hermes.h`: shared runtime interfaces.

## Operational Paths

- Repo/workdir: `/home/hermes/Projects/hermes`
- Service file: `/etc/systemd/system/hermes.service`
- Runtime env: `/home/hermes/.config/hermes/hermes.env`

## Multi-Project Expansion Plan

Planned operating model:

- Keep `hermes` as base/reference project account.
- For each new app, create a new Linux user + matching mailbox account.
- Deploy same hermes daemon under that user and project path.
- Run one service per project user (for example `hermes-<project>.service`).
- Start work by emailing that account; Hermes creates a fresh session for new threads.

## Git Identity Standard

- Name: `Vitor Hugo`
- Email: `vtr88@yahoo.com.br`

Do not use previous `contato@vitor.win` for new commits.

## Restart / Deploy Commands

Manual restart only:

```sh
sudo systemctl daemon-reload
sudo systemctl restart hermes
sudo systemctl status hermes --no-pager
sudo journalctl -u hermes -n 120 --no-pager
```

Rebuild + tests + restart:

```sh
cd /home/hermes/Projects/hermes
make -B && make test
./scripts/hermes-redeploy.sh hermes hermes "Vitor Hugo" "vtr88@yahoo.com.br"
```

Build + tests + push + restart:

```sh
cd /home/hermes/Projects/hermes
./scripts/hermes-push-restart.sh hermes hermes main
```

## Email Verification Checklist

1. Send plain request email (no slash command needed).
2. Confirm reply includes assistant output (not opencode CLI help text).
3. Send follow-up in same thread and confirm context continuity.
4. Trigger an approval-needed action; confirm token email appears.
5. Reply with `/approve <token>` and confirm action resumes.
6. Confirm footer includes turn/cumulative stats, budget, workdir, modified files, `opencode stats`.

## First Prompt To Send By Email

```text
Read SESSION_HANDOFF.md.
Summarize current Hermes architecture in 3 bullets and list next 2 improvements.
```
