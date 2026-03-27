from __future__ import annotations

import os
import tomllib
from pathlib import Path

from .models import AppConfig, MailboxProfile


def load_dotenv(path: Path) -> None:
	if not path.exists():
		return
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		key, value = line.split("=", 1)
		key = key.strip()
		value = value.strip().strip('"').strip("'")
		os.environ.setdefault(key, value)


def _env_int(name: str, default: int) -> int:
	raw = os.getenv(name)
	if not raw:
		return default
	try:
		return int(raw)
	except ValueError:
		return default


def load_app_config() -> AppConfig:
	load_dotenv(Path(".env"))
	api_key = os.getenv("OPENAI_API_KEY", "").strip()
	if not api_key:
		raise ValueError("OPENAI_API_KEY is required")
	config_dir = Path(os.getenv("HERMES_CONFIG_DIR", "mailboxes.d")).expanduser()
	state_dir = Path(os.getenv("HERMES_STATE_DIR", "var")).expanduser()
	return AppConfig(
		config_dir=config_dir,
		state_dir=state_dir,
		default_model=os.getenv("HERMES_MODEL", "gpt-5.4").strip() or "gpt-5.4",
		default_reasoning_effort=os.getenv("HERMES_REASONING_EFFORT", "medium").strip() or "medium",
		max_tool_rounds=max(1, _env_int("HERMES_MAX_TOOL_ROUNDS", 24)),
		max_workers=max(1, _env_int("HERMES_MAX_WORKERS", 8)),
		openai_api_key=api_key,
	)


def load_mailbox_profiles(app: AppConfig) -> list[MailboxProfile]:
	if not app.config_dir.exists():
		raise ValueError(f"mailbox config dir does not exist: {app.config_dir}")
	profiles: list[MailboxProfile] = []
	for path in sorted(app.config_dir.glob("*.toml")):
		data = tomllib.loads(path.read_text(encoding="utf-8"))
		profiles.append(_profile_from_toml(path, data, app))
	if not profiles:
		raise ValueError(f"no mailbox profiles found in {app.config_dir}")
	return profiles


def _profile_from_toml(path: Path, data: dict[str, object], app: AppConfig) -> MailboxProfile:
	required = [
		"name",
		"email_address",
		"username",
		"password",
		"imap_host",
		"smtp_host",
		"projects_root",
	]
	for key in required:
		if key not in data or not str(data[key]).strip():
			raise ValueError(f"{path}: missing required field {key}")
	allowed_raw = data.get("allowed_from", [])
	allowed = [str(item).strip().lower() for item in allowed_raw if str(item).strip()]
	root = Path(str(data["projects_root"])).expanduser()
	return MailboxProfile(
		name=str(data["name"]).strip(),
		email_address=str(data["email_address"]).strip(),
		username=str(data["username"]).strip(),
		password=str(data["password"]).strip(),
		imap_host=str(data["imap_host"]).strip(),
		smtp_host=str(data["smtp_host"]).strip(),
		allowed_from=allowed,
		projects_root=root,
		model=str(data.get("model", app.default_model)).strip() or app.default_model,
		reasoning_effort=str(data.get("reasoning_effort", app.default_reasoning_effort)).strip()
		or app.default_reasoning_effort,
		folder=str(data.get("folder", "INBOX")).strip() or "INBOX",
		poll_seconds=max(5, int(data.get("poll_seconds", 30))),
		imap_port=int(data.get("imap_port", 993)),
		smtp_port=int(data.get("smtp_port", 465)),
		imap_ssl=bool(data.get("imap_ssl", True)),
		smtp_ssl=bool(data.get("smtp_ssl", True)),
		concurrency=max(1, int(data.get("concurrency", 2))),
	)
