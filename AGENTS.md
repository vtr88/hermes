# AGENTS.md
Guidance for autonomous coding agents in this repository.

## Repository Snapshot
- The current directory is empty (no source, manifests, or tests detected).
- No prior `AGENTS.md` was found.
- No Cursor rules found in `.cursor/rules/` or `.cursorrules`.
- No Copilot rules found in `.github/copilot-instructions.md`.
- This file provides default behavior until real project files exist.

## Build / Lint / Test Commands
Use repository-defined commands first; use fallback commands only when needed.

### Toolchain Detection
Identify ecosystem from manifests before running commands:
- Node: `package.json`, `pnpm-lock.yaml`, `yarn.lock`, `bun.lock*`
- Python: `pyproject.toml`, `requirements.txt`, `poetry.lock`, `pytest.ini`
- Go: `go.mod`
- Rust: `Cargo.toml`
- Java/Kotlin: `build.gradle*`, `pom.xml`
- .NET: `*.sln`, `*.csproj`

### Node / JavaScript / TypeScript
If `package.json` exists:
- Install: `npm ci` (or lockfile-matching package manager)
- Build: `npm run build`
- Lint: `npm run lint`
- Test all: `npm test` or `npm run test`
- Single test file: `npm test -- path/to/test.spec.ts`
- Single test name: `npm test -- -t "test name"`
Framework-specific fallbacks:
- Vitest file: `npx vitest run path/to/test.spec.ts`
- Vitest name: `npx vitest run -t "test name"`
- Jest file: `npx jest path/to/test.spec.ts`
- Jest name: `npx jest -t "test name"`

### Python
- Lint: `ruff check .`
- Format check: `ruff format --check .` or `black --check .`
- Type check: `mypy .` or `pyright` (if configured)
- Test all: `pytest`
- Single test file: `pytest tests/test_file.py`
- Single test function: `pytest tests/test_file.py::test_name`
- Name filter: `pytest -k "name_fragment"`

### Go
- Build: `go build ./...`
- Lint: `golangci-lint run` (if configured)
- Test all: `go test ./...`
- Single package: `go test ./path/to/pkg`
- Single test: `go test ./path/to/pkg -run TestName`

### Rust
- Build: `cargo build`
- Lint: `cargo clippy --all-targets --all-features -D warnings`
- Format check: `cargo fmt -- --check`
- Test all: `cargo test`
- Single test: `cargo test test_name`

### Java / Kotlin
Gradle:
- Build: `./gradlew build`
- Lint: `./gradlew lint` (or configured static analysis)
- Test all: `./gradlew test`
- Single test: `./gradlew test --tests "com.example.ClassName.testName"`
Maven:
- Build: `mvn -DskipTests package`
- Test all: `mvn test`
- Single test class: `mvn -Dtest=ClassName test`
- Single test method: `mvn -Dtest=ClassName#methodName test`

### .NET
- Build: `dotnet build`
- Test all: `dotnet test`
- Single test: `dotnet test --filter "FullyQualifiedName~TestName"`

### Command Execution Rules
- Prefer repo scripts/config over guessed defaults.
- Start with smallest useful check (single test / changed module).
- Run broader suites for medium/high-risk changes.
- If toolchain is missing, report exact dependency blocker and stop.

## Code Style Guidelines
Apply defaults below unless project config overrides them.

### Imports
- Keep imports explicit, minimal, and used.
- Group order: standard library, third-party, internal modules.
- Separate groups with one blank line.
- Avoid wildcard imports unless project convention requires them.

### Formatting
- Use the configured formatter/linter as source of truth.
- Do not hand-format against formatter output.
- Keep lines readable (target ~100 columns unless configured otherwise).
- No trailing whitespace; always end files with newline.

### Types
- Prefer explicit types at public boundaries.
- Use local inference when type is obvious and readability improves.
- Avoid `any`/untyped escapes unless unavoidable.
- Represent optional/nullability explicitly.

### Naming
- Use descriptive, domain-relevant names.
- Follow language-idiomatic casing consistently.
- Name booleans as predicates (`is`, `has`, `can`, `should`).
- Avoid unclear abbreviations unless already standard in codebase.

### Error Handling
- Validate inputs at boundaries and fail fast.
- Never swallow errors silently.
- Include actionable context in error messages.
- Preserve root cause/context when wrapping errors.

### Code Structure
- Prefer small, single-purpose functions.
- Use guard clauses to avoid deep nesting.
- Keep side effects localized and explicit.
- Separate pure business logic from I/O and framework glue.

### Comments and Documentation
- Prioritize clear code over excessive comments.
- Comment non-obvious intent, invariants, or tradeoffs.
- Keep comments/docs synced with code changes.
- Update public-facing docs for behavior/interface changes.

### Testing
- Add or update tests for behavior changes.
- Cover success paths, edge cases, and failures.
- Keep tests deterministic; avoid flaky timing/network dependencies.

### Security and Dependencies
- Never commit secrets, tokens, or credentials.
- Prefer pinned/locked dependency workflows.
- Minimize new dependencies and justify significant additions.
- Sanitize and validate untrusted input at boundaries.

## Cursor and Copilot Rules
No repository-specific instruction files currently exist:
- `.cursor/rules/`
- `.cursorrules`
- `.github/copilot-instructions.md`
If these files appear later, treat them as higher-priority local policy and merge their constraints here.

## Maintenance
When project files are added, replace generic commands with exact repo commands, verify single-test examples, and document project-specific style exceptions.
