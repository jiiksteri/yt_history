#ifndef HTTPS_H__INCLUDED
#define HTTPS_H__INCLUDED

#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct https_engine;

int https_engine_init(struct https_engine **https);
void https_engine_destroy(struct https_engine *https);

void https_request(struct https_engine *https,
		   const char *host, int port,
		   const char *method, const char *path,
		   const char *access_token,
		   struct evbuffer *body,
		   void (*read_cb)(struct evbuffer *buf, void *arg),
		   void (*done_cb)(char *err_msg, void *arg),
		   void *cb_arg);



#endif
