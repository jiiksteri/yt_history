#ifndef REPLY_H__INCLUDED
#define REPLY_H__INCLUDED

#include <event2/http.h>

void reply(struct evhttp_request *req, const char *fmt, ...);

#endif
