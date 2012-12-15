#include "auth.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <event2/http.h>
#include <event2/buffer.h>

#include <event2/keyvalq_struct.h>

#include "conf.h"
#include "reply.h"

struct auth_engine {
	char auth_url[2048];
	char client_id[512];
	char client_secret[512];
	int local_port;
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


static void request_token(struct auth_engine *auth, struct evhttp_request *req,
			  const char *code)
{
	evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "request_token() not implemented");
}


static void auth_cb(struct auth_engine *auth, struct evhttp_request *req, struct evkeyvalq *params)
{
	const char *error;
	const char *code;

	if ((error = evhttp_find_header(params, "error")) != NULL) {
		if (strcmp(error, "access_denied") == 0) {
			reply(req,
			      "<p>So um.. You're new to this, aren't you?"
			      "You're supposed to say yes. Let's try"
			      " <a href=\"/\">again</a>, ok?");
		} else {
			reply(req, "<p>Uh oh</p>");
		}
	} else if ((code = evhttp_find_header(params, "code")) != NULL) {
		request_token(auth, req, code);
	} else {
		evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "TBD: Handle auth callback");
	}
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
	int err;
	int n;

	auth = malloc(sizeof(*auth));
	if (auth == NULL) {
		return errno;
	}

	memset(auth, 0, sizeof(*auth));
	auth->local_port = local_port;

	if ((err = conf_read("client_id", auth->client_id, sizeof(auth->client_id))) != 0) {
		free(auth);
		return err;
	}

	if ((err = conf_read("client_secret", auth->client_secret, sizeof(auth->client_secret))) != 0) {
		free(auth);
		return err;
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
		     auth->client_id, local_port);

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
