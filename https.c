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
#include <event2/bufferevent_ssl.h>

#include <openssl/ssl.h>

struct https_engine {
	SSL_CTX *ssl_ctx;
	struct event_base *event_base;
};

int https_engine_init(struct https_engine **httpsp)
{
	struct https_engine *https = malloc(sizeof(*https));
	if (https == NULL) {
		return errno;
	}

	memset(https, 0, sizeof(*https));

	SSL_load_error_strings();
	SSL_library_init();

	https->ssl_ctx = SSL_CTX_new(SSLv23_method());
	if (https->ssl_ctx == NULL) {
		/* Can't be arsed to do the full error stack parsing
		 * nonsense.
		 */
		return ENOMEM;
	}

	https->event_base = event_base_new();
	if (https->event_base == NULL) {
		SSL_CTX_free(https->ssl_ctx);
		return ENOMEM;
	}

	*httpsp = https;

	return 0;
}

void https_engine_destroy(struct https_engine *https)
{
	event_base_free(https->event_base);
	SSL_CTX_free(https->ssl_ctx);
	free(https);
}

struct request_ctx {

	struct event_base *event_base;

	const char *host;
	int port;
	const char *method;
	const char *path;

	struct evbuffer *request_body;

	void (*read_cb)(struct evbuffer *buf, void *arg);
	void *cb_arg;

	char *error;

	enum { READ_STATUS, READ_HEADERS, READ_BODY } read_state;
	int status;

	int content_length;
	int consumed;

};

static void cb_write(struct bufferevent *bev, void *arg)
{
	printf("%s(): Write exhausted\n", __func__);
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

	for (i = 0; line[i] != ' ' && i < len; i++) {
		;
	}
	if (i == len) {
		printf("%s(): Invalid status line '%s'\n", __func__, line);
	} else {
		req->status = atoi(&line[i+1]);
	}
}

