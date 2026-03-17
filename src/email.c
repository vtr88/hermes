#include "hermes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef HERMES_WITH_CURL
#include <curl/curl.h>
#endif

typedef struct {
	char *buf;
	size_t len;
} mem_t;

typedef struct {
	const char *data;
	size_t len;
	size_t at;
} payload_t;

#ifdef HERMES_WITH_CURL
static size_t payload_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
	payload_t *p;
	size_t want;
	size_t left;

	p = ud;
	want = size * nmemb;
	left = p->len - p->at;
	if (left == 0)
		return 0;
	if (want > left)
		want = left;
	memcpy(ptr, p->data + p->at, want);
	p->at += want;
	return want;
}
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

static void trim_ascii(char *s)
{
	char *e;

	if (!s || !*s)
		return;
	e = s + strlen(s) - 1;
	while (e >= s && isspace((unsigned char)*e))
		*e-- = '\0';
	while (*s && isspace((unsigned char)*s))
		memmove(s, s + 1, strlen(s));
}

static char *extract_angle_id(const char *s)
{
	const char *a;
	const char *b;

	if (!s)
		return NULL;
	a = strchr(s, '<');
	b = strchr(s, '>');
	if (!a || !b || b <= a)
		return xstrdup(s);
	return xstrndup(a, (size_t)(b - a + 1));
}

static char *header_value(const char *headers, const char *name)
{
	const char *p;
	size_t nlen;

	if (!headers || !name)
		return NULL;
	nlen = strlen(name);
	p = headers;
	while (*p) {
		const char *line;
		const char *end;
		char *v;

		line = p;
		end = strstr(line, "\n");
		if (!end)
			end = line + strlen(line);
		if ((size_t)(end - line) > nlen + 1 && strncasecmp(line, name, nlen) == 0 && line[nlen] == ':') {
			v = xstrndup(line + nlen + 1, (size_t)(end - (line + nlen + 1)));
			if (!v)
				return NULL;
			trim_ascii(v);
			return v;
		}
		p = *end ? end + 1 : end;
	}
	return NULL;
}

#ifdef HERMES_WITH_CURL
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
	char *next;
	mem_t *m;
	size_t n;

	m = ud;
	n = size * nmemb;
	next = realloc(m->buf, m->len + n + 1);
	if (!next)
		return 0;
	m->buf = next;
	memcpy(m->buf + m->len, ptr, n);
	m->len += n;
	m->buf[m->len] = '\0';
	return n;
}

static int imap_exec(const hermes_config_t *cfg, const char *url, const char *cmd, char **out)
{
	CURL *c;
	CURLcode rc;
	mem_t resp;

	if (!cfg || !url || !out)
		return -1;
	*out = NULL;
	resp.buf = NULL;
	resp.len = 0;

	c = curl_easy_init();
	if (!c)
		return -1;

	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERNAME, cfg->mail_user);
	curl_easy_setopt(c, CURLOPT_PASSWORD, cfg->mail_pass);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	if (cmd)
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, cmd);

	rc = curl_easy_perform(c);
	curl_easy_cleanup(c);
	if (rc != CURLE_OK) {
		free(resp.buf);
		fprintf(stderr, "imap: %s\n", curl_easy_strerror(rc));
		return -1;
	}
	*out = resp.buf ? resp.buf : xstrdup("");
	return *out ? 0 : -1;
}

static int parse_search_uids(const char *s, unsigned long **uids_out, size_t *n_out)
{
	const char *p;
	unsigned long *uids;
	size_t n;

	if (!s || !uids_out || !n_out)
		return -1;
	*uids_out = NULL;
	*n_out = 0;
	uids = NULL;
	n = 0;
	p = s;

	while (*p) {
		if (isdigit((unsigned char)*p)) {
			char *ep;
			unsigned long v;
			unsigned long *next;

			v = strtoul(p, &ep, 10);
			if (ep == p) {
				p++;
				continue;
			}
			next = realloc(uids, (n + 1) * sizeof(*uids));
			if (!next) {
				free(uids);
				return -1;
			}
			uids = next;
			uids[n++] = v;
			p = ep;
			continue;
		}
		p++;
	}

	*uids_out = uids;
	*n_out = n;
	return 0;
}

