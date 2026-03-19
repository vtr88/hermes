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

static int natural_command_from_text(const char *body, char **command_out, int *force_approval_out)
{
	char *cmd;
	int wants_commit;
	int wants_push;

	if (!body || !command_out || !force_approval_out)
		return -1;
	*command_out = NULL;
	*force_approval_out = 0;

	if (contains_ci(body, "git status") || (contains_ci(body, "status") && contains_ci(body, "git"))) {
		*command_out = xstrdup("git status --short --branch");
		return *command_out ? 1 : -1;
	}
	if (contains_ci(body, "git diff") || (contains_ci(body, "diff") && contains_ci(body, "git"))) {
		*command_out = xstrdup("git diff");
		return *command_out ? 1 : -1;
	}
	if (contains_ci(body, "run tests") || contains_ci(body, "make test") || contains_ci(body, "test suite")) {
		*command_out = xstrdup("make test");
		return *command_out ? 1 : -1;
	}
	if (contains_ci(body, "build") && contains_ci(body, "make")) {
		*command_out = xstrdup("make");
		return *command_out ? 1 : -1;
	}

	wants_commit = contains_ci(body, "commit");
	wants_push = contains_ci(body, "push") || contains_ci(body, "github");
	if (wants_commit) {
		if (wants_push)
			cmd = xstrdup("git add -A && git commit -m \"chore: update project changes\" && git push origin main");
		else
			cmd = xstrdup("git add -A && git commit -m \"chore: update project changes\"");
		if (!cmd)
			return -1;
		*command_out = cmd;
		*force_approval_out = 1;
		return 1;
	}

	if (contains_ci(body, "list files")) {
		*command_out = xstrdup("ls -la");
		return *command_out ? 1 : -1;
	}

	return 0;
}

static int contains_any(const char *s, const char **bad, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (strstr(s, bad[i]))
			return 1;
	}
	return 0;
}

static int is_destructive_command(const char *cmd)
{
	const char *bad[] = {
		" rm ", "rm -", " mv ", " cp ", " truncate ", " dd ", "chmod ", "chown ",
		"git reset", "git clean", "git checkout", "git push --force", "mkfs", "reboot",
		"shutdown", "userdel", "passwd ", "apt-get remove", "apt remove", "systemctl "
	};
	char *s;
	int hit;

	s = NULL;
	hit = 0;
	s = trimdup(cmd);
	if (!s)
		return 1;
	if (starts_with(s, "rm ") || starts_with(s, "mv ") || starts_with(s, "cp "))
		hit = 1;
	if (!hit && contains_any(s, bad, sizeof(bad) / sizeof(bad[0])))
		hit = 1;
	free(s);
	return hit;
}

static int is_read_only_command(const char *cmd)
{
	const char *safe_prefix[] = {
		"ls", "pwd", "git status", "git diff", "git log", "cat ", "grep ", "rg ",
		"find ", "wc ", "stat ", "make test", "make lint", "./build/test_config"
	};
	const char *shell_ops[] = {";", "&&", "||", "|", ">", "<"};
	char *s;
	size_t i;
	int ok;

	s = trimdup(cmd);
	if (!s)
		return 0;
	if (*s == '\0') {
		free(s);
		return 0;
	}
	if (contains_any(s, shell_ops, sizeof(shell_ops) / sizeof(shell_ops[0]))) {
		free(s);
		return 0;
	}
	ok = 0;
	for (i = 0; i < sizeof(safe_prefix) / sizeof(safe_prefix[0]); i++) {
		if (starts_with(s, safe_prefix[i])) {
			ok = 1;
			break;
		}
	}
	free(s);
	return ok;
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

int tool_try_handle(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg,
	char **reply_out, int *handled_out)
{
	char *body;
	char *line;
	char *cmd;

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

	if (starts_with(line, "/run ")) {
		int destructive;
		int readonly;

		cmd = trimdup(line + 5);
		if (!cmd) {
			free(body);
			return -1;
		}
		destructive = is_destructive_command(cmd);
		readonly = is_read_only_command(cmd);
		*handled_out = 1;
		if (!readonly || destructive) {
			char *token;

			token = token_generate();
			if (!token) {
				free(cmd);
				free(body);
				return -1;
			}
			if (db_pending_create(db, msg->thread_key, token, cmd, 1) < 0) {
				free(token);
				free(cmd);
				free(body);
				return -1;
			}
			if (build_approval_reply(token, cmd, destructive, reply_out) < 0) {
				free(token);
				free(cmd);
				free(body);
				return -1;
			}
			free(token);
			free(cmd);
			free(body);
			return 0;
		}
		{
			char *out;
			int code;

			out = NULL;
			code = -1;
			if (run_command_capture(cfg, cmd, &out, &code) < 0) {
				free(cmd);
				free(body);
				return -1;
			}
			if (build_exec_reply(cfg, cmd, out, code, reply_out) < 0) {
				free(out);
				free(cmd);
				free(body);
				return -1;
			}
			free(out);
		}
		free(cmd);
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
			char *out;
			int code;

			out = NULL;
			code = -1;
			if (run_command_capture(cfg, pending, &out, &code) < 0) {
				free(token);
				free(pending);
				free(body);
				return -1;
			}
			if (build_exec_reply(cfg, pending, out, code, reply_out) < 0) {
				free(out);
				free(token);
				free(pending);
				free(body);
				return -1;
			}
			free(out);
		}
		free(token);
		free(pending);
		free(body);
		return 0;
	}

skip_approve:

	{
		int force_approval;
		int m;

		cmd = NULL;
		force_approval = 0;
		m = natural_command_from_text(msg->body ? msg->body : "", &cmd, &force_approval);
		if (m < 0) {
			free(body);
			return -1;
		}
		if (m == 1 && cmd) {
			int destructive;
			int readonly;

			destructive = is_destructive_command(cmd);
			readonly = is_read_only_command(cmd);
			*handled_out = 1;
			if (force_approval || !readonly || destructive) {
				char *token;

				token = token_generate();
				if (!token) {
					free(cmd);
					free(body);
					return -1;
				}
				if (db_pending_create(db, msg->thread_key, token, cmd, 1) < 0) {
					free(token);
					free(cmd);
					free(body);
					return -1;
				}
				if (build_approval_reply(token, cmd, destructive, reply_out) < 0) {
					free(token);
					free(cmd);
					free(body);
					return -1;
				}
				free(token);
				free(cmd);
				free(body);
				return 0;
			}
			{
				char *out;
				int code;

				out = NULL;
				code = -1;
				if (run_command_capture(cfg, cmd, &out, &code) < 0) {
					free(cmd);
					free(body);
					return -1;
				}
				if (build_exec_reply(cfg, cmd, out, code, reply_out) < 0) {
					free(out);
					free(cmd);
					free(body);
					return -1;
				}
				free(out);
			}
			free(cmd);
			free(body);
			return 0;
		}
	}

	free(body);
	return 0;
}
