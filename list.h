#ifndef LIST_H__INCLUDED
#define LIST_H__INCLUDED

#include <event2/http.h>
#include "store.h"
#include "auth.h"

void list_handle(struct auth_engine *auth, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri);



#endif
