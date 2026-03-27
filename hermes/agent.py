from __future__ import annotations

import json
from typing import Any

from .models import AgentResult, ApprovalRecord, MailboxProfile, ThreadState
from .storage import Storage
from .tools import ToolCall, Toolbox


class OpenAICodeAgent:
	def __init__(self, storage: Storage, api_key: str, max_tool_rounds: int) -> None:
		try:
			from openai import OpenAI
		except ImportError as exc:
			raise RuntimeError("openai package is required; install with `pip install -e .`") from exc
		self.client = OpenAI(api_key=api_key)
		self.storage = storage
		self.max_tool_rounds = max_tool_rounds

	def run_turn(
		self,
		profile: MailboxProfile,
		thread: ThreadState,
		user_text: str,
		approval: ApprovalRecord | None = None,
	) -> AgentResult:
		toolbox = Toolbox(self.storage, thread, approval=approval)
		instructions = _build_instructions(profile, thread)
		response = self.client.responses.create(
			model=thread.model,
			reasoning={"effort": thread.reasoning_effort},
			instructions=instructions,
			input=user_text,
			tools=toolbox.specs,
			previous_response_id=thread.previous_response_id,
		)
		for _ in range(self.max_tool_rounds):
			calls = _extract_calls(response)
			if not calls:
				break
			outputs = []
			for call in calls:
				result = toolbox.execute(call)
				outputs.append(
					{
						"type": "function_call_output",
						"call_id": call.call_id,
						"output": json.dumps(result),
					}
				)
			response = self.client.responses.create(
				model=thread.model,
				reasoning={"effort": thread.reasoning_effort},
				instructions=instructions,
				input=outputs,
				tools=toolbox.specs,
				previous_response_id=response.id,
			)
		reply = getattr(response, "output_text", None) or _fallback_text(response)
		return AgentResult(
			reply_text=reply.strip() or "I finished the turn but produced no plain-text reply.",
			response_id=getattr(response, "id", None),
			plan_json=toolbox.plan_json,
		)


def _build_instructions(profile: MailboxProfile, thread: ThreadState) -> str:
	return (
		"You are Hermes, a Codex-style coding agent replying through email.\n"
		"Be warm, concise, and action-oriented.\n"
		"Read code before editing it.\n"
		"Use update_plan for substantial work.\n"
		"Prefer apply_patch or write_file for edits.\n"
		"Run validation before claiming success when practical.\n"
		"If a tool returns status `needs_approval`, stop using tools and ask the user to reply with "
		"`/approve TOKEN` in the same email thread.\n"
		"Never access paths outside the workspace.\n"
		"Workspace: "
		f"{thread.workspace}\n"
		"Mailbox: "
		f"{profile.name}\n"
		"Current subject: "
		f"{thread.subject}\n"
	)


def _extract_calls(response: Any) -> list[ToolCall]:
	calls: list[ToolCall] = []
	for item in getattr(response, "output", []) or []:
		item_type = _get(item, "type")
		if item_type not in {"function_call", "tool_call"}:
			continue
		name = _get(item, "name") or _get(item, "tool_name")
		call_id = _get(item, "call_id")
		arguments = _get(item, "arguments")
		if not name or not call_id:
			continue
		if isinstance(arguments, str):
			try:
				parsed = json.loads(arguments)
			except json.JSONDecodeError:
				parsed = {}
		elif isinstance(arguments, dict):
			parsed = arguments
		else:
			parsed = {}
		calls.append(ToolCall(call_id=call_id, name=name, arguments=parsed))
	return calls


def _fallback_text(response: Any) -> str:
	parts: list[str] = []
	for item in getattr(response, "output", []) or []:
		if _get(item, "type") == "message":
			for content in _get(item, "content", []) or []:
				text = _get(content, "text")
				if text:
					parts.append(text)
	return "\n".join(parts)


def _get(item: Any, key: str) -> Any:
	if isinstance(item, dict):
		return item.get(key)
	return getattr(item, key, None)
