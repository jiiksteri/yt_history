#include "feed.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <expat.h>

#include "verbose.h"

/* Fields we're interested in. Now, the code that actually figures out
 * we're interested in something can't really stand daylight..
 */
enum {
	F_TITLE,
	F_UPLOADER,
	F_CONTENT,
	F_PLAYER,
	F_UPDATED,
	F_PUBLISHED,
	F_THUMBNAIL,
	F_LINK_PREVIOUS,
	F_LINK_NEXT,
	F__COUNT,
};


struct feed {

	XML_Parser parser;
	struct evbuffer *sink;
	int header_sent;
	int navi_sent;

	/* parser state */

	int in_entry;
	char *fields[F__COUNT];
	struct evbuffer *cdata_buf;
};

static void XMLCALL cdata(void *user_data, const char *s, int len)
{
	struct feed *feed = user_data;

	if (feed->cdata_buf != NULL) {
		evbuffer_add(feed->cdata_buf, s, len);
	}
}


static void clear_fields(struct feed *feed)
{
	int i;

	for (i = 0; i < F__COUNT; i++) {
		free(feed->fields[i]);
		feed->fields[i] = NULL;
	}
}

static int find_cdata_target(const char *element)
{
	int ret;

	if (strcmp(element, "updated") == 0) {
		ret = F_UPDATED;
	} else if (strcmp(element, "published") == 0) {
		ret = F_PUBLISHED;
	} else if (strcmp(element, "title") == 0) {
		ret = F_TITLE;
	} else {
		ret = -1;
	}

	return ret;
}

static const char *find_attribute_value(const char **attrs, const char *name)
{
	int i;
	const char *value;

	for (i = 0, value = NULL; attrs[i]; i += 2) {
		if (strcmp(attrs[i], name) == 0) {
			value = attrs[i+1];
			break;
		}
	}

	return value;
}

static void copy_attribute_override(char **tgt, const char **attrs, const char *name)
{
	const char *new_value;

	new_value = find_attribute_value(attrs, name);
	if (new_value != NULL) {
		if (*tgt != NULL) {
			free(*tgt);
		}
		*tgt = strdup(new_value);
	}
}

static void copy_attribute_keep_old(char **tgt, const char **attrs, const char *name)
{
	if (*tgt == NULL) {
		copy_attribute_override(tgt, attrs, name);
	}
}

static void handle_navigation_link(struct feed *feed, const char **attrs)
{
	const char *rel;
	int field;

	rel = find_attribute_value(attrs, "rel");
	if (rel != NULL) {
		if (strcmp(rel, "next") == 0) {
			field = F_LINK_NEXT;
		} else if (strcmp(rel, "previous") == 0) {
			field = F_LINK_PREVIOUS;
		} else {
			field = -1;
		}

		if (field != -1) {
			copy_attribute_keep_old(&feed->fields[field], attrs, "href");
		}
	}
}

static void XMLCALL element_start(void *user_data, const char *element, const char **attrs)
{
	struct feed *feed = user_data;

	if (strcmp(element, "entry") == 0) {
		feed->in_entry++;
	} else if (feed->in_entry == 0 && strcmp(element, "link") == 0) {
		handle_navigation_link(feed, attrs);
	} else if (feed->in_entry) {
		if (find_cdata_target(element) != -1) {
			feed->cdata_buf = evbuffer_new();
		}

		if (strcmp(element, "content") == 0) {
			copy_attribute_override(&feed->fields[F_CONTENT], attrs, "src");
		} else if (strcmp(element, "media:thumbnail") == 0) {
			copy_attribute_keep_old(&feed->fields[F_THUMBNAIL], attrs, "url");
		} else if (strcmp(element, "media:player") == 0) {
			copy_attribute_keep_old(&feed->fields[F_PLAYER], attrs, "url");
		} else if (strcmp(element, "media:credit") == 0) {
			/* This is tricky. There are other credit entries than
			 * uploader, probably */
			copy_attribute_keep_old(&feed->fields[F_UPLOADER], attrs, "yt:display");
		}
	}
}