static int build_uid_url(const char *base, unsigned long uid, const char *section, char **url_out)
{
	char *u;
	int n;

	if (!base || !section || !url_out)
		return -1;
	n = snprintf(NULL, 0, "%s/;UID=%lu/;SECTION=%s", base, uid, section);
	if (n < 0)
		return -1;
	u = malloc((size_t)n + 1);
	if (!u)
		return -1;
	snprintf(u, (size_t)n + 1, "%s/;UID=%lu/;SECTION=%s", base, uid, section);
	*url_out = u;
	return 0;
}

static int message_from_uid(const hermes_config_t *cfg, unsigned long uid, hermes_message_t *m)
{
	char *hurl;
	char *burl;
	char *headers;
	char *body;
	char *msgid;
	char *refs;
	char *ireply;
	char *subject;
	char *from;

	if (!cfg || !m)
		return -1;
	memset(m, 0, sizeof(*m));
	hurl = NULL;
	burl = NULL;
	headers = NULL;
	body = NULL;
	msgid = NULL;
	refs = NULL;
	ireply = NULL;
	subject = NULL;
	from = NULL;

	if (build_uid_url(cfg->imap_url, uid, "HEADER", &hurl) < 0)
		goto fail;
	if (build_uid_url(cfg->imap_url, uid, "TEXT", &burl) < 0)
		goto fail;
	if (imap_exec(cfg, hurl, NULL, &headers) < 0)
		goto fail;
	if (imap_exec(cfg, burl, NULL, &body) < 0)
		goto fail;

	msgid = header_value(headers, "Message-ID");
	if (!msgid)
		msgid = header_value(headers, "Message-Id");
	if (!msgid)
		msgid = xstrdup("<missing-message-id>");
	if (!msgid)
		goto fail;

	subject = header_value(headers, "Subject");
	if (!subject)
		subject = xstrdup("Hermes");
	if (!subject)
		goto fail;

	from = header_value(headers, "From");
	if (!from)
		from = xstrdup(cfg->mail_from ? cfg->mail_from : "unknown");
	if (!from)
		goto fail;

	refs = header_value(headers, "References");
	ireply = header_value(headers, "In-Reply-To");

	m->message_id = extract_angle_id(msgid);
	if (!m->message_id)
		goto fail;
	if (refs && *refs)
		m->thread_key = extract_angle_id(refs);
	else if (ireply && *ireply)
		m->thread_key = extract_angle_id(ireply);
	else
		m->thread_key = xstrdup(m->message_id);
	if (!m->thread_key)
		goto fail;

	m->subject = subject;
	subject = NULL;
	m->from = from;
	from = NULL;
	m->body = body;
	body = NULL;

	free(hurl);
	free(burl);
	free(headers);
	free(msgid);
	free(refs);
	free(ireply);
	free(subject);
	free(from);
	return 0;

fail:
	free(hurl);
	free(burl);
	free(headers);
	free(body);
	free(msgid);
	free(refs);
	free(ireply);
	free(subject);
	free(from);
	email_messages_free(m, 1);
	memset(m, 0, sizeof(*m));
	return -1;
}
#endif

int email_poll(const hermes_config_t *cfg, hermes_message_t **msgs_out, size_t *len_out)
{
	if (!msgs_out || !len_out)
		return -1;
	*msgs_out = NULL;
	*len_out = 0;

#ifdef HERMES_WITH_CURL
	if (cfg && cfg->imap_url && cfg->mail_user && cfg->mail_pass) {
		char *search;
		unsigned long *uids;
		size_t n;
		size_t i;
		hermes_message_t *msgs;
		size_t outn;

		search = NULL;
		uids = NULL;
		n = 0;
		msgs = NULL;
		outn = 0;
		if (imap_exec(cfg, cfg->imap_url, "SEARCH UNSEEN", &search) < 0)
			return -1;
		if (parse_search_uids(search, &uids, &n) < 0) {
			free(search);
			return -1;
		}
		free(search);
		if (n == 0) {
			free(uids);
			return 0;
		}

		msgs = calloc(n, sizeof(*msgs));
		if (!msgs) {
			free(uids);
			return -1;
		}
		for (i = 0; i < n; i++) {
			if (message_from_uid(cfg, uids[i], &msgs[outn]) == 0)
				outn++;
		}
		free(uids);
		if (outn == 0) {
			free(msgs);
			return 0;
		}
		*msgs_out = msgs;
		*len_out = outn;
		return 0;
	}
#else
	(void)cfg;
#endif

	return 0;
}

