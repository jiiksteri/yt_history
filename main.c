
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "auth.h"

struct app {
	struct auth_engine *auth;
};

static int is_auth_cb(const char *uri_str)
{
	struct evhttp_uri *uri;
	struct evkeyvalq params;
	const char *state;
	int is_auth;

	/* I wonder how memory management for
	 * all these things extracted from uri should be handled.
	 *
	 * Peeking at the source evkeyvalq is a standard queue, and
	 * header parsing has suitable accessors for it so..
	 *
	 * Anyway, this should live in auth.c somewhere. auth_check() which
	 * would then internally either auth_dispatch() or auth_cb()?
	 */
	memset(&params, 0, sizeof(params));

	uri = evhttp_uri_parse(uri_str);

	/* Does the query need to be freed separately? */
	evhttp_parse_query_str(evhttp_uri_get_query(uri), &params);

	state = evhttp_find_header(&params, "state");
	is_auth = (state != NULL) && (strcmp(state, "auth") == 0);

	evhttp_clear_headers(&params);
	evhttp_uri_free(uri);

	return is_auth;
}

static void handle_request(struct evhttp_request *req, void *_app)
{
	struct app *app = _app;
	const char *uri;

	uri = evhttp_request_get_uri(req);

	printf("%s(): %s\n", __func__, uri);

	if (strcmp(uri, "/") == 0) {
		auth_dispatch(app->auth, req);
	} else if (is_auth_cb(uri)) {
		auth_cb(app->auth, req);
	} else {
		evhttp_send_error(req, HTTP_NOTFOUND, NULL);
	}
}

static int lport(struct evhttp_bound_socket *sock)
{
	struct sockaddr_storage saddr;
	socklen_t alen;
	evutil_socket_t fd;
	int port;

	fd = evhttp_bound_socket_get_fd(sock);
	alen = sizeof(saddr);
	if (getsockname(fd, (struct sockaddr *)&saddr, &alen) == -1) {
		perror("getsockname()");
		return -1;
	}

	switch (saddr.ss_family) {
	case AF_INET:
		port = ntohs(((struct sockaddr_in *)&saddr)->sin_port);
		break;
	case AF_INET6:
		port = ntohs(((struct sockaddr_in6 *)&saddr)->sin6_port);
		break;
	default:
		fprintf(stderr, "Unsupported address family: %d\n", saddr.ss_family);
		port = -1;
		break;
	}

	return port;
}

static void interrupted(evutil_socket_t fd, short events, void *base)
{
	event_base_loopexit(base, NULL);
}

int main(int argc, char **argv)
{
	struct app app;
	struct event_base *base;
	struct evhttp *http;
	struct evhttp_bound_socket *sock;
	struct event *interrupt_event;
	int opt, err, port = 0;

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			port = atoi(optarg);
			break;
		default:
			return 1;
		}
	}

	base = event_base_new();
	if (!base) {
		return 1;
	}

	/* Trap SIGINT. The handler will call event_base_loopexit()
	 * and we get to do cleanup.
	 */
	interrupt_event = evsignal_new(base, SIGINT, interrupted, base);
	if (interrupt_event == NULL) {
		fprintf(stderr, "Failed to trap SIGINT\n");
		event_base_free(base);
		return 1;
	}
	evsignal_add(interrupt_event, NULL);

	http = evhttp_new(base);
	if (http == NULL) {
		perror("evhttp_new()");
		evsignal_del(interrupt_event);
		event_free(interrupt_event);
		event_base_free(base);
		return 1;
	}

	if ((sock = evhttp_bind_socket_with_handle(http, "localhost", port)) == NULL) {
		perror("evhttp_bind_socket()");
		evhttp_free(http);
		evsignal_del(interrupt_event);
		event_free(interrupt_event);
		event_base_free(base);
		return 1;
	}

	/* If we had port=0, it's now allocated by bind() */
	port = lport(sock);

	if ((err = auth_init(&app.auth, port)) != 0) {
		fprintf(stderr, "auth_init(): %s\n", strerror(err));
		evhttp_free(http);
		evsignal_del(interrupt_event);
		event_free(interrupt_event);
		event_base_free(base);
		return err;
	}

	printf("http://localhost:%d/\n", lport(sock));

	evhttp_set_gencb(http, handle_request, &app);

	event_base_dispatch(base);
	printf("Interrupted\n");

	evsignal_del(interrupt_event);
	event_free(interrupt_event);

	auth_destroy(app.auth);
	evhttp_free(http);
	event_base_free(base);

	return 0;

}
