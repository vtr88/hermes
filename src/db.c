#include "hermes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HERMES_WITH_SQLITE
#include <sqlite3.h>
#endif

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

static int append_fmt_pair(char **acc, const char *role, const char *text)
{
	int n;
	char *line;

	n = snprintf(NULL, 0, "%s:\n%s\n\n", role, text ? text : "");
	if (n < 0)
		return -1;
	line = malloc((size_t)n + 1);
	if (!line)
		return -1;
	snprintf(line, (size_t)n + 1, "%s:\n%s\n\n", role, text ? text : "");
	if (append_text(acc, line) < 0) {
		free(line);
		return -1;
	}
	free(line);
	return 0;
}

#ifndef HERMES_WITH_SQLITE
static int line_has_message_id(const char *line, const char *message_id)
{
	size_t n;

	n = strlen(message_id);
	if (strncmp(line, message_id, n) != 0)
		return 0;
	return line[n] == '\t';
}
#endif

int db_open(hermes_db_t *db, const char *path)
{
	if (!db || !path)
		return -1;
	db->path = xstrdup(path);
	if (!db->path)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		char *err;

		sql = NULL;
		err = NULL;
		rc = sqlite3_open(path, &sql);
		if (rc != SQLITE_OK) {
			if (sql)
				sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"CREATE TABLE IF NOT EXISTS messages(" \
			"message_id TEXT PRIMARY KEY," \
			"thread_key TEXT NOT NULL," \
			"subject TEXT NOT NULL DEFAULT ''," \
			"sender TEXT NOT NULL DEFAULT ''," \
			"body TEXT NOT NULL DEFAULT ''," \
			"reply_text TEXT NOT NULL DEFAULT ''," \
			"created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))" \
			");",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"CREATE INDEX IF NOT EXISTS idx_messages_thread_created "
			"ON messages(thread_key, created_at);",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"CREATE TABLE IF NOT EXISTS pending_actions(" \
			"thread_key TEXT NOT NULL," \
			"token TEXT NOT NULL," \
			"command TEXT NOT NULL," \
			"created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))," \
			"PRIMARY KEY(thread_key, token)" \
			");",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"CREATE TABLE IF NOT EXISTS usage_totals(" \
			"id INTEGER PRIMARY KEY CHECK (id = 1)," \
			"prompt_tokens INTEGER NOT NULL DEFAULT 0," \
			"completion_tokens INTEGER NOT NULL DEFAULT 0," \
			"total_tokens INTEGER NOT NULL DEFAULT 0," \
			"cost_usd REAL NOT NULL DEFAULT 0" \
			");",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"INSERT OR IGNORE INTO usage_totals(id, prompt_tokens, completion_tokens, total_tokens, cost_usd) "
			"VALUES(1, 0, 0, 0, 0);",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_exec(sql,
			"CREATE TABLE IF NOT EXISTS thread_sessions(" \
			"thread_key TEXT PRIMARY KEY," \
			"session_id TEXT NOT NULL DEFAULT ''" \
			");",
			NULL, NULL, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "db: %s\n", err ? err : "sql error");
			sqlite3_free(err);
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_close(sql);
		return 0;
	}
#else
	FILE *f;

	f = fopen(path, "a+");
	if (!f)
		return -1;
	fclose(f);
	return 0;
#endif
}

void db_close(hermes_db_t *db)
{
	if (!db)
		return;
	free(db->path);
	db->path = NULL;
}

int db_seen_message(hermes_db_t *db, const char *message_id, int *seen)
{
	if (!db || !db->path || !message_id || !seen)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;

		rc = sqlite3_prepare_v2(sql,
			"SELECT 1 FROM messages WHERE message_id = ?1 LIMIT 1;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_bind_text(st, 1, message_id, -1, SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			sqlite3_finalize(st);
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_step(st);
		*seen = (rc == SQLITE_ROW);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return 0;
	}
#else
	FILE *f;
	char line[8192];

	f = fopen(db->path, "r");
	if (!f)
		return -1;

	*seen = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line_has_message_id(line, message_id)) {
			*seen = 1;
			break;
		}
	}
	fclose(f);
	return 0;
