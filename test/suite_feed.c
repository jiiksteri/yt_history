
#include <CUnit/CUnit.h>
#include "test_util.h"

#include "feed.h"

#include <stdio.h>

#include <event2/buffer.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <expat.h>


static void drain_buffer(FILE *where, struct evbuffer *buf)
{
	char cbuf[1024];
	int removed;

	while ((removed = evbuffer_remove(buf, cbuf, sizeof(cbuf)-1)) > 0) {
		cbuf[removed] = '\0';
		fprintf(where, "%s", cbuf);
	}
}


static void consume_file(struct feed *feed, const char *fname)
{
	char buf[1024];
	struct evbuffer *source;
	int fd;
	int n;

	source = evbuffer_new();
	fd = open(fname, O_RDONLY);

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		evbuffer_add(source, buf, n);
	}

	close(fd);

	feed_consume(feed, source);
	evbuffer_free(source);
}


static void assert_link(const char *parsed_link, const char *expected)
{
	CU_ASSERT_PTR_NOT_NULL_FATAL(parsed_link);
	CU_ASSERT_STRING_EQUAL(parsed_link, expected);
}

struct navi_hunt_context {
	int in_navi_div;
	char **prevp;
	char **nextp;
};

static const char *attr_value(const char **attrs, const char *name)
{
	const char *value;
	int i;

	for (i = 0, value = NULL; attrs[i]; i += 2) {
		if (strcmp(attrs[i], name) == 0) {
			value = attrs[i+1];
			break;
		}
	}
	return value;
}



static int attr_is(const char **attrs, const char *name, const char *what)
{
	const char *value = attr_value(attrs, name);
	return value != NULL && strcmp(value, what) == 0;
}


static int id_is(const char **attrs, const char *what)
{
	return attr_is(attrs, "id", what);
}

static int class_is(const char **attrs, const char *what)
{
	return attr_is(attrs, "class", what);
}

static void navi_link_hunter_element_start(void *user_data,
					   const char *element, const char **attrs)
{
	struct navi_hunt_context *ctx = user_data;

	if (strcmp(element, "div") == 0 && class_is(attrs, "navi")) {
		ctx->in_navi_div++;
	} else if (strcmp(element, "a") == 0) {
		if (ctx->in_navi_div > 0) {
			if (id_is(attrs, "prev")) {
				*ctx->prevp = strdup(attr_value(attrs, "href"));
			} else if (id_is(attrs, "next")) {
				*ctx->nextp = strdup(attr_value(attrs, "href"));
			}
		}
	}
}

static void navi_link_hunter_element_end(void *user_data, const char *element)
{
	struct navi_hunt_context *ctx = user_data;

	if (strcmp(element, "div") == 0 && ctx->in_navi_div) {
		ctx->in_navi_div--;
	}
}

static int hunt_navigation_links(char **prevp, char **nextp, struct evbuffer *buf)
{
	char input[512];
	int n;
	int err;
	struct navi_hunt_context ctx;
	XML_Parser p;

	/* Error code 36 is "parsing finished" */
	int XMLERR_PARSING_FINISHED = 36;

	memset(&ctx, 0, sizeof(ctx));
	ctx.prevp = prevp;
	ctx.nextp = nextp;


	p = XML_ParserCreate(NULL);
	XML_SetUserData(p, &ctx);
	XML_SetElementHandler(p,
			      navi_link_hunter_element_start,
			      navi_link_hunter_element_end);

	while ((n = evbuffer_remove(buf, input, sizeof(input))) >= 0) {
		if (XML_Parse(p, input, n, n == 0) == 0) {
			err = XML_GetErrorCode(p);
			if (err != XMLERR_PARSING_FINISHED) {
				fprintf(stdout, "%s(): [%d]: %s\n", __func__,
					err, XML_ErrorString(err));
			}
			break;
		}
	}

	CU_ASSERT_NOT_EQUAL(n, -1);
	XML_ParserFree(p);
	return err == XMLERR_PARSING_FINISHED ? 0 : err;
}

static void test_parse_navigation_links(void)
{
	struct feed *feed;
	struct evbuffer *sink;
	int err;
	char *prev, *next;

	sink = evbuffer_new();

	CU_ASSERT_EQUAL(0, feed_init(&feed, sink));

	consume_file(feed, "minimal.atom.xml");
	feed_final(feed);

	prev = NULL;
	next = NULL;


	/* No, this is not how you do unit tests.. */
	CU_ASSERT_EQUAL(err = hunt_navigation_links(&prev, &next, sink), 0);
	if (err == 0) {
		assert_link(prev, "/list?start-index=79&max-results=1&");
		assert_link(next, "/list?start-index=81&max-results=1&");
	}

	free(prev);
	free(next);

	drain_buffer(stdout, sink);

	feed_destroy(feed);
	evbuffer_free(sink);
}




static CU_TestInfo tests[] = {
	DECLARE_TESTINFO(test_parse_navigation_links),
	CU_TEST_INFO_NULL,
};

const CU_SuiteInfo suite_feed = {
	"feed parser", 0, 0, tests,
};
