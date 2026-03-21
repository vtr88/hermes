#define _POSIX_C_SOURCE 200809L

#include "hermes.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_10ms(void)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 10000000L;
	nanosleep(&ts, NULL);
}

static char *xstrdup(const char *s)
{
	char *p;
	size_t n;

	if (!s)
		return NULL;
	n = strlen(s) + 1;
	p = malloc(n);
	if (!p)
		return NULL;
	memcpy(p, s, n);
	return p;
}

static int append_text(char **acc, const char *text)
{
	char *next;
	size_t a;
	size_t n;

	if (!acc || !text)
		return -1;
	a = *acc ? strlen(*acc) : 0;
	n = strlen(text);
	next = realloc(*acc, a + n + 1);
	if (!next)
		return -1;
	*acc = next;
	memcpy(*acc + a, text, n);
	(*acc)[a + n] = '\0';
	return 0;
}

static char *trimdup(const char *s)
{
	char *p;
	char *e;

	p = xstrdup(s ? s : "");
	if (!p)
		return NULL;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		memmove(p, p + 1, strlen(p));
	e = p + strlen(p);
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return p;
}

static int starts_with(const char *s, const char *prefix)
{
	if (!s || !prefix)
		return 0;
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char *extract_approval_token(const char *line, const char *body)
{
	const char *p;

	if (line && starts_with(line, "/approve "))
		return trimdup(line + 9);
	if (line && starts_with(line, "approve "))
		return trimdup(line + 8);

	if (body) {
		p = strstr(body, "/approve ");
		if (p)
			return trimdup(p + 9);
		p = strstr(body, "approve ");
		if (p)
			return trimdup(p + 8);
	}
	return NULL;
}

static int contains_ci(const char *haystack, const char *needle)
{
	char *h;
	char *n;
	char *p;
	int ok;
	size_t i;

	if (!haystack || !needle)
		return 0;
	h = xstrdup(haystack);
	n = xstrdup(needle);
	if (!h || !n) {
		free(h);
		free(n);
		return 0;
	}
	for (i = 0; h[i]; i++)
		if (h[i] >= 'A' && h[i] <= 'Z')
			h[i] = (char)(h[i] - 'A' + 'a');
	for (i = 0; n[i]; i++)
		if (n[i] >= 'A' && n[i] <= 'Z')
			n[i] = (char)(n[i] - 'A' + 'a');
	p = strstr(h, n);
	ok = p != NULL;
	free(h);
	free(n);
	return ok;
}

static int requires_approval_command(const char *cmd)
{
	(void)cmd;
	return 0;
}

static char *token_generate(void)
{
	unsigned char b[6];
	char *t;
	FILE *f;
	int i;

	f = fopen("/dev/urandom", "rb");
	if (f) {
		if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
			fclose(f);
			f = NULL;
		}
	}
	if (f)
		fclose(f);
	if (!f) {
		srand((unsigned int)(time(NULL) ^ getpid()));
		for (i = 0; i < (int)sizeof(b); i++)
			b[i] = (unsigned char)(rand() & 0xff);
	}
	t = malloc(13);
	if (!t)
		return NULL;
	for (i = 0; i < (int)sizeof(b); i++)
		snprintf(t + i * 2, 3, "%02x", b[i]);
	t[12] = '\0';
	return t;
}