#endif
}

int db_store_message(hermes_db_t *db, const hermes_message_t *msg, const char *reply_text)
{
	if (!db || !db->path || !msg || !msg->message_id || !msg->thread_key)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;

		rc = sqlite3_prepare_v2(sql,
			"INSERT OR IGNORE INTO messages(message_id, thread_key, subject, sender, body, reply_text) "
			"VALUES(?1, ?2, ?3, ?4, ?5, ?6);",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}

		sqlite3_bind_text(st, 1, msg->message_id, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, msg->thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 3, msg->subject ? msg->subject : "", -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 4, msg->from ? msg->from : "", -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 5, msg->body ? msg->body : "", -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 6, reply_text ? reply_text : "", -1, SQLITE_STATIC);

		rc = sqlite3_step(st);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		if (rc != SQLITE_DONE)
			return -1;
		return 0;
	}
#else
	FILE *f;

	f = fopen(db->path, "a");
	if (!f)
		return -1;
	fprintf(f, "%s\t%s\t%s\n",
		msg->message_id,
		msg->thread_key,
		reply_text ? reply_text : "");
	fclose(f);
	return 0;
#endif
}

int db_thread_context(hermes_db_t *db, const char *thread_key, int max_items, char **out)
{
	if (!db || !out)
		return -1;
	*out = NULL;
	if (!thread_key || max_items <= 0)
		return 0;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;
		char *acc;

		acc = NULL;
		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;

		rc = sqlite3_prepare_v2(sql,
			"SELECT body, reply_text FROM messages WHERE thread_key = ?1 "
			"ORDER BY created_at DESC LIMIT ?2;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_int(st, 2, max_items);

		while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
			const char *body;
			const char *reply;

			body = (const char *)sqlite3_column_text(st, 0);
			reply = (const char *)sqlite3_column_text(st, 1);
			if (append_fmt_pair(&acc, "Previous user", body ? body : "") < 0) {
				sqlite3_finalize(st);
				sqlite3_close(sql);
				free(acc);
				return -1;
			}
			if (reply && *reply && append_fmt_pair(&acc, "Previous assistant", reply) < 0) {
				sqlite3_finalize(st);
				sqlite3_close(sql);
				free(acc);
				return -1;
			}
		}

		sqlite3_finalize(st);
		sqlite3_close(sql);
		if (rc != SQLITE_DONE) {
			free(acc);
			return -1;
		}
		*out = acc;
		return 0;
	}
#else
	(void)db;
	(void)thread_key;
	(void)max_items;
	return 0;
#endif
}

int db_pending_create(hermes_db_t *db, const char *thread_key, const char *token, const char *command)
{
	if (!db || !thread_key || !token || !command)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;

		rc = sqlite3_prepare_v2(sql,
			"INSERT OR REPLACE INTO pending_actions(thread_key, token, command) VALUES(?1, ?2, ?3);",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, token, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 3, command, -1, SQLITE_STATIC);
		rc = sqlite3_step(st);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return rc == SQLITE_DONE ? 0 : -1;
	}
#else
	return -1;
#endif
}

int db_pending_consume(hermes_db_t *db, const char *thread_key, const char *token, char **command_out)
{
	if (!db || !thread_key || !token || !command_out)
		return -1;
	*command_out = NULL;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;
		const char *cmd;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;

		rc = sqlite3_prepare_v2(sql,
			"SELECT command FROM pending_actions WHERE thread_key = ?1 AND token = ?2 LIMIT 1;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, token, -1, SQLITE_STATIC);
		rc = sqlite3_step(st);
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(st);
			sqlite3_close(sql);
			return 1;
		}
		cmd = (const char *)sqlite3_column_text(st, 0);
		*command_out = xstrdup(cmd ? cmd : "");
		sqlite3_finalize(st);
		if (!*command_out) {
			sqlite3_close(sql);
			return -1;
		}

		rc = sqlite3_prepare_v2(sql,
			"DELETE FROM pending_actions WHERE thread_key = ?1 AND token = ?2;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			free(*command_out);
			*command_out = NULL;
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, token, -1, SQLITE_STATIC);
		rc = sqlite3_step(st);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return rc == SQLITE_DONE ? 0 : -1;
	}
