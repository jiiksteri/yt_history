#include "auth.h"

#include <event2/http.h>

void auth_dispatch(struct evhttp_request *req)
{
	/* TODO: Send the user googlewards. */
	evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "TBD: Redirect to google auth");
}
