from __future__ import annotations

import imaplib
import re
import smtplib
from email import policy
from email.message import EmailMessage
from email.parser import BytesParser
from email.utils import getaddresses, make_msgid, parseaddr
from html.parser import HTMLParser
from typing import Iterable, List, Optional

from .models import IncomingMessage, MailboxProfile

MESSAGE_ID_RE = re.compile(r"<[^>]+>")
APPROVAL_RE = re.compile(r"(?:^|\s)(?:/approve|approve)\s+([A-Za-z0-9_\-]+)", re.IGNORECASE)


class _HTMLToText(HTMLParser):
	def __init__(self) -> None:
		super().__init__()
		self.parts: list[str] = []

	def handle_data(self, data: str) -> None:
		if data:
			self.parts.append(data)

	def text(self) -> str:
		return " ".join(part.strip() for part in self.parts if part.strip())


def extract_approval_token(text: str) -> Optional[str]:
	match = APPROVAL_RE.search(text)
	if not match:
		return None
	return match.group(1)


def extract_message_ids(raw: Optional[str]) -> List[str]:
	if not raw:
		return []
	return MESSAGE_ID_RE.findall(raw)


def normalize_subject(subject: Optional[str]) -> str:
	text = (subject or "").strip()
	return text or "New project"


def build_thread_key(
	message_id: str,
	references: Iterable[str],
	in_reply_to: Optional[str],
	subject: str,
) -> str:
	refs = list(references)
	if refs:
		return refs[0]
	if in_reply_to:
		return in_reply_to
	if message_id:
		return message_id
	return normalize_subject(subject).lower()


def html_to_text(html: str) -> str:
	parser = _HTMLToText()
	parser.feed(html)
	return parser.text()


def body_from_email(message: EmailMessage) -> str:
	if message.is_multipart():
		plain = message.get_body(preferencelist=("plain",))
		if plain is not None:
			return (plain.get_content() or "").strip()
		html = message.get_body(preferencelist=("html",))
		if html is not None:
			return html_to_text(html.get_content() or "").strip()
		parts = []
		for part in message.walk():
			if part.get_content_maintype() == "multipart":
				continue
			if part.get_content_disposition() == "attachment":
				continue
			content = part.get_content()
			if isinstance(content, str) and content.strip():
				parts.append(content.strip())
		return "\n\n".join(parts).strip()
	content = message.get_content()
	if message.get_content_type() == "text/html":
		return html_to_text(content)
	return str(content).strip()


def parse_incoming(uid: str, raw_message: bytes) -> IncomingMessage:
	message = BytesParser(policy=policy.default).parsebytes(raw_message)
	from_name, from_email = parseaddr(message.get("From", ""))
	reply_tos = getaddresses(message.get_all("Reply-To", []))
	reply_to = reply_tos[0][1] if reply_tos else from_email
	msgid = extract_message_ids(message.get("Message-ID")) or [message.get("Message-ID", "").strip()]
	references = extract_message_ids(message.get("References"))
	in_reply = extract_message_ids(message.get("In-Reply-To"))
	message_id = msgid[0] if msgid and msgid[0] else f"<fallback-{uid}>"
	in_reply_to = in_reply[0] if in_reply else None
	subject = normalize_subject(message.get("Subject"))
	return IncomingMessage(
		uid=uid,
		message_id=message_id,
		thread_key=build_thread_key(message_id, references, in_reply_to, subject),
		subject=subject,
		sender_name=from_name,
		sender_email=from_email.lower(),
		reply_to=reply_to or from_email,
		body_text=body_from_email(message),
		references=references,
		in_reply_to=in_reply_to,
	)


class MailboxClient:
	def __init__(self, profile: MailboxProfile) -> None:
		self.profile = profile

	def fetch_unseen(self) -> list[IncomingMessage]:
		conn = self._imap()
		try:
			status, _ = conn.select(self.profile.folder)
			if status != "OK":
				return []
			status, data = conn.uid("search", None, "UNSEEN")
			if status != "OK" or not data or not data[0]:
				return []
			messages: list[IncomingMessage] = []
			for raw_uid in data[0].decode().split():
				status, fetched = conn.uid("fetch", raw_uid, "(BODY.PEEK[])")
				if status != "OK" or not fetched:
					continue
				payload = b""
				for item in fetched:
					if isinstance(item, tuple):
						payload = item[1]
						break
				if payload:
					messages.append(parse_incoming(raw_uid, payload))
			return messages
		finally:
			with contextlib.suppress(Exception):
				conn.close()
			with contextlib.suppress(Exception):
				conn.logout()

	def mark_seen(self, uid: str) -> None:
		conn = self._imap()
		try:
			conn.select(self.profile.folder)
			conn.uid("store", uid, "+FLAGS", "(\\Seen)")
		finally:
			with contextlib.suppress(Exception):
				conn.close()
			with contextlib.suppress(Exception):
				conn.logout()

	def send_reply(self, original: IncomingMessage, body: str) -> None:
		msg = EmailMessage()
		msg["From"] = self.profile.email_address
		msg["To"] = original.reply_to or original.sender_email
		msg["Subject"] = _reply_subject(original.subject)
		msg["Message-ID"] = make_msgid(domain=self.profile.email_address.split("@")[-1])
		msg["In-Reply-To"] = original.message_id
		refs = list(original.references)
		if original.message_id not in refs:
			refs.append(original.message_id)
		if refs:
			msg["References"] = " ".join(refs[-10:])
		msg.set_content(body)
		if self.profile.smtp_ssl:
			smtp = smtplib.SMTP_SSL(self.profile.smtp_host, self.profile.smtp_port, timeout=60)
		else:
			smtp = smtplib.SMTP(self.profile.smtp_host, self.profile.smtp_port, timeout=60)
		with smtp:
			smtp.login(self.profile.username, self.profile.password)
			smtp.send_message(msg)

	def _imap(self) -> imaplib.IMAP4:
		if self.profile.imap_ssl:
			conn: imaplib.IMAP4 = imaplib.IMAP4_SSL(self.profile.imap_host, self.profile.imap_port)
		else:
			conn = imaplib.IMAP4(self.profile.imap_host, self.profile.imap_port)
		conn.login(self.profile.username, self.profile.password)
		return conn


def _reply_subject(subject: str) -> str:
	if subject.lower().startswith("re:"):
		return subject
	return f"Re: {subject}"


import contextlib
