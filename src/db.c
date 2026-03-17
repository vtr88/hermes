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
