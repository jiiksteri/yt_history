/*
 * So libevent 2.1 would ship a sane api for running evhttp_requests on
 * top of SSL sockets.
 *
 * Sadly, that's alpha. 2.0, which is stable, doesn't ship such goodies.
 * Fortunately libevent bufferevents still make things like this a
 * breeze.
 */

#include "https.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <event2/event.h>
#include <event2/buffer.h>

#include "verbose.h"

#include "conn_stash.h"

struct https_engine {
	struct conn_stash *conn_stash;
	struct event_base *event_base;
};

int https_engine_init(struct https_engine **httpsp, struct event_base *event_base)
{
	int err;

	struct https_engine *https = malloc(sizeof(*https));
	if (https == NULL) {
		return errno;
	}

	memset(https, 0, sizeof(*https));

	err = conn_stash_init(&https->conn_stash, event_base);
	if (err != 0) {
		free(https);
		return errno;
	}

	https->event_base = event_base;

	*httpsp = https;

	return 0;
}

void https_engine_destroy(struct https_engine *https)
{
	conn_stash_destroy(https->conn_stash);
	free(https);
}

struct request_ctx {

	const char *host;
	int port;
	const char *method;
	const char *path;

	const char *access_token;

	struct evbuffer *request_body;


	struct https_cb_ops *cb_ops;
	void *cb_arg;

	char *error;

	enum { READ_STATUS, READ_HEADERS, READ_BODY, READ_DONE } read_state;

	int status;
	char *status_line;

	int content_length;
	int consumed;

	int chunked;
	size_t chunk_size;
	size_t chunk_left;

	struct conn_stash *conn_stash;

};

static void cb_write(struct bufferevent *bev, void *arg)
{
	verbose(FIREHOSE, "%s(): Write exhausted\n", __func__);
	bufferevent_disable(bev, EV_WRITE);
	bufferevent_enable(bev, EV_READ);
}

static char *read_line(struct bufferevent *bev, size_t *n)
{
	return evbuffer_readln(bufferevent_get_input(bev),
			       n, EVBUFFER_EOL_CRLF_STRICT);
}

static void parse_status(struct request_ctx *req, const char *line, size_t len)
{
	int i;

	req->status_line = strdup(line);
	for (i = 0; line[i] != ' ' && i < len; i++) {
		;
	}
	if (i == len) {
		verbose(ERROR, "%s(): Invalid status line '%s'\n", __func__, line);
	} else {
		req->status = atoi(&line[i+1]);
	}
}

static int read_chunk_size(struct request_ctx *req, struct bufferevent *bev)
{
	char *line;
	char *eptr;
	size_t n;
	unsigned long val;
	int err;


	/* A chunk ends with CRLF that's not accounted for by chunk size.
	 * So we should have an "empty" line followed by next chunk size
	 */
	line = NULL;
	do {
		free(line);
		line = read_line(bev, &n);
	} while (line != NULL && *line == '\0');

	err = 0;
	if (line != NULL) {
		errno = 0;
		val = strtoul(line, &eptr, 16);
		if (errno != 0) {
			err = errno;
			verbose(ERROR, "%s(): bad chunk size '%s': %s\n",
			       __func__, line, strerror(errno));
		} else {
			req->chunk_size = val;
			req->chunk_left = val;
			verbose(FIREHOSE,
				"%s(): chunk size: %zd, from '%s'\n",
				__func__, val, line);
			err = 0;
		}
		free(line);
	}
	return err;
}

static const char *pretty_state(char *buf, size_t len, int state)
{
	switch (state) {
	case READ_STATUS: snprintf(buf, len, "READ_STATUS"); break;
	case READ_HEADERS: snprintf(buf, len, "READ_HEADERS"); break;
	case READ_BODY: snprintf(buf, len, "READ_BODY"); break;
	case READ_DONE: snprintf(buf, len, "READ_DONE"); break;
	default: snprintf(buf, len, "UNKNWN(%d)", state); break;
	}
	return buf;
}

static void set_read_state(struct request_ctx *req, int state)
{
	char s1[32];
	char s2[32];

	if (verbose_adjust_level(0) >= VERBOSE) {
		verbose(VERBOSE, "%s(): %s -> %s\n", __func__,
			pretty_state(s1, sizeof(s1), req->read_state),
			pretty_state(s2, sizeof(s2), state));
	}

	req->read_state = state;

}

