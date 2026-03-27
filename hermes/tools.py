from __future__ import annotations

import json
import os
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .models import ApprovalRecord, ThreadState
from .storage import Storage


SAFE_TOP_LEVEL = {
	"ls",
	"pwd",
	"cat",
	"sed",
	"find",
	"rg",
	"grep",
	"git",
	"python",
	"python3",
	"pytest",
	"make",
	"npm",
	"pnpm",
	"yarn",
	"uv",
	"cargo",
	"go",
	"node",
}

BANNED_TOP_LEVEL = {
	"sudo",
	"su",
	"doas",
	"rm",
	"dd",
	"mkfs",
	"mount",
	"umount",
	"shutdown",
	"reboot",
	"systemctl",
	"service",
	"launchctl",
	"chown",
}


@dataclass(slots=True)
class ToolCall:
	call_id: str
	name: str
	arguments: dict[str, Any]


class Toolbox:
	def __init__(
		self,
		storage: Storage,
		thread: ThreadState,
		approval: ApprovalRecord | None = None,
	) -> None:
		self.storage = storage
		self.thread = thread
		self.plan_json = thread.plan_json
		self.approval = approval

	@property
	def specs(self) -> list[dict[str, Any]]:
		return [
			_function(
				"update_plan",
				"Track a short task plan for the current thread.",
				{
					"type": "object",
					"properties": {
						"explanation": {"type": "string"},
						"items": {
							"type": "array",
							"items": {
								"type": "object",
								"properties": {
									"step": {"type": "string"},
									"status": {
										"type": "string",
										"enum": ["pending", "in_progress", "completed"],
									},
								},
								"required": ["step", "status"],
							},
						},
					},
					"required": ["items"],
				},
			),
			_function(
				"list_files",
				"List files relative to the workspace.",
				{
					"type": "object",
					"properties": {
						"path": {"type": "string"},
						"recursive": {"type": "boolean"},
						"limit": {"type": "integer"},
					},
				},
			),
			_function(
				"read_file",
				"Read a text file relative to the workspace.",
				{
					"type": "object",
					"properties": {
						"path": {"type": "string"},
						"start_line": {"type": "integer"},
						"max_lines": {"type": "integer"},
					},
					"required": ["path"],
				},
			),
			_function(
				"search_text",
				"Search for text inside workspace files.",
				{
					"type": "object",
					"properties": {
						"pattern": {"type": "string"},
						"path": {"type": "string"},
					},
					"required": ["pattern"],
				},
			),
			_function(
				"write_file",
				"Write a full file relative to the workspace.",
				{
					"type": "object",
					"properties": {
						"path": {"type": "string"},
						"content": {"type": "string"},
					},
					"required": ["path", "content"],
				},
			),
			_function(
				"apply_patch",
				"Apply a unified diff patch inside the workspace.",
				{
					"type": "object",
					"properties": {
						"patch": {"type": "string"},
					},
					"required": ["patch"],
				},
			),
			_function(
				"run_command",
				"Run a local command in the workspace. Risky commands require approval.",
				{
					"type": "object",
					"properties": {
						"command": {
							"oneOf": [
								{"type": "string"},
								{"type": "array", "items": {"type": "string"}},
							]
						},
						"workdir": {"type": "string"},
						"timeout_sec": {"type": "integer"},
					},
					"required": ["command"],
				},
			),
		]

	def execute(self, call: ToolCall) -> dict[str, Any]:
		handler = getattr(self, f"_tool_{call.name}", None)
		if handler is None:
			return {"status": "error", "message": f"unknown tool: {call.name}"}
		return handler(**call.arguments)

	def _tool_update_plan(self, items: list[dict[str, str]], explanation: str = "") -> dict[str, Any]:
		self.plan_json = json.dumps(items)
		return {"status": "ok", "explanation": explanation, "items": items}

	def _tool_list_files(self, path: str = ".", recursive: bool = False, limit: int = 200) -> dict[str, Any]:
		root = self._resolve(path)
		if not root.exists():
			return {"status": "error", "message": f"path does not exist: {path}"}
		items = []
		if recursive:
			for child in sorted(root.rglob("*")):
				if len(items) >= limit:
					break
				items.append(self._rel(child))
		else:
			for child in sorted(root.iterdir()):
				if len(items) >= limit:
					break
				items.append(self._rel(child))
		return {"status": "ok", "items": items}

	def _tool_read_file(self, path: str, start_line: int = 1, max_lines: int = 200) -> dict[str, Any]:
		target = self._resolve(path)
		if not target.is_file():
			return {"status": "error", "message": f"not a file: {path}"}
		lines = target.read_text(encoding="utf-8").splitlines()
		start = max(1, start_line)
		end = start + max(1, max_lines) - 1
		selected = lines[start - 1 : end]
		return {
			"status": "ok",
			"path": self._rel(target),
			"start_line": start,
			"content": "\n".join(selected),
		}

	def _tool_search_text(self, pattern: str, path: str = ".") -> dict[str, Any]:
		root = self._resolve(path)
		results: list[str] = []
		for candidate in sorted(root.rglob("*")):
			if not candidate.is_file() or ".git" in candidate.parts:
				continue
			try:
				for lineno, line in enumerate(candidate.read_text(encoding="utf-8").splitlines(), start=1):
					if pattern in line:
						results.append(f"{self._rel(candidate)}:{lineno}:{line}")
					if len(results) >= 200:
						return {"status": "ok", "matches": results}
			except UnicodeDecodeError:
				continue
		return {"status": "ok", "matches": results}

	def _tool_write_file(self, path: str, content: str) -> dict[str, Any]:
		target = self._resolve(path)
		target.parent.mkdir(parents=True, exist_ok=True)
		target.write_text(content, encoding="utf-8")
		return {"status": "ok", "path": self._rel(target), "bytes": len(content.encode("utf-8"))}

	def _tool_apply_patch(self, patch: str) -> dict[str, Any]:
		try:
			check = subprocess.run(
				["git", "-C", str(self.thread.workspace), "apply", "--check", "--whitespace=nowarn", "-"],
				input=patch,
				capture_output=True,
				text=True,
				check=False,
			)
			if check.returncode != 0:
				return {
					"status": "error",
					"message": "patch check failed",
					"stderr": check.stderr.strip(),
				}
			apply = subprocess.run(
				["git", "-C", str(self.thread.workspace), "apply", "--whitespace=nowarn", "-"],
				input=patch,
				capture_output=True,
				text=True,
				check=False,
			)
		except FileNotFoundError:
			return {"status": "error", "message": "git is not available"}
		if apply.returncode != 0:
			return {"status": "error", "message": "patch apply failed", "stderr": apply.stderr.strip()}
		return {"status": "ok"}

	def _tool_run_command(
		self,
		command: str | list[str],
		workdir: str = ".",
		timeout_sec: int = 600,
	) -> dict[str, Any]:
		argv = shlex.split(command) if isinstance(command, str) else list(command)
		if not argv:
			return {"status": "error", "message": "empty command"}
		cwd = self._resolve(workdir)
		if not cwd.exists():
			return {"status": "error", "message": f"workdir does not exist: {workdir}"}
		if self._needs_approval(argv, cwd):
			token = self.storage.create_approval(self.thread.id, argv, self._rel(cwd))
			return {
				"status": "needs_approval",
				"approval_token": token,
				"message": "command requires approval; ask the user to reply with /approve TOKEN",
				"command": argv,
				"workdir": self._rel(cwd),
			}
		env = os.environ.copy()
		env["CI"] = "1"
		proc = subprocess.run(
			argv,
			cwd=cwd,
			env=env,
			capture_output=True,
			text=True,
			timeout=max(1, timeout_sec),
			check=False,
		)
		return {
			"status": "ok",
			"command": argv,
			"workdir": self._rel(cwd),
			"exit_code": proc.returncode,
			"stdout": proc.stdout[-12000:],
			"stderr": proc.stderr[-12000:],
		}

	def _needs_approval(self, argv: list[str], cwd: Path) -> bool:
		top = argv[0]
		if self.approval and argv == self.approval.command and self._rel(cwd) == self.approval.workdir:
			return False
		if top in BANNED_TOP_LEVEL:
			return True
		if top not in SAFE_TOP_LEVEL:
			return True
		if top == "git" and len(argv) > 1 and argv[1] in {"reset", "clean", "push", "fetch", "pull", "clone"}:
			return True
		if top in {"npm", "pnpm", "yarn", "pip", "python", "python3", "uv", "cargo", "go"} and any(
			part in {"install", "add", "remove", "sync"} for part in argv[1:]
		):
			return True
		return False

	def _resolve(self, path: str) -> Path:
		candidate = (self.thread.workspace / path).resolve()
		workspace = self.thread.workspace.resolve()
		if not str(candidate).startswith(str(workspace)):
			raise ValueError(f"path escapes workspace: {path}")
		return candidate

	def _rel(self, path: Path) -> str:
		return str(path.resolve().relative_to(self.thread.workspace.resolve())) or "."


def _function(name: str, description: str, parameters: dict[str, Any]) -> dict[str, Any]:
	return {
		"type": "function",
		"name": name,
		"description": description,
		"parameters": parameters,
	}