static void filter_query_into(struct evbuffer *buf, const char *full_url)
{
	struct evkeyvalq params;
	const char *val;

	/* FIXME: Remember that passthrough thing we do if
	 * there's _any_ alt=... format specifier? Yeah, that one.
	 * Great idea. Now we get to jump through hoops dropping it from
	 * the next/previous links.
	 *
	 * Just go make passthrough an explicit url parameter, and teach
	 * /list how to deal with alt=atom, so we don't have to do this.
	 */

	memset(&params, 0, sizeof(params));
	if (evhttp_parse_query_str(full_url, &params) == -1) {
		/* Bad idea, we're kind of committed to this :( */
		return;
	}

	/* If we'd dig into the internals we could just iterate the parameters
	 * and skip alt=... As it is, we'll just pick start-index and
	 * max-results. Those are the only ones /list? will pass to Google
	 * anyway
	 */
	if ((val = evhttp_find_header(&params, "start-index")) != NULL) {
		evbuffer_add_printf(buf, "start-index=%s&amp;", val);
	}

	if ((val = evhttp_find_header(&params, "max-results")) != NULL) {
		evbuffer_add_printf(buf, "max-results=%s&amp;", val);
	}

	evhttp_clear_headers(&params);
}

static void parse_and_send_link(struct evbuffer *buf, const char *full_url,
				const char *id, const char *name)
{
	evbuffer_add_printf(buf, "<a id='%s' href='", id);

	if (full_url != NULL) {
		evbuffer_add_printf(buf, "/list?");
		filter_query_into(buf, full_url);
	}

	evbuffer_add_printf(buf, "'>%s</a>\n", name);
}

static void send_navi(struct feed *feed)
{

	evbuffer_add_printf(feed->sink, "<div class='navi'>\n");

	if (feed->fields[F_LINK_PREVIOUS] != NULL) {
		parse_and_send_link(feed->sink, feed->fields[F_LINK_PREVIOUS],
				    "prev", "Previous");
	}

	if (feed->fields[F_LINK_NEXT] != NULL) {
		parse_and_send_link(feed->sink, feed->fields[F_LINK_NEXT],
				    "next", "Next");
	}

	evbuffer_add_printf(feed->sink, "</div>");
}

static char *amp_to_amp(char *to, size_t sz, const char *from)
{
	int ind = 0;

	while (from && *from && ind < sz) {
		if (*from == '&') {
			to[ind++] = '&';
			to[ind++] = 'a';
			to[ind++] = 'm';
			to[ind++] = 'p';
			to[ind++] = ';';
		} else {
			to[ind++] = *from;
		}
		from++;
	}

	if (ind == sz) {
		verbose(ERROR, "%s(): player links grow tall here."
			" I had to truncate this: '%s'\n",
			__func__, from);
		ind = sz-1;
	}

	to[ind++] = '\0';
	return to;
}

static void flush_element(struct feed *feed)
{
	char clean_player[128];

	if (!feed->navi_sent) {
		send_navi(feed);
		feed->navi_sent = 1;
	}

	/* Sure, browsers deal with unescaped &'s in URLs just fine.
	 * But our fancy unit... massive expat-using html-parsing test
	 * thing is picky.
	 */
	amp_to_amp(clean_player, sizeof(clean_player), feed->fields[F_PLAYER]);

	evbuffer_add_printf(feed->sink,
			    "<div class='entry'>\n"
			    "  <a href='%s'>\n"
			    "    <img src='%s' class='thumbnail'/>\n"
			    "  </a>\n"
			    "  <div class='info'>\n"
			    "    <p class='title'>\n"
			    "      <a href='%s'>%s</a>\n"
			    "    </p>\n"
			    "    <p class='uploader'>%s</p>\n"
			    "    <div class='dates'>\n"
			    "      <p>Last: %s</p>\n"
			    "      <p>First: %s</p>\n"
			    "    </div>\n"
			    "  </div>\n"
			    "</div>\n",
			    clean_player,
			    feed->fields[F_THUMBNAIL],
			    clean_player,
			    feed->fields[F_TITLE],
			    feed->fields[F_UPLOADER],
			    feed->fields[F_UPDATED],
			    feed->fields[F_PUBLISHED]);

}

