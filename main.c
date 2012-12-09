
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <event2/event.h>
#include <event2/http.h>

#include "auth.h"

static void handle_request(struct evhttp_request *req, void *arg)
{
	const char *uri;

	uri = evhttp_request_get_uri(req);

	printf("%s(): %s\n", __func__, uri);

	if (strcmp(uri, "/") == 0) {
		auth_dispatch(req);
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

int main(int argc, char **argv)
{
	struct event_base *base;
	struct evhttp *http;
	struct evhttp_bound_socket *sock;
	int opt, port = 0;

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

	http = evhttp_new(base);
	if (http == NULL) {
		perror("evhttp_new()");
		event_base_free(base);
		return 1;
	}

	if ((sock = evhttp_bind_socket_with_handle(http, "localhost", port)) == NULL) {
		perror("evhttp_bind_socket()");
		evhttp_free(http);
		event_base_free(base);
		return 1;
	}

	printf("http://localhost:%d/\n", lport(sock));

	evhttp_set_gencb(http, handle_request, NULL);

	event_base_dispatch(base);

	evhttp_free(http);
	event_base_free(base);

	return 0;

}
