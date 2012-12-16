#include "list.h"

#include <event2/http.h>
#include "store.h"
#include "auth.h"
#include "https.h"

#include <stdio.h>

static void read_list(struct evbuffer *buf, void *arg)
{
	char data[1024];
	int removed;

	while ((removed = evbuffer_remove(buf, data, sizeof(data)-1)) > 0) {
		data[removed] = '\0';
		printf("%s(): %s", __func__, data);
	}
}

void list_handle(struct auth_engine *auth, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri)
{
	char *err_msg;
	const char *access_token;

	access_token = session_get_value(session, "access_token");
	printf("%s(): using access token %s\n", __func__,
	       session_get_value(session, "access_token"));

	if (access_token == NULL) {
		printf("%s(): No access token in session. What now?", __func__);
		evhttp_send_error(req, HTTP_INTERNAL,
				  "No access token in session!");
		return;
	}


	err_msg = https_request(auth_https(auth),
				"gdata.youtube.com", 443,
				"GET",
				"/feeds/api/users/default/watch_history?v=2",
				access_token,
				(struct evbuffer *)NULL,
				read_list, NULL);

	if (err_msg != NULL) {
		evhttp_send_error(req, HTTP_INTERNAL, err_msg);
	} else {
		/* Either we've spewed the data directly from
		 * read_list() or we should provide a context
		 * argument for it and do it here. Think.
		 */
		evhttp_send_error(req, HTTP_INTERNAL,
				  "Someone needs to think");
	}
	free(err_msg);
}
