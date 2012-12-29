#ifndef HTTPS_H__INCLUDED
#define HTTPS_H__INCLUDED

#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct https_engine;

int https_engine_init(struct https_engine **https, struct event_base *event_base,
		      int no_keepalive);

void https_engine_destroy(struct https_engine *https);

struct https_cb_ops {
	void (*read)(struct evbuffer *buf, void *arg);
	void (*done)(char *err_mg, void *arg);
	void (*response_header)(const char *name, const char *value, void *arg);
};

void https_request(struct https_engine *https,
		   const char *host, int port,
		   const char *method, const char *path,
		   const char *access_token,
		   struct evbuffer *body,
		   struct https_cb_ops *cb_ops,
		   void *cb_arg);



#endif
