from __future__ import annotations

import logging
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from .agent import OpenAICodeAgent
from .mailbox import MailboxClient, extract_approval_token
from .models import AppConfig, IncomingMessage, MailboxProfile, ThreadState
from .storage import Storage

LOG = logging.getLogger(__name__)


class HermesService:
	def __init__(self, app: AppConfig, profiles: list[MailboxProfile]) -> None:
		self.app = app
		self.profiles = profiles
		self.storage = Storage(app.state_dir / "hermes.db")
		self.agent = OpenAICodeAgent(self.storage, api_key=app.openai_api_key, max_tool_rounds=app.max_tool_rounds)
		self.stop_event = threading.Event()
		self.executor = ThreadPoolExecutor(max_workers=app.max_workers)
		self.thread_locks = {}
		self.thread_locks_guard = threading.Lock()

	def serve_forever(self) -> None:
		threads = [
			threading.Thread(target=self._mailbox_loop, name=f"mailbox-{profile.name}", args=(profile,), daemon=True)
			for profile in self.profiles
		]
		for thread in threads:
			thread.start()
		try:
			while not self.stop_event.is_set():
				time.sleep(0.5)
		except KeyboardInterrupt:
			LOG.info("interrupt received, stopping")
			self.stop_event.set()
		finally:
			self.executor.shutdown(wait=True, cancel_futures=False)

	def _mailbox_loop(self, profile: MailboxProfile) -> None:
		client = MailboxClient(profile)
		while not self.stop_event.is_set():
			try:
				for message in client.fetch_unseen():
					self.executor.submit(self._process_message, profile, message)
			except Exception as exc:
				LOG.exception("mailbox poll failed for %s: %s", profile.name, exc)
			self.stop_event.wait(profile.poll_seconds)

	def _process_message(self, profile: MailboxProfile, message: IncomingMessage) -> None:
		lock = self._thread_lock(profile.name, message.thread_key)
		with lock:
			if self.storage.seen_message(profile.name, message.message_id):
				return
			if profile.allowed_from and message.sender_email.lower() not in profile.allowed_from:
				LOG.warning("skip sender %s for mailbox %s", message.sender_email, profile.name)
				return
			thread, created = self.storage.get_or_create_thread(profile, message)
			if created:
				self.storage.ensure_workspace(thread)
			approval = None
			token = extract_approval_token(f"{message.subject}\n{message.body_text}")
			if token:
				approval = self.storage.consume_approval(profile.name, thread.thread_key, token)
			user_text = self._render_user_text(thread, message, approval)
			result = self.agent.run_turn(profile, thread, user_text, approval=approval)
			self.storage.update_thread(thread.id, result.response_id, result.plan_json)
			self.storage.log_messages(
				thread.id,
				[
					("incoming", message.body_text),
					("outgoing", result.reply_text),
				],
			)
			client = MailboxClient(profile)
			client.send_reply(message, result.reply_text)
			client.mark_seen(message.uid)
			self.storage.mark_processed(profile.name, message)

	def _render_user_text(
		self,
		thread: ThreadState,
		message: IncomingMessage,
		approval,
	) -> str:
		lines = [
			f"From: {message.sender_email}",
			f"Subject: {message.subject}",
			f"Workspace: {thread.workspace}",
			"",
			"Email body:",
			message.body_text.strip() or "(empty message body)",
		]
		if approval is not None:
			lines.extend(
				[
					"",
					"The user approved a previously blocked command.",
					f"Approved command: {' '.join(approval.command)}",
					f"Approved workdir: {approval.workdir}",
				]
			)
		return "\n".join(lines)

	def _thread_lock(self, mailbox: str, thread_key: str) -> threading.Lock:
		key = (mailbox, thread_key)
		with self.thread_locks_guard:
			lock = self.thread_locks.get(key)
			if lock is None:
				lock = threading.Lock()
				self.thread_locks[key] = lock
			return lock


def ensure_runtime_dirs(app: AppConfig, profiles: list[MailboxProfile]) -> None:
	app.state_dir.mkdir(parents=True, exist_ok=True)
	for profile in profiles:
		profile.projects_root.mkdir(parents=True, exist_ok=True)
		(profile.projects_root / ".keep").touch(exist_ok=True)


def setup_logging() -> None:
	logging.basicConfig(
		level=logging.INFO,
		format="%(asctime)s %(levelname)s %(name)s: %(message)s",
	)