static void copy_cdata_to_field(struct feed *feed, int field)
{
	int needed;

	needed = evbuffer_get_length(feed->cdata_buf) + 1;
	feed->fields[field] = malloc(needed);
	evbuffer_remove(feed->cdata_buf, feed->fields[field], needed);
	feed->fields[field][needed-1] = '\0';
}

static void XMLCALL element_end(void *user_data, const char *element)
{
	struct feed *feed = user_data;
	int cdata_target;

	if (strcmp("entry", element) == 0) {
		flush_element(feed);
		clear_fields(feed);
		if (feed->cdata_buf != NULL) {
			evbuffer_free(feed->cdata_buf);
			feed->cdata_buf = NULL;
		}
		feed->in_entry--;
	} else if (feed->in_entry && (cdata_target = find_cdata_target(element)) != -1) {
		copy_cdata_to_field(feed, cdata_target);
		evbuffer_free(feed->cdata_buf);
		feed->cdata_buf = NULL;
	}
}


int feed_init(struct feed **feedp, struct evbuffer *sink)
{
	struct feed *feed;

	if ((feed = malloc(sizeof(*feed))) == NULL) {
		return ENOMEM;
	}
	memset(feed, 0, sizeof(*feed));

	feed->parser = XML_ParserCreate(NULL);
	XML_SetUserData(feed->parser, feed);
	XML_SetElementHandler(feed->parser, element_start, element_end);
	XML_SetCharacterDataHandler(feed->parser, cdata);

	feed->sink = sink;

	*feedp = feed;
	return 0;

}
void feed_destroy(struct feed *feed)
{
	if (feed != NULL) {

		XML_ParserFree(feed->parser);

		if (feed->cdata_buf != NULL) {
			evbuffer_free(feed->cdata_buf);
		}

		clear_fields(feed);
		free(feed);
	}
}

static const char *HEADER =
	"<html>\n"
	"<head>\n"
	"  <style>\n"
	"    body {"
	"      background-color: #666666;"
	"      font-family: sans-serif;"
	"      font-size: 16px;"
	"      margin-top: 20px;"
	"    }\n"
	"    a { color: inherit; }"
	"    div.entry {"
	"      margin-left: 20px; margin-right: 20px;"
	"      margin-top: 10px;"
	"      margin-bottom: 10px;"
	"      background-color: #888888;"
	"    }\n"
	"    p.uploader { padding-left: 10px; padding-top: 5px; }\n"
	"    div.info {"
	"      display: inline-block;"
	"    }\n"
	"    div.info p { margin-left: 10px; margin-top: 0em; margin-bottom: 0em; }\n"
	"    div.info p.title { margin-top: 10px; }"
	"    div.info p.uploader { font-size: 12px; }"
	"    div.info div.dates { font-size: 10px; margin-top: 16px; }\n"
	"    img.thumbnail {"
	"      padding: 5px;"
	"    background-color: black;"
	"      vertical-align: top;"
	"    }\n"
	"  </style>\n"
	"  <title>watch_history</title>\n"
	"</head>\n"
	"<body>\n";

static const char *FOOTER = "\n</body></html>\n";


int feed_consume(struct feed *feed, struct evbuffer *buf)
{
	char input[1024];
	int removed;

	if (!feed->header_sent) {
		evbuffer_add(feed->sink, HEADER, strlen(HEADER));
		feed->header_sent++;
	}

	while ((removed = evbuffer_remove(buf, input, sizeof(input))) > 0) {
		XML_Parse(feed->parser, input, removed, 0);
		/* TODO: Handle error. */
	}

	return 0;
}

int feed_final(struct feed *feed)
{
	char one;

	XML_Parse(feed->parser, &one, 0, 1);
	evbuffer_add(feed->sink, FOOTER, strlen(FOOTER));
	return 0;
}
