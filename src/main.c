#define _POSIX_C_SOURCE 200809L

#include "hermes.h"

#include <ctype.h>
#include <signal.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef HERMES_WITH_CURL
#include <curl/curl.h>
#endif

static volatile sig_atomic_t stop_flag;

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

static char *email_addr_only(const char *from)
{
	const char *a;
	const char *b;

	if (!from)
		return NULL;
	a = strchr(from, '<');
	b = strchr(from, '>');
	if (a && b && b > a + 1)
		return xstrndup(a + 1, (size_t)(b - (a + 1)));
	return xstrndup(from, strlen(from));
}

static void trim_ascii(char *s)
{
	char *e;

	if (!s || !*s)
		return;
	e = s + strlen(s) - 1;
	while (e >= s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n'))
		*e-- = '\0';
	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
}

static int sender_allowed(const hermes_config_t *cfg, const char *from)
{
	char *addr;
	char *rules;
	char *tok;
	int allowed;

	if (!cfg || !from)
		return 0;
	if (!cfg->allow_from || !*cfg->allow_from)
		return 1;

	addr = email_addr_only(from);
	if (!addr)
		return 0;
	rules = xstrndup(cfg->allow_from, strlen(cfg->allow_from));
	if (!rules) {
		free(addr);
		return 0;
	}

	allowed = 0;
	tok = strtok(rules, ",");
	while (tok) {
		trim_ascii(tok);
		if (*tok && strcasecmp(tok, addr) == 0) {
			allowed = 1;
			break;
		}
		tok = strtok(NULL, ",");
	}

	free(rules);
	free(addr);
	return allowed;
}

static int starts_with_ci(const char *s, const char *prefix)
{
	if (!s || !prefix)
		return 0;
	while (*prefix && *s) {
		if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
			return 0;
		s++;
		prefix++;
	}
	return *prefix == '\0';
}

static int is_restart_intent(const char *s)
{
	while (s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
		s++;
	if (!s || !*s)
		return 0;
	if (starts_with_ci(s, "restart"))
		return 1;
	if (starts_with_ci(s, "please restart"))
		return 1;
	return 0;
}

static int run_cmd_wait(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int restart_hermes_service(void)
{
	char *restart_argv[] = { "sudo", "systemctl", "restart", "hermes", NULL };
	char *status_argv[] = { "sudo", "systemctl", "is-active", "hermes", NULL };

	if (run_cmd_wait(restart_argv) != 0)
		return -1;
	if (run_cmd_wait(status_argv) != 0)
		return -1;
	return 0;
}

static char *capture_modified_files(const hermes_config_t *cfg)
{
	char cmd[1024];
	FILE *p;
	char line[512];
	char *out;
	int shown;

	if (!cfg || !cfg->workdir)
		return xstrndup("(workdir not set)\n", 18);
	if (snprintf(cmd, sizeof(cmd), "git -C \"%s\" status --short", cfg->workdir) < 0)
		return xstrndup("(cannot render command)\n", 24);
	p = popen(cmd, "r");
	if (!p)
		return xstrndup("(git not available)\n", 20);
	out = NULL;
	shown = 0;
	while (fgets(line, sizeof(line), p)) {
		if (shown >= 30) {
			append_text(&out, "...\n");
			break;
		}
		append_text(&out, line);
		shown++;
	}
	pclose(p);
	if (!out)
		out = xstrndup("(none)\n", 7);
	return out;
}

static char *capture_opencode_stats(const hermes_config_t *cfg)
{
	char cmd[1024];
	FILE *p;
	char line[512];
	char *out;
	int shown;

	if (!cfg || !cfg->workdir)
		return xstrndup("(workdir not set)\n", 18);
	if (snprintf(cmd, sizeof(cmd), "cd \"%s\" && opencode stats 2>/dev/null", cfg->workdir) < 0)
		return xstrndup("(cannot render command)\n", 24);
	p = popen(cmd, "r");
	if (!p)
		return xstrndup("(opencode stats unavailable)\n", 28);
	out = NULL;
	shown = 0;
	while (fgets(line, sizeof(line), p)) {
		if (shown >= 80) {
			append_text(&out, "...\n");
			break;
		}
		append_text(&out, line);
		shown++;
	}
	pclose(p);
	if (!out)
		out = xstrndup("(no stats output)\n", 18);
	return out;
}

static char *append_metrics_footer(const hermes_config_t *cfg, hermes_db_t *db, const char *reply,
	const hermes_usage_t *turn)
{
	hermes_usage_t total;
	char *files;
	char *stats;
	char *out;
	char budget[192];
	int n;

	if (!cfg || !db)
		return NULL;
	memset(&total, 0, sizeof(total));
	if (db_usage_get(db, &total) < 0)
		memset(&total, 0, sizeof(total));
	files = capture_modified_files(cfg);
	if (!files)
		files = xstrndup("(unknown)\n", 10);
	if (!files)
		return NULL;
	stats = capture_opencode_stats(cfg);
	if (!stats)
		stats = xstrndup("(stats unavailable)\n", 20);
	if (!stats) {
		free(files);
		return NULL;
	}

	if (cfg->budget_usd > 0.0) {
		double left;
		double pct;

		left = cfg->budget_usd - total.cost_usd;
		if (left < 0.0)
			left = 0.0;
		pct = (total.cost_usd / cfg->budget_usd) * 100.0;
		snprintf(budget, sizeof(budget), "spent=$%.4f left=$%.4f used=%.2f%%", total.cost_usd, left, pct);
	} else {
		snprintf(budget, sizeof(budget), "spent=$%.4f left=n/a used=n/a", total.cost_usd);
	}

		n = snprintf(NULL, 0,
		"%s\n\n---\n"
		"Context: in=%ld out=%ld total=%ld (this turn)\n"
		"Cumulative: in=%ld out=%ld total=%ld\n"
		"Budget: %s\n"
		"Workdir: %s\n"
		"Modified files:\n%s\n"
		"OpenCode stats:\n%s",
		reply ? reply : "",
		turn ? turn->prompt_tokens : 0,
		turn ? turn->completion_tokens : 0,
		turn ? turn->total_tokens : 0,
		total.prompt_tokens,
		total.completion_tokens,
		total.total_tokens,
		budget,
		cfg->workdir ? cfg->workdir : "(unset)",
		files,
		stats);
	if (n < 0) {
		free(files);
		free(stats);
		return NULL;
	}
	out = malloc((size_t)n + 1);
	if (!out) {
		free(files);
		free(stats);
		return NULL;
	}
	snprintf(out, (size_t)n + 1,
		"%s\n\n---\n"
		"Context: in=%ld out=%ld total=%ld (this turn)\n"
		"Cumulative: in=%ld out=%ld total=%ld\n"
		"Budget: %s\n"
		"Workdir: %s\n"
		"Modified files:\n%s\n"
		"OpenCode stats:\n%s",
		reply ? reply : "",
		turn ? turn->prompt_tokens : 0,
		turn ? turn->completion_tokens : 0,
		turn ? turn->total_tokens : 0,
		total.prompt_tokens,
		total.completion_tokens,
		total.total_tokens,
		budget,
		cfg->workdir ? cfg->workdir : "(unset)",
		files,
		stats);
	free(files);
	free(stats);
	return out;
}

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static int handle_message(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg)
{
	char *final_reply;
	char *reply;
	hermes_usage_t usage;
	int restart_requested;
	int seen;
	int handled;

	final_reply = NULL;
	reply = NULL;
	memset(&usage, 0, sizeof(usage));
	restart_requested = 0;
	seen = 0;
	handled = 0;
	if (!sender_allowed(cfg, msg->from)) {
		fprintf(stderr, "skip: sender not allowlisted: %s\n", msg->from ? msg->from : "(null)");
		return 0;
	}
	if (db_seen_message(db, msg->message_id, &seen) < 0)
		return -1;
	if (seen)
		return 0;
	if (is_restart_intent(msg->body) || is_restart_intent(msg->subject)) {
		handled = 1;
		restart_requested = 1;
		reply = xstrndup("Restarting Hermes now.", 21);
		if (!reply)
			return -1;
	} else {
		if (tool_try_handle(cfg, db, msg, &reply, &usage, &handled) < 0)
			return -1;
		if (!handled) {
			free(reply);
			return 0;
		}
	}
	final_reply = append_metrics_footer(cfg, db, reply, &usage);
	if (!final_reply)
		final_reply = xstrndup(reply ? reply : "", strlen(reply ? reply : ""));
	if (!final_reply) {
		free(reply);
		return -1;
	}
	if (email_send(cfg, msg, final_reply) < 0) {
		free(final_reply);
		free(reply);
		return -1;
	}
	if (db_store_message(db, msg, final_reply) < 0) {
		free(final_reply);
		free(reply);
		return -1;
	}
	free(final_reply);
	free(reply);
	if (restart_requested && restart_hermes_service() < 0)
		fprintf(stderr, "restart: failed for thread %s\n", msg->thread_key ? msg->thread_key : "(null)");
	return 0;
}

static void run_loop(const hermes_config_t *cfg, hermes_db_t *db)
{
	hermes_message_t *msgs;
	size_t i;
	size_t len;

	while (!stop_flag) {
		msgs = NULL;
		len = 0;
		if (email_poll(cfg, &msgs, &len) == 0) {
			for (i = 0; i < len; i++)
				handle_message(cfg, db, &msgs[i]);
			email_messages_free(msgs, len);
		}
		sleep((unsigned int)cfg->poll_seconds);
	}
}

int main(void)
{
	hermes_config_t cfg;
	hermes_db_t db;

	memset(&cfg, 0, sizeof(cfg));
	memset(&db, 0, sizeof(db));

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

#ifdef HERMES_WITH_CURL
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
		fprintf(stderr, "curl: global init failed\n");
		return 1;
	}
#endif

	if (cfg_load(&cfg) < 0) {
		fprintf(stderr, "config: set HERMES_MAIL_FROM\n");
		return 1;
	}
	if (db_open(&db, cfg.db_path) < 0) {
		fprintf(stderr, "db: cannot open %s\n", cfg.db_path ? cfg.db_path : "(null)");
		cfg_free(&cfg);
		return 1;
	}

	fprintf(stderr, "hermesd: started, poll=%d sec\n", cfg.poll_seconds);
	run_loop(&cfg, &db);
	fprintf(stderr, "hermesd: stopping\n");

	db_close(&db);
	cfg_free(&cfg);
#ifdef HERMES_WITH_CURL
	curl_global_cleanup();
#endif
	return 0;
}
