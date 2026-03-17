#ifndef HERMES_H
#define HERMES_H

#include <stddef.h>
typedef struct {
	char *openai_key;
	char *openai_model;
	char *openai_url;
	char *imap_url;
	char *smtp_url;
	char *mail_user;
	char *mail_pass;
	char *mail_from;
	char *mail_to;
	char *db_path;
	int poll_seconds;
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

int openai_generate(const hermes_config_t *cfg, const char *prompt, char **reply_out);

int email_poll(const hermes_config_t *cfg, hermes_message_t **msgs_out, size_t *len_out);
int email_send(const hermes_config_t *cfg, const hermes_message_t *msg, const char *reply_text);
void email_messages_free(hermes_message_t *msgs, size_t len);

#endif
