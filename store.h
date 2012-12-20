#ifndef STORE_H__INCLUDED
#define STORE_H__INCLUDED

#include <event2/http.h>

struct store;
struct session;

int store_init(struct store **storep, int nel);
void store_destroy(struct store *store);


int session_ensure(struct store *store, struct session **sessionp, struct evhttp_request *req);
void session_free(struct session *session);

int session_set_value(struct session *session, const char *key, const char *value);
const char *session_get_value(struct session *session, const char *key);


#endif
