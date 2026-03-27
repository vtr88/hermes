from __future__ import annotations

import json
import secrets
import sqlite3
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Iterable

from .models import ApprovalRecord, IncomingMessage, MailboxProfile, ThreadState


def _utc_now() -> str:
	return datetime.now(tz=UTC).isoformat()


def _slugify(text: str) -> str:
	out = []
	for ch in text.lower():
		if ch.isalnum():
			out.append(ch)
		elif ch in {" ", "-", "_"}:
			out.append("-")
	slug = "".join(out).strip("-")
	return slug or "session"


class Storage:
	def __init__(self, db_path: Path) -> None:
		self.db_path = db_path
		self.db_path.parent.mkdir(parents=True, exist_ok=True)
		self._init_db()

	def _connect(self) -> sqlite3.Connection:
		conn = sqlite3.connect(self.db_path, timeout=30, check_same_thread=False)
		conn.row_factory = sqlite3.Row
		return conn

	def _init_db(self) -> None:
		with self._connect() as conn:
			conn.executescript(
				"""
				CREATE TABLE IF NOT EXISTS threads(
					id INTEGER PRIMARY KEY AUTOINCREMENT,
					mailbox TEXT NOT NULL,
					thread_key TEXT NOT NULL,
					subject TEXT NOT NULL,
					sender_email TEXT NOT NULL,
					workspace TEXT NOT NULL,
					model TEXT NOT NULL,
					reasoning_effort TEXT NOT NULL,
					previous_response_id TEXT,
					plan_json TEXT NOT NULL DEFAULT '[]',
					created_at TEXT NOT NULL,
					updated_at TEXT NOT NULL,
					UNIQUE(mailbox, thread_key)
				);

				CREATE TABLE IF NOT EXISTS processed_messages(
					mailbox TEXT NOT NULL,
					message_id TEXT NOT NULL,
					thread_key TEXT NOT NULL,
					created_at TEXT NOT NULL,
					PRIMARY KEY(mailbox, message_id)
				);

				CREATE TABLE IF NOT EXISTS approvals(
					token TEXT PRIMARY KEY,
					thread_id INTEGER NOT NULL,
					command_json TEXT NOT NULL,
					workdir TEXT NOT NULL,
					status TEXT NOT NULL,
					created_at TEXT NOT NULL
				);

				CREATE TABLE IF NOT EXISTS message_log(
					id INTEGER PRIMARY KEY AUTOINCREMENT,
					thread_id INTEGER NOT NULL,
					direction TEXT NOT NULL,
					body TEXT NOT NULL,
					created_at TEXT NOT NULL
				);
				"""
			)

	def seen_message(self, mailbox: str, message_id: str) -> bool:
		with self._connect() as conn:
			row = conn.execute(
				"SELECT 1 FROM processed_messages WHERE mailbox = ? AND message_id = ?",
				(mailbox, message_id),
			).fetchone()
		return row is not None

	def mark_processed(self, mailbox: str, message: IncomingMessage) -> None:
		with self._connect() as conn:
			conn.execute(
				"""
				INSERT OR IGNORE INTO processed_messages(mailbox, message_id, thread_key, created_at)
				VALUES(?, ?, ?, ?)
				""",
				(mailbox, message.message_id, message.thread_key, _utc_now()),
			)

	def get_or_create_thread(
		self,
		profile: MailboxProfile,
		message: IncomingMessage,
	) -> tuple[ThreadState, bool]:
		with self._connect() as conn:
			row = conn.execute(
				"SELECT * FROM threads WHERE mailbox = ? AND thread_key = ?",
				(profile.name, message.thread_key),
			).fetchone()
			if row:
				return self._row_to_thread(row), False
			workspace = self._new_workspace_path(profile, message.subject)
			now = _utc_now()
			conn.execute(
				"""
				INSERT INTO threads(
					mailbox, thread_key, subject, sender_email, workspace, model,
					reasoning_effort, previous_response_id, plan_json, created_at, updated_at
				)
				VALUES(?, ?, ?, ?, ?, ?, ?, NULL, '[]', ?, ?)
				""",
				(
					profile.name,
					message.thread_key,
					message.subject,
					message.sender_email,
					str(workspace),
					profile.model,
					profile.reasoning_effort,
					now,
					now,
				),
			)
			row = conn.execute(
				"SELECT * FROM threads WHERE mailbox = ? AND thread_key = ?",
				(profile.name, message.thread_key),
			).fetchone()
			if row is None:
				raise RuntimeError("failed to create thread")
			return self._row_to_thread(row), True

	def ensure_workspace(self, thread: ThreadState) -> None:
		thread.workspace.mkdir(parents=True, exist_ok=True)
		if (thread.workspace / ".git").exists():
			return
		subprocess.run(
			["git", "init", "-q", str(thread.workspace)],
			check=False,
			capture_output=True,
			text=True,
		)

	def update_thread(self, thread_id: int, response_id: str | None, plan_json: str) -> None:
		with self._connect() as conn:
			conn.execute(
				"""
				UPDATE threads
				SET previous_response_id = ?, plan_json = ?, updated_at = ?
				WHERE id = ?
				""",
				(response_id, plan_json, _utc_now(), thread_id),
			)

	def log_messages(self, thread_id: int, entries: Iterable[tuple[str, str]]) -> None:
		with self._connect() as conn:
			conn.executemany(
				"INSERT INTO message_log(thread_id, direction, body, created_at) VALUES(?, ?, ?, ?)",
				[(thread_id, direction, body, _utc_now()) for direction, body in entries],
			)

	def create_approval(self, thread_id: int, command: list[str], workdir: str) -> str:
		token = secrets.token_urlsafe(9)
		with self._connect() as conn:
			conn.execute(
				"""
				INSERT INTO approvals(token, thread_id, command_json, workdir, status, created_at)
				VALUES(?, ?, ?, ?, 'pending', ?)
				""",
				(token, thread_id, json.dumps(command), workdir, _utc_now()),
			)
		return token

	def consume_approval(self, mailbox: str, thread_key: str, token: str) -> ApprovalRecord | None:
		with self._connect() as conn:
			row = conn.execute(
				"""
				SELECT a.token, a.thread_id, a.command_json, a.workdir
				FROM approvals AS a
				JOIN threads AS t ON t.id = a.thread_id
				WHERE a.token = ? AND a.status = 'pending' AND t.mailbox = ? AND t.thread_key = ?
				""",
				(token, mailbox, thread_key),
			).fetchone()
			if row is None:
				return None
			conn.execute("UPDATE approvals SET status = 'used' WHERE token = ?", (token,))
		return ApprovalRecord(
			token=row["token"],
			thread_id=row["thread_id"],
			command=json.loads(row["command_json"]),
			workdir=row["workdir"],
		)

	def _new_workspace_path(self, profile: MailboxProfile, subject: str) -> Path:
		base = profile.projects_root.expanduser()
		base.mkdir(parents=True, exist_ok=True)
		name = f"{datetime.now(tz=UTC).strftime('%Y%m%d-%H%M%S')}-{_slugify(subject)}"
		path = base / name
		counter = 1
		while path.exists():
			counter += 1
			path = base / f"{name}-{counter}"
		return path

	@staticmethod
	def _row_to_thread(row: sqlite3.Row) -> ThreadState:
		return ThreadState(
			id=int(row["id"]),
			mailbox=str(row["mailbox"]),
			thread_key=str(row["thread_key"]),
			subject=str(row["subject"]),
			sender_email=str(row["sender_email"]),
			workspace=Path(str(row["workspace"])),
			model=str(row["model"]),
			reasoning_effort=str(row["reasoning_effort"]),
			previous_response_id=row["previous_response_id"],
			plan_json=str(row["plan_json"]),
		)
