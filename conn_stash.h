#ifndef CONN_STASH_H__INCLUDED
#define CONN_STASH_H__INCLUDED

#include <event2/bufferevent.h>

struct conn_stash;

int conn_stash_init(struct conn_stash **stashp, struct event_base *event_base);
void conn_stash_destroy(struct conn_stash *stash);


struct bufferevent *conn_stash_get_bev(struct conn_stash *stash,
				       const char *host,
				       int port);

void conn_stash_put_bev(struct conn_stash *stash, struct bufferevent *bev);

int conn_stash_reconnect(struct conn_stash *stash, struct bufferevent **bevp);

#endif