static int run_command_capture(const hermes_config_t *cfg, const char *cmd, char **out, int *exit_code)
{
	int p[2];
	pid_t pid;
	char buf[1024];
	char *acc;
	int status;
	int timed_out;
	time_t start;
	int n;
	int child_done;

	if (!cfg || !cmd || !out || !exit_code)
		return -1;
	*out = NULL;
	*exit_code = -1;
	acc = NULL;
	if (pipe(p) < 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}

	if (pid == 0) {
		if (cfg->workdir && chdir(cfg->workdir) < 0)
			_exit(126);
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
		close(p[0]);
		close(p[1]);
		execl("/bin/sh", "sh", "-lc", cmd, (char *)NULL);
		_exit(127);
	}

	close(p[1]);
	fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
	status = 0;
	timed_out = 0;
	child_done = 0;
	start = time(NULL);

	for (;;) {
		n = (int)read(p[0], buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			if (append_text(&acc, buf) < 0) {
				kill(pid, SIGKILL);
				waitpid(pid, NULL, 0);
				close(p[0]);
				free(acc);
				return -1;
			}
		}

		if (!child_done) {
			pid_t w;

			w = waitpid(pid, &status, WNOHANG);
			if (w == pid)
				child_done = 1;
		}

		if (!child_done && difftime(time(NULL), start) > cfg->tool_timeout_sec) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			child_done = 1;
			timed_out = 1;
		}

		if (child_done && n == 0)
			break;
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			break;
		sleep_10ms();
	}

	close(p[0]);
	if (timed_out)
		append_text(&acc, "\n[hermes] command timed out\n");
	if (!acc)
		acc = xstrdup("(no output)\n");
	if (!acc)
		return -1;

	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		*exit_code = 128 + WTERMSIG(status);
	else
		*exit_code = -1;
	*out = acc;
	return 0;
}

static int build_exec_reply(const hermes_config_t *cfg, const char *cmd, const char *output, int code,
	char **reply)
{
	char *out;
	int n;

	n = snprintf(NULL, 0,
		"Executed in `%s`\n"
		"Command: `%s`\n"
		"Exit: %d\n\n"
		"Output:\n%s",
		cfg->workdir ? cfg->workdir : ".",
		cmd,
		code,
		output ? output : "(no output)");
	if (n < 0)
		return -1;
	out = malloc((size_t)n + 1);
	if (!out)
		return -1;
	snprintf(out, (size_t)n + 1,
		"Executed in `%s`\n"
		"Command: `%s`\n"
		"Exit: %d\n\n"
		"Output:\n%s",
		cfg->workdir ? cfg->workdir : ".",
		cmd,
		code,
		output ? output : "(no output)");
	*reply = out;
	return 0;
}

static int append_section(char **acc, const char *title, const char *body)
{
	int n;
	char *buf;

	n = snprintf(NULL, 0, "\n%s\n%s\n", title, body ? body : "");
	if (n < 0)
		return -1;
	buf = malloc((size_t)n + 1);
	if (!buf)
		return -1;
	snprintf(buf, (size_t)n + 1, "\n%s\n%s\n", title, body ? body : "");
	if (append_text(acc, buf) < 0) {
		free(buf);
		return -1;
	}
	free(buf);
	return 0;
}

static int execute_and_format(const hermes_config_t *cfg, const char *cmd, char **reply_out)
{
	char *out;
	char *msg;
	char *status_out;
	char *log_out;
	char *note;
	int code;
	int rc;

	out = NULL;
	msg = NULL;
	status_out = NULL;
	log_out = NULL;
	note = NULL;
	code = -1;

	rc = run_command_capture(cfg, cmd, &out, &code);
	if (rc < 0)
		return -1;
	rc = build_exec_reply(cfg, cmd, out, code, &msg);
	free(out);
	if (rc < 0)
		return -1;

	if (code == 0)
		note = xstrdup("Assistant notes:\n- Command finished successfully.\n- I added quick repo context below.");
	else
		note = xstrdup("Assistant notes:\n- Command failed.\n- Check output and fix the reported error, then retry.");
	if (!note || append_section(&msg, "", note) < 0) {
		free(note);
		free(msg);
		return -1;
	}
	free(note);

	if (run_command_capture(cfg, "git status --short --branch", &status_out, &code) == 0) {
		if (append_section(&msg, "Repo status:", status_out) < 0) {
			free(status_out);
			free(msg);
			return -1;
		}
		free(status_out);
	}

	if (contains_ci(cmd, "git commit") || contains_ci(cmd, "git push")) {
		if (run_command_capture(cfg, "git log -1 --oneline", &log_out, &code) == 0) {
			if (append_section(&msg, "Latest commit:", log_out) < 0) {
				free(log_out);
				free(msg);
				return -1;
			}
			free(log_out);
		}
	}

	*reply_out = msg;
	return 0;
}

