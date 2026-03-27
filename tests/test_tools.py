from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hermes.models import ThreadState
from hermes.storage import Storage
from hermes.tools import ToolCall, Toolbox


class ToolTests(unittest.TestCase):
	def test_write_read_and_plan(self) -> None:
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			storage = Storage(root / "state.db")
			workspace = root / "workspace"
			workspace.mkdir()
			thread = ThreadState(
				id=1,
				mailbox="alice",
				thread_key="<root>",
				subject="Subject",
				sender_email="you@example.com",
				workspace=workspace,
				model="gpt-5.4",
				reasoning_effort="medium",
				previous_response_id=None,
				plan_json="[]",
			)
			toolbox = Toolbox(storage, thread)
			toolbox.execute(ToolCall("1", "write_file", {"path": "README.md", "content": "hello"}))
			result = toolbox.execute(ToolCall("2", "read_file", {"path": "README.md"}))
			self.assertEqual("ok", result["status"])
			self.assertIn("hello", result["content"])
			plan = toolbox.execute(
				ToolCall(
					"3",
					"update_plan",
					{"items": [{"step": "Read repo", "status": "completed"}]},
				)
			)
			self.assertEqual("ok", plan["status"])

	def test_run_command_requires_approval_for_git_push(self) -> None:
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			storage = Storage(root / "state.db")
			workspace = root / "workspace"
			workspace.mkdir()
			thread = ThreadState(
				id=1,
				mailbox="alice",
				thread_key="<root>",
				subject="Subject",
				sender_email="you@example.com",
				workspace=workspace,
				model="gpt-5.4",
				reasoning_effort="medium",
				previous_response_id=None,
				plan_json="[]",
			)
			toolbox = Toolbox(storage, thread)
			result = toolbox.execute(ToolCall("1", "run_command", {"command": ["git", "push"]}))
			self.assertEqual("needs_approval", result["status"])


if __name__ == "__main__":
	unittest.main()