static void drain_body(struct request_ctx *req, struct bufferevent *bev)
{
	struct evbuffer *buf;
	int before, after;

	buf = bufferevent_get_input(bev);
	before = evbuffer_get_length(buf);
	req->read_cb(bufferevent_get_input(bev), req->cb_arg);
	after = evbuffer_get_length(buf);
	req->consumed += before - after;
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

static const char *pretty_state(char *buf, size_t len, int state)
{
	switch (state) {
	case READ_STATUS: snprintf(buf, len, "READ_STATUS"); break;
	case READ_HEADERS: snprintf(buf, len, "READ_HEADERS"); break;
	case READ_BODY: snprintf(buf, len, "READ_BODY"); break;
	default: snprintf(buf, len, "UNKNWN(%d)", state); break;
	}
	return buf;
}

static void set_read_state(struct request_ctx *req, int state)
{
	char s1[32];
	char s2[32];

	printf("%s(): %s -> %s\n", __func__,
	       pretty_state(s1, sizeof(s1), req->read_state),
	       pretty_state(s2, sizeof(s2), state));
	req->read_state = state;

}

static void cb_read(struct bufferevent *bev, void *arg)
{
	char sbuf[32];
	struct request_ctx *req = arg;
	char *line;
	size_t n;

	printf("%s() state %s\n", __func__, pretty_state(sbuf, sizeof(sbuf), req->read_state));

	while (req->read_state == READ_STATUS || req->read_state == READ_HEADERS) {

		line = read_line(bev, &n);
		if (line == NULL) {
			return;
		}

		if (req->read_state == READ_STATUS) {
			printf("%s(): status line: '%s'\n", __func__, line);
			parse_status(req, line, n);
			set_read_state(req, READ_HEADERS);
		} else {
			while (line && req->read_state == READ_HEADERS) {
				if (*line == '\0') {
					set_read_state(req, READ_BODY);
				} else {
					char *key, *val;
					printf("%s(): header line '%s'\n", __func__, line);
					header_keyval(&key, &val, line);
					if (strcmp(key, "Content-Length") == 0) {
						req->content_length = atoi(val);
					}
				}
				line = read_line(bev, &n);
			}
		}

		free(line);

	}

	if (req->read_state == READ_BODY) {
		drain_body(req, bev);
	}
}

static void cb_event(struct bufferevent *bev, short what, void *arg)
{
	struct request_ctx *req = arg;
	switch (what) {
	case BEV_EVENT_CONNECTED:
		evbuffer_add_printf(bufferevent_get_output(bev),
				    "%s %s HTTP/1.1\r\n"
				    "Host: %s\r\n"
				    "Connection: close\r\n",
				    req->method,
				    req->path,
				    req->host);

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
		break;

	case BEV_EVENT_ERROR:
		printf("%s() Received error event. Bytes now %d/%d\n",
		       __func__, req->consumed, req->content_length);
		/* For some reason, one that we get to figure out,
		 * we get a BEV_EVENT_ERROR once the remote end is
		 * done with us, even as it just sent a successful
		 * reply.
		 *
		 * If the response code was ok and we managed to
		 * read anything at all, skip reporting the error
		 * to the caller and let it deal with the possibly
		 * broken response.
		 */
		if (req->status != 200) {
			/* Pshaw. I suppose we could have a better
			 * message.
			 */
			req->error = "Token request went bewm";
		}
		event_base_loopexit(req->event_base, NULL);
		break;
	case BEV_EVENT_EOF:
		event_base_loopexit(req->event_base, NULL);
		break;
	default:
		printf("%s(): Unhandled event: %d\n", __func__, what);
		break;
	}
}


char *https_request(struct https_engine *https,
		    const char *host, int port,
		    const char *method, const char *path,
		    struct evbuffer *body,
		    void (*read_cb)(struct evbuffer *buf, void *arg),
		    void *cb_arg)
{
	struct request_ctx request;
	struct bufferevent *bev;
	BIO *bio;
	SSL *ssl;

	bio = BIO_new(BIO_s_connect());
	if (bio == NULL) {
		return "Failed to set up BIO";
	}

	BIO_set_nbio(bio, 1);
	BIO_set_conn_hostname(bio, host);
	BIO_set_conn_int_port(bio, &port);

	ssl = SSL_new(https->ssl_ctx);
	if (ssl == NULL) {
		BIO_free(bio);
		return "Failed to set up SSL";
	}

	SSL_set_bio(ssl, bio, bio);
	SSL_connect(ssl);


	bev = bufferevent_openssl_socket_new(https->event_base, -1, ssl,
					     BUFFEREVENT_SSL_CONNECTING,
					     BEV_OPT_CLOSE_ON_FREE);

	memset(&request, 0, sizeof(request));
	request.event_base = https->event_base;
	request.method = method;
	request.host = host;
	request.port = port;
	request.path = path;
	request.request_body = body;
	request.read_cb = read_cb;
	request.cb_arg = cb_arg;

	bufferevent_setcb(bev, cb_read, cb_write, cb_event, &request);

	/*
	 * This is insanely fragile.
	 *
	 * We run the event loop with the thread doing the
	 * http_post(). We rely on exiting the loop when
	 * _this_ request is done.
	 *
	 * In practice, this should work, as we're only
	 * called from the dispatch thread of the original
	 * server event base, and as that's stuck here we
	 * don't get called by anything else.
	 *
	 * The fact that we're abusing the calling thread
	 * means we also get away with keeping the request
	 * on the stack. It's not going away until we are
	 * and we're only going away once the request is
	 * done and nothing is touching it.
	 */
	event_base_dispatch(https->event_base);

	return request.error != NULL
		? strdup(request.error)
		: NULL;
}


char *https_post(struct https_engine *https,
		 const char *host, int port,
		 const char *path,
		 struct evbuffer *body,
		 void (*read_cb)(struct evbuffer *buf, void *arg),
		 void *cb_arg)
{
	return https_request(https,
			     host, port,
			     "POST", path,
			     body,
			     read_cb, cb_arg);
}
