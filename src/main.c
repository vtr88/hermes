#include "hermes.h"

#include <signal.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HERMES_WITH_CURL
#include <curl/curl.h>
#endif

static volatile sig_atomic_t stop_flag;

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

static char *clamped_prompt(const hermes_config_t *cfg, const char *body)
{
	char *p;
	size_t n;

	if (!body)
		return xstrndup("", 0);
	n = strlen(body);
	if (cfg && cfg->max_prompt_chars > 0 && n > (size_t)cfg->max_prompt_chars)
		n = (size_t)cfg->max_prompt_chars;
	p = xstrndup(body, n);
	if (!p)
		return NULL;
	return p;
}

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static int handle_message(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg)
{
	char *prompt;
	char *reply;
	int seen;

	prompt = NULL;
	reply = NULL;
	seen = 0;
	if (!sender_allowed(cfg, msg->from)) {
		fprintf(stderr, "skip: sender not allowlisted: %s\n", msg->from ? msg->from : "(null)");
		return 0;
	}
	if (db_seen_message(db, msg->message_id, &seen) < 0)
		return -1;
	if (seen)
		return 0;

	prompt = clamped_prompt(cfg, msg->body);
	if (!prompt)
		return -1;
	if (openai_generate(cfg, prompt, &reply) < 0) {
		free(prompt);
		return -1;
}
	free(prompt);
	if (email_send(cfg, msg, reply) < 0) {
		free(reply);
		return -1;
	}
	if (db_store_message(db, msg, reply) < 0) {
		free(reply);
		return -1;
	}
	free(reply);
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
		fprintf(stderr, "config: set HERMES_OPENAI_KEY and HERMES_MAIL_FROM\n");
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
