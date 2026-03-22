#define _POSIX_C_SOURCE 200809L

#include "hermes.h"

#include <ctype.h>
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

static int is_blank_text(const char *s)
{
	if (!s)
		return 1;
	while (*s) {
		if (!isspace((unsigned char)*s))
			return 0;
		s++;
	}
	return 1;
}

static char *xstrndup(const char *s, size_t n)
{
	char *p;

	p = malloc(n + 1);
	if (!p)
		return NULL;
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

static char *trim_dup(const char *s)
{
	const char *b;
	const char *e;

	if (!s)
		return xstrdup("");
	b = s;
	while (*b && isspace((unsigned char)*b))
		b++;
	e = s + strlen(s);
	while (e > b && isspace((unsigned char)e[-1]))
		e--;
	return xstrndup(b, (size_t)(e - b));
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

static void sleep_10ms(void)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 10000000L;
	nanosleep(&ts, NULL);
}

static int run_opencode_capture(const hermes_config_t *cfg, const char *session_id, const char *prompt,
	char **out, int *exit_code)
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

	if (!cfg || !prompt || !out || !exit_code)
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
		if (session_id && *session_id) {
			execlp("opencode", "opencode", "run", "--format", "json", "--session", session_id,
				"--dir", cfg->workdir ? cfg->workdir : ".", "--", prompt, (char *)NULL);
		} else {
			execlp("opencode", "opencode", "run", "--format", "json", "--dir",
				cfg->workdir ? cfg->workdir : ".", "--", prompt, (char *)NULL);
		}
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
		append_text(&acc, "\n[hermes] opencode timed out\n");
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

static char *json_unescape(const char *p)
{
	char *out;
	char *w;

	out = malloc(strlen(p) + 1);
	if (!out)
		return NULL;
	w = out;
	while (*p) {
		if (*p == '"')
			break;
		if (*p == '\\') {
			p++;
			if (!*p)
				break;
			switch (*p) {
			case 'n':
				*w++ = '\n';
				break;
			case 'r':
				*w++ = '\r';
				break;
			case 't':
				*w++ = '\t';
				break;
			case '"':
				*w++ = '"';
				break;
			case '\\':
				*w++ = '\\';
				break;
			default:
				*w++ = *p;
				break;
			}
			p++;
			continue;
		}
		*w++ = *p++;
	}
	*w = '\0';
	return out;
}

static char *find_last_string_value(const char *json, const char *key)
{
	char pat[96];
	const char *p;
	char *out;
	int n;

	if (!json || !key)
		return NULL;
	n = snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	if (n < 0 || (size_t)n >= sizeof(pat))
		return NULL;
	p = json;
	out = NULL;
	while ((p = strstr(p, pat)) != NULL) {
		char *cur;

		p += strlen(pat);
		cur = json_unescape(p);
		if (!cur)
			return out;
		free(out);
		out = cur;
	}
	return out;
}

static int parse_approve_token_text(const char *text, char **token_out)
{
	char *clean;
	char *p;
	char *tok;
	size_t n;

	if (!token_out)
		return -1;
	*token_out = NULL;
	if (!text)
		return 0;
	clean = trim_dup(text);
	if (!clean)
		return -1;
	p = clean;
	if (strncmp(p, "/approve", 8) == 0 && (p[8] == ' ' || p[8] == '\t'))
		p += 8;
	else if (strncmp(p, "approve", 7) == 0 && (p[7] == ' ' || p[7] == '\t'))
		p += 7;
	else {
		free(clean);
		return 0;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	n = strcspn(p, " \t\r\n");
	if (n == 0) {
		free(clean);
		return 0;
	}
	tok = xstrndup(p, n);
	free(clean);
	if (!tok)
		return -1;
	*token_out = tok;
	return 1;
}

static int parse_approval_request(const char *json, char **token_out, char **command_out)
{
	char *token;
	char *cmd;

	if (!token_out || !command_out)
		return -1;
	*token_out = NULL;
	*command_out = NULL;
	if (!json)
		return 0;
	if (!strstr(json, "\"status\":\"pending\""))
		return 0;
	token = find_last_string_value(json, "callID");
	cmd = find_last_string_value(json, "command");
	if (!token || !*token || !cmd || !*cmd) {
		free(token);
		free(cmd);
		return 0;
	}
	*token_out = token;
	*command_out = cmd;
	return 1;
}

static long find_last_long(const char *s, const char *key)
{
	char pat[64];
	const char *p;
	long v;
	int n;

	n = snprintf(pat, sizeof(pat), "\"%s\":", key);
	if (n < 0 || (size_t)n >= sizeof(pat))
		return 0;
	p = s;
	v = 0;
	while ((p = strstr(p, pat)) != NULL) {
		char *ep;
		long cur;

		p += strlen(pat);
		cur = strtol(p, &ep, 10);
		if (ep != p)
			v = cur;
	}
	return v;
}

static double find_last_double(const char *s, const char *key)
{
	char pat[64];
	const char *p;
	double v;
	int n;

	n = snprintf(pat, sizeof(pat), "\"%s\":", key);
	if (n < 0 || (size_t)n >= sizeof(pat))
		return 0.0;
	p = s;
	v = 0.0;
	while ((p = strstr(p, pat)) != NULL) {
		char *ep;
		double cur;

		p += strlen(pat);
		cur = strtod(p, &ep);
		if (ep != p)
			v = cur;
	}
	return v;
}

static int parse_opencode_json(const char *json, char **session_id_out, char **text_out, hermes_usage_t *usage)
{
	const char *p;
	char *acc;

	if (!json || !session_id_out || !text_out || !usage)
		return -1;
	*session_id_out = NULL;
	*text_out = NULL;
	memset(usage, 0, sizeof(*usage));

	p = strstr(json, "\"sessionID\":\"");
	if (p) {
		p += strlen("\"sessionID\":\"");
		*session_id_out = json_unescape(p);
	}

	acc = NULL;
	p = json;
	while ((p = strstr(p, "\"type\":\"text\"")) != NULL) {
		const char *t;
		char *txt;

		t = strstr(p, "\"text\":\"");
		if (!t)
			break;
		t += strlen("\"text\":\"");
		txt = json_unescape(t);
		if (!txt)
			return -1;
		if (append_text(&acc, txt) < 0 || append_text(&acc, "\n") < 0) {
			free(txt);
			free(acc);
			return -1;
		}
		free(txt);
		p = t;
	}

	if (!acc)
		acc = xstrdup("");
	if (!acc)
		return -1;
	*text_out = acc;

	usage->total_tokens = find_last_long(json, "total");
	usage->prompt_tokens = find_last_long(json, "input");
	usage->completion_tokens = find_last_long(json, "output");
	usage->cost_usd = find_last_double(json, "cost");
	return 0;
}

static int run_opencode_turn(const hermes_config_t *cfg, const char *session_id, const char *prompt,
	char **session_id_out, char **reply_out, char **raw_out, hermes_usage_t *usage)
{
	char *out;
	int code;
	const char *sid;

	if (!cfg || !prompt || !session_id_out || !reply_out || !usage)
		return -1;
	*session_id_out = NULL;
	*reply_out = NULL;
	if (raw_out)
		*raw_out = NULL;
	sid = NULL;
	if (session_id && *session_id)
		sid = session_id;
	else if (cfg->opencode_session_id && *cfg->opencode_session_id)
		sid = cfg->opencode_session_id;

	out = NULL;
	code = -1;
	if (run_opencode_capture(cfg, sid, prompt, &out, &code) < 0)
		return -1;
	if (raw_out)
		*raw_out = xstrdup(out);
	if (code != 0) {
		*reply_out = out;
		*session_id_out = session_id && *session_id ? xstrdup(session_id) : NULL;
		return 0;
	}

	if (parse_opencode_json(out, session_id_out, reply_out, usage) < 0) {
		free(out);
		return -1;
	}
	free(out);
	if (!*reply_out)
		*reply_out = xstrdup("(no response text)");
	return *reply_out ? 0 : -1;
}

int tool_try_handle(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg,
	char **reply_out, hermes_usage_t *usage_out, int *handled_out)
{
	char *session_id;
	char *new_session_id;
	char *raw;
	char *token;
	char *approval_command;
	char *approved_command;
	char *prompt_heap;
	char *reply;
	const char *prompt;
	hermes_usage_t usage;
	int rc;

	if (!cfg || !db || !msg || !reply_out || !usage_out || !handled_out)
		return -1;
	*handled_out = 1;
	*reply_out = NULL;
	memset(usage_out, 0, sizeof(*usage_out));
	session_id = NULL;
	new_session_id = NULL;
	raw = NULL;
	token = NULL;
	approval_command = NULL;
	approved_command = NULL;
	prompt_heap = NULL;
	reply = NULL;
	prompt = NULL;
	rc = 0;
	memset(&usage, 0, sizeof(usage));

	rc = parse_approve_token_text(msg->body, &token);
	if (rc < 0)
		goto fail;
	if (rc == 0)
		rc = parse_approve_token_text(msg->subject, &token);
	if (rc < 0)
		goto fail;
	if (rc > 0) {
		int needs_approval;

		needs_approval = 0;
		rc = db_pending_consume(db, msg->thread_key, token, &approved_command, &needs_approval);
		if (rc < 0)
			goto fail;
		if (rc == 1) {
			reply = xstrdup("No pending approval found for this token in this thread.");
			if (!reply)
				goto fail;
			*reply_out = reply;
			free(token);
			return 0;
		}
		prompt = approved_command;
		free(token);
		token = NULL;
	} else {
		prompt = msg->body;
	}

	if (!prompt || !*prompt)
		prompt = msg->subject;
	prompt_heap = trim_dup(prompt);
	if (!prompt_heap)
		goto fail;
	if (is_blank_text(prompt_heap)) {
		free(prompt_heap);
		prompt_heap = xstrdup("Please respond briefly and confirm session is active.");
		if (!prompt_heap)
			goto fail;
	}
	prompt = prompt_heap;
	if (!prompt || !*prompt)
		prompt = "Please respond briefly and confirm session is active.";

	if (db_session_get(db, msg->thread_key, &session_id) < 0)
		goto fail;
	if (run_opencode_turn(cfg, session_id, prompt, &new_session_id, &reply, &raw, &usage) < 0)
		goto fail;

	free(token);
	token = NULL;
	rc = parse_approval_request(raw, &token, &approval_command);
	if (rc < 0)
		goto fail;
	if (rc > 0 && token && approval_command) {
		if (db_pending_create(db, msg->thread_key, token, approval_command, 1) == 0) {
			append_text(&reply, "\n\nApproval required. Reply with /approve ");
			append_text(&reply, token);
		}
	}

	if (new_session_id && *new_session_id)
		db_session_set(db, msg->thread_key, new_session_id);
	if (usage.total_tokens > 0 || usage.cost_usd > 0.0)
		db_usage_add(db, &usage);
	*usage_out = usage;

	*reply_out = reply ? reply : xstrdup("(no response)");
	free(session_id);
	free(new_session_id);
	free(raw);
	free(token);
	free(approval_command);
	free(approved_command);
	free(prompt_heap);
	return *reply_out ? 0 : -1;

fail:
	free(session_id);
	free(new_session_id);
	free(raw);
	free(token);
	free(approval_command);
	free(approved_command);
	free(prompt_heap);
	free(reply);
	return -1;
}
