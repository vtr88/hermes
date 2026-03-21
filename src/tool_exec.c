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

static void sleep_10ms(void)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 10000000L;
	nanosleep(&ts, NULL);
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
	char **session_id_out, char **reply_out, hermes_usage_t *usage)
{
	char cmd[4096];
	char *out;
	int code;
	int n;

	if (!cfg || !prompt || !session_id_out || !reply_out || !usage)
		return -1;
	*session_id_out = NULL;
	*reply_out = NULL;

	if (session_id && *session_id) {
		n = snprintf(cmd, sizeof(cmd),
			"opencode run --format json --session %s --dir \"%s\" \"%s\"",
			session_id,
			cfg->workdir,
			prompt);
	} else if (cfg->opencode_session_id && *cfg->opencode_session_id) {
		n = snprintf(cmd, sizeof(cmd),
			"opencode run --format json --session %s --dir \"%s\" \"%s\"",
			cfg->opencode_session_id,
			cfg->workdir,
			prompt);
	} else {
		n = snprintf(cmd, sizeof(cmd),
			"opencode run --format json --dir \"%s\" \"%s\"",
			cfg->workdir,
			prompt);
	}
	if (n < 0 || (size_t)n >= sizeof(cmd))
		return -1;

	out = NULL;
	code = -1;
	if (run_command_capture(cfg, cmd, &out, &code) < 0)
		return -1;
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
	char **reply_out, int *handled_out)
{
	char *session_id;
	char *new_session_id;
	char *reply;
	hermes_usage_t usage;

	if (!cfg || !db || !msg || !reply_out || !handled_out)
		return -1;
	*handled_out = 1;
	*reply_out = NULL;
	session_id = NULL;
	new_session_id = NULL;
	reply = NULL;
	memset(&usage, 0, sizeof(usage));

	if (db_session_get(db, msg->thread_key, &session_id) < 0)
		return -1;
	if (run_opencode_turn(cfg, session_id, msg->body ? msg->body : "", &new_session_id, &reply, &usage) < 0) {
		free(session_id);
		return -1;
	}
	if (new_session_id && *new_session_id)
		db_session_set(db, msg->thread_key, new_session_id);
	if (usage.total_tokens > 0 || usage.cost_usd > 0.0)
		db_usage_add(db, &usage);

	*reply_out = reply ? reply : xstrdup("(no response)");
	free(session_id);
	free(new_session_id);
	return *reply_out ? 0 : -1;
}
