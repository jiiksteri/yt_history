#include "reply.h"

#include <event2/buffer.h>
#include <event2/http.h>

static void replyv(struct evhttp_request *req, const char *fmt, va_list ap)
{
	struct evbuffer *buf;

	buf = evbuffer_new();
	evbuffer_add_vprintf(buf, fmt, ap);
	evhttp_send_reply(req, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}

void reply(struct evhttp_request *req, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	replyv(req, fmt, ap);
	va_end(ap);
}


void reply_redirect(struct evhttp_request *req, const char *where)
{
	struct evbuffer *buf;

	buf = evbuffer_new();

	evhttp_add_header(evhttp_request_get_output_headers(req),
			  "Location", where);
	evhttp_send_reply(req, HTTP_MOVETEMP, "If you were so kind", buf);

	evbuffer_free(buf);
}