#else
	return 1;
#endif
}

int db_usage_add(hermes_db_t *db, const hermes_usage_t *usage)
{
	if (!db || !usage)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;
		rc = sqlite3_prepare_v2(sql,
			"UPDATE usage_totals SET "
			"prompt_tokens = prompt_tokens + ?1, "
			"completion_tokens = completion_tokens + ?2, "
			"total_tokens = total_tokens + ?3, "
			"cost_usd = cost_usd + ?4 "
			"WHERE id = 1;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_int64(st, 1, usage->prompt_tokens);
		sqlite3_bind_int64(st, 2, usage->completion_tokens);
		sqlite3_bind_int64(st, 3, usage->total_tokens);
		sqlite3_bind_double(st, 4, usage->cost_usd);
		rc = sqlite3_step(st);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return rc == SQLITE_DONE ? 0 : -1;
	}
#else
	return 0;
#endif
}

int db_usage_get(hermes_db_t *db, hermes_usage_t *usage_out)
{
	if (!db || !usage_out)
		return -1;
	memset(usage_out, 0, sizeof(*usage_out));

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;
		rc = sqlite3_prepare_v2(sql,
			"SELECT prompt_tokens, completion_tokens, total_tokens, cost_usd "
			"FROM usage_totals WHERE id = 1;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		rc = sqlite3_step(st);
		if (rc == SQLITE_ROW) {
			usage_out->prompt_tokens = sqlite3_column_int64(st, 0);
			usage_out->completion_tokens = sqlite3_column_int64(st, 1);
			usage_out->total_tokens = sqlite3_column_int64(st, 2);
			usage_out->cost_usd = sqlite3_column_double(st, 3);
		}
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return 0;
	}
#else
	return 0;
#endif
}

int db_session_get(hermes_db_t *db, const char *thread_key, char **session_id_out)
{
	if (!db || !thread_key || !session_id_out)
		return -1;
	*session_id_out = NULL;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;
		const char *sid;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;
		rc = sqlite3_prepare_v2(sql,
			"SELECT session_id FROM thread_sessions WHERE thread_key = ?1 LIMIT 1;",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		rc = sqlite3_step(st);
		if (rc == SQLITE_ROW) {
			sid = (const char *)sqlite3_column_text(st, 0);
			if (sid && *sid)
				*session_id_out = xstrdup(sid);
		}
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return 0;
	}
#else
	return 0;
#endif
}

int db_session_set(hermes_db_t *db, const char *thread_key, const char *session_id)
{
	if (!db || !thread_key || !session_id)
		return -1;

#ifdef HERMES_WITH_SQLITE
	{
		int rc;
		sqlite3 *sql;
		sqlite3_stmt *st;

		sql = NULL;
		st = NULL;
		rc = sqlite3_open(db->path, &sql);
		if (rc != SQLITE_OK)
			return -1;
		rc = sqlite3_prepare_v2(sql,
			"INSERT OR REPLACE INTO thread_sessions(thread_key, session_id) VALUES(?1, ?2);",
			-1, &st, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(sql);
			return -1;
		}
		sqlite3_bind_text(st, 1, thread_key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, session_id, -1, SQLITE_STATIC);
		rc = sqlite3_step(st);
		sqlite3_finalize(st);
		sqlite3_close(sql);
		return rc == SQLITE_DONE ? 0 : -1;
	}
#else
	return 0;
#endif
}
