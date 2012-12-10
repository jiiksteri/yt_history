#include "auth.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <event2/http.h>
#include <event2/buffer.h>

#include "conf.h"

struct auth_engine {
	char auth_url[2048];
};



void auth_dispatch(struct auth_engine *auth, struct evhttp_request *req)
{
	struct evbuffer *buf;

	evhttp_add_header(evhttp_request_get_output_headers(req),
			  "Location", auth->auth_url);

	buf = evbuffer_new();
	evbuffer_add_printf(buf, "Please authenticate");
	evhttp_send_reply(req, HTTP_MOVETEMP, "Please authenticate", buf);
	evbuffer_free(buf);
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
