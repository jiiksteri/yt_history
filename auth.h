#ifndef AUTH_H__INCLUDED
#define AUTH_H__INCLUDED

#include <event2/http.h>
#include "store.h"

struct auth_engine;

int auth_init(struct auth_engine **authp, int local_port);

void auth_destroy(struct auth_engine *auth);

void auth_handle(struct auth_engine *auth, struct session *session, struct evhttp_request *req, struct evhttp_uri *uri);

#endif
