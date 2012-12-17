#include "list.h"

#include <event2/http.h>
#include "store.h"
#include "auth.h"
#include "https.h"
#include "feed.h"

#include <stdio.h>
#include <string.h>

static void read_list(struct evbuffer *buf, void *arg)
{
	struct feed *feed = arg;
	feed_consume(feed, buf);
}

void list_handle(struct auth_engine *auth, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri)
{
	char *err_msg;
	const char *access_token;
	struct feed *feed;
	int err;

	access_token = session_get_value(session, "access_token");
	printf("%s(): using access token %s\n", __func__,
	       session_get_value(session, "access_token"));

	if (access_token == NULL) {
		printf("%s(): No access token in session. What now?", __func__);
		evhttp_send_error(req, HTTP_INTERNAL,
				  "No access token in session!");
		return;
	}


	if ((err = feed_init(&feed, evhttp_request_get_output_buffer(req))) != 0) {
		printf("%s(): feed_init(): %s\n", __func__, strerror(err));
		evhttp_send_error(req, HTTP_INTERNAL, "feed_init() failed");
	}


	err_msg = https_request(auth_https(auth),
				"gdata.youtube.com", 443,
				"GET",
				"/feeds/api/users/default/watch_history?v=2",
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
