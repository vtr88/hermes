#include "hermes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *envdup(const char *key)
{
	const char *v;

	v = getenv(key);
	if (!v || !*v)
		return NULL;
	return xstrdup(v);
}

static char *dup_default(const char *value)
{
	if (!value)
		return NULL;
	return xstrdup(value);
}

static char *derive_workdir_from_mail_user(const char *mail_user)
{
	const char *at;
	char *name;
	char *path;
	int n;

	if (!mail_user || !*mail_user)
		return NULL;
	at = strchr(mail_user, '@');
	if (!at || at == mail_user)
		return NULL;
	name = xstrdup(mail_user);
	if (!name)
		return NULL;
	name[at - mail_user] = '\0';
	n = snprintf(NULL, 0, "/home/%s/Projects/%s", name, name);
	if (n < 0) {
		free(name);
		return NULL;
	}
	path = malloc((size_t)n + 1);
	if (!path) {
		free(name);
		return NULL;
	}
	snprintf(path, (size_t)n + 1, "/home/%s/Projects/%s", name, name);
	free(name);
	return path;
}

int cfg_load(hermes_config_t *cfg)
{
	if (!cfg)
		return -1;

	memset(cfg, 0, sizeof(*cfg));
	cfg->opencode_session_id = envdup("HERMES_OPENCODE_SESSION_ID");
	cfg->imap_url = envdup("HERMES_IMAP_URL");
	cfg->smtp_url = envdup("HERMES_SMTP_URL");
	cfg->mail_user = envdup("HERMES_MAIL_USER");
	cfg->mail_pass = envdup("HERMES_MAIL_PASS");
	cfg->mail_from = envdup("HERMES_MAIL_FROM");
	cfg->mail_to = envdup("HERMES_MAIL_TO");
	cfg->allow_from = envdup("HERMES_ALLOW_FROM");
	cfg->workdir = envdup("HERMES_WORKDIR");
	if (!cfg->workdir)
		cfg->workdir = derive_workdir_from_mail_user(cfg->mail_user);
	if (!cfg->workdir)
		cfg->workdir = dup_default("/srv/hermes");
	cfg->db_path = envdup("HERMES_DB_PATH");
	if (!cfg->db_path)
		cfg->db_path = dup_default("build/hermes.db");

	cfg->tool_timeout_sec = 0;
	{
		char *t;

		t = envdup("HERMES_TOOL_TIMEOUT_SEC");
		if (t) {
			int n;

			n = atoi(t);
			if (n == 0 || (n >= 5 && n <= 86400))
				cfg->tool_timeout_sec = n;
			free(t);
		}
	}

	cfg->poll_seconds = 30;
	{
		char *p;

		p = envdup("HERMES_POLL_SECONDS");
		if (p) {
			int n;

			n = atoi(p);
			if (n > 0)
				cfg->poll_seconds = n;
			free(p);
		}
	}

	cfg->budget_usd = 0.0;
	{
		char *b;

		b = envdup("HERMES_BUDGET_USD");
		if (b) {
			cfg->budget_usd = strtod(b, NULL);
			free(b);
		}
	}

	if (!cfg->mail_from) {
		cfg_free(cfg);
		return -1;
	}
	return 0;
}

void cfg_free(hermes_config_t *cfg)
{
	if (!cfg)
		return;

	free(cfg->opencode_session_id);
	free(cfg->imap_url);
	free(cfg->smtp_url);
	free(cfg->mail_user);
	free(cfg->mail_pass);
	free(cfg->mail_from);
	free(cfg->mail_to);
	free(cfg->allow_from);
	free(cfg->workdir);
	free(cfg->db_path);
	memset(cfg, 0, sizeof(*cfg));
}