static void flush_input(struct request_ctx *req, struct evbuffer *buf)
{
	int len;

	while ((len = evbuffer_get_length(buf)) > 0) {
		verbose(VERBOSE, "%s(): input bytes left: %d\n",
			__func__, len);
		req->cb_ops->read(buf, req->cb_arg);
	}
}

static void request_done(struct request_ctx *req, struct bufferevent *bev)
{
	/* Force the remaining bytes down our consumer's throat. */
	flush_input(req, bufferevent_get_input(bev));

	req->cb_ops->done(req->error, req->cb_arg);
	conn_stash_put_bev(req->conn_stash, bev);
	free(req->status_line);
	free(req);
}

static void drain_body(struct request_ctx *req, struct bufferevent *bev)
{
	struct evbuffer *buf;
	struct evbuffer *saved;
	int before, after;

	if (req->chunked && req->chunk_size == -1) {
		/* Transfer-Encoding: chunked but we don't have size yet. */
		if (read_chunk_size(req, bev) != 0) {
			verbose(ERROR, "%s(): could not read chunk size!\n", __func__);
		}
	}

	buf = bufferevent_get_input(bev);

	saved = NULL;
	before = evbuffer_get_length(buf);
	if (req->chunked && req->chunk_size > 0 && req->chunk_left < before) {
		/*
		 * Save the real buffer away and give the callback
		 * a temporary one, just the chunk that remains.
		 */
		saved = buf;
		buf = evbuffer_new();
		evbuffer_remove_buffer(saved, buf, req->chunk_left);
		before = evbuffer_get_length(buf);
	}


	req->cb_ops->read(buf, req->cb_arg);
	after = evbuffer_get_length(buf);
	req->consumed += before - after;
	req->chunk_left -= (before - after);
	if (req->chunk_size > 0 && req->chunk_left == 0) {
		/* We've consumed the whole chunk.
		 * Signal that we need another one.
		 */
		req->chunk_size = -1;
	}

	if (saved != NULL) {
		/* saved is actually the buffer in bev */
		evbuffer_prepend_buffer(saved, buf);
		evbuffer_free(buf);
	}

	if ((req->chunked && req->chunk_size == 0) ||
	    (req->content_length > 0 && req->consumed == req->content_length)) {
		set_read_state(req, READ_DONE);
	}


}

static void header_keyval(char **key, char **val, char *line)
{
	int i;

	*key = line;
	for (i = 0; line[i] != '\0' && line[i] != ':'; i++) {
		;
	}

	if (line[i] == ':') {
		line[i] = '\0';
		while (line[++i] == ' ') {
			;
		}
	}
	*val = &line[i];
}


static void handle_header(struct request_ctx *req, const char *key, const char *val)
{
	if (strcmp(key, "Content-Length") == 0) {
		req->content_length = atoi(val);
	} else if (strcmp(key, "Transfer-Encoding") == 0 &&
		   strcmp(val, "chunked") == 0) {
		req->chunked = 1;
		req->chunk_size = -1;
		req->chunk_left = -1;
	}

	if (req->cb_ops->response_header) {
		req->cb_ops->response_header(key, val, req->cb_arg);
	}
}

static void cb_read(struct bufferevent *bev, void *arg)
{
	struct request_ctx *req = arg;
	char *line;
	size_t n;

	while (req->read_state == READ_STATUS || req->read_state == READ_HEADERS) {

		line = read_line(bev, &n);
		if (line == NULL) {
			return;
		}

		if (req->read_state == READ_STATUS) {
			verbose(VERBOSE, "%s(): status line: '%s'\n", __func__, line);
			parse_status(req, line, n);
			set_read_state(req, READ_HEADERS);
		} else {
			while (line && req->read_state == READ_HEADERS) {
				if (*line == '\0') {
					set_read_state(req, READ_BODY);
				} else {
					char *key, *val;
					verbose(VERBOSE, "%s(): header line '%s'\n", __func__, line);
					header_keyval(&key, &val, line);
					handle_header(req, key, val);
					free(line);
					line = read_line(bev, &n);
				}
			}
		}

		free(line);

	}

	while (req->read_state == READ_BODY && evbuffer_get_length(bufferevent_get_input(bev)) > 0) {
		drain_body(req, bev);
	}

	if (req->read_state == READ_DONE) {
		request_done(req, bev);
	}

}

