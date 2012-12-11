#include "auth.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <event2/http.h>
#include <event2/buffer.h>

#include <event2/keyvalq_struct.h>

#include "conf.h"

struct auth_engine {
	char auth_url[2048];
};



static void auth_dispatch(struct auth_engine *auth, struct evhttp_request *req)
{
	struct evbuffer *buf;

	evhttp_add_header(evhttp_request_get_output_headers(req),
			  "Location", auth->auth_url);

	buf = evbuffer_new();
	evbuffer_add_printf(buf, "Please authenticate");
	evhttp_send_reply(req, HTTP_MOVETEMP, "Please authenticate", buf);
	evbuffer_free(buf);
}


static void auth_cb(struct auth_engine *auth, struct evhttp_request *req)
{
	evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "TBD: Handle auth callback");
}



static int is_auth_cb(struct evhttp_uri *uri)
{
	struct evkeyvalq params;
	const char *state;
	int is_auth;

	/* I wonder how memory management for
	 * all these things extracted from uri should be handled.
	 *
	 * Peeking at the source evkeyvalq is a standard queue, and
	 * header parsing has suitable accessors for it so..
	 *
	 * Anyway, this should live in auth.c somewhere. auth_check() which
	 * would then internally either auth_dispatch() or auth_cb()?
	 */
	memset(&params, 0, sizeof(params));

	/* Does the query need to be freed separately? */
	evhttp_parse_query_str(evhttp_uri_get_query(uri), &params);

	state = evhttp_find_header(&params, "state");
	is_auth = (state != NULL) && (strcmp(state, "auth") == 0);

	evhttp_clear_headers(&params);

	return is_auth;
}

void auth_handle(struct auth_engine *auth, struct evhttp_request *req, struct evhttp_uri *uri)
{
	if (is_auth_cb(uri)) {
		auth_cb(auth, req);
	} else {
		auth_dispatch(auth, req);
	}
}


int auth_init(struct auth_engine **authp, int local_port)
{
	struct auth_engine *auth;
	char client_id[512];
	int err;
	int n;

	if ((err = conf_read("client_id", client_id, sizeof(client_id))) != 0) {
		return err;
	}

	auth = malloc(sizeof(*auth));
	if (auth == NULL) {
		return errno;
	}

	n = snprintf(auth->auth_url, sizeof(auth->auth_url),
		     "https://accounts.google.com/o/oauth2/auth"
		     "?client_id=%s"
		     "&redirect_uri=http://localhost:%d"
		     "&response_type=code"
		     "&scope=https://gdata.youtube.com"
		     "&approval_prompt=auto"
		     "&access_type=online"
		     "&state=auth",
		     client_id, local_port);

	if (n == sizeof(auth->auth_url)) {
		free(auth);
		return EINVAL;
	}

	*authp = auth;
	return 0;
}

void auth_destroy(struct auth_engine *auth)
{
	free(auth);
}
