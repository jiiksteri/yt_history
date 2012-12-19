#ifndef LIST_H__INCLUDED
#define LIST_H__INCLUDED

#include <event2/http.h>
#include "store.h"
#include "https.h"

void list_handle(struct https_engine *https, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri);



#endif