static int build_approval_reply(const char *token, const char *cmd, int destructive, char **reply)
{
	char *out;
	int n;

	n = snprintf(NULL, 0,
		"Approval required before running command.\n\n"
		"Classification: %s\n"
		"Command: `%s`\n"
		"Token: `%s`\n\n"
		"Reply with:\n/approve %s\n",
		destructive ? "destructive" : "write-capable",
		cmd,
		token,
		token);
	if (n < 0)
		return -1;
	out = malloc((size_t)n + 1);
	if (!out)
		return -1;
	snprintf(out, (size_t)n + 1,
		"Approval required before running command.\n\n"
		"Classification: %s\n"
		"Command: `%s`\n"
		"Token: `%s`\n\n"
		"Reply with:\n/approve %s\n",
		destructive ? "destructive" : "write-capable",
		cmd,
		token,
		token);
	*reply = out;
	return 0;
}

static char *extract_tag_block(const char *text, const char *tag)
{
	char open[64];
	char close[64];
	const char *a;
	const char *b;
	char *out;
	int n;

	if (!text || !tag)
		return NULL;
	n = snprintf(open, sizeof(open), "<%s>", tag);
	if (n < 0 || (size_t)n >= sizeof(open))
		return NULL;
	n = snprintf(close, sizeof(close), "</%s>", tag);
	if (n < 0 || (size_t)n >= sizeof(close))
		return NULL;
	a = strstr(text, open);
	if (!a)
		return NULL;
	a += strlen(open);
	b = strstr(a, close);
	if (!b || b <= a)
		return NULL;
	out = malloc((size_t)(b - a) + 1);
	if (!out)
		return NULL;
	memcpy(out, a, (size_t)(b - a));
	out[b - a] = '\0';
	return trimdup(out);
}

static char *extract_run_command(const char *model_reply)
{
	char *blk;
	char *line;
	char *cmd;

	blk = extract_tag_block(model_reply, "run");
	if (!blk)
		return NULL;
	line = strtok(blk, "\n");
	while (line) {
		if (starts_with(line, "command:")) {
			cmd = trimdup(line + 8);
			free(blk);
			return cmd;
		}
		line = strtok(NULL, "\n");
	}
	free(blk);
	return NULL;
}

static char *build_agent_prompt(const char *history)
{
	const char *sys;
	int n;
	char *out;

	sys =
		"You are Hermes, an email coding agent that should behave like a CLI coding assistant.\n"
		"Decide the next best step and either run one shell command or provide a final answer.\n"
		"You DO have access to local shell and repository through the <run> command path.\n"
		"Never say you cannot access files, git, or local repo.\n"
		"If the user asks for any project change, tests, commit, or push, produce <run> steps.\n"
		"When you need a command, reply ONLY with:\n"
		"<run>\n"
		"command: <single shell command>\n"
		"</run>\n"
		"When done, reply ONLY with:\n"
		"<final>\n"
		"<assistant response in plain text>\n"
		"</final>\n"
		"Use concise teammate tone and be explicit about results.\n";
	n = snprintf(NULL, 0, "%s\nSession context:\n%s", sys, history ? history : "");
	if (n < 0)
		return NULL;
	out = malloc((size_t)n + 1);
	if (!out)
		return NULL;
	snprintf(out, (size_t)n + 1, "%s\nSession context:\n%s", sys, history ? history : "");
	return out;
}

static int append_agent_event(char **history, const char *title, const char *body)
{
	char *chunk;
	int n;

	n = snprintf(NULL, 0, "%s\n%s\n\n", title, body ? body : "");
	if (n < 0)
		return -1;
	chunk = malloc((size_t)n + 1);
	if (!chunk)
		return -1;
	snprintf(chunk, (size_t)n + 1, "%s\n%s\n\n", title, body ? body : "");
	if (append_text(history, chunk) < 0) {
		free(chunk);
		return -1;
	}
	free(chunk);
	return 0;
}

