#include "list.h"

#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "store.h"
#include "auth.h"
#include "https.h"
#include "feed.h"
#include "reply.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void read_list(struct evbuffer *buf, void *arg)
{
	struct feed *feed = arg;
	feed_consume(feed, buf);
}

static int atoi_limited(const char *raw, int min, int max)
{
	int cand;

	cand = atoi(raw);
	if (cand < min) {
		printf("%s(): limiting '%s' -> %d -> %d\n",
		       __func__, raw, cand, min);
		cand = min;
	} else if (cand > max) {
		printf("%s(): limiting '%s' -> %d -> %d\n",
		       __func__, raw, cand, max);
		cand = max;
	}
	return cand;
}

static void setup_pagination(int *start, int *max, struct evhttp_uri *uri)
{
	struct evkeyvalq params;
	const char *raw;

	*start = 1;
	*max = 26;

	memset(&params, 0, sizeof(params));

	evhttp_parse_query_str(evhttp_uri_get_query(uri), &params);
	raw = evhttp_find_header(&params, "start-index");
	if (raw != NULL) {
		*start = atoi_limited(raw, 1, 1000000);
	}

	raw = evhttp_find_header(&params, "max-results");
	if (raw != NULL) {
		*max = atoi_limited(raw, 1, 50);
	}
}

void list_handle(struct auth_engine *auth, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri)
{
	char query_buf[512];
	char *err_msg;
	const char *access_token;
	struct feed *feed;
	int err;
	int start_index, max_results;

	access_token = session_get_value(session, "access_token");
	printf("%s(): using access token %s\n", __func__,
	       session_get_value(session, "access_token"));

	if (access_token == NULL) {
		reply_redirect(req, "/");
		return;
	}

	setup_pagination(&start_index, &max_results, uri);
	snprintf(query_buf, sizeof(query_buf),
		 "/feeds/api/users/default/watch_history?v=2"
		 "&start-index=%d&max-results=%d",
		 start_index, max_results);

	printf("%s(): query_buf: '%s'\n", __func__, query_buf);

	if ((err = feed_init(&feed, evhttp_request_get_output_buffer(req))) != 0) {
		printf("%s(): feed_init(): %s\n", __func__, strerror(err));
		evhttp_send_error(req, HTTP_INTERNAL, "feed_init() failed");
	}


	err_msg = https_request(auth_https(auth),
				"gdata.youtube.com", 443,
				"GET",
				query_buf,
				access_token,
				(struct evbuffer *)NULL,
				read_list, feed);

	feed_final(feed);
	feed_destroy(feed);

	if (err_msg != NULL) {
		evhttp_send_error(req, HTTP_INTERNAL, err_msg);
	} else {
		evhttp_send_reply(req, HTTP_OK, "OK",
				  evhttp_request_get_output_buffer(req));
	}
	free(err_msg);
}
