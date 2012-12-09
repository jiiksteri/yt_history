#ifndef AUTH_H__INCLUDED
#define AUTH_H__INCLUDED

#include <event2/http.h>

void auth_dispatch(struct evhttp_request *req);

#endif
