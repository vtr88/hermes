from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hermes.models import IncomingMessage, MailboxProfile
from hermes.storage import Storage


class StorageTests(unittest.TestCase):
	def test_thread_and_approval_lifecycle(self) -> None:
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			storage = Storage(root / "state.db")
			profile = MailboxProfile(
				name="alice",
				email_address="alice@example.com",
				username="alice@example.com",
				password="pw",
				imap_host="imap.example.com",
				smtp_host="smtp.example.com",
				allowed_from=["you@example.com"],
				projects_root=root / "projects",
				model="gpt-5.4",
				reasoning_effort="medium",
			)
			message = IncomingMessage(
				uid="1",
				message_id="<msg-1>",
				thread_key="<msg-1>",
				subject="New Project",
				sender_name="You",
				sender_email="you@example.com",
				reply_to="you@example.com",
				body_text="hello",
			)
			thread, created = storage.get_or_create_thread(profile, message)
			self.assertTrue(created)
			storage.ensure_workspace(thread)
			self.assertTrue(thread.workspace.exists())
			token = storage.create_approval(thread.id, ["python3", "-m", "unittest"], ".")
			approval = storage.consume_approval("alice", "<msg-1>", token)
			self.assertIsNotNone(approval)
			self.assertEqual(["python3", "-m", "unittest"], approval.command)


if __name__ == "__main__":
	unittest.main()
