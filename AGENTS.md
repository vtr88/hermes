# AGENTS.md
Operating rules for coding agents in `hermes`.

## Scope
- This repository is C-only.
- Target style is minimalist, Unix-first, suckless-inspired C.
- Keep code small, direct, and readable.
- Avoid framework creep and unnecessary dependencies.

## Local Policy Files
- No Cursor rules found in `.cursor/rules/`.
- No `.cursorrules` found.
- No Copilot rules found in `.github/copilot-instructions.md`.
- If these files appear later, they override this document.

## Build / Lint / Test Commands
Use `make` targets as the source of truth.

### Core Commands
- Build debug: `make`
- Build release: `make release`
- Run daemon: `make run`
- Run static checks: `make lint`
- Run tests: `make test`
- Clean: `make clean`

### Single-Test Execution
- Run one test by name: `make test TEST=test_name`
- Run one test binary: `./tests/test_config`
- Run one case with CMocka/Criterion filter (if adopted):
  - `./tests/test_config --single test_name`

### Tooling Expectations
- Compiler: `cc` with C17 (`-std=c17`)
- Warnings: `-Wall -Wextra -Wpedantic -Werror`
- Sanitizers in debug when possible: `-fsanitize=address,undefined`
- Format check (if `clang-format` exists): `make fmt-check`
- Static analysis (if `clang-tidy` exists): `make lint`

## Project Layout
- `src/` runtime code
- `include/` headers
- `tests/` unit/integration tests
- `ops/` service examples and deployment notes
- `build/` local artifacts (gitignored)

## C Style Guide

### Language and Standard Library
- Use portable C17.
- Prefer libc + POSIX APIs over large external layers.
- Keep external libraries minimal and justified.
- No C++.

### File and Function Size
- Keep files focused by module.
- Keep functions short and single-purpose.
- Prefer extraction over deep nesting.
- Use early returns to reduce indentation.

### Headers and Includes
- Include what you use.
- Order includes:
  1) corresponding local header,
  2) project headers,
  3) system headers.
- One blank line between groups.
- Avoid transitive include reliance.

### Naming
- Files/modules: `lower_snake_case`.
- Functions/variables: `lower_snake_case`.
- Struct typedef names: `snake_case_t`.
- Macros/constants: `UPPER_SNAKE_CASE`.
- Internal-only symbols should be `static`.

### Formatting
- Tabs for indentation, tabsize 8 (suckless style).
- Braces on same line for functions and control blocks.
- One declaration per line.
- Keep lines readable; hard cap at 100 unless unavoidable.
- No trailing whitespace.

### Memory and Ownership
- Make ownership obvious at API boundaries.
- Pair alloc/free in same module where possible.
- Free on all error paths.
- Set pointers to `NULL` after free when reused.
- Avoid hidden global mutable state.

### Error Handling
- Return `0` on success, `-1` on failure unless API dictates otherwise.
- Set `errno` or propagate precise error codes.
- Emit concise diagnostics to `stderr`/syslog with context.
- Do not swallow errors.
- Fail fast at boundaries; recover only when meaningful.

### Control Flow
- Prefer guard clauses over nested `if` towers.
- Avoid `goto` except for single cleanup label patterns.
- No recursion unless clearly bounded and justified.
- Keep state machines explicit and small.

### Concurrency
- Default to single-process event loop for simplicity.
- Add threads only with clear performance need.
- Protect shared state explicitly.

### Comments and Documentation
- Avoid obvious comments.
- Comment protocol edge-cases, invariants, and tricky decisions.
- Keep comments short and current.
- Document public APIs in headers.

## Security and Ops Constraints
- Never commit secrets, API keys, or passwords.
- Read credentials from environment or local untracked config.
- Run daemon as least-privileged dedicated user (`hermes`) if possible.
- Do not modify existing mail server config by default.
- Hermes must connect as a normal IMAP/SMTP client only.

## Testing Expectations
- Add tests with every behavior change.
- Prefer deterministic tests; avoid real network in unit tests.
- Use small fixture inputs and stable outputs.
- Cover success path, malformed input, and failure path.

## Agent Workflow
- Read existing code before editing.
- Make the smallest coherent change first.
- Build and run targeted tests before broad checks.
- Keep commits focused and message intent-first.
- If missing context blocks progress, state blocker and best next action.

## Maintenance
- Keep this file aligned with actual `Makefile` targets.
- Update single-test instructions when test harness evolves.
- Add project-specific protocol notes as IMAP/SMTP features land.
