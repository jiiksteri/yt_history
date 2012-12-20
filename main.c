
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/http.h>

#include "auth.h"
#include "store.h"
#include "list.h"

struct app {

	struct event_base *base;

	struct evhttp_bound_socket *sock;
	struct evhttp *http;

	struct https_engine *https;
	struct auth_engine *auth;
	struct store *store;

	struct event *interrupt_event;

	int port;
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
	} else if (strcmp(path, "/list") == 0) {
		if ((err = session_ensure(app->store, &session, req)) != 0) {
			printf("%s(): %s\n", __func__, strerror(err));
			evhttp_send_error(req, HTTP_INTERNAL, "Failed to ensure session");
		} else {
			list_handle(app->https, session, req, uri);
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
	int opt, err;

	memset(&app, 0, sizeof(app));

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			app.port = atoi(optarg);
			break;
		default:
			err = EXIT_FAILURE;
			goto out_cleanup;
		}
	}

	if ((app.base = event_base_new()) == NULL) {
		err = errno;
		goto out_cleanup;
	}

	/* Trap SIGINT. The handler will call event_base_loopexit()
	 * and we get to do cleanup.
	 */
	app.interrupt_event = evsignal_new(app.base,
					   SIGINT, interrupted,
					   app.base);
	if (app.interrupt_event == NULL) {
		err = errno;
		fprintf(stderr, "Failed to trap SIGINT\n");
		goto out_cleanup;
	}
	evsignal_add(app.interrupt_event, NULL);

	if ((app.http = evhttp_new(app.base)) == NULL) {
		err = errno;
		perror("evhttp_new()");
		goto out_cleanup;
	}

	app.sock = evhttp_bind_socket_with_handle(app.http,
						  "localhost", app.port);
	if (app.sock == NULL) {
		err = errno;
		perror("evhttp_bind_socket()");
		goto out_cleanup;
	}

	err = store_init(&app.store, 10);
	if (err != 0) {
		fprintf(stderr, "session_store_init(): %s\n", strerror(err));
		goto out_cleanup;
	}

	if (https_engine_init(&app.https, app.base) != 0) {
		err = errno;
		fprintf(stderr, "https_init(): %s\n", strerror(err));
		goto out_cleanup;
	}


	/* If we had port=0, it's now allocated by bind() */
	app.port = lport(app.sock);

	if ((err = auth_init(&app.auth, app.https, app.port)) != 0) {
		fprintf(stderr, "auth_init(): %s\n", strerror(err));
		goto out_cleanup;
	}

	printf("http://localhost:%d/\n", app.port);

	evhttp_set_gencb(app.http, handle_request, &app);

	event_base_dispatch(app.base);
	printf("Interrupted\n");
	err = 0;

 out_cleanup:

	store_destroy(app.store);

	evsignal_del(app.interrupt_event);
	event_free(app.interrupt_event);

	auth_destroy(app.auth);
	https_engine_destroy(app.https);
	evhttp_free(app.http);
	event_base_free(app.base);

	return err;

}
