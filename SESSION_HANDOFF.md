# Hermes Session Handoff

## What This Project Is

Hermes is a minimalist C daemon that runs OpenCode through email.
It reads inbound IMAP messages, executes with `opencode run --format json`, and replies by SMTP in the same thread.

## Current Behavior (Important)

- Thread continuity is required and active: one email thread should keep one OpenCode session.
- Hermes should only request `/approve <token>` when OpenCode returns a pending approval.
- User replies must be clean plain text (no raw JSON/event dump in normal responses).
- Runtime is lean and opencode-only; legacy OpenAI path has been removed.

## User Direction

The user wants Hermes to stay minimal in docs and code:

- keep code small and direct,
- remove unused paths and duplicated logic,
- avoid verbose README/docs,
- prioritize reliability of session continuity and practical responses over extra features.

## Immediate Next Steps

1. Verify continuity on multiple reply turns in the same thread after restart/deploy.
2. Verify approval flow end-to-end only when OpenCode explicitly asks for approval.
3. Keep trimming unnecessary code/comments while preserving behavior.
4. Keep README and handoff docs short and direct.

## Runtime Paths

- Repo/workdir: `/home/hermes/Projects/hermes`
- Service: `/etc/systemd/system/hermes.service`
- Env file: `/home/hermes/.config/hermes/hermes.env`

## Deploy/Restart

```sh
git pull && make && make test && sudo systemctl restart hermes
```

Quick restart only:

```sh
sudo systemctl restart hermes
```
