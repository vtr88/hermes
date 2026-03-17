#include "hermes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HERMES_WITH_CURL
#include <curl/curl.h>
#endif

#ifdef HERMES_WITH_CURL
typedef struct {
	char *buf;
	size_t len;
} mem_t;
#endif

#ifdef HERMES_WITH_CURL
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

static char *json_escape(const char *s)
{
	char *out;
	const char *p;
	char *w;
	size_t cap;

	if (!s)
		return xstrdup("");
	cap = strlen(s) * 2 + 1;
	out = malloc(cap);
	if (!out)
		return NULL;
	w = out;

	for (p = s; *p; p++) {
		if ((size_t)(w - out + 3) >= cap) {
			char *next;

			cap *= 2;
			next = realloc(out, cap);
			if (!next) {
				free(out);
				return NULL;
			}
			w = next + (w - out);
			out = next;
		}
		switch (*p) {
		case '\\':
			*w++ = '\\';
			*w++ = '\\';
			break;
		case '"':
			*w++ = '\\';
			*w++ = '"';
			break;
		case '\n':
			*w++ = '\\';
			*w++ = 'n';
			break;
		case '\r':
			*w++ = '\\';
			*w++ = 'r';
			break;
		case '\t':
			*w++ = '\\';
			*w++ = 't';
			break;
		default:
			*w++ = *p;
		}
	}
	*w = '\0';
	return out;
}

static char *extract_output_text(const char *json)
{
	const char *k;
	const char *p;
	char *out;
	char *w;

	if (!json)
		return NULL;
	k = "\"output_text\":\"";
	p = strstr(json, k);
	if (!p)
		return NULL;
	p += strlen(k);
	out = malloc(strlen(p) + 1);
	if (!out)
		return NULL;
	w = out;

	while (*p) {
		if (*p == '\\') {
			p++;
			if (*p == 'n')
				*w++ = '\n';
			else if (*p == 'r')
				*w++ = '\r';
			else if (*p == 't')
				*w++ = '\t';
			else if (*p == '"')
				*w++ = '"';
			else if (*p == '\\')
				*w++ = '\\';
			else if (*p == '\0')
				break;
			else
				*w++ = *p;
			if (*p)
				p++;
			continue;
		}
		if (*p == '"')
			break;
		*w++ = *p++;
	}
	*w = '\0';
	return out;
}

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
#endif

int openai_generate(const hermes_config_t *cfg, const char *prompt, char **reply_out)
{
	const char *p;

	if (!cfg || !cfg->openai_key || !reply_out)
		return -1;
	p = prompt ? prompt : "";

#ifdef HERMES_WITH_CURL
	{
		CURL *c;
		CURLcode rc;
		char *esc;
		char *body;
		char auth[1024];
		mem_t resp;
		struct curl_slist *hdrs;
		char *text;
		int n;

		*reply_out = NULL;
		esc = json_escape(p);
		if (!esc)
			return -1;

		n = snprintf(NULL, 0,
			"{\"model\":\"%s\",\"input\":\"%s\"}",
			cfg->openai_model,
			esc);
		if (n < 0) {
			free(esc);
			return -1;
		}
		body = malloc((size_t)n + 1);
		if (!body) {
			free(esc);
			return -1;
		}
		snprintf(body, (size_t)n + 1,
			"{\"model\":\"%s\",\"input\":\"%s\"}",
			cfg->openai_model,
			esc);
		free(esc);

		resp.buf = NULL;
		resp.len = 0;
		hdrs = NULL;
		c = curl_easy_init();
		if (!c) {
			free(body);
			return -1;
		}

		snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->openai_key);
		hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
		hdrs = curl_slist_append(hdrs, auth);
		curl_easy_setopt(c, CURLOPT_URL, cfg->openai_url);
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);

		rc = curl_easy_perform(c);
		curl_slist_free_all(hdrs);
		curl_easy_cleanup(c);
		free(body);
		if (rc != CURLE_OK) {
			fprintf(stderr, "openai: %s\n", curl_easy_strerror(rc));
			free(resp.buf);
			return -1;
		}

		text = extract_output_text(resp.buf);
		if (!text)
			text = resp.buf ? xstrdup(resp.buf) : xstrdup("");
		free(resp.buf);
		if (!text)
			return -1;
		*reply_out = text;
		return 0;
	}
#else
	{
		char *out;
		size_t n;

		n = strlen(p) + 160;
		out = malloc(n);
		if (!out)
			return -1;
		snprintf(out, n,
			"[hermes] built without libcurl.\nInstall libcurl dev package, then rebuild.\nPrompt:\n%s\n",
			p);
		*reply_out = out;
		return 0;
	}
#endif
}