static int handle_agent_mode(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg,
	char **reply_out, int *handled_out)
{
	char *history;
	int step;

	if (!cfg || !db || !msg || !reply_out || !handled_out)
		return -1;
	history = NULL;
	if (append_agent_event(&history, "User", msg->body ? msg->body : "") < 0)
		return -1;

	for (step = 0; step < 4; step++) {
		char *prompt;
		char *model;
		char *cmd;

		prompt = build_agent_prompt(history);
		if (!prompt) {
			free(history);
			return -1;
		}
		model = NULL;
		if (openai_generate(cfg, prompt, &model) < 0) {
			free(prompt);
			free(history);
			return -1;
		}
		free(prompt);

		cmd = extract_run_command(model);
		if (cmd) {
			int needs_approval;

			needs_approval = requires_approval_command(cmd);
			if (needs_approval) {
				char *token;
				char *approval;

				token = token_generate();
				if (!token) {
					free(cmd);
					free(model);
					free(history);
					return -1;
				}
				if (db_pending_create(db, msg->thread_key, token, cmd, 1) < 0) {
					free(token);
					free(cmd);
					free(model);
					free(history);
					return -1;
				}
				approval = NULL;
				if (build_approval_reply(token, cmd, 1, &approval) < 0) {
					free(token);
					free(cmd);
					free(model);
					free(history);
					return -1;
				}
				*reply_out = approval;
				*handled_out = 1;
				free(token);
				free(cmd);
				free(model);
				free(history);
				return 0;
			}

			{
				char *exec_reply;

				exec_reply = NULL;
				if (execute_and_format(cfg, cmd, &exec_reply) < 0) {
					free(cmd);
					free(model);
					free(history);
					return -1;
				}
				if (append_agent_event(&history, "Tool command", cmd) < 0 ||
					append_agent_event(&history, "Tool result", exec_reply) < 0) {
					free(exec_reply);
					free(cmd);
					free(model);
					free(history);
					return -1;
				}
				free(exec_reply);
			}
			free(cmd);
			free(model);
			continue;
		}

		{
			char *final;

			final = extract_tag_block(model, "final");
			if (!final)
				final = trimdup(model);
			free(model);
			if (!final) {
				free(history);
				return -1;
			}
			*reply_out = final;
			*handled_out = 1;
			free(history);
			return 0;
		}
	}

	*reply_out = xstrdup("I could not complete all steps safely in one pass. Please refine the request.");
	*handled_out = 1;
	free(history);
	return *reply_out ? 0 : -1;
}

int tool_try_handle(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg,
	char **reply_out, int *handled_out)
{
	char *body;
	char *line;

	if (!cfg || !db || !msg || !reply_out || !handled_out)
		return -1;
	*handled_out = 0;
	*reply_out = NULL;
	body = trimdup(msg->body ? msg->body : "");
	if (!body)
		return -1;

	line = strtok(body, "\n");
	if (!line) {
		free(body);
		return 0;
	}

	{
		char *token;
		char *pending;
		int needs;
		int rc;

		token = extract_approval_token(line, msg->body);
		if (!token)
			goto skip_approve;
		if (!*token) {
			free(token);
			free(body);
			return -1;
		}
		pending = NULL;
		needs = 0;
		*handled_out = 1;
		rc = db_pending_consume(db, msg->thread_key, token, &pending, &needs);
		if (rc == 1) {
			*reply_out = xstrdup("No pending command found for this approval token.");
			free(token);
			free(body);
			return *reply_out ? 0 : -1;
		}
		if (rc < 0 || !pending) {
			free(token);
			free(body);
			return -1;
		}
		{
			if (execute_and_format(cfg, pending, reply_out) < 0) {
				free(token);
				free(pending);
				free(body);
				return -1;
			}
		}
		free(token);
		free(pending);
		free(body);
		return 0;
	}

skip_approve:

	if (handle_agent_mode(cfg, db, msg, reply_out, handled_out) < 0) {
		free(body);
		return -1;
	}
	if (*handled_out) {
		free(body);
		return 0;
	}

	free(body);
	return 0;
}
