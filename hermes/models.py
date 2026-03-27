from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List, Optional


@dataclass
class AppConfig:
	config_dir: Path
	state_dir: Path
	default_model: str
	default_reasoning_effort: str
	max_tool_rounds: int
	max_workers: int
	openai_api_key: str


@dataclass
class MailboxProfile:
	name: str
	email_address: str
	username: str
	password: str
	imap_host: str
	smtp_host: str
	allowed_from: List[str]
	projects_root: Path
	model: str
	reasoning_effort: str
	folder: str = "INBOX"
	poll_seconds: int = 30
	imap_port: int = 993
	smtp_port: int = 465
	imap_ssl: bool = True
	smtp_ssl: bool = True
	concurrency: int = 2


@dataclass
class IncomingMessage:
	uid: str
	message_id: str
	thread_key: str
	subject: str
	sender_name: str
	sender_email: str
	reply_to: str
	body_text: str
	references: List[str] = field(default_factory=list)
	in_reply_to: Optional[str] = None


@dataclass
class ThreadState:
	id: int
	mailbox: str
	thread_key: str
	subject: str
	sender_email: str
	workspace: Path
	model: str
	reasoning_effort: str
	previous_response_id: Optional[str]
	plan_json: str


@dataclass
class ApprovalRecord:
	token: str
	thread_id: int
	command: List[str]
	workdir: str


@dataclass
class AgentResult:
	reply_text: str
	response_id: Optional[str]
	plan_json: str


def as_jsonable(value: Any) -> Any:
	if isinstance(value, Path):
		return str(value)
	return value
