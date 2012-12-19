#include "list.h"

#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "store.h"
#include "https.h"
#include "feed.h"
#include "reply.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct list_request_ctx {

	char query_buf[512];

	struct feed *feed;

	struct evhttp_request *original_request;
};

static void read_list(struct evbuffer *buf, void *arg)
{
	struct list_request_ctx *ctx = arg;

	feed_consume(ctx->feed, buf);
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

static void setup_pagination(int *start, int *max, struct evkeyvalq *params)
{
	const char *raw;

	*start = 1;
	*max = 26;

	raw = evhttp_find_header(params, "start-index");
	if (raw != NULL) {
		*start = atoi_limited(raw, 1, 1000000);
	}

	raw = evhttp_find_header(params, "max-results");
	if (raw != NULL) {
		*max = atoi_limited(raw, 1, 50);
	}
}

static void done_free(char *err_msg, void *arg)
{
	struct list_request_ctx *ctx = arg;

	if (err_msg != NULL) {
		evhttp_send_error(ctx->original_request,
				  HTTP_INTERNAL, err_msg);
	} else {
		evhttp_send_reply(ctx->original_request,
				  HTTP_OK, "OK",
				  evhttp_request_get_output_buffer(ctx->original_request));
	}
	free(err_msg);
	free(ctx);

}

static void done_list(char *err_msg, void *arg)
{
	struct list_request_ctx *ctx = arg;

	feed_final(ctx->feed);
	feed_destroy(ctx->feed);

	done_free(err_msg, ctx);
}

static void build_query(struct list_request_ctx *ctx, struct evhttp_uri *uri)
{
	struct evkeyvalq params;
	int start_index, max_results;


	memset(&params, 0, sizeof(params));
	evhttp_parse_query_str(evhttp_uri_get_query(uri), &params);

	setup_pagination(&start_index, &max_results, &params);

	snprintf(ctx->query_buf, sizeof(ctx->query_buf),
		 "/feeds/api/users/default/watch_history?v=2"
		 "&start-index=%d&max-results=%d",
		 start_index, max_results);


	evhttp_clear_headers(&params);
}



static struct https_cb_ops list_cb_ops = {
	.read = read_list,
	.done = done_list,
};

static int setup_feed(struct list_request_ctx *ctx, struct evhttp_request *req)
{
	int err;

	if ((err = feed_init(&ctx->feed, evhttp_request_get_output_buffer(req))) != 0) {
		printf("%s(): feed_init(): %s\n", __func__, strerror(err));
		evhttp_send_error(req, HTTP_INTERNAL, "feed_init() failed");
	}
	return err;
}


void list_handle(struct https_engine *https, struct session *session,
		 struct evhttp_request *req, struct evhttp_uri *uri)
{
	struct list_request_ctx *ctx;
	const char *access_token;
	int err;

	access_token = session_get_value(session, "access_token");
	printf("%s(): using access token %s\n", __func__, access_token);

	if (access_token == NULL) {
		reply_redirect(req, "/");
		return;
	}

	if ((ctx = malloc(sizeof(*ctx))) == NULL) {
		evhttp_send_error(req, HTTP_INTERNAL, "Out of memory");
		return;
	}
	ctx->original_request = req;

	build_query(ctx, uri);

	printf("%s(): query_buf: '%s'\n", __func__, ctx->query_buf);

	if ((err = setup_feed(ctx, req)) != 0) {
		/* It already sent an error */
		free(ctx);
		return;
	}


	https_request(https,
		      "gdata.youtube.com", 443,
		      "GET",
		      ctx->query_buf,
		      access_token,
		      (struct evbuffer *)NULL,
		      &list_cb_ops, ctx);
}
