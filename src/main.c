#include "hermes.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HERMES_WITH_CURL
#include <curl/curl.h>
#endif

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static int handle_message(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg)
{
	char *reply;
	int seen;

	reply = NULL;
	seen = 0;
	if (db_seen_message(db, msg->message_id, &seen) < 0)
		return -1;
	if (seen)
		return 0;

	if (openai_generate(cfg, msg->body, &reply) < 0)
		return -1;
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