static void store_remote_error(struct request_ctx *req)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "%s hates me: %s",
		 req->host, req->status_line);
	buf[sizeof(buf)-1] = '\0';
	req->error = strdup(buf);
}

static void submit_request(struct bufferevent *bev, struct request_ctx *req)
{
	evbuffer_add_printf(bufferevent_get_output(bev),
			    "%s %s HTTP/1.1\r\n"
			    "Host: %s\r\n"
			    "Connection: close\r\n",
			    req->method,
			    req->path,
			    req->host);

	if (req->access_token != NULL) {
		evbuffer_add_printf(bufferevent_get_output(bev),
				    "Authorization: Bearer %s\r\n",
				    req->access_token);
	}

	if (strcmp(req->method, "POST") == 0) {
		evbuffer_add_printf(bufferevent_get_output(bev),
				    "Content-Type: application/x-www-form-urlencoded\r\n");
	}

	if (req->request_body != NULL) {
		evbuffer_add_printf(bufferevent_get_output(bev),
				    "Content-Length: %zd\r\n",
				    evbuffer_get_length(req->request_body));
	}

	evbuffer_add_printf(bufferevent_get_output(bev), "\r\n");

	if (req->request_body != NULL) {
		evbuffer_add_buffer(bufferevent_get_output(bev),
				    req->request_body);
	}

	bufferevent_enable(bev, EV_READ|EV_WRITE);

}


static void cb_event(struct bufferevent *bev, short what, void *arg)
{
	struct request_ctx *req = arg;
	switch (what & ~(BEV_EVENT_READING|BEV_EVENT_WRITING)) {
	case BEV_EVENT_CONNECTED:
		break;

	case BEV_EVENT_ERROR:
		verbose(ERROR,
			"%s(): Received error event. I have status %d,"
			" so it's probably %s\n", __func__, req->status,
			req->status == 200
			? "a dirty shutdown"
			: "something serious");

		/* For some reason, one that we get to figure out,
		 * we get a BEV_EVENT_ERROR once the remote end is
		 * done with us, even as it just sent a successful
		 * reply.
		 *
		 * If the response code was ok and we managed to
		 * read anything at all, skip reporting the error
		 * to the caller and let it deal with the possibly
		 * broken response.
		 *
		 * A bit of debugging shows that this is most likely
		 * the remote end doing a "dirty shutdown".
		 *
		 * A more recent libevent2 would have a way of quiescing
		 * this via
		 *
		 *   bufferevent_openssl_set_allow_dirty_shutdown()
		 *
		 * and we'd only get BEV_EVENT_EOF.
		 *
		 * But I'm not at all convinced that's the whole story.
		 */
		if (req->status != 200) {
			store_remote_error(req);
		}
		request_done(req, bev);
		break;
	case BEV_EVENT_EOF:
		request_done(req, bev);
		break;
	default:
		verbose(ERROR, "%s(): Unhandled event: %d\n", __func__, what);
		break;
	}
}


void https_request(struct https_engine *https,
		   const char *host, int port,
		   const char *method, const char *path,
		   const char *access_token,
		   struct evbuffer *body,
		   struct https_cb_ops *cb_ops,
		   void *cb_arg)
{
	struct request_ctx *request;
	struct bufferevent *bev;

	if ((request = malloc(sizeof(*request))) == NULL) {
		cb_ops->done("Out of memory", cb_arg);
		return;
	}
	memset(request, 0, sizeof(*request));
	request->method = method;
	request->host = host;
	request->port = port;
	request->path = path;
	request->access_token = access_token;
	request->request_body = body;
	request->cb_ops = cb_ops;
	request->cb_arg = cb_arg;
	request->conn_stash = https->conn_stash;

	bev = conn_stash_get_bev(https->conn_stash, host, port);
	if (bev == NULL) {
		cb_ops->done("Failed to set up connection", cb_arg);
		free(request);
		return;
	}

	bufferevent_setcb(bev, cb_read, cb_write, cb_event, request);
	submit_request(bev, request);

}
