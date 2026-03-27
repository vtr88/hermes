from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from hermes.config import load_app_config, load_mailbox_profiles


class ConfigTests(unittest.TestCase):
	def test_load_profiles(self) -> None:
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			config_dir = root / "mailboxes.d"
			config_dir.mkdir()
			(config_dir / "alice.toml").write_text(
				"""
name = "alice"
email_address = "alice@example.com"
username = "alice@example.com"
password = "pw"
imap_host = "imap.example.com"
smtp_host = "smtp.example.com"
projects_root = "./projects/alice"
allowed_from = ["you@example.com"]
""".strip(),
				encoding="utf-8",
			)
			os.environ["OPENAI_API_KEY"] = "test-key"
			os.environ["HERMES_CONFIG_DIR"] = str(config_dir)
			os.environ["HERMES_STATE_DIR"] = str(root / "var")
			app = load_app_config()
			profiles = load_mailbox_profiles(app)
			self.assertEqual(1, len(profiles))
			self.assertEqual("alice", profiles[0].name)
			self.assertEqual("gpt-5.4", profiles[0].model)


if __name__ == "__main__":
	unittest.main()
