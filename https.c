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

#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct https_engine {

};

int https_engine_init(struct https_engine **httpsp)
{
	struct https_engine *https = malloc(sizeof(*https));
	if (https == NULL) {
		return errno;
	}

	memset(https, 0, sizeof(*https));

	/* TODO: Real initialization here. */

	*httpsp = https;

	return 0;
}

void https_engine_destroy(struct https_engine *https)
{
	free(https);
}


char *https_post(struct https_engine *https,
		 const char *host, int port,
		 struct evbuffer *body,
		 bufferevent_data_cb read_cb,
		 bufferevent_event_cb event_cb)
{
	return strdup("https_post() NOT IMPLEMENTED");
}
