#ifndef HTTPS_H__INCLUDED
#define HTTPS_H__INCLUDED

#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct https_engine;

int https_engine_init(struct https_engine **https);
void https_engine_destroy(struct https_engine *https);

char *https_post(struct https_engine *https,
		 const char *host, int port,
		 struct evbuffer *body,
		 bufferevent_data_cb read_cb,
		 bufferevent_event_cb event_cb);

#endif
