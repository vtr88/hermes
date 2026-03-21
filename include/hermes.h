#ifndef HERMES_H
#define HERMES_H

#include <stddef.h>
typedef struct {
	long prompt_tokens;
	long completion_tokens;
	long total_tokens;
	double cost_usd;
} hermes_usage_t;

typedef struct {
	char *openai_key;
	char *openai_model;
	char *openai_url;
	char *openai_system;
	char *imap_url;
	char *smtp_url;
	char *mail_user;
	char *mail_pass;
	char *mail_from;
	char *mail_to;
	char *allow_from;
	char *workdir;
	char *db_path;
	int max_prompt_chars;
	int tool_timeout_sec;
	int poll_seconds;
	double budget_usd;
	double input_usd_per_mtok;
	double output_usd_per_mtok;
} hermes_config_t;

typedef struct {
	char *message_id;
	char *thread_key;
	char *subject;
	char *from;
	char *body;
} hermes_message_t;

typedef struct {
	char *path;
} hermes_db_t;

int cfg_load(hermes_config_t *cfg);
void cfg_free(hermes_config_t *cfg);

int db_open(hermes_db_t *db, const char *path);
void db_close(hermes_db_t *db);
int db_seen_message(hermes_db_t *db, const char *message_id, int *seen);
int db_store_message(hermes_db_t *db, const hermes_message_t *msg, const char *reply_text);
int db_thread_context(hermes_db_t *db, const char *thread_key, int max_items, char **out);
int db_pending_create(hermes_db_t *db, const char *thread_key, const char *token, const char *command,
	int needs_approval);
int db_pending_consume(hermes_db_t *db, const char *thread_key, const char *token, char **command_out,
	int *needs_approval_out);
int db_usage_add(hermes_db_t *db, const hermes_usage_t *usage);
int db_usage_get(hermes_db_t *db, hermes_usage_t *usage_out);
int db_session_get(hermes_db_t *db, const char *thread_key, char **session_id_out);
int db_session_set(hermes_db_t *db, const char *thread_key, const char *session_id);

int openai_generate(const hermes_config_t *cfg, const char *prompt, char **reply_out);
void openai_last_usage(hermes_usage_t *usage_out);

int tool_try_handle(const hermes_config_t *cfg, hermes_db_t *db, const hermes_message_t *msg,
	char **reply_out, int *handled_out);

int email_poll(const hermes_config_t *cfg, hermes_message_t **msgs_out, size_t *len_out);
int email_send(const hermes_config_t *cfg, const hermes_message_t *msg, const char *reply_text);
void email_messages_free(hermes_message_t *msgs, size_t len);

#endif
