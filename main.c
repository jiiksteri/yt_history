
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/http.h>

#include "auth.h"
#include "store.h"

struct app {
	struct auth_engine *auth;
	struct store *store;
};


static void handle_request(struct evhttp_request *req, void *_app)
{
	struct app *app = _app;
	struct evhttp_uri *uri;
	struct session *session;
	const char *uri_str;
	const char *path;
	int err;

	uri_str = evhttp_request_get_uri(req);

	printf("%s(): %s\n", __func__, uri_str);

	uri = evhttp_uri_parse(uri_str);
	path = evhttp_uri_get_path(uri);

	if (strcmp(path, "/") == 0) {
		if ((err = session_ensure(app->store, &session, req)) != 0) {
			printf("%s(): %s\n", __func__, strerror(err));
			evhttp_send_error(req, HTTP_INTERNAL, "Failed to ensure session");
		} else {
			auth_handle(app->auth, session, req, uri);
		}
	} else {
		evhttp_send_error(req, HTTP_NOTFOUND, NULL);
	}

	evhttp_uri_free(uri);
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

	err = store_init(&app.store);
	if (err != 0) {
		fprintf(stderr, "session_store_init(): %s\n", strerror(err));
		evhttp_free(http);
		evsignal_del(interrupt_event);
		event_free(interrupt_event);
		event_base_free(base);
		return err;
	}

	/* If we had port=0, it's now allocated by bind() */
	port = lport(sock);

	if ((err = auth_init(&app.auth, port)) != 0) {
		fprintf(stderr, "auth_init(): %s\n", strerror(err));
		store_destroy(app.store);
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

	store_destroy(app.store);

	evsignal_del(interrupt_event);
	event_free(interrupt_event);

	auth_destroy(app.auth);
	evhttp_free(http);
	event_base_free(base);

	return 0;

}
