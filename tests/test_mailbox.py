from __future__ import annotations

import unittest

from hermes.mailbox import build_thread_key, extract_approval_token, parse_incoming


RAW_MESSAGE = b"""From: Alice <alice@example.com>\r
To: agent@example.com\r
Message-ID: <msg-1@example.com>\r
Subject: Start Project\r
Content-Type: text/plain; charset=utf-8\r
\r
Build a tiny HTTP server.\r
"""


class MailboxTests(unittest.TestCase):
	def test_parse_incoming(self) -> None:
		msg = parse_incoming("42", RAW_MESSAGE)
		self.assertEqual("<msg-1@example.com>", msg.message_id)
		self.assertEqual("alice@example.com", msg.sender_email)
		self.assertIn("HTTP server", msg.body_text)

	def test_build_thread_key_prefers_references(self) -> None:
		key = build_thread_key("<new>", ["<root>"], None, "Hello")
		self.assertEqual("<root>", key)

	def test_extract_approval(self) -> None:
		token = extract_approval_token("please /approve abc-123 now")
		self.assertEqual("abc-123", token)


if __name__ == "__main__":
	unittest.main()
