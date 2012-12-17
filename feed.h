#ifndef FEED_H__INCLUDED
#define FEED_H__INCLUDED

/*
 * XML video list feed parsing routines.
 */

#include <event2/buffer.h>

struct feed;

int feed_init(struct feed **feedp, struct evbuffer *sink);
void feed_destroy(struct feed *feed);

int feed_consume(struct feed *feed, struct evbuffer *buf);
int feed_final(struct feed *feed);

#endif