int email_send(const hermes_config_t *cfg, const hermes_message_t *msg, const char *reply_text)
{
	const char *subject;
	const char *to;
	const char *body;

	if (!cfg || !cfg->mail_from)
		return -1;

	subject = msg && msg->subject ? msg->subject : "Hermes";
	to = cfg->mail_to ? cfg->mail_to : (msg && msg->from ? msg->from : cfg->mail_from);
	body = reply_text ? reply_text : "";

#ifdef HERMES_WITH_CURL
	if (cfg->smtp_url && cfg->mail_user && cfg->mail_pass) {
		char *mail;
		int n;
		CURL *c;
		CURLcode rc;
		payload_t p;
		struct curl_slist *rcpt;

		n = snprintf(NULL, 0,
			"From: <%s>\r\n"
			"To: <%s>\r\n"
			"Subject: Re: %s\r\n"
			"In-Reply-To: %s\r\n"
			"References: %s\r\n"
			"Content-Type: text/plain; charset=utf-8\r\n"
			"\r\n"
			"%s\r\n",
			cfg->mail_from,
			to,
			subject,
			(msg && msg->message_id) ? msg->message_id : "",
			(msg && msg->thread_key) ? msg->thread_key : ((msg && msg->message_id) ? msg->message_id : ""),
			body);
		if (n < 0)
			return -1;
		mail = malloc((size_t)n + 1);
		if (!mail)
			return -1;
		snprintf(mail, (size_t)n + 1,
			"From: <%s>\r\n"
			"To: <%s>\r\n"
			"Subject: Re: %s\r\n"
			"In-Reply-To: %s\r\n"
			"References: %s\r\n"
			"Content-Type: text/plain; charset=utf-8\r\n"
			"\r\n"
			"%s\r\n",
			cfg->mail_from,
			to,
			subject,
			(msg && msg->message_id) ? msg->message_id : "",
			(msg && msg->thread_key) ? msg->thread_key : ((msg && msg->message_id) ? msg->message_id : ""),
			body);

		p.data = mail;
		p.len = strlen(mail);
		p.at = 0;

		c = curl_easy_init();
		if (!c) {
			free(mail);
			return -1;
		}

		rcpt = NULL;
		rcpt = curl_slist_append(rcpt, to);
		curl_easy_setopt(c, CURLOPT_URL, cfg->smtp_url);
		curl_easy_setopt(c, CURLOPT_USERNAME, cfg->mail_user);
		curl_easy_setopt(c, CURLOPT_PASSWORD, cfg->mail_pass);
		curl_easy_setopt(c, CURLOPT_MAIL_FROM, cfg->mail_from);
		curl_easy_setopt(c, CURLOPT_MAIL_RCPT, rcpt);
		curl_easy_setopt(c, CURLOPT_READFUNCTION, payload_cb);
		curl_easy_setopt(c, CURLOPT_READDATA, &p);
		curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(c, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);

		rc = curl_easy_perform(c);
		curl_slist_free_all(rcpt);
		curl_easy_cleanup(c);
		free(mail);
		if (rc != CURLE_OK) {
			fprintf(stderr, "smtp: %s\n", curl_easy_strerror(rc));
			return -1;
		}
		return 0;
	}
#endif

	{
		FILE *f;

		f = fopen("build/outbox.log", "a");
		if (!f)
			return -1;
		fprintf(f, "FROM=%s\nTO=%s\nSUBJECT=Re: %s\n\n%s\n---\n",
			cfg->mail_from,
			to,
			subject,
			body);
		fclose(f);
	}
	return 0;
}

void email_messages_free(hermes_message_t *msgs, size_t len)
{
	size_t i;

	if (!msgs)
		return;
	for (i = 0; i < len; i++) {
		free(msgs[i].message_id);
		free(msgs[i].thread_key);
		free(msgs[i].subject);
		free(msgs[i].from);
		free(msgs[i].body);
	}
	free(msgs);
}
