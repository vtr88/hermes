# AGENTS.md
Operating rules for coding agents in `hermes`.

## Scope
- This repository is Python-only.
- Hermes is an email-driven coding agent that runs local project workspaces.
- Target style is small, explicit, and Unix-friendly.
- Prefer standard-library solutions unless a dependency clearly earns its keep.

## Source Of Truth
- `pyproject.toml` defines packaging and Python compatibility.
- `README.md` defines operator-facing behavior and deployment expectations.
- Mailbox profile TOML files define runtime users/mailboxes.

## Build / Lint / Test Commands
Use direct Python commands unless the repo adds a dedicated task runner later.

### Core Commands
- Build/check syntax: `python3 -m compileall hermes tests`
- Run service: `python3 -m hermes`
- Run static checks: `ruff check hermes tests`
- Run tests: `python3 -m unittest discover -s tests -p 'test_*.py' -v`
- Clean caches/artifacts: remove `__pycache__`, `.ruff_cache`, and local build outputs

### Tooling Expectations
- Python: 3.9+
- Package manager: `pip`
- Tests: stdlib `unittest`
- Lint: prefer `ruff` when available, otherwise fall back to syntax checks

## Project Layout
- `hermes/` runtime code
- `tests/` unit tests
- `mailboxes.d/` example mailbox profiles
- `ops/` service examples and deployment notes

## Python Style Guide

### General
- Keep modules focused.
- Prefer dataclasses and small helper functions over sprawling classes.
- Use type hints on public functions and data structures.
- Prefer `pathlib.Path` over manual path string handling.

### Naming
- Files/modules: `lower_snake_case.py`
- Functions/variables: `lower_snake_case`
- Classes: `CapWords`
- Constants: `UPPER_SNAKE_CASE`
- Internal helpers should be prefixed with `_` when not part of the public module API.

### Control Flow
- Prefer guard clauses over nested `if` towers.
- Keep command/tool loops explicit and easy to inspect.
- Avoid hidden global mutable state.

### Error Handling
- Fail with actionable messages.
- Keep operator-facing logs concise.
- Never swallow subprocess or mailbox errors without recording context.

### Security
- Never commit secrets, tokens, or mailbox passwords.
- Keep all filesystem operations inside the configured workspace roots.
- Require explicit approval for risky shell commands.
- Treat email senders as untrusted unless allowlisted.

### Comments And Docs
- Comment invariants, security checks, and non-obvious protocol decisions.
- Keep comments short and current.

## Testing Expectations
- Add or update tests with every behavior change.
- Prefer deterministic tests with temporary directories and fixture emails.
- Cover success paths and the main failure/approval paths.

## Agent Workflow
- Read the existing code before editing.
- Make the smallest coherent change that advances the full feature.
- Run local checks before finishing.
- Keep deployment docs aligned with real config fields and service behavior.
