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
	cfg->openai_key = envdup("HERMES_OPENAI_KEY");
	cfg->openai_model = envdup("HERMES_OPENAI_MODEL");
	if (!cfg->openai_model)
		cfg->openai_model = dup_default("gpt-5.3-codex");
	cfg->openai_url = envdup("HERMES_OPENAI_URL");
	if (!cfg->openai_url)
		cfg->openai_url = dup_default("https://api.openai.com/v1/responses");
	cfg->openai_system = envdup("HERMES_OPENAI_SYSTEM");
	cfg->opencode_session_id = envdup("HERMES_OPENCODE_SESSION_ID");
	if (!cfg->openai_system)
		cfg->openai_system = dup_default(
			"You are Hermes, an email coding assistant. "
			"Reply in plain text, not JSON. "
			"Be concise, helpful, and practical like a terminal coding assistant.");
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

	cfg->max_prompt_chars = 12000;
	{
		char *m;

		m = envdup("HERMES_MAX_PROMPT_CHARS");
		if (m) {
			int n;

			n = atoi(m);
			if (n > 1000)
				cfg->max_prompt_chars = n;
			free(m);
		}
	}

	cfg->tool_timeout_sec = 30;
	{
		char *t;

		t = envdup("HERMES_TOOL_TIMEOUT_SEC");
		if (t) {
			int n;

			n = atoi(t);
			if (n >= 5 && n <= 600)
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

	cfg->input_usd_per_mtok = 0.0;
	{
		char *v;

		v = envdup("HERMES_INPUT_USD_PER_MTOK");
		if (v) {
			cfg->input_usd_per_mtok = strtod(v, NULL);
			free(v);
		}
	}

	cfg->output_usd_per_mtok = 0.0;
	{
		char *v;

		v = envdup("HERMES_OUTPUT_USD_PER_MTOK");
		if (v) {
			cfg->output_usd_per_mtok = strtod(v, NULL);
			free(v);
		}
	}

	if (!cfg->openai_key || !cfg->mail_from) {
		cfg_free(cfg);
		return -1;
	}
	return 0;
}

void cfg_free(hermes_config_t *cfg)
{
	if (!cfg)
		return;

	free(cfg->openai_key);
	free(cfg->openai_model);
	free(cfg->openai_url);
	free(cfg->openai_system);
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
