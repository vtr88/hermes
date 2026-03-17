#define _POSIX_C_SOURCE 200809L

#include "hermes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_missing_required(void)
{
	hermes_config_t cfg;
	int rc;

	unsetenv("HERMES_OPENAI_KEY");
	setenv("HERMES_MAIL_FROM", "bot@example.com", 1);

	rc = cfg_load(&cfg);
	if (rc == 0) {
		cfg_free(&cfg);
		return -1;
	}
	return 0;
}

static int test_defaults(void)
{
	hermes_config_t cfg;
	int rc;

	setenv("HERMES_OPENAI_KEY", "k", 1);
	setenv("HERMES_MAIL_FROM", "bot@example.com", 1);
	unsetenv("HERMES_OPENAI_MODEL");

	rc = cfg_load(&cfg);
	if (rc < 0)
		return -1;
	if (!cfg.openai_model || strcmp(cfg.openai_model, "gpt-5.3-codex") != 0) {
		cfg_free(&cfg);
		return -1;
	}
	cfg_free(&cfg);
	return 0;
}

int main(int argc, char **argv)
{
	int fail;

	fail = 0;
	if (argc == 1 || strcmp(argv[1], "test_missing_required") == 0)
		fail |= test_missing_required() < 0;
	if (argc == 1 || strcmp(argv[1], "test_defaults") == 0)
		fail |= test_defaults() < 0;

	if (fail) {
		fprintf(stderr, "test_config: FAIL\n");
		return 1;
	}
	fprintf(stderr, "test_config: OK\n");
	return 0;
}
