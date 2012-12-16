#ifndef HTTPS_H__INCLUDED
#define HTTPS_H__INCLUDED

#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct https_engine;

int https_engine_init(struct https_engine **https);
void https_engine_destroy(struct https_engine *https);

char *https_post(struct https_engine *https,
		 const char *host, int port,
		 const char *path,
		 struct evbuffer *body,
		 void (*read_cb)(struct evbuffer *buf, void *cb_arg),
		 void *cb_arg);

#endif
