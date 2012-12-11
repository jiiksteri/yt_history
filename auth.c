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


static void auth_cb(struct auth_engine *auth, struct evhttp_request *req, struct evkeyvalq *params)
{
	evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "TBD: Handle auth callback");
}



void auth_handle(struct auth_engine *auth, struct evhttp_request *req, struct evhttp_uri *uri)
{
	struct evkeyvalq params;
	const char *state;

	memset(&params, 0, sizeof(params));
	evhttp_parse_query_str(evhttp_uri_get_query(uri), &params);

	state = evhttp_find_header(&params, "state");
	if (state != NULL && strcmp(state, "auth") == 0) {
		auth_cb(auth, req, &params);
	} else {
		auth_dispatch(auth, req);
	}

	evhttp_clear_headers(&params);
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
