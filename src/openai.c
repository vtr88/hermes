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

static char *json_unescape_quoted(const char *p, const char **endp)
{
	char *out;
	char *w;

	out = malloc(strlen(p) + 1);
	if (!out)
		return NULL;
	w = out;
	while (*p) {
		if (*p == '"') {
			if (endp)
				*endp = p + 1;
			*w = '\0';
			return out;
		}
		if (*p == '\\') {
			p++;
			if (!*p)
				break;
			switch (*p) {
			case 'n':
				*w++ = '\n';
				break;
			case 'r':
				*w++ = '\r';
				break;
			case 't':
				*w++ = '\t';
				break;
			case '"':
				*w++ = '"';
				break;
			case '\\':
				*w++ = '\\';
				break;
			default:
				*w++ = *p;
				break;
			}
			p++;
			continue;
		}
		*w++ = *p++;
	}
	if (endp)
		*endp = p;
	*w = '\0';
	return out;
}

static int append_line(char **acc, const char *line)
{
	char *next;
	size_t a;
	size_t l;

	if (!acc || !line)
		return -1;
	a = *acc ? strlen(*acc) : 0;
	l = strlen(line);
	next = realloc(*acc, a + l + 3);
	if (!next)
		return -1;
	*acc = next;
	memcpy(*acc + a, line, l);
	a += l;
	(*acc)[a++] = '\n';
	(*acc)[a] = '\0';
	return 0;
}

static char *extract_output_text(const char *json)
{
	const char *p;
	char *acc;

	if (!json)
		return NULL;

	p = strstr(json, "\"output_text\":\"");
	if (p) {
		p += strlen("\"output_text\":\"");
		return json_unescape_quoted(p, NULL);
	}

	acc = NULL;
	p = json;
	while ((p = strstr(p, "\"type\":\"output_text\"")) != NULL) {
		const char *t;
		const char *end;
		char *line;

		t = strstr(p, "\"text\":\"");
		if (!t)
			break;
		t += strlen("\"text\":\"");
		line = json_unescape_quoted(t, &end);
		if (!line) {
			free(acc);
			return NULL;
		}
		if (*line && append_line(&acc, line) < 0) {
			free(line);
			free(acc);
			return NULL;
		}
		free(line);
		p = end;
	}

	if (acc && *acc)
		return acc;
	free(acc);
	return NULL;
}

static char *build_input(const hermes_config_t *cfg, const char *prompt)
{
	int n;
	char *out;

	n = snprintf(NULL, 0,
		"System:\n%s\n\nUser:\n%s\n\nAssistant:",
		cfg->openai_system ? cfg->openai_system : "",
		prompt ? prompt : "");
	if (n < 0)
		return NULL;
	out = malloc((size_t)n + 1);
	if (!out)
		return NULL;
	snprintf(out, (size_t)n + 1,
		"System:\n%s\n\nUser:\n%s\n\nAssistant:",
		cfg->openai_system ? cfg->openai_system : "",
		prompt ? prompt : "");
	return out;
}

static char *compact_json_hint(const char *json)
{
	char *out;
	size_t i;
	size_t j;

	if (!json)
		return xstrdup("[hermes] empty OpenAI response");
	out = malloc(512);
	if (!out)
		return NULL;
	for (i = 0, j = 0; json[i] && j < 500; i++) {
		if (json[i] == '\n' || json[i] == '\r' || json[i] == '\t')
			continue;
		out[j++] = json[i];
	}
	out[j] = '\0';
	return out;
}
#endif

int openai_generate(const hermes_config_t *cfg, const char *prompt, char **reply_out)
{
	if (!cfg || !cfg->openai_key || !reply_out)
		return -1;

#ifdef HERMES_WITH_CURL
	{
		CURL *c;
		CURLcode rc;
		char *esc;
		char *input;
		char *body;
		char auth[1024];
		mem_t resp;
		struct curl_slist *hdrs;
		char *text;
		int n;

		*reply_out = NULL;
		input = build_input(cfg, prompt ? prompt : "");
		if (!input)
			return -1;
		esc = json_escape(input);
		free(input);
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
			text = compact_json_hint(resp.buf);
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

		n = strlen(prompt ? prompt : "") + 200;
		out = malloc(n);
		if (!out)
			return -1;
		snprintf(out, n,
			"[hermes] built without libcurl.\nInstall libcurl dev package, then rebuild.\nPrompt:\n%s\n",
			prompt ? prompt : "");
		*reply_out = out;
		return 0;
	}
#endif
}
